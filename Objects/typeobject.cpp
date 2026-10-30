#include "cxxpython/objects.hpp"
#include "cxxpython/type.hpp"

#include <algorithm>

namespace cxxpython::core {
TypeSnapshot::TypeSnapshot(InitialType head, Value name, Value qualname, Value dictionary,
                           std::optional<Value> storage, Value bases, Value mro)
    : header(head), name(name), qualname(qualname), dictionary(dictionary), bases(bases), mro(mro),
      storage(storage), finalizer([&]() -> std::optional<Value> {
          if (storage) {
              const auto &data = *reinterpret_cast<const DictStorage *>(storage->object());
              for (const auto &[key, index] : data.index)
                  if (key == U"__del__")
                      return data.entries[index].value;
          }
          return {};
      }())
{}
TypeView::TypeView(Attachment &context, Value type)
    : context_(context), owner_(context), metadata_(context)
{
    owner_.ok(type);
    context.load(type, context.payload<TypeObject>(type).metadata, metadata_);
}
std::optional<Value> TypeView::snapshot() const
{
    return metadata_.tag() == ResultTag::Ok ? std::optional<Value>{metadata_.value()}
                                            : std::optional<Value>{};
}
std::variant<std::u32string, Value> TypeView::name() const
{
    auto meta = snapshot();
    return meta ? std::variant<std::u32string, Value>{context_.payload<TypeSnapshot>(*meta).name}
                : context_.payload<TypeObject>(owner_.value()).name;
}
std::variant<std::u32string, Value> TypeView::qualname() const
{
    auto meta = snapshot();
    return meta
               ? std::variant<std::u32string, Value>{context_.payload<TypeSnapshot>(*meta).qualname}
               : context_.payload<TypeObject>(owner_.value()).qualname;
}
std::optional<Value> TypeView::dictionary() const
{
    auto meta = snapshot();
    return meta ? std::optional<Value>{context_.payload<TypeSnapshot>(*meta).dictionary}
                : context_.payload<TypeObject>(owner_.value()).dictionary;
}
std::optional<Value> TypeView::storage() const
{
    auto meta = snapshot();
    if (meta)
        return context_.payload<TypeSnapshot>(*meta).storage;
    DictView dictionary(context_, this->dictionary());
    if (auto storage = dictionary.storage()) {
        if (!staged_storage_)
            staged_storage_.emplace(context_);
        staged_storage_->ok(*storage);
        return staged_storage_->value();
    }
    return {};
}

namespace {
TypeObject &ready_type(Attachment &context, Handle handle)
{
    auto &type = context.payload<TypeObject>(handle);
    if (type.state == TypeState::Building)
        throw TypeDefinitionError("base or metaclass structure is not ready");
    return type;
}

std::vector<Value> merge_mro(Attachment &context, const std::vector<Handle> &bases)
{
    std::vector<Value> direct;
    std::vector<std::span<const Value>> sequences;
    for (auto base : bases) {
        auto value = base.get(context);
        if (std::find(direct.begin(), direct.end(), value) != direct.end())
            throw TypeDefinitionError("duplicate base class");
        direct.push_back(value);
        if (!ready_type(context, base).subclassable)
            throw TypeDefinitionError("type is not an acceptable base");
        sequences.emplace_back(ready_type(context, base).mro);
    }
    sequences.emplace_back(direct);
    std::vector<Value> result;
    for (;;) {
        bool any = false;
        std::optional<Value> candidate;
        for (auto sequence : sequences) {
            if (sequence.empty())
                continue;
            any = true;
            auto head = sequence.front();
            bool in_tail = std::any_of(sequences.begin(), sequences.end(), [head](auto other) {
                return !other.empty() &&
                       std::find(other.begin() + 1, other.end(), head) != other.end();
            });
            if (!in_tail) {
                candidate = head;
                break;
            }
        }
        if (!any)
            return result;
        if (!candidate)
            throw TypeDefinitionError("cannot create a consistent method resolution order");
        result.push_back(*candidate);
        for (auto &sequence : sequences)
            if (!sequence.empty() && sequence.front() == *candidate)
                sequence = sequence.subspan(1);
    }
}

void create(Attachment &context, const TypeSpec &spec, const LayoutDescriptor *native_layout,
            ReturnSlot &out)
{
    out.reset(context);
    if (spec.dictionary && !context.core_ready())
        throw TypeDefinitionError("managed type namespaces require an initialized core runtime");
    auto valid_text = [](const std::u32string &text) {
        return std::all_of(text.begin(), text.end(),
                           [](char32_t point) { return point <= 0x10ffff; });
    };
    if (!valid_text(spec.name) || (spec.qualname && !valid_text(*spec.qualname)) ||
        (spec.module && !valid_text(*spec.module)))
        throw TypeDefinitionError("native type metadata has an invalid Unicode codepoint");
    RootScope roots(context);
    auto object = roots.root(context.object_type());
    auto type = roots.root(context.type_type());
    std::vector<Handle> bases(spec.bases.begin(), spec.bases.end());
    if (bases.empty())
        bases.push_back(object);
    auto ancestors = merge_mro(context, bases);

    auto metaclass = detail::select_metaclass(context, spec.metaclass.value_or(type), bases, roots);
    Handle solid = object;
    Handle best_base = bases.front();
    for (auto base : bases) {
        auto candidate = roots.root(*ready_type(context, base).solid_base);
        if (!is_subtype(context, solid, candidate)) {
            if (!is_subtype(context, candidate, solid))
                throw TypeDefinitionError("multiple bases have incompatible native layouts");
            solid = candidate;
            best_base = base;
        }
    }

    auto layout = ready_type(context, solid).instance_layout;
    auto slot_names = ready_type(context, solid).slot_names;
    bool introduces_layout = false;
    if (native_layout) {
        if (!native_layout->native_family || !native_layout->trace || !native_layout->destroy ||
            native_layout->size < sizeof(ObjectHeader) ||
            native_layout->alignment < alignof(ObjectHeader) ||
            (native_layout->alignment & (native_layout->alignment - 1)) != 0)
            throw TypeDefinitionError("invalid native layout descriptor");
        auto prefix = layout;
        prefix.fields = {};
        if (native_layout->fields != InstanceFieldPlan{})
            throw TypeDefinitionError("native layout fields must be planned by TypeReady");
        introduces_layout = prefix != *native_layout;
        if (introduces_layout && solid.get(context) != object.get(context))
            throw TypeDefinitionError("native definition changes an inherited storage layout");
        layout = *native_layout;
    }
    const bool metatype =
        std::find(ancestors.begin(), ancestors.end(), type.get(context)) != ancestors.end();
    bool allow_slots = true;
    bool inherited_dictionary = false, inherited_weakrefs = false, allow_weakrefs = true;
    for (auto base : bases) {
        allow_slots &= ready_type(context, base).allow_slots;
        inherited_dictionary |= ready_type(context, base).instance_layout.fields.dictionary;
        inherited_weakrefs |= ready_type(context, base).instance_layout.fields.weakrefs;
        allow_weakrefs &= ready_type(context, base).allow_weakrefs;
    }
    bool dictionary = inherited_dictionary || (!metatype && !native_layout && !spec.slots);
    bool weakrefs = inherited_weakrefs || (!native_layout && !spec.slots && allow_weakrefs);
    std::vector<std::u32string> own_slots;
    if (spec.slots && !spec.slots->empty() && !allow_slots)
        throw TypeDefinitionError("nonempty slots are not allowed for this native base");
    if (spec.slots)
        for (auto name : *spec.slots) {
            if (!is_identifier(name))
                throw TypeDefinitionError("slot names must be identifiers");
            if (name == U"__dict__") {
                if (dictionary || metatype)
                    throw TypeDefinitionError("instance dictionary slot already provided");
                dictionary = true;
                continue;
            }
            if (name == U"__weakref__") {
                if (weakrefs || !allow_weakrefs)
                    throw TypeDefinitionError("weakref slot is already provided or disallowed");
                weakrefs = true;
                continue;
            }
            if (!allow_slots)
                throw TypeDefinitionError("nonempty slots are not allowed for this native base");
            if (name.starts_with(U"__") && !name.ends_with(U"__")) {
                auto first = spec.name.find_first_not_of(U'_');
                if (first != std::u32string::npos)
                    name = U"_" + spec.name.substr(first) + name;
            }
            if (spec.dictionary &&
                detail::name_lookup(context, spec.dictionary->get(context), name))
                throw TypeDefinitionError("slot conflicts with a class variable",
                                          CoreObject::ValueErrorType);
            own_slots.push_back(std::move(name));
        }
    std::sort(own_slots.begin(), own_slots.end());
    slot_names.insert(slot_names.end(), own_slots.begin(), own_slots.end());
    layout.fields = {slot_names.size(), dictionary, weakrefs};
    (void)layout.storage_size();

    // Only a fully initialized type is handed to the caller. The staged
    // object is rooted while all host buffers and graph edges are filled.
    ReturnSlot staged(context);
    context.allocate_instance<TypeObject>(metaclass, staged, spec.name, layout);
    auto created = roots.root(staged.value());
    auto &payload = context.payload<TypeObject>(created);
    payload.qualname = spec.qualname.value_or(spec.name);
    payload.bootstrap_module = spec.module;
    for (auto base : bases)
        payload.bases.push_back(base.get(context));
    payload.mro.reserve(ancestors.size() + 1);
    payload.mro.push_back(created.get(context));
    payload.mro.insert(payload.mro.end(), ancestors.begin(), ancestors.end());
    payload.solid_base = introduces_layout ? created.get(context) : solid.get(context);
    if (!own_slots.empty())
        payload.solid_base = created.get(context);
    payload.base = best_base.get(context);
    payload.slot_names = std::move(slot_names);
    payload.allow_slots = allow_slots;
    payload.allow_weakrefs = allow_weakrefs;
    context.account_external(created, payload.external_bytes());
    payload.state = TypeState::Structural;
    if (context.core_ready())
        detail::type_ready(context, created, spec.dictionary);
    out.ok(created.get(context));
}
} // namespace

void TypeObject::trace(Tracer &tracer) const noexcept
{
    tracer.visit(metadata);
    if (auto value = std::get_if<Value>(&name))
        tracer.visit(*value);
    if (auto value = std::get_if<Value>(&qualname))
        tracer.visit(*value);
    for (auto base : bases)
        tracer.visit(base);
    for (auto entry : mro)
        tracer.visit(entry);
    if (solid_base)
        tracer.visit(*solid_base);
    if (base)
        tracer.visit(*base);
    if (dictionary)
        tracer.visit(*dictionary);
}

std::size_t TypeObject::external_bytes() const noexcept
{
    auto string_bytes = [this](const std::u32string &text) {
        const auto address = reinterpret_cast<std::uintptr_t>(text.data());
        const auto start = reinterpret_cast<std::uintptr_t>(this);
        return address >= start && address - start < sizeof(*this)
                   ? 0U
                   : (text.capacity() + 1) * sizeof(char32_t);
    };
    auto bytes = (bases.capacity() + mro.capacity()) * sizeof(Value);
    if (auto text = std::get_if<std::u32string>(&name))
        bytes += string_bytes(*text);
    if (auto text = std::get_if<std::u32string>(&qualname))
        bytes += string_bytes(*text);
    if (bootstrap_module)
        bytes += string_bytes(*bootstrap_module);
    bytes += slot_names.capacity() * sizeof(std::u32string);
    for (const auto &name : slot_names) {
        auto address = reinterpret_cast<std::uintptr_t>(name.data());
        auto start = reinterpret_cast<std::uintptr_t>(&name);
        if (address < start || address - start >= sizeof(name))
            bytes += (name.capacity() + 1) * sizeof(char32_t);
    }
    return bytes;
}

bool is_subtype(Attachment &context, Handle type, Handle base)
{
    auto &left = ready_type(context, type);
    (void)ready_type(context, base);
    return std::find(left.mro.begin(), left.mro.end(), base.get(context)) != left.mro.end();
}
bool is_subtype(Attachment &context, Value type, Value base)
{
    auto &left = context.payload<TypeObject>(type);
    auto &right = context.payload<TypeObject>(base);
    if (left.state == TypeState::Building || right.state == TypeState::Building)
        throw std::logic_error("subtype check requires initialized type metadata");
    return std::find(left.mro.begin(), left.mro.end(), base) != left.mro.end();
}

void create_type(Attachment &context, const TypeSpec &spec, ReturnSlot &out)
{
    create(context, spec, nullptr, out);
}

void create_native_type(Attachment &context, const NativeTypeSpec &spec, ReturnSlot &out)
{
    create(context, spec.type, &spec.layout, out);
}

bool is_layout_compatible(Attachment &context, Value left, Value right)
{
    auto &a = context.payload<TypeObject>(left);
    auto &b = context.payload<TypeObject>(right);
    if (a.instance_layout != b.instance_layout || a.slot_names != b.slot_names)
        return false;
    auto storage_base = [&](Value type) {
        for (;;) {
            auto &data = context.payload<TypeObject>(type);
            if (!data.base)
                return type;
            auto prefix = data.instance_layout;
            auto &parent_type = context.payload<TypeObject>(*data.base);
            auto parent = parent_type.instance_layout;
            prefix.fields = {};
            parent.fields = {};
            if (prefix != parent || data.slot_names != parent_type.slot_names)
                return type;
            type = *data.base;
        }
    };
    left = storage_base(left);
    right = storage_base(right);
    if (left == right)
        return true;
    auto &left_base = context.payload<TypeObject>(left);
    auto &right_base = context.payload<TypeObject>(right);
    if (!left_base.base || left_base.base != right_base.base ||
        left_base.slot_names != right_base.slot_names ||
        left_base.instance_layout != right_base.instance_layout)
        return false;
    auto prefix = left_base.instance_layout;
    auto parent = context.payload<TypeObject>(*left_base.base).instance_layout;
    prefix.fields = {};
    parent.fields = {};
    return prefix == parent;
}

LayoutDescriptor Attachment::require_instance_layout(Handle type, const LayoutDescriptor &layout)
{
    auto approved = ready_type(*this, type).instance_layout;
    auto prefix = approved;
    prefix.fields = {};
    if (prefix != layout)
        throw std::invalid_argument("instance layout does not match its Python type");
    return approved;
}

namespace detail {
namespace {
bool commit_type(Attachment &context, Value type, std::optional<Value> expected, Value desired)
{
    const auto epoch = context.type_epoch();
    NoSafepoint commit(context);
    if (!context.begin_publication(PublicationDomain::Type, epoch))
        return false;
    const bool published = context.compare_exchange(
        type, context.payload<TypeObject>(type).metadata, expected, desired);
    context.end_publication(PublicationDomain::Type, epoch);
    return published;
}
} // namespace
void publish_type(Attachment &context, Value type)
{
    TypeView view(context, type);
    if (view.snapshot())
        return;
    if (!view.dictionary())
        throw std::logic_error("ready type has no namespace");
    context.store(*view.dictionary(),
                  context.payload<DictObject>(*view.dictionary()).namespace_owner, type);
    RootScope roots(context);
    ReturnSlot staged(context), temporary(context);
    cxxpython::core::make_tuple(context, context.payload<TypeObject>(type).bases, temporary);
    const auto bases = roots.root(temporary.value());
    cxxpython::core::make_tuple(context, context.payload<TypeObject>(type).mro, temporary);
    const auto mro = roots.root(temporary.value());
    context.allocate<TypeSnapshot>(staged, std::get<Value>(view.name()),
                                   std::get<Value>(view.qualname()), *view.dictionary(),
                                   view.storage(), bases.get(context), mro.get(context));
    while (!commit_type(context, type, {}, staged.value())) {
        context.load(type, context.payload<TypeObject>(type).metadata, temporary);
        if (temporary.tag() != ResultTag::Empty)
            throw std::logic_error("unpublished type changed concurrently");
        context.poll();
    }
    auto &data = context.payload<TypeObject>(type);
    discard_name_table(context, *view.dictionary());
    data.name = std::u32string{};
    data.qualname = std::u32string{};
    data.dictionary.reset();
    context.account_external(type, data.external_bytes());
}
void type_name_store(Attachment &context, Value type, Value name, bool qualified)
{
    RootScope roots(context);
    roots.root(type);
    roots.root(name);
    ReturnSlot staged(context);
    for (;;) {
        {
            TypeView old(context, type);
            if (!old.snapshot())
                throw std::logic_error("type mutation requires published metadata");
            context.allocate<TypeSnapshot>(staged, qualified ? std::get<Value>(old.name()) : name,
                                           qualified ? name : std::get<Value>(old.qualname()),
                                           *old.dictionary(), old.storage(),
                                           context.payload<TypeSnapshot>(*old.snapshot()).bases,
                                           context.payload<TypeSnapshot>(*old.snapshot()).mro);
            if (commit_type(context, type, old.snapshot(), staged.value()))
                return;
        }
        staged.reset();
        context.poll();
    }
}
bool type_namespace_change(Attachment &context, Value type, const std::u32string &name,
                           std::optional<Value> key, std::optional<Value> value)
{
    RootScope roots(context);
    roots.root(type);
    if (key)
        roots.root(*key);
    if (value)
        roots.root(*value);
    ReturnSlot storage(context), staged(context);
    for (;;) {
        {
            TypeView old(context, type);
            if (!old.snapshot())
                throw std::logic_error("type mutation requires published metadata");
            if (!copy_name_storage(context, old.storage(), name, key, value, storage))
                return false;
            context.allocate<TypeSnapshot>(staged, std::get<Value>(old.name()),
                                           std::get<Value>(old.qualname()), *old.dictionary(),
                                           storage.value(),
                                           context.payload<TypeSnapshot>(*old.snapshot()).bases,
                                           context.payload<TypeSnapshot>(*old.snapshot()).mro);
            if (commit_type(context, type, old.snapshot(), staged.value()))
                return true;
        }
        staged.reset();
        storage.reset();
        context.poll();
    }
}
bool publish_type_dictionary(Attachment &context, Value type, Value expected, Value storage)
{
    RootScope roots(context);
    roots.root(type);
    roots.root(expected);
    roots.root(storage);
    const auto &old = context.payload<TypeSnapshot>(expected);
    ReturnSlot staged(context);
    context.allocate<TypeSnapshot>(staged, old.name, old.qualname, old.dictionary, storage,
                                   old.bases, old.mro);
    return commit_type(context, type, expected, staged.value());
}
void type_ready(Attachment &context, Handle type, std::optional<Handle> source)
{
    auto &payload = ready_type(context, type);
    if (payload.state == TypeState::Ready)
        return;
    RootScope roots(context);
    ReturnSlot text(context);
    const auto name = std::get<std::u32string>(payload.name);
    const auto qualname = std::get<std::u32string>(payload.qualname);
    make_str(context, name, text);
    payload.name = text.value();
    if (qualname != name)
        make_str(context, qualname, text);
    payload.qualname = text.value();
    context.account_external(type, payload.external_bytes());
    initialize_type_dictionary(context, type);
    if (source) {
        DictView dictionary(context, source->get(context));
        copy_dictionary_entries(context, *payload.dictionary, dictionary.entries());
    }
    if (name_lookup(context, *payload.dictionary, U"__eq__") &&
        !name_lookup(context, *payload.dictionary, U"__hash__")) {
        make_str(context, U"__hash__", text);
        name_store(context, *payload.dictionary, text.value(), context.builtin(CoreObject::None));
    }
    if (payload.bootstrap_module && !name_lookup(context, *payload.dictionary, U"__module__")) {
        make_str(context, U"__module__", text);
        auto key = roots.root(text.value());
        make_str(context, *payload.bootstrap_module, text);
        name_store(context, *payload.dictionary, key.get(context), text.value());
    }
    if (payload.instance_layout.fields.dictionary &&
        !std::any_of(payload.bases.begin(), payload.bases.end(),
                     [&](Value base) {
                         return context.payload<TypeObject>(base).instance_layout.fields.dictionary;
                     }) &&
        !name_lookup(context, *payload.dictionary, U"__dict__"))
        install_instance_dictionary(context, type);
    if (context.core_ready() && payload.instance_layout.fields.weakrefs &&
        !std::any_of(payload.bases.begin(), payload.bases.end(),
                     [&](Value base) {
                         return context.payload<TypeObject>(base).instance_layout.fields.weakrefs;
                     }) &&
        !name_lookup(context, *payload.dictionary, U"__weakref__"))
        install_instance_weakrefs(context, type);
    const auto inherited =
        payload.base ? context.payload<TypeObject>(*payload.base).slot_names.size() : 0;
    install_slot_descriptors(context, type, inherited);
    payload.bootstrap_module.reset();
    context.account_external(type, payload.external_bytes());
    payload.state = TypeState::Ready;
    if (context.core_ready())
        publish_type(context, type.get(context));
}
Handle select_metaclass(Attachment &context, Handle metaclass, std::span<const Handle> bases,
                        RootScope &roots)
{
    if (!is_subtype(context, metaclass.get(context), context.type_type()))
        throw TypeDefinitionError("metaclass must derive from type");
    for (auto base : bases) {
        auto candidate = roots.root(context.type_of(base));
        if (is_subtype(context, metaclass, candidate))
            continue;
        if (!is_subtype(context, candidate, metaclass))
            throw TypeDefinitionError("metaclass conflict");
        metaclass = candidate;
    }
    return metaclass;
}
void build_type_seeds(Attachment &context, ReturnSlot &object_out, ReturnSlot &type_out)
{
    RootScope roots(context);
    context.allocate<TypeObject>(object_out, U"object", LayoutDescriptor::of<InstanceObject>());
    auto object = roots.root(object_out.value());
    context.allocate<TypeObject>(type_out, U"type", LayoutDescriptor::of<TypeObject>());
    auto type = roots.root(type_out.value());
    context.store(object, context.payload<TypeObject>(object).header.type, type);
    context.store(type, context.payload<TypeObject>(type).header.type, type);

    auto &object_data = context.payload<TypeObject>(object);
    object_data.mro = {object.get(context)};
    object_data.solid_base = object.get(context);
    auto &type_data = context.payload<TypeObject>(type);
    type_data.instance_layout.fields.weakrefs = true;
    type_data.bases = {object.get(context)};
    type_data.base = object.get(context);
    type_data.mro = {type.get(context), object.get(context)};
    type_data.solid_base = type.get(context);
    type_data.allow_slots = false;
    context.account_external(object, object_data.external_bytes());
    context.account_external(type, type_data.external_bytes());
    object_data.state = TypeState::Structural;
    type_data.state = TypeState::Structural;
}
} // namespace detail
} // namespace cxxpython::core
