#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <bit>

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
std::size_t entry_bytes(bool named)
{
    return sizeof(DictObject::Entry) + 2 * sizeof(void *) +
           sizeof(DictObject::HashIndex::value_type) + 4 * sizeof(void *) +
           (named ? sizeof(DictObject::Index::value_type) + 6 * sizeof(void *) : 0);
}
void remove_entry(Attachment &context, Value dictionary, DictObject::Storage &data,
                  std::list<DictObject::Entry>::iterator entry,
                  std::list<DictObject::Entry> &removed)
{
    std::size_t bytes = entry_bytes(false);
    if (context.has_layout<StrObject>(entry->key)) {
        const auto &label = context.payload<StrObject>(entry->key).value;
        auto found = data.index.find(label);
        if (found != data.index.end() && found->second == entry) {
            bytes = entry_bytes(true);
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
    data.bytes += entry_bytes(bool(name));
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
        bool named = false;
        if (context.has_layout<StrObject>(entry.key)) {
            const auto inserted = names.emplace(context.payload<StrObject>(entry.key).value, at);
            named = inserted.second;
        }
        hashes.emplace(entry.hash, at);
        bytes += entry_bytes(named);
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
        const auto *text = string ? &context.payload<StrObject>(key).value : nullptr;
        std::int64_t hash_value = 0;
        ReturnSlot temporary(context), owner(context);
        if (cached_hash)
            hash_value = *cached_hash;
        else if (!string) {
            hash(context, key, temporary);
            if (temporary.tag() == ResultTag::Raised) {
                out.raised(temporary.exception());
                return out.result();
            }
            if (!int_fits_signed(context, temporary.value(), hash_value))
                throw std::logic_error("hash protocol did not normalize its result");
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
                DictView snapshot(context, metadata.storage());
                const auto entries = snapshot.entries();
                std::optional<std::size_t> found;
                bool compare = true;
                if (string && metadata.storage()) {
                    const auto &table = context.payload<DictStorage>(*metadata.storage());
                    const auto at = table.index.find(*text);
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
            if (binding)
                *binding = {at->hash, at->identity};
            if (operation == DictOperation::Store) {
                at->value = value->get(context);
                out.ok(context.builtin(CoreObject::None));
            } else {
                out.ok(at->value);
                if (operation == DictOperation::Erase)
                    detail::remove_entry(context, dictionary.get(context), data, at, removed);
            }
        };
        if (string && namespace_locked(context, data, [&] {
                const auto found = data.index.find(*text);
                if (found == data.index.end() ||
                    context.type_of(found->second->key) != context.builtin(CoreObject::StrType))
                    return false;
                finish(found->second);
                return true;
            }))
            return out.result();
        if (string && !cached_hash)
            hash_value = detail::name_hash(context, *text);
        std::list<DictObject::Entry> prepared;
        DictObject::Index::node_type name_node;
        DictObject::HashIndex::node_type hash_node;
        if (operation == DictOperation::Store || operation == DictOperation::SetDefault) {
            prepared.push_back({key.get(context), value->get(context), hash_value});
            DictObject::Index names;
            if (string) {
                names.emplace(*text, prepared.begin());
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
ResultView detail::dict_lookup_binding(Attachment &context, Handle dictionary, Handle key,
                                       DictBinding &binding, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Lookup, out, {},
                             &binding);
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
    std::list<DictObject::Entry> removed;
    namespace_locked(context, data, [&] {
        if (data.names.empty())
            return;
        const auto entry = std::prev(data.names.end());
        key.ok(entry->key);
        value.ok(entry->value);
        remove_entry(context, dictionary.get(context), data, entry, removed);
    });
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
ResultView detail::dict_erase_hash(Attachment &context, Handle dictionary, Handle key,
                                   std::int64_t hash, ReturnSlot &out)
{
    return access_dictionary(context, dictionary, key, {}, DictOperation::Erase, out, hash);
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
