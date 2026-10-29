#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core {
DictView::DictView(Attachment &context, std::optional<Value> dictionary)
    : context_(context), storage_(context)
{
    if (!dictionary)
        return;
    if (context.has_layout<DictStorage>(*dictionary)) {
        storage_.ok(*dictionary);
        return;
    }
    auto &data = context.payload<DictObject>(*dictionary);
    ReturnSlot owner(context);
    context.load(*dictionary, data.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok) {
        TypeView type(context, owner.value());
        if (type.snapshot()) {
            if (auto storage = type.storage())
                storage_.ok(*storage);
            return;
        }
    }
    context.load(*dictionary, data.storage, storage_);
}
std::optional<Value> DictView::storage() const
{
    return storage_.tag() == ResultTag::Ok ? std::optional<Value>{storage_.value()}
                                           : std::optional<Value>{};
}
std::span<const DictObject::Entry> DictView::entries() const
{
    auto value = storage();
    return value ? std::span<const DictObject::Entry>{context_.payload<DictStorage>(*value).entries}
                 : std::span<const DictObject::Entry>{};
}
std::optional<Value> DictView::find(const std::u32string &name) const
{
    auto value = storage();
    if (!value)
        return {};
    const auto &data = context_.payload<DictStorage>(*value);
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
std::optional<Value> name_lookup(Attachment &context, Value dictionary, const std::u32string &name)
{
    return DictView(context, dictionary).find(name);
}
bool copy_name_storage(Attachment &context, std::optional<Value> source, const std::u32string &name,
                       std::optional<Value> key, std::optional<Value> value, ReturnSlot &out)
{
    DictView old(context, source);
    std::vector<DictObject::Entry> entries(old.entries().begin(), old.entries().end());
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return context.payload<StrObject>(entry.key).value == name;
    });
    if (value) {
        if (found != entries.end())
            found->value = *value;
        else {
            if (!key)
                throw std::logic_error("new name requires a managed key");
            entries.push_back({*key, *value});
        }
    } else {
        if (found == entries.end())
            return false;
        entries.erase(found);
    }
    std::unordered_map<std::u32string, std::size_t> index;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto [entry, inserted] = index.emplace(context.payload<StrObject>(entries[i].key).value, i);
        (void)inserted;
        bytes += string_bytes(entry->first);
        if (i % 64 == 63)
            context.poll();
    }
    context.allocate<DictStorage>(out, std::move(entries), std::move(index), bytes);
    return true;
}
void name_store(Attachment &context, Value dictionary, Value name, Value value)
{
    RootScope roots(context);
    roots.root(dictionary);
    roots.root(name);
    roots.root(value);
    (void)context.type_of(value);
    const auto &text = context.payload<StrObject>(name).value;
    auto &data = context.payload<DictObject>(dictionary);
    ReturnSlot staged(context);
    ReturnSlot owner(context);
    context.load(dictionary, data.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot()) {
        type_namespace_change(context, owner.value(), text, name, value);
        return;
    }
    for (;;) {
        {
            DictView old(context, dictionary);
            copy_name_storage(context, old.storage(), text, name, value, staged);
            if (context.compare_exchange(dictionary, data.storage, old.storage(), staged.value()))
                return;
        }
        staged.reset();
        context.poll();
    }
}
bool name_erase(Attachment &context, Value dictionary, const std::u32string &name)
{
    RootScope roots(context);
    roots.root(dictionary);
    auto &data = context.payload<DictObject>(dictionary);
    ReturnSlot staged(context);
    ReturnSlot owner(context);
    context.load(dictionary, data.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot())
        return type_namespace_change(context, owner.value(), name, {}, {});
    for (;;) {
        {
            DictView old(context, dictionary);
            if (!copy_name_storage(context, old.storage(), name, {}, {}, staged))
                return false;
            if (context.compare_exchange(dictionary, data.storage, old.storage(), staged.value()))
                return true;
        }
        staged.reset();
        context.poll();
    }
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
                       ReturnSlot &out)
{
    out.reset(context);
    signature.validate();
    if (!entry)
        throw std::invalid_argument("native function requires an entrypoint");
    context.allocate_instance<NativeFunctionObject>(
        type, out, std::move(name), std::move(signature), entry, std::move(captures));
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
        if (id == CoreObject::FunctionType)
            spec.slots = std::vector<std::u32string>{U"__dict__"};
        if (id == CoreObject::StopIterationType)
            spec.slots = std::vector<std::u32string>{U"value"};
        create_native_type(context, NativeTypeSpec{spec, layout},
                           *outputs[static_cast<std::size_t>(id)]);
        core.push_back(roots.root(outputs[static_cast<std::size_t>(id)]->result().value()));
    };
    define(CoreObject::NoneType, "NoneType", LayoutDescriptor::of<NoneObject>());
    define(CoreObject::StrType, "str", LayoutDescriptor::of<StrObject>());
    define(CoreObject::TupleType, "tuple", LayoutDescriptor::of<TupleObject>());
    define(CoreObject::DictType, "dict", LayoutDescriptor::of<DictObject>());
    define(CoreObject::IntType, "int", LayoutDescriptor::of<IntObject>());
    define(CoreObject::BoolType, "bool", LayoutDescriptor::of<IntObject>(),
           at(CoreObject::IntType));
    define(CoreObject::NotImplementedType, "NotImplementedType",
           LayoutDescriptor::of<NoneObject>());
    define(CoreObject::BaseExceptionType, "BaseException", LayoutDescriptor::of<ExceptionObject>());
    define(CoreObject::ExceptionType, "Exception", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
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
    define(CoreObject::StopIterationType, "StopIteration", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::NameErrorType, "NameError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::UnboundLocalErrorType, "UnboundLocalError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::NameErrorType));
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
    install_protocols(context, core);
    install_value_protocols(context, core);
    install_execution(context, core);
    install_exception_runtime(context, core);
    install_ranges(context, core);
    install_iteration(context, core);
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        context.payload<TypeObject>(core[i]).immutable = true;
        context.payload<TypeObject>(core[i]).state = TypeState::Ready;
    }
    for (auto type : {context.object_type(), context.type_type()})
        context.payload<TypeObject>(type).state = TypeState::Ready;
    for (auto id :
         {CoreObject::NoneType, CoreObject::BoolType, CoreObject::NotImplementedType,
          CoreObject::NativeFunctionType, CoreObject::MethodDescriptorType,
          CoreObject::BoundMethodType, CoreObject::GetSetDescriptorType,
          CoreObject::MappingProxyType, CoreObject::MemberDescriptorType, CoreObject::CellType,
          CoreObject::CodeType, CoreObject::FunctionType, CoreObject::FrameType,
          CoreObject::TracebackType, CoreObject::RangeType, CoreObject::RangeIteratorType,
          CoreObject::TupleIteratorType, CoreObject::StrIteratorType})
        context.payload<TypeObject>(at(id)).subclassable = false;

    auto &builtins = *outputs[static_cast<std::size_t>(CoreObject::Builtins)];
    context.allocate_instance<DictObject>(at(CoreObject::DictType), builtins);
    core.push_back(roots.root(builtins.value()));
    const auto mapping = builtins.value();
    auto publish = [&](Value key, Value value) { name_store(context, mapping, key, value); };
    for (auto id : {CoreObject::StrType,
                    CoreObject::TupleType,
                    CoreObject::DictType,
                    CoreObject::IntType,
                    CoreObject::BoolType,
                    CoreObject::BaseExceptionType,
                    CoreObject::ExceptionType,
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
                    CoreObject::StopIterationType,
                    CoreObject::RangeType,
                    CoreObject::NameErrorType,
                    CoreObject::UnboundLocalErrorType,
                    CoreObject::StaticMethodType,
                    CoreObject::ClassMethodType})
        publish(std::get<Value>(context.payload<TypeObject>(at(id)).name), at(id).get(context));
    for (auto type : {context.object_type(), context.type_type()})
        publish(std::get<Value>(context.payload<TypeObject>(type).name), type);
    for (auto [name, id] : {std::pair{U"None", CoreObject::None},
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
}
} // namespace detail
} // namespace cxxpython::core
