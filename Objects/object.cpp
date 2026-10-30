#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <bit>
#include <unordered_set>

namespace cxxpython::core {
namespace {
template <class F>
decltype(auto) namespace_locked(Attachment &context, DictObject::Storage &data, F &&operation)
{
    for (;;) {
        std::unique_lock lock(data.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            NoSafepoint commit(context);
            return operation();
        }
        NativeWait parked(context);
        data.mutex.lock();
        data.mutex.unlock();
    }
}
} // namespace
DictView::DictView(Attachment &context, std::optional<Value> dictionary)
    : context_(context), roots_(context), storage_(context)
{
    if (!dictionary)
        return;
    roots_.root(*dictionary);
    if (context.has_layout<DictStorage>(*dictionary)) {
        storage_.ok(*dictionary);
        return;
    }
    auto &shell = context.payload<DictObject>(*dictionary);
    ReturnSlot owner(context);
    context.load(*dictionary, shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok) {
        TypeView type(context, owner.value());
        if (type.snapshot()) {
            if (auto storage = type.storage())
                storage_.ok(*storage);
            return;
        }
    }
    auto &data = *shell.contents;
    for (;;) {
        const auto count = namespace_locked(context, data, [&] { return data.names.size(); });
        names_.reserve(count);
        if (namespace_locked(context, data, [&] {
                if (data.names.size() > names_.capacity())
                    return false;
                names_.assign(data.names.begin(), data.names.end());
                return true;
            }))
            break;
    }
    for (const auto &entry : names_) {
        roots_.root(entry.key);
        roots_.root(entry.value);
    }
}
std::optional<Value> DictView::storage() const
{
    if (storage_.tag() == ResultTag::Empty && !names_.empty()) {
        detail::copy_dictionary_storage(context_, names_, storage_);
    }
    return storage_.tag() == ResultTag::Ok ? std::optional<Value>{storage_.value()}
                                           : std::optional<Value>{};
}
std::span<const DictObject::Entry> DictView::entries() const
{
    const auto value = storage_.tag() == ResultTag::Ok ? std::optional<Value>{storage_.value()}
                                                       : std::optional<Value>{};
    return value ? std::span<const DictObject::Entry>{context_.payload<DictStorage>(*value).entries}
                 : std::span<const DictObject::Entry>{names_};
}
std::optional<Value> DictView::find(const std::u32string &name) const
{
    if (storage_.tag() != ResultTag::Ok) {
        for (const auto &entry : names_)
            if (context_.has_layout<StrObject>(entry.key) &&
                context_.payload<StrObject>(entry.key).value == name)
                return entry.value;
        return {};
    }
    const auto &data = context_.payload<DictStorage>(storage_.value());
    auto found = data.index.find(name);
    return found == data.index.end() ? std::optional<Value>{} : data.entries[found->second].value;
}

bool read_cell(Attachment &context, Value cell, ReturnSlot &out)
{
    context.load(cell, context.payload<CellObject>(cell).contents, out);
    return out.tag() == ResultTag::Ok;
}
void write_cell(Attachment &context, Value cell, std::optional<Value> value)
{
    context.store(cell, context.payload<CellObject>(cell).contents, value);
}

namespace {
template <Protocol Operation>
void builtin_protocol(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto value = roots.root(args[0]);
    if constexpr (Operation == Protocol::Len)
        length(context, value, out);
    else if constexpr (Operation == Protocol::Repr)
        repr(context, value, out);
    else if constexpr (Operation == Protocol::Hash)
        hash(context, value, out);
}
#include "unicode_identifiers.inc"

bool in_ranges(std::span<const std::pair<char32_t, char32_t>> ranges, char32_t point)
{
    auto next = std::upper_bound(ranges.begin(), ranges.end(), point,
                                 [](auto value, auto range) { return value < range.first; });
    return next != ranges.begin() && point <= (next - 1)->second;
}
template <class String> std::size_t string_bytes(const String &text) noexcept
{
    auto data = reinterpret_cast<std::uintptr_t>(text.data());
    auto start = reinterpret_cast<std::uintptr_t>(&text);
    return data >= start && data - start < sizeof(text)
               ? 0
               : (text.capacity() + 1) * sizeof(typename String::value_type);
}
} // namespace
bool is_identifier(std::u32string_view text)
{
    if (text.empty() || (text.front() != U'_' && !in_ranges(xid_start, text.front())))
        return false;
    for (auto point : text.substr(1))
        if (!in_ranges(xid_continue, point))
            return false;
    return true;
}
std::size_t StrObject::external_bytes() const noexcept
{
    return string_bytes(value);
}
std::size_t SignatureLayout::external_bytes() const noexcept
{
    auto bytes = parameters.capacity() * sizeof(Parameter);
    for (const auto &parameter : parameters)
        bytes += string_bytes(parameter.name);
    if (varargs)
        bytes += string_bytes(*varargs);
    if (varkwargs)
        bytes += string_bytes(*varkwargs);
    return bytes;
}
std::size_t NativeFunctionObject::external_bytes() const noexcept
{
    return string_bytes(name) + signature.external_bytes() + captures.capacity() * sizeof(Value);
}
std::size_t DictStorage::external_bytes() const noexcept
{
    auto bytes = entries.capacity() * sizeof(DictObject::Entry);
    if (index.bucket_count() > 1)
        bytes += index.bucket_count() * sizeof(void *);
    bytes += index.size() * sizeof(decltype(index)::value_type);
    return bytes + index_string_bytes;
}
void DictStorage::trace(Tracer &tracer) const noexcept
{
    for (const auto &entry : entries) {
        tracer.visit(entry.key);
        tracer.visit(entry.value);
    }
}
void ExceptionObject::trace(Tracer &tracer) const noexcept
{
    tracer.visit(args);
    tracer.visit(traceback);
    tracer.visit(cause);
    tracer.visit(context);
}
void SignatureLayout::trace(Tracer &tracer) const noexcept
{
    for (const auto &parameter : parameters)
        if (parameter.default_value)
            tracer.visit(*parameter.default_value);
}
void NativeFunctionObject::trace(Tracer &tracer) const noexcept
{
    signature.trace(tracer);
    if (sequence_owner)
        tracer.visit(*sequence_owner);
    for (auto value : captures)
        tracer.visit(value);
}
void SignatureLayout::validate() const
{
    std::unordered_set<std::u32string> names;
    ParameterKind previous = ParameterKind::PositionalOnly;
    bool default_seen = false;
    for (const auto &parameter : parameters) {
        if (parameter.name.empty() || !names.insert(parameter.name).second ||
            parameter.kind < previous)
            throw std::invalid_argument("invalid native signature parameter order or name");
        if (parameter.kind != ParameterKind::KeywordOnly) {
            if (default_seen && !parameter.default_value)
                throw std::invalid_argument("required positional parameter follows a default");
            default_seen |= parameter.default_value.has_value();
        }
        previous = parameter.kind;
    }
    for (const auto *name : {&varargs, &varkwargs})
        if (*name && ((**name).empty() || !names.insert(**name).second))
            throw std::invalid_argument("duplicate or empty variadic parameter name");
}

void make_str(Attachment &context, std::u32string value, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(context.builtin(CoreObject::StrType));
    for (auto point : value)
        if (point > 0x10ffff)
            throw std::invalid_argument("invalid Unicode code point");
    context.allocate_instance<StrObject>(type, out, std::move(value));
}
void make_tuple(Attachment &context, std::span<const Value> values, ReturnSlot &out)
{
    RootScope roots(context);
    for (auto value : values)
        roots.root(value);
    auto type = roots.root(context.builtin(CoreObject::TupleType));
    context.allocate_instance<TupleObject>(type, out,
                                           std::vector<Value>(values.begin(), values.end()));
}
void validate_exception(Attachment &context, Value exception)
{
    if (!context.has_layout<ExceptionObject>(exception) ||
        !is_subtype(context, context.type_of(exception),
                    context.builtin(CoreObject::BaseExceptionType)))
        throw std::logic_error("Raised requires a BaseException instance");
}
void raise_error(Attachment &context, CoreObject type, std::u32string message, ReturnSlot &out)
{
    out.reset(context);
    try {
        RootScope roots(context);
        ReturnSlot temporary(context);
        make_str(context, std::move(message), temporary);
        auto text = roots.root(temporary.value());
        std::array args{text.get(context)};
        cxxpython::core::make_tuple(context, args, temporary);
        auto arguments = roots.root(temporary.value());
        auto exception_type = roots.root(context.builtin(type));
        context.allocate_instance<ExceptionObject>(exception_type, temporary,
                                                   arguments.get(context));
        detail::link_exception_context(context, temporary.value());
        out.raised(temporary.value());
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
}
void make_native_function(Attachment &context, std::string name, SignatureLayout signature,
                          NativeEntry entry, std::span<const Value> captures, ReturnSlot &out)
{
    RootScope roots(context);
    for (const auto &parameter : signature.parameters)
        if (parameter.default_value)
            roots.root(*parameter.default_value);
    for (auto capture : captures)
        roots.root(capture);
    auto type = roots.root(context.builtin(CoreObject::NativeFunctionType));
    detail::allocate_function(context, type, std::move(name), std::move(signature), entry,
                              {captures.begin(), captures.end()}, out);
}
void make_method_descriptor(Attachment &context, Handle owner, Handle function, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(context.builtin(CoreObject::MethodDescriptorType));
    (void)context.payload<TypeObject>(owner);
    if (context.type_of(function) != context.builtin(CoreObject::NativeFunctionType))
        throw std::invalid_argument("method descriptor requires an exact native function");
    context.allocate_instance<MethodDescriptorObject>(type, out, function.get(context),
                                                      owner.get(context));
}

namespace detail {
std::int64_t name_hash(Attachment &context, const std::u32string &text)
{
    const auto word = text.empty()
                          ? std::uint64_t{0}
                          : context.keyed_hash(std::as_bytes(std::span(text.data(), text.size())));
    const auto result = std::bit_cast<std::int64_t>(word);
    return result == -1 ? -2 : result;
}
namespace {
std::size_t entry_bytes(const DictObject::Entry &, const std::u32string *name)
{
    return sizeof(DictObject::Entry) + 2 * sizeof(void *) +
           sizeof(DictObject::HashIndex::value_type) + 4 * sizeof(void *) +
           (name ? sizeof(DictObject::Index::value_type) + 6 * sizeof(void *) + string_bytes(*name)
                 : 0);
}
void remove_entry(Attachment &context, Value dictionary, DictObject::Storage &data,
                  std::list<DictObject::Entry>::iterator entry,
                  std::list<DictObject::Entry> &removed)
{
    std::size_t bytes = entry_bytes(*entry, nullptr);
    if (context.has_layout<StrObject>(entry->key)) {
        const auto &label = context.payload<StrObject>(entry->key).value;
        auto found = data.index.find(label);
        if (found != data.index.end() && found->second == entry) {
            bytes = entry_bytes(*entry, &found->first);
            data.index.erase(found);
        }
    }
    const auto [begin, end] = data.hashes.equal_range(entry->hash);
    for (auto at = begin; at != end; ++at)
        if (at->second == entry) {
            data.hashes.erase(at);
            break;
        }
    data.bytes -= bytes;
    removed.splice(removed.end(), data.names, entry);
    ++data.keys_version;
    context.account_external(dictionary, data.external_bytes());
}
void insert_entry(Attachment &context, Value dictionary, DictObject::Storage &data,
                  std::list<DictObject::Entry> &prepared, DictObject::Index::node_type &name,
                  DictObject::HashIndex::node_type &hash)
{
    if (!data.next_identity)
        throw std::bad_alloc();
    auto &entry = prepared.front();
    entry.identity = data.next_identity;
    // Both index nodes are prepared before the non-allocating commit.
    data.hashes.insert(std::move(hash));
    ++data.next_identity;
    data.bytes += entry_bytes(entry, name ? &name.key() : nullptr);
    data.names.splice(data.names.end(), prepared);
    if (name)
        data.index.insert(std::move(name));
    ++data.keys_version;
    context.account_external(dictionary, data.external_bytes());
}
} // namespace
std::optional<Value> name_lookup(Attachment &context, Value dictionary, const std::u32string &name)
{
    RootScope roots(context);
    roots.root(dictionary);
    auto &shell = context.payload<DictObject>(dictionary);
    ReturnSlot owner(context);
    context.load(dictionary, shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot())
        return DictView(context, dictionary).find(name);
    auto &data = *shell.contents;
    return namespace_locked(context, data, [&]() -> std::optional<Value> {
        const auto found = data.index.find(name);
        return found == data.index.end() ? std::optional<Value>{} : found->second->value;
    });
}
bool copy_name_storage(Attachment &context, std::optional<Value> source, const std::u32string &name,
                       std::optional<Value> key, std::optional<Value> value, ReturnSlot &out)
{
    DictView old(context, source);
    std::vector<DictObject::Entry> entries(old.entries().begin(), old.entries().end());
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return context.has_layout<StrObject>(entry.key) &&
               context.payload<StrObject>(entry.key).value == name;
    });
    if (value) {
        if (found != entries.end())
            found->value = *value;
        else {
            if (!key)
                throw std::logic_error("new name requires a managed key");
            entries.push_back({*key, *value, name_hash(context, name)});
        }
    } else {
        if (found == entries.end())
            return false;
        entries.erase(found);
    }
    copy_dictionary_storage(context, entries, out);
    return true;
}
void copy_dictionary_storage(Attachment &context, std::span<const DictObject::Entry> source,
                             ReturnSlot &out)
{
    RootScope roots(context);
    for (const auto &entry : source) {
        roots.root(entry.key);
        roots.root(entry.value);
    }
    std::vector<DictObject::Entry> entries(source.begin(), source.end());
    std::unordered_map<std::u32string, std::size_t> index;
    std::size_t bytes = 0;
    bool exact = true;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        exact &= context.type_of(entries[i].key) == context.builtin(CoreObject::StrType);
        if (!context.has_layout<StrObject>(entries[i].key))
            continue;
        auto [entry, inserted] = index.emplace(context.payload<StrObject>(entries[i].key).value, i);
        (void)inserted;
        bytes += string_bytes(entry->first);
        if (i % 64 == 63)
            context.poll();
    }
    context.allocate<DictStorage>(out, std::move(entries), std::move(index), bytes, exact);
}
void name_store(Attachment &context, Value dictionary, Value name, Value value)
{
    RootScope roots(context);
    roots.root(dictionary);
    roots.root(name);
    roots.root(value);
    (void)context.type_of(value);
    const auto &text = context.payload<StrObject>(name).value;
    auto &shell = context.payload<DictObject>(dictionary);
    ReturnSlot owner(context);
    context.load(dictionary, shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot()) {
        type_namespace_change(context, owner.value(), text, name, value);
        return;
    }
    auto &data = *shell.contents;
    if (namespace_locked(context, data, [&] {
            const auto found = data.index.find(text);
            if (found == data.index.end())
                return false;
            found->second->value = value;
            return true;
        }))
        return;
    std::list<DictObject::Entry> prepared{{name, value, name_hash(context, text)}};
    DictObject::Index index;
    index.emplace(text, prepared.begin());
    auto node = index.extract(index.begin());
    DictObject::HashIndex hashes;
    hashes.emplace(prepared.front().hash, prepared.begin());
    auto hash = hashes.extract(hashes.begin());
    namespace_locked(context, data, [&] {
        const auto found = data.index.find(text);
        if (found != data.index.end())
            found->second->value = value;
        else {
            insert_entry(context, dictionary, data, prepared, node, hash);
        }
        context.account_external(dictionary, data.external_bytes());
    });
}
bool name_erase(Attachment &context, Value dictionary, const std::u32string &name)
{
    RootScope roots(context);
    roots.root(dictionary);
    auto &shell = context.payload<DictObject>(dictionary);
    ReturnSlot owner(context);
    context.load(dictionary, shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot())
        return type_namespace_change(context, owner.value(), name, {}, {});
    auto &data = *shell.contents;
    std::list<DictObject::Entry> entries;
    return namespace_locked(context, data, [&] {
        const auto found = data.index.find(name);
        if (found == data.index.end())
            return false;
        remove_entry(context, dictionary, data, found->second, entries);
        return true;
    });
}
bool dict_erase_binding(Attachment &context, Handle dictionary, DictBinding binding,
                        Handle expected)
{
    const auto target = dictionary.get(context), value = expected.get(context);
    auto &data = *context.payload<DictObject>(target).contents;
    std::list<DictObject::Entry> entries;
    return namespace_locked(context, data, [&] {
        const auto [begin, end] = data.hashes.equal_range(binding.hash);
        for (auto at = begin; at != end; ++at)
            if (at->second->identity == binding.identity && at->second->value == value) {
                remove_entry(context, target, data, at->second, entries);
                return true;
            }
        return false;
    });
}
void discard_name_table(Attachment &context, Value dictionary)
{
    auto &data = *context.payload<DictObject>(dictionary).contents;
    std::list<DictObject::Entry> entries;
    DictObject::Index index;
    DictObject::HashIndex hashes;
    namespace_locked(context, data, [&] {
        data.names.swap(entries);
        data.index.swap(index);
        data.hashes.swap(hashes);
        ++data.keys_version;
        data.bytes = 0;
        context.account_external(dictionary, sizeof(DictObject::Storage));
    });
}
void copy_dictionary_entries(Attachment &context, Value destination,
                             std::span<const DictObject::Entry> source)
{
    RootScope roots(context);
    roots.root(destination);
    std::list<DictObject::Entry> entries;
    DictObject::Index names;
    DictObject::HashIndex hashes;
    std::uint64_t identity = 1;
    std::size_t bytes = 0;
    for (auto entry : source) {
        roots.root(entry.key);
        roots.root(entry.value);
        entry.identity = identity++;
        entries.push_back(entry);
        const auto at = std::prev(entries.end());
        const std::u32string *name = nullptr;
        if (context.has_layout<StrObject>(entry.key)) {
            const auto inserted = names.emplace(context.payload<StrObject>(entry.key).value, at);
            if (inserted.second)
                name = &inserted.first->first;
        }
        hashes.emplace(entry.hash, at);
        bytes += entry_bytes(entry, name);
    }
    auto &data = *context.payload<DictObject>(destination).contents;
    namespace_locked(context, data, [&] {
        if (!data.names.empty())
            throw std::logic_error("dictionary clone requires an empty destination");
        data.names.swap(entries);
        data.index.swap(names);
        data.hashes.swap(hashes);
        data.next_identity = identity;
        data.bytes = bytes;
        ++data.keys_version;
        context.account_external(destination, data.external_bytes());
    });
}
void initialize_type_dictionary(Attachment &context, Handle type)
{
    auto &payload = context.payload<TypeObject>(type);
    if (payload.dictionary)
        return;
    RootScope roots(context);
    auto dictionary_type = roots.root(context.builtin(CoreObject::DictType));
    ReturnSlot out(context);
    context.allocate_instance<DictObject>(dictionary_type, out);
    context.store(out.value(), context.payload<DictObject>(out.value()).namespace_owner,
                  type.get(context));
    payload.dictionary = out.value();
}
void allocate_function(Attachment &context, Handle type, std::string name,
                       SignatureLayout signature, NativeEntry entry, std::vector<Value> captures,
                       ReturnSlot &out, std::optional<Value> sequence_owner)
{
    out.reset(context);
    signature.validate();
    if (!entry)
        throw std::invalid_argument("native function requires an entrypoint");
    context.allocate_instance<NativeFunctionObject>(type, out, std::move(name),
                                                    std::move(signature), entry,
                                                    std::move(captures), sequence_owner);
}
void build_core_objects(Attachment &context, std::span<ReturnSlot *const> outputs)
{
    RootScope roots(context);
    std::vector<Handle> core;
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto define = [&](CoreObject id, const char *name, LayoutDescriptor layout,
                      std::optional<Handle> base = {}) {
        std::vector<Handle> bases;
        if (base)
            bases.push_back(*base);
        TypeSpec spec{name, bases};
        spec.module = U"builtins";
        if (id == CoreObject::FunctionType || id == CoreObject::ModuleType)
            spec.slots = std::vector<std::u32string>{U"__dict__"};
        if (id == CoreObject::ImportErrorType)
            spec.slots = std::vector<std::u32string>{U"name", U"path"};
        if (id == CoreObject::StopIterationType)
            spec.slots = std::vector<std::u32string>{U"value"};
        create_native_type(context, NativeTypeSpec{spec, layout},
                           *outputs[static_cast<std::size_t>(id)]);
        core.push_back(roots.root(outputs[static_cast<std::size_t>(id)]->result().value()));
    };
    define(CoreObject::PropertyType, "property", LayoutDescriptor::of<PropertyObject>());
    define(CoreObject::NoneType, "NoneType", LayoutDescriptor::of<NoneObject>());
    define(CoreObject::StrType, "str", LayoutDescriptor::of<StrObject>());
    define(CoreObject::TupleType, "tuple", LayoutDescriptor::of<TupleObject>());
    define(CoreObject::DictType, "dict", LayoutDescriptor::of<DictObject>());
    define(CoreObject::IntType, "int", LayoutDescriptor::of<IntObject>());
    define(CoreObject::BoolType, "bool", LayoutDescriptor::of<IntObject>(),
           at(CoreObject::IntType));
    define(CoreObject::FloatType, "float", LayoutDescriptor::of<FloatObject>());
    define(CoreObject::NotImplementedType, "NotImplementedType",
           LayoutDescriptor::of<NoneObject>());
    define(CoreObject::EllipsisType, "ellipsis", LayoutDescriptor::of<NoneObject>());
    define(CoreObject::BaseExceptionType, "BaseException", LayoutDescriptor::of<ExceptionObject>());
    define(CoreObject::ExceptionType, "Exception", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
    define(CoreObject::AssertionErrorType, "AssertionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::TypeErrorType, "TypeError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::AttributeErrorType, "AttributeError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::ValueErrorType, "ValueError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::RuntimeErrorType, "RuntimeError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::MemoryErrorType, "MemoryError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::RecursionErrorType, "RecursionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::RuntimeErrorType));
    define(CoreObject::ArithmeticErrorType, "ArithmeticError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::ZeroDivisionErrorType, "ZeroDivisionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ArithmeticErrorType));
    define(CoreObject::OverflowErrorType, "OverflowError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ArithmeticErrorType));
    define(CoreObject::LookupErrorType, "LookupError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::IndexErrorType, "IndexError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::LookupErrorType));
    define(CoreObject::KeyErrorType, "KeyError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::LookupErrorType));
    define(CoreObject::StopIterationType, "StopIteration", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::NameErrorType, "NameError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::UnboundLocalErrorType, "UnboundLocalError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::NameErrorType));
    define(CoreObject::ImportErrorType, "ImportError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::ModuleNotFoundErrorType, "ModuleNotFoundError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ImportErrorType));
    define(CoreObject::SyntaxErrorType, "SyntaxError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::IndentationErrorType, "IndentationError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::SyntaxErrorType));
    define(CoreObject::TabErrorType, "TabError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::IndentationErrorType));
    define(CoreObject::OSErrorType, "OSError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::NativeFunctionType, "builtin_function_or_method",
           LayoutDescriptor::of<NativeFunctionObject>());
    define(CoreObject::MethodDescriptorType, "method_descriptor",
           LayoutDescriptor::of<MethodDescriptorObject>());
    define(CoreObject::BoundMethodType, "method", LayoutDescriptor::of<BoundMethodObject>());
    define(CoreObject::GetSetDescriptorType, "getset_descriptor",
           LayoutDescriptor::of<GetSetDescriptorObject>());
    define(CoreObject::MappingProxyType, "mappingproxy",
           LayoutDescriptor::of<MappingProxyObject>());
    define(CoreObject::StaticMethodType, "staticmethod",
           LayoutDescriptor::of<StaticMethodObject>());
    define(CoreObject::ClassMethodType, "classmethod", LayoutDescriptor::of<ClassMethodObject>());
    define(CoreObject::MemberDescriptorType, "member_descriptor",
           LayoutDescriptor::of<MemberDescriptorObject>());
    define(CoreObject::CellType, "cell", LayoutDescriptor::of<CellObject>());
    define(CoreObject::CodeType, "code", execution_layout(CoreObject::CodeType));
    define(CoreObject::FunctionType, "function", execution_layout(CoreObject::FunctionType));
    define(CoreObject::FrameType, "frame", execution_layout(CoreObject::FrameType));
    define(CoreObject::TracebackType, "traceback", execution_layout(CoreObject::TracebackType));
    define(CoreObject::RangeType, "range", LayoutDescriptor::of<RangeObject>());
    define(CoreObject::RangeIteratorType, "range_iterator",
           LayoutDescriptor::of<RangeIteratorObject>());
    define(CoreObject::TupleIteratorType, "tuple_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::StrIteratorType, "str_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::ModuleType, "module", LayoutDescriptor::of<ModuleObject>());
    define(CoreObject::DictKeysType, "dict_keys", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictValuesType, "dict_values", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictItemsType, "dict_items", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictKeyIteratorType, "dict_keyiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::DictValueIteratorType, "dict_valueiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::DictItemIteratorType, "dict_itemiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::ListType, "list", LayoutDescriptor::of<ListObject>());
    define(CoreObject::ListIteratorType, "list_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::SequenceIteratorType, "iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::SliceType, "slice", LayoutDescriptor::of<SliceObject>());
    context.payload<TypeObject>(at(CoreObject::TupleType)).allow_slots = false;
    context.payload<TypeObject>(at(CoreObject::IntType)).allow_slots = false;
    for (auto type : core) {
        ReturnSlot dictionary(context);
        context.allocate_instance<DictObject>(at(CoreObject::DictType), dictionary);
        context.payload<TypeObject>(type).dictionary = dictionary.value();
    }
    for (auto seed : {context.object_type(), context.type_type()}) {
        ReturnSlot dictionary(context);
        context.allocate_instance<DictObject>(at(CoreObject::DictType), dictionary);
        context.payload<TypeObject>(seed).dictionary = dictionary.value();
    }
    auto &none_out = *outputs[static_cast<std::size_t>(CoreObject::None)];
    context.allocate_instance<NoneObject>(at(CoreObject::NoneType), none_out);
    core.push_back(roots.root(none_out.value()));
    auto &not_implemented = *outputs[static_cast<std::size_t>(CoreObject::NotImplemented)];
    context.allocate_instance<NoneObject>(at(CoreObject::NotImplementedType), not_implemented);
    core.push_back(roots.root(not_implemented.value()));
    auto &ellipsis = *outputs[static_cast<std::size_t>(CoreObject::Ellipsis)];
    context.allocate_instance<NoneObject>(at(CoreObject::EllipsisType), ellipsis);
    core.push_back(roots.root(ellipsis.value()));
    for (auto id : {CoreObject::False, CoreObject::True, CoreObject::IntZero, CoreObject::IntOne}) {
        auto &output = *outputs[static_cast<std::size_t>(id)];
        const bool boolean = id == CoreObject::False || id == CoreObject::True;
        const std::int64_t value = id == CoreObject::True || id == CoreObject::IntOne ? 1 : 0;
        context.allocate_instance<IntObject>(
            at(boolean ? CoreObject::BoolType : CoreObject::IntType), output, value);
        core.push_back(roots.root(output.value()));
    }
    auto &tuple_out = *outputs[static_cast<std::size_t>(CoreObject::EmptyTuple)];
    context.allocate_instance<TupleObject>(at(CoreObject::TupleType), tuple_out,
                                           std::vector<Value>{});
    core.push_back(roots.root(tuple_out.value()));
    auto &error_out = *outputs[static_cast<std::size_t>(CoreObject::MemoryError)];
    context.allocate_instance<ExceptionObject>(at(CoreObject::MemoryErrorType), error_out,
                                               tuple_out.value());
    core.push_back(roots.root(error_out.value()));
    ReturnSlot text(context);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__module__");
    auto module_key = roots.root(text.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"builtins");
    auto module_value = roots.root(text.value());
    auto initialize_names = [&](Value type) {
        auto &payload = context.payload<TypeObject>(type);
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text,
                                             std::get<std::u32string>(payload.name));
        payload.name = text.value();
        payload.qualname = text.value();
        payload.bootstrap_module.reset();
        context.account_external(type, payload.external_bytes());
    };
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        initialize_names(core[i].get(context));
        name_store(context, *context.payload<TypeObject>(core[i]).dictionary,
                   module_key.get(context), module_value.get(context));
    }
    for (auto type : {context.object_type(), context.type_type()}) {
        initialize_names(type);
        name_store(context, *context.payload<TypeObject>(type).dictionary, module_key.get(context),
                   module_value.get(context));
    }
    install_call_descriptors(context, core);
    install_attribute_descriptors(context, core);
    install_construction(context, core);
    install_numbers(context, core);
    install_floats(context, core);
    install_protocols(context, core);
    install_dictionaries(context, core);
    install_lists(context, core);
    install_value_protocols(context, core);
    install_format(context, core);
    install_execution(context, core);
    install_exception_runtime(context, core);
    install_ranges(context, core);
    install_slices(context, core);
    install_iteration(context, core);
    install_modules(context, core);
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        context.payload<TypeObject>(core[i]).immutable = true;
        context.payload<TypeObject>(core[i]).state = TypeState::Ready;
    }
    for (auto type : {context.object_type(), context.type_type()})
        context.payload<TypeObject>(type).state = TypeState::Ready;
    for (auto id : {CoreObject::NoneType,
                    CoreObject::BoolType,
                    CoreObject::NotImplementedType,
                    CoreObject::EllipsisType,
                    CoreObject::NativeFunctionType,
                    CoreObject::MethodDescriptorType,
                    CoreObject::BoundMethodType,
                    CoreObject::GetSetDescriptorType,
                    CoreObject::MappingProxyType,
                    CoreObject::MemberDescriptorType,
                    CoreObject::CellType,
                    CoreObject::CodeType,
                    CoreObject::FunctionType,
                    CoreObject::FrameType,
                    CoreObject::TracebackType,
                    CoreObject::RangeType,
                    CoreObject::RangeIteratorType,
                    CoreObject::TupleIteratorType,
                    CoreObject::StrIteratorType,
                    CoreObject::DictKeysType,
                    CoreObject::DictValuesType,
                    CoreObject::DictItemsType,
                    CoreObject::DictKeyIteratorType,
                    CoreObject::DictValueIteratorType,
                    CoreObject::DictItemIteratorType,
                    CoreObject::ListIteratorType,
                    CoreObject::SequenceIteratorType,
                    CoreObject::SliceType})
        context.payload<TypeObject>(at(id)).subclassable = false;

    auto &builtins = *outputs[static_cast<std::size_t>(CoreObject::Builtins)];
    context.allocate_instance<DictObject>(at(CoreObject::DictType), builtins);
    core.push_back(roots.root(builtins.value()));
    const auto mapping = builtins.value();
    auto publish = [&](Value key, Value value) { name_store(context, mapping, key, value); };
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"builtins");
    const auto module_name = roots.root(text.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__name__");
    publish(text.value(), module_name.get(context));
    for (auto id : {CoreObject::StrType,
                    CoreObject::TupleType,
                    CoreObject::ListType,
                    CoreObject::DictType,
                    CoreObject::IntType,
                    CoreObject::BoolType,
                    CoreObject::FloatType,
                    CoreObject::BaseExceptionType,
                    CoreObject::ExceptionType,
                    CoreObject::AssertionErrorType,
                    CoreObject::TypeErrorType,
                    CoreObject::AttributeErrorType,
                    CoreObject::ValueErrorType,
                    CoreObject::RuntimeErrorType,
                    CoreObject::MemoryErrorType,
                    CoreObject::RecursionErrorType,
                    CoreObject::ArithmeticErrorType,
                    CoreObject::ZeroDivisionErrorType,
                    CoreObject::OverflowErrorType,
                    CoreObject::LookupErrorType,
                    CoreObject::IndexErrorType,
                    CoreObject::KeyErrorType,
                    CoreObject::StopIterationType,
                    CoreObject::RangeType,
                    CoreObject::SliceType,
                    CoreObject::NameErrorType,
                    CoreObject::UnboundLocalErrorType,
                    CoreObject::ImportErrorType,
                    CoreObject::ModuleNotFoundErrorType,
                    CoreObject::SyntaxErrorType,
                    CoreObject::IndentationErrorType,
                    CoreObject::TabErrorType,
                    CoreObject::OSErrorType,
                    CoreObject::PropertyType,
                    CoreObject::StaticMethodType,
                    CoreObject::ClassMethodType})
        publish(std::get<Value>(context.payload<TypeObject>(at(id)).name), at(id).get(context));
    for (auto type : {context.object_type(), context.type_type()})
        publish(std::get<Value>(context.payload<TypeObject>(type).name), type);
    for (auto [name, id] : {std::pair{U"None", CoreObject::None},
                            {U"Ellipsis", CoreObject::Ellipsis},
                            {U"True", CoreObject::True},
                            {U"False", CoreObject::False},
                            {U"NotImplemented", CoreObject::NotImplemented}}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, name);
        publish(text.value(), at(id).get(context));
    }
    auto operation = [&](const char *name, std::u32string key, NativeEntry entry) {
        SignatureLayout signature;
        signature.parameters = {{U"object", ParameterKind::PositionalOnly}};
        ReturnSlot function(context);
        context.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType),
                                                        function, name, std::move(signature), entry,
                                                        std::vector<Value>{});
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, std::move(key));
        publish(text.value(), function.value());
    };
    operation("len", U"len", builtin_protocol<Protocol::Len>);
    operation("repr", U"repr", builtin_protocol<Protocol::Repr>);
    operation("hash", U"hash", builtin_protocol<Protocol::Hash>);
    install_iterator_builtins(context, core, mapping);
    install_class_builtins(context, core, mapping);
    install_import_builtins(context, core, mapping);
    auto &modules = *outputs[static_cast<std::size_t>(CoreObject::Modules)];
    context.allocate_instance<DictObject>(at(CoreObject::DictType), modules);
    core.push_back(roots.root(modules.value()));
}
} // namespace detail

