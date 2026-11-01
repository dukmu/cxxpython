#include "internal/abstract.hpp"
#include "internal/dict.hpp"

#include <algorithm>
#include <bit>

namespace cxxpython::core {
DictObject::DictObject(InitialType head) : header(head), contents(std::make_unique<Storage>()) {}
DictObject::~DictObject() noexcept = default;
void DictObject::trace(Tracer &tracer) const noexcept
{
    tracer.visit(namespace_owner);
    for (const auto &node : contents->names) {
        tracer.visit(node.entry.key);
        tracer.visit(node.entry.value);
    }
}
std::size_t DictObject::external_bytes() const noexcept
{
    return contents->external_bytes();
}
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
                names_.clear();
                for (const auto &node : data.names)
                    names_.push_back(node.entry);
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
    return context_.payload<DictStorage>(storage_.value()).find(name);
}

std::size_t DictStorage::external_bytes() const noexcept
{
    auto bytes = entries.capacity() * sizeof(DictObject::Entry);
    if (index.bucket_count() > 1)
        bytes += index.bucket_count() * sizeof(void *);
    bytes += index.size() * sizeof(decltype(index)::value_type);
    return bytes;
}
void DictStorage::trace(Tracer &tracer) const noexcept
{
    for (const auto &entry : entries) {
        tracer.visit(entry.key);
        tracer.visit(entry.value);
    }
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
void remove_entry(Attachment &context, Value dictionary, DictObject::Storage &data,
                  DictObject::Node &node, std::list<DictObject::Node> &removed)
{
    if (node.name_hook.is_linked())
        data.index.erase(data.index.iterator_to(node));
    data.hashes.erase(data.hashes.iterator_to(node));
    removed.splice(removed.end(), data.names, node.order);
    ++data.keys_version;
    context.account_external(dictionary, data.external_bytes());
}
void insert_entry(Attachment &context, Value dictionary, DictObject::Storage &data,
                  std::list<DictObject::Node> &prepared)
{
    if (!data.next_identity)
        throw std::bad_alloc();
    auto &node = prepared.front();
    node.entry.identity = data.next_identity;
    data.hashes.insert(node);
    ++data.next_identity;
    data.names.splice(data.names.end(), prepared);
    if (node.name)
        data.index.insert(node);
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
        return found == data.index.end() ? std::optional<Value>{} : found->entry.value;
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
    std::unordered_map<std::u32string_view, std::size_t> index;
    bool exact = true;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        exact &= context.type_of(entries[i].key) == context.builtin(CoreObject::StrType);
        if (!context.has_layout<StrObject>(entries[i].key))
            continue;
        index.emplace(context.payload<StrObject>(entries[i].key).value, i);
        if (i % 64 == 63)
            context.poll();
    }
    context.allocate<DictStorage>(out, std::move(entries), std::move(index), exact);
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
            found->entry.value = value;
            return true;
        }))
        return;
    std::list<DictObject::Node> prepared;
    prepared.emplace_back(DictObject::Entry{name, value, name_hash(context, text)}, &text);
    prepared.back().order = prepared.begin();
    namespace_locked(context, data, [&] {
        const auto found = data.index.find(text);
        if (found != data.index.end())
            found->entry.value = value;
        else {
            insert_entry(context, dictionary, data, prepared);
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
    std::list<DictObject::Node> entries;
    return namespace_locked(context, data, [&] {
        const auto found = data.index.find(name);
        if (found == data.index.end())
            return false;
        remove_entry(context, dictionary, data, *found, entries);
        return true;
    });
}
bool dict_erase_binding(Attachment &context, Handle dictionary, DictBinding binding,
                        Handle expected)
{
    const auto target = dictionary.get(context), value = expected.get(context);
    auto &data = *context.payload<DictObject>(target).contents;
    std::list<DictObject::Node> entries;
    return namespace_locked(context, data, [&] {
        const auto at = data.hashes.find({binding.hash, binding.identity});
        if (at == data.hashes.end() || at->entry.value != value)
            return false;
        remove_entry(context, target, data, *at, entries);
        return true;
    });
}
void discard_name_table(Attachment &context, Value dictionary)
{
    auto &data = *context.payload<DictObject>(dictionary).contents;
    std::list<DictObject::Node> entries;
    DictObject::Storage::Index index;
    DictObject::Storage::HashIndex hashes;
    namespace_locked(context, data, [&] {
        data.names.swap(entries);
        data.index.swap(index);
        data.hashes.swap(hashes);
        ++data.keys_version;
        context.account_external(dictionary, sizeof(DictObject::Storage));
    });
}
void copy_dictionary_entries(Attachment &context, Value destination,
                             std::span<const DictObject::Entry> source)
{
    RootScope roots(context);
    roots.root(destination);
    std::list<DictObject::Node> entries;
    DictObject::Storage::Index names;
    DictObject::Storage::HashIndex hashes;
    std::uint64_t identity = 1;
    for (auto entry : source) {
        roots.root(entry.key);
        roots.root(entry.value);
        entry.identity = identity++;
        const std::u32string *name = nullptr;
        if (context.has_layout<StrObject>(entry.key))
            name = &context.payload<StrObject>(entry.key).value;
        entries.emplace_back(entry, name);
        auto &node = entries.back();
        node.order = std::prev(entries.end());
        if (node.name)
            names.insert(node);
        hashes.insert(node);
    }
    auto &data = *context.payload<DictObject>(destination).contents;
    namespace_locked(context, data, [&] {
        if (!data.names.empty())
            throw std::logic_error("dictionary clone requires an empty destination");
        data.names.swap(entries);
        data.index.swap(names);
        data.hashes.swap(hashes);
        data.next_identity = identity;
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
    out.reset(context);
    try {
        RootScope roots(context);
        if (!context.has_layout<DictObject>(dictionary.get(context)) ||
            !is_subtype(context, context.type_of(dictionary),
                        context.builtin(CoreObject::DictType))) {
            raise_error(context, CoreObject::TypeErrorType, U"dictionary operation requires dict",
                        out);
            return out.result();
        }
        const bool string = context.type_of(key) == context.builtin(CoreObject::StrType);
        const auto *text = string ? &context.payload<StrObject>(key).value : nullptr;
        std::int64_t hash_value = 0;
        ReturnSlot temporary(context), owner(context);
        if (cached_hash)
            hash_value = *cached_hash;
        else if (!string) {
            const auto word = detail::hash_word(context, key, temporary);
            if (!word) {
                out.raised(temporary.exception());
                return out.result();
            }
            hash_value = *word;
        }
        auto &shell = context.payload<DictObject>(dictionary);
        auto &data = *shell.contents;
        context.load(dictionary.get(context), shell.namespace_owner, owner);
        if (owner.tag() == ResultTag::Ok && TypeView(context, owner.value()).snapshot()) {
            if (binding)
                throw std::logic_error("binding tokens require an ordinary dictionary");
            if (operation != DictOperation::Lookup &&
                context.payload<TypeObject>(owner.value()).immutable) {
                raise_error(context, CoreObject::TypeErrorType, U"cannot modify immutable type",
                            out);
                return out.result();
            }
            if (string && !cached_hash)
                hash_value = detail::name_hash(context, *text);
            for (;;) {
                TypeView metadata(context, owner.value());
                const auto *table = metadata.namespace_data();
                auto entries = table ? std::span<const DictObject::Entry>{table->entries}
                                     : std::span<const DictObject::Entry>{};
                std::optional<std::size_t> found;
                bool compare = true;
                if (string && table) {
                    if (table->exact_string_keys) {
                        const auto at = table->index.find(*text);
                        if (at != table->index.end())
                            found = at->second;
                        compare = false;
                    }
                }
                if (compare) {
                    for (std::size_t i = 0; i < entries.size(); ++i) {
                        const auto &entry = entries[i];
                        if (entry.hash != hash_value)
                            continue;
                        detail::container_equal_owned(context, roots.root(entry.key), key,
                                                      temporary);
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
                TypeView current(context, owner.value());
                if (current.snapshot() != metadata.snapshot()) {
                    const auto *storage = current.namespace_data();
                    const auto updated = storage
                                             ? std::span<const DictObject::Entry>{storage->entries}
                                             : std::span<const DictObject::Entry>{};
                    bool stable = true;
                    if (found) {
                        // Unrelated publication must not repeat a successful __eq__.
                        // Keep the same key identity, but observe its current value.
                        const auto key = entries[*found].key;
                        const auto at =
                            std::find_if(updated.begin(), updated.end(), [&](const auto &entry) {
                                return entry.hash == hash_value && entry.key == key;
                            });
                        stable = at != updated.end();
                        if (stable)
                            found = static_cast<std::size_t>(at - updated.begin());
                    } else {
                        // A miss remains valid if its complete hash bucket is unchanged.
                        auto before = entries.begin(), after = updated.begin();
                        for (;;) {
                            before = std::find_if(before, entries.end(), [&](const auto &entry) {
                                return entry.hash == hash_value;
                            });
                            after = std::find_if(after, updated.end(), [&](const auto &entry) {
                                return entry.hash == hash_value;
                            });
                            if (before == entries.end() || after == updated.end()) {
                                stable = before == entries.end() && after == updated.end();
                                break;
                            }
                            if (before->key != after->key) {
                                stable = false;
                                break;
                            }
                            ++before;
                            ++after;
                        }
                    }
                    if (!stable) {
                        context.poll();
                        continue;
                    }
                    entries = updated;
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
                if (detail::publish_type_dictionary(context, owner.value(), *current.snapshot(),
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
        std::list<DictObject::Node> removed;
        auto finish = [&](DictObject::Node &node) {
            auto &entry = node.entry;
            if (binding)
                *binding = {entry.hash, entry.identity};
            if (operation == DictOperation::Store) {
                entry.value = value->get(context);
                out.ok(context.builtin(CoreObject::None));
            } else {
                out.ok(entry.value);
                if (operation == DictOperation::Erase)
                    detail::remove_entry(context, dictionary.get(context), data, node, removed);
            }
        };
        if (string && namespace_locked(context, data, [&] {
                const auto found = data.index.find(*text);
                if (found == data.index.end() ||
                    context.type_of(found->entry.key) != context.builtin(CoreObject::StrType))
                    return false;
                if (data.hashes.lower_bound({found->entry.hash, 0}) !=
                    data.hashes.iterator_to(*found))
                    return false;
                finish(*found);
                return true;
            }))
            return out.result();
        if (string && !cached_hash)
            hash_value = detail::name_hash(context, *text);
        std::list<DictObject::Node> prepared;
        for (;;) {
            if (namespace_locked(context, data, [&] {
                    const auto first = data.hashes.lower_bound({hash_value, 0});
                    if (first == data.hashes.end() || first->entry.hash != hash_value)
                        return operation == DictOperation::Lookup ||
                               operation == DictOperation::Erase;
                    // A later identity match must still observe earlier equality callbacks.
                    if (first->entry.key != key.get(context))
                        return false;
                    finish(*first);
                    return true;
                }))
                return out.result();
            RootScope iteration(context);
            std::vector<DictObject::Entry> candidates;
            std::uint64_t version;
            for (;;) {
                const auto count = namespace_locked(context, data, [&] {
                    version = data.keys_version;
                    const auto [begin, end] = data.equal_hashes(hash_value);
                    return static_cast<std::size_t>(std::distance(begin, end));
                });
                if (!count)
                    break;
                candidates.reserve(count);
                if (namespace_locked(context, data, [&] {
                        const auto [begin, end] = data.equal_hashes(hash_value);
                        if (static_cast<std::size_t>(std::distance(begin, end)) >
                            candidates.capacity())
                            return false;
                        version = data.keys_version;
                        for (auto at = begin; at != end; ++at)
                            candidates.push_back(at->entry);
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
                if (!namespace_locked(context, data, [&] {
                        return data.keys_version == version ||
                               data.hashes.find({hash_value, entry.identity}) != data.hashes.end();
                    }))
                    continue;
                detail::container_equal_owned(context, iteration.root(entry.key), key, temporary);
                if (temporary.tag() == ResultTag::Raised) {
                    out.raised(temporary.exception());
                    return out.result();
                }
                if (temporary.value() != context.builtin(CoreObject::True))
                    continue;
                const bool committed = namespace_locked(context, data, [&] {
                    const auto at = data.hashes.find({hash_value, entry.identity});
                    if (at == data.hashes.end())
                        return false;
                    finish(*at);
                    return true;
                });
                if (committed)
                    return out.result();
                restart = true;
                break;
            }
            if (!restart) {
                if ((operation == DictOperation::Store || operation == DictOperation::SetDefault) &&
                    prepared.empty()) {
                    prepared.emplace_back(
                        DictObject::Entry{key.get(context), value->get(context), hash_value}, text);
                    prepared.back().order = prepared.begin();
                }
                if (namespace_locked(context, data, [&] {
                        if (data.keys_version != version) {
                            auto [at, end] = data.equal_hashes(hash_value);
                            for (const auto &entry : candidates) {
                                if (at == end || at->entry.identity != entry.identity)
                                    return false;
                                ++at;
                            }
                            if (at != end)
                                return false;
                        }
                        if (operation == DictOperation::Store ||
                            operation == DictOperation::SetDefault) {
                            detail::insert_entry(context, dictionary.get(context), data, prepared);
                            if (binding)
                                *binding = {data.names.back().entry.hash,
                                            data.names.back().entry.identity};
                            out.ok(operation == DictOperation::SetDefault
                                       ? value->get(context)
                                       : context.builtin(CoreObject::None));
                        }
                        return true;
                    }))
                    return out.result();
            }
            context.poll();
        }
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
ResultView dictionary_operation(Attachment &context, Handle dictionary, Handle key,
                                std::optional<Handle> value, DictOperation operation,
                                ReturnSlot &out, std::optional<std::int64_t> cached_hash = {},
                                detail::DictBinding *binding = nullptr)
{
    out.check_caller(context);
    if (binding)
        *binding = {};
    try {
        RootScope roots(context);
        dictionary = roots.root(dictionary.get(context));
        key = roots.root(key.get(context));
        if (value)
            value = roots.root(value->get(context));
        return access_dictionary(context, dictionary, key, value, operation, out, cached_hash,
                                 binding);
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
} // namespace
ResultView dict_lookup(Attachment &context, Handle dictionary, Handle key, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, {}, DictOperation::Lookup, out);
}
ResultView detail::dict_lookup_binding(Attachment &context, Handle dictionary, Handle key,
                                       DictBinding &binding, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, {}, DictOperation::Lookup, out, {},
                                &binding);
}
ResultView dict_store(Attachment &context, Handle dictionary, Handle key, Handle value,
                      ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, value, DictOperation::Store, out);
}
ResultView detail::dict_store_binding(Attachment &context, Handle dictionary, Handle key,
                                      Handle value, DictBinding &binding, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, value, DictOperation::Store, out, {},
                                &binding);
}
ResultView dict_erase(Attachment &context, Handle dictionary, Handle key, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, {}, DictOperation::Erase, out);
}
ResultView dict_setdefault(Attachment &context, Handle dictionary, Handle key, Handle value,
                           ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, value, DictOperation::SetDefault, out);
}
ResultView detail::dict_lookup_owned(Attachment &context, Handle dictionary, Handle key,
                                     ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Lookup, out);
}
ResultView detail::dict_store_owned(Attachment &context, Handle dictionary, Handle key,
                                    Handle value, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, value, DictOperation::Store, out);
}
ResultView detail::dict_erase_owned(Attachment &context, Handle dictionary, Handle key,
                                    ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Erase, out);
}
ResultView detail::dict_setdefault_owned(Attachment &context, Handle dictionary, Handle key,
                                         Handle value, ReturnSlot &out)
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
void detail::dict_popitem(Attachment &context, Handle dictionary, ReturnSlot &key,
                          ReturnSlot &value)
{
    key.check_caller(context);
    value.check_caller(context);
    RootScope roots(context);
    dictionary = roots.root(dictionary.get(context));
    key.reset(context);
    value.reset(context);
    auto &shell = context.payload<DictObject>(dictionary);
    ReturnSlot owner(context);
    context.load(dictionary.get(context), shell.namespace_owner, owner);
    if (owner.tag() == ResultTag::Ok) {
        raise_error(context, CoreObject::TypeErrorType, U"cannot pop a type namespace", key);
        return;
    }
    auto &data = *shell.contents;
    std::list<DictObject::Node> removed;
    namespace_locked(context, data, [&] {
        if (data.names.empty())
            return;
        auto &node = data.names.back();
        key.ok(node.entry.key);
        value.ok(node.entry.value);
        remove_entry(context, dictionary.get(context), data, node, removed);
    });
}
ResultView detail::dict_lookup_hash(Attachment &context, Handle dictionary, Handle key,
                                    std::int64_t hash, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, {}, DictOperation::Lookup, out, hash);
}
ResultView detail::dict_store_hash(Attachment &context, Handle dictionary, Handle key, Handle value,
                                   std::int64_t hash, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, value, DictOperation::Store, out, hash);
}
ResultView detail::dict_erase_hash(Attachment &context, Handle dictionary, Handle key,
                                   std::int64_t hash, ReturnSlot &out)
{
    return dictionary_operation(context, dictionary, key, {}, DictOperation::Erase, out, hash);
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
                                ReturnSlot &out, std::optional<Handle> iterator_type)
{
    RootScope roots(context);
    dictionary = roots.root(dictionary.get(context));
    auto type = roots.root(
        iterator_type
            ? iterator_type->get(context)
            : context.builtin(kind == DictViewKind::Keys     ? CoreObject::DictKeyIteratorType
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
            std::optional<Value> old;
            if (snapshot.tag() == ResultTag::Ok)
                old.emplace(snapshot.value());
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
                    std::find_if(data.names.cbegin(), data.names.cend(), [&](const auto &node) {
                        return node.entry.identity > state.last_identity;
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
            const auto &entry = (state.cursor++)->entry;
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
                    context.type_of(iterator) == context.builtin(CoreObject::SetIteratorType)
                        ? U"Set changed size during iteration"
                    : changed_size ? U"dictionary changed size during iteration"
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