namespace {
enum class DictOperation { Lookup, Store, Erase, SetDefault };
ResultView access_dictionary(Attachment &context, Handle dictionary, Handle key,
                             std::optional<Handle> value, DictOperation operation, ReturnSlot &out,
                             std::optional<std::int64_t> cached_hash = {},
                             detail::DictBinding *binding = nullptr)
{
    out.check_caller(context);
    if (binding)
        *binding = {};
    RootScope roots(context);
    dictionary = roots.root(dictionary.get(context));
    key = roots.root(key.get(context));
    if (value)
        value = roots.root(value->get(context));
    out.reset(context);
    try {
        if (!context.has_layout<DictObject>(dictionary.get(context)) ||
            !is_subtype(context, context.type_of(dictionary),
                        context.builtin(CoreObject::DictType))) {
            raise_error(context, CoreObject::TypeErrorType, U"dictionary operation requires dict",
                        out);
            return out.result();
        }
        const bool string = context.type_of(key) == context.builtin(CoreObject::StrType);
        std::int64_t hash_value;
        ReturnSlot temporary(context), owner(context);
        if (cached_hash)
            hash_value = *cached_hash;
        else if (string)
            hash_value = detail::name_hash(context, context.payload<StrObject>(key).value);
        else {
            hash(context, key, temporary);
            if (temporary.tag() == ResultTag::Raised) {
                out.raised(temporary.exception());
                return out.result();
            }
            if (!int_fits_signed(context, temporary.value(), hash_value))
                throw std::logic_error("hash protocol did not normalize its result");
        }
        auto &data = *context.payload<DictObject>(dictionary).contents;
        context.load(dictionary.get(context),
                     context.payload<DictObject>(dictionary).namespace_owner, owner);
        if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot()) {
            if (binding)
                throw std::logic_error("binding tokens require an ordinary dictionary");
            if (operation != DictOperation::Lookup &&
                context.payload<TypeObject>(owner.value()).immutable) {
                raise_error(context, CoreObject::TypeErrorType, U"cannot modify immutable type",
                            out);
                return out.result();
            }
            for (;;) {
                TypeView metadata(context, owner.value());
                DictView snapshot(context, metadata.storage());
                const auto entries = snapshot.entries();
                std::optional<std::size_t> found;
                bool compare = true;
                if (string && metadata.storage()) {
                    const auto &table = context.payload<DictStorage>(*metadata.storage());
                    const auto at = table.index.find(context.payload<StrObject>(key).value);
                    if (at != table.index.end() && context.type_of(entries[at->second].key) ==
                                                       context.builtin(CoreObject::StrType)) {
                        found = at->second;
                        compare = false;
                    } else if (table.exact_string_keys)
                        compare = false;
                }
                if (compare) {
                    for (std::size_t i = 0; i < entries.size(); ++i) {
                        const auto &entry = entries[i];
                        if (entry.hash != hash_value)
                            continue;
                        container_equal(context, roots.root(entry.key), key, temporary);
                        if (temporary.tag() == ResultTag::Raised) {
                            out.raised(temporary.exception());
                            return out.result();
                        }
                        if (temporary.value() == context.builtin(CoreObject::True)) {
                            found = i;
                            break;
                        }
                    }
                }
                // Equality may reenter Python and publish another namespace.
                if (TypeView(context, owner.value()).snapshot() != metadata.snapshot()) {
                    context.poll();
                    continue;
                }
                if (operation == DictOperation::Lookup ||
                    (operation == DictOperation::SetDefault && found) ||
                    (operation == DictOperation::Erase && !found)) {
                    if (found)
                        out.ok(entries[*found].value);
                    return out.result();
                }
                std::vector<DictObject::Entry> changed(entries.begin(), entries.end());
                if (operation == DictOperation::Erase)
                    changed.erase(changed.begin() + *found);
                else if (found)
                    changed[*found].value = value->get(context);
                else
                    changed.push_back({key.get(context), value->get(context), hash_value});
                detail::copy_dictionary_storage(context, changed, temporary);
                if (detail::publish_type_dictionary(context, owner.value(), *metadata.snapshot(),
                                                    temporary.value())) {
                    out.ok(operation == DictOperation::Erase ? entries[*found].value
                           : operation == DictOperation::SetDefault
                               ? value->get(context)
                               : context.builtin(CoreObject::None));
                    return out.result();
                }
                context.poll();
            }
        }
        std::list<DictObject::Entry> removed;
        auto finish = [&](auto at) {
            if (operation == DictOperation::Store) {
                at->value = value->get(context);
                if (binding)
                    *binding = {at->hash, at->identity};
                out.ok(context.builtin(CoreObject::None));
            } else {
                out.ok(at->value);
                if (operation == DictOperation::Erase)
                    detail::remove_entry(context, dictionary.get(context), data, at, removed);
            }
        };
        if (string && namespace_locked(context, data, [&] {
                const auto found = data.index.find(context.payload<StrObject>(key).value);
                if (found == data.index.end() ||
                    context.type_of(found->second->key) != context.builtin(CoreObject::StrType))
                    return false;
                finish(found->second);
                return true;
            }))
            return out.result();
        std::list<DictObject::Entry> prepared;
        DictObject::Index::node_type name_node;
        DictObject::HashIndex::node_type hash_node;
        if (operation == DictOperation::Store || operation == DictOperation::SetDefault) {
            prepared.push_back({key.get(context), value->get(context), hash_value});
            DictObject::Index names;
            if (string) {
                names.emplace(context.payload<StrObject>(key).value, prepared.begin());
                name_node = names.extract(names.begin());
            }
            DictObject::HashIndex hashes;
            hashes.emplace(hash_value, prepared.begin());
            hash_node = hashes.extract(hashes.begin());
        }
        for (;;) {
            RootScope iteration(context);
            std::vector<DictObject::Entry> candidates;
            std::uint64_t version;
            for (;;) {
                const auto count =
                    namespace_locked(context, data, [&] { return data.hashes.count(hash_value); });
                candidates.reserve(count);
                if (namespace_locked(context, data, [&] {
                        if (data.hashes.count(hash_value) > candidates.capacity())
                            return false;
                        version = data.keys_version;
                        const auto [begin, end] = data.hashes.equal_range(hash_value);
                        for (auto at = begin; at != end; ++at)
                            candidates.push_back(*at->second);
                        return true;
                    }))
                    break;
            }
            for (const auto &entry : candidates) {
                iteration.root(entry.key);
                iteration.root(entry.value);
            }
            bool restart = false;
            for (const auto &entry : candidates) {
                container_equal(context, iteration.root(entry.key), key, temporary);
                if (temporary.tag() == ResultTag::Raised) {
                    out.raised(temporary.exception());
                    return out.result();
                }
                if (temporary.value() != context.builtin(CoreObject::True))
                    continue;
                const bool committed = namespace_locked(context, data, [&] {
                    const auto [begin, end] = data.hashes.equal_range(hash_value);
                    for (auto at = begin; at != end; ++at)
                        if (at->second->identity == entry.identity) {
                            finish(at->second);
                            return true;
                        }
                    return false;
                });
                if (committed)
                    return out.result();
                restart = true;
                break;
            }
            if (!restart && namespace_locked(context, data, [&] {
                    if (data.keys_version != version)
                        return false;
                    if (operation == DictOperation::Store ||
                        operation == DictOperation::SetDefault) {
                        detail::insert_entry(context, dictionary.get(context), data, prepared,
                                             name_node, hash_node);
                        if (binding)
                            *binding = {data.names.back().hash, data.names.back().identity};
                        out.ok(operation == DictOperation::SetDefault
                                   ? value->get(context)
                                   : context.builtin(CoreObject::None));
                    }
                    return true;
                }))
                return out.result();
            context.poll();
        }
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
} // namespace
ResultView dict_lookup(Attachment &context, Handle dictionary, Handle key, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Lookup, out);
}
ResultView dict_store(Attachment &context, Handle dictionary, Handle key, Handle value,
                      ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, value, DictOperation::Store, out);
}
ResultView detail::dict_store_binding(Attachment &context, Handle dictionary, Handle key,
                                      Handle value, DictBinding &binding, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, value, DictOperation::Store, out, {},
                             &binding);
}
ResultView dict_erase(Attachment &context, Handle dictionary, Handle key, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Erase, out);
}
ResultView dict_setdefault(Attachment &context, Handle dictionary, Handle key, Handle value,
                           ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, value, DictOperation::SetDefault, out);
}
void dict_clear(Attachment &context, Handle dictionary, ReturnSlot &out)
{
    RootScope roots(context);
    dictionary = roots.root(dictionary.get(context));
    out.reset(context);
    try {
        ReturnSlot owner(context);
        context.load(dictionary.get(context),
                     context.payload<DictObject>(dictionary).namespace_owner, owner);
        if (owner.tag() == ResultTag::Ok) {
            raise_error(context, CoreObject::TypeErrorType, U"cannot clear a type namespace", out);
            return;
        }
        detail::discard_name_table(context, dictionary.get(context));
        out.ok(context.builtin(CoreObject::None));
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
}
ResultView detail::dict_lookup_hash(Attachment &context, Handle dictionary, Handle key,
                                    std::int64_t hash, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Lookup, out, hash);
}
ResultView detail::dict_store_hash(Attachment &context, Handle dictionary, Handle key, Handle value,
                                   std::int64_t hash, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, value, DictOperation::Store, out, hash);
}
std::size_t detail::dictionary_size(Attachment &context, Value dictionary)
{
    RootScope roots(context);
    roots.root(dictionary);
    auto &shell = context.payload<DictObject>(dictionary);
    ReturnSlot owner(context);
    context.load(dictionary, shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot())
        return DictView(context, dictionary).entries().size();
    return namespace_locked(context, *shell.contents, [&] { return shell.contents->names.size(); });
}
void detail::make_dict_iterator(Attachment &context, Handle dictionary, DictViewKind kind,
                                ReturnSlot &out)
{
    RootScope roots(context);
    dictionary = roots.root(dictionary.get(context));
    auto type = roots.root(
        context.builtin(kind == DictViewKind::Keys     ? CoreObject::DictKeyIteratorType
                        : kind == DictViewKind::Values ? CoreObject::DictValueIteratorType
                                                       : CoreObject::DictItemIteratorType));
    context.allocate_instance<DictIteratorObject>(type, out, dictionary.get(context), kind);
    auto iterator = roots.root(out.value());
    auto &data = *context.payload<DictObject>(dictionary).contents;
    auto &state = context.payload<DictIteratorObject>(iterator);
    ReturnSlot owner(context);
    context.load(dictionary.get(context), context.payload<DictObject>(dictionary).namespace_owner,
                 owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot()) {
        DictView view(context, dictionary.get(context));
        state.expected_size = state.remaining = view.entries().size();
        context.store(iterator.get(context), state.snapshot, view.storage());
    } else
        namespace_locked(context, data, [&] {
            state.version = data.keys_version;
            state.expected_size = state.remaining = data.names.size();
            state.cursor = data.names.begin();
        });
}
void detail::next_dict_item(Attachment &context, Handle iterator, ReturnSlot &out)
{
    RootScope roots(context);
    iterator = roots.root(iterator.get(context));
    auto &state = context.payload<DictIteratorObject>(iterator);
    ReturnSlot key(context), value(context), snapshot(context), owner(context);
    context.load(iterator.get(context), state.dictionary, owner);
    if (owner.tag() == ResultTag::Empty) {
        stop_iteration(context, out);
        return;
    }
    auto dictionary = roots.root(owner.value());
    auto &data = *context.payload<DictObject>(dictionary).contents;
    context.load(dictionary.get(context), context.payload<DictObject>(dictionary).namespace_owner,
                 owner);
    std::optional<DictView> view;
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot())
        view.emplace(context, dictionary.get(context));
    bool changed_size = false, changed_keys = false, stopped = false;
    namespace_locked(context, data, [&] {
        const auto size = view ? view->entries().size() : data.names.size();
        if (state.exhausted) {
            stopped = true;
            return;
        }
        if (state.invalid || size != state.expected_size) {
            state.invalid = changed_size = true;
            return;
        }
        if (view) {
            context.load(iterator.get(context), state.snapshot, snapshot);
            const auto old = snapshot.tag() == ResultTag::Ok
                                 ? std::optional<Value>{snapshot.value()}
                                 : std::nullopt;
            const auto current = view->storage();
            if (old != current) {
                if (old) {
                    const auto &before = context.payload<DictStorage>(*old).entries;
                    const auto after = view->entries();
                    for (std::size_t i = 0; i < size; ++i)
                        if (before[i].key != after[i].key) {
                            state.invalid = changed_keys = true;
                            return;
                        }
                }
                context.store(iterator.get(context), state.snapshot, current);
            }
            if (state.position == size) {
                state.exhausted = stopped = true;
                return;
            }
            const auto &entry = view->entries()[state.position++];
            key.ok(entry.key);
            value.ok(entry.value);
            --state.remaining;
        } else {
            if (state.version != data.keys_version) {
                state.cursor =
                    std::find_if(data.names.cbegin(), data.names.cend(), [&](const auto &entry) {
                        return entry.identity > state.last_identity;
                    });
                state.version = data.keys_version;
            }
            if (state.cursor == data.names.cend()) {
                state.exhausted = stopped = true;
                return;
            }
            if (!state.remaining) {
                state.invalid = changed_keys = true;
                return;
            }
            const auto &entry = *state.cursor++;
            state.last_identity = entry.identity;
            --state.remaining;
            key.ok(entry.key);
            value.ok(entry.value);
        }
    });
    if (changed_keys || stopped) {
        context.store(iterator.get(context), state.dictionary, {});
        context.store(iterator.get(context), state.snapshot, {});
    }
    if (changed_size || changed_keys)
        raise_error(context, CoreObject::RuntimeErrorType,
                    changed_size ? U"dictionary changed size during iteration"
                                 : U"dictionary keys changed during iteration",
                    out);
    else if (stopped)
        stop_iteration(context, out);
    else if (state.kind == DictViewKind::Keys)
        out.ok(key.value());
    else if (state.kind == DictViewKind::Values)
        out.ok(value.value());
    else {
        const std::array pair{key.value(), value.value()};
        cxxpython::core::make_tuple(context, pair, out);
    }
}
} // namespace cxxpython::core
