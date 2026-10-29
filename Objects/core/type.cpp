#include "cxxpython/core/type.hpp"
#include "cxxpython/core/objects.hpp"

#include <algorithm>

namespace cxxpython::core
{
    namespace
    {
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
            for (auto base : bases)
            {
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
            for (;;)
            {
                bool any = false;
                std::optional<Value> candidate;
                for (auto sequence : sequences)
                {
                    if (sequence.empty())
                        continue;
                    any = true;
                    auto head = sequence.front();
                    bool in_tail = std::any_of(sequences.begin(), sequences.end(), [head](auto other) {
                        return !other.empty() &&
                               std::find(other.begin() + 1, other.end(), head) != other.end();
                    });
                    if (!in_tail)
                    {
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

        void create(Attachment &context, const TypeSpec &spec,
                    const LayoutDescriptor *native_layout, ReturnSlot &out)
        {
            out.reset(context);
            if (spec.dictionary && !context.core_ready())
                throw TypeDefinitionError("managed type namespaces require the B3 image");
            auto valid_text = [](const std::u32string &text) {
                return std::all_of(text.begin(), text.end(), [](char32_t point) { return point <= 0x10ffff; });
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
            for (auto base : bases)
            {
                auto candidate = roots.root(*ready_type(context, base).solid_base);
                if (!is_subtype(context, solid, candidate))
                {
                    if (!is_subtype(context, candidate, solid))
                        throw TypeDefinitionError("multiple bases have incompatible native layouts");
                    solid = candidate;
                    best_base = base;
                }
            }

            auto layout = ready_type(context, solid).instance_layout;
            auto slot_names = ready_type(context, solid).slot_names;
            bool introduces_layout = false;
            if (native_layout)
            {
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
            const bool metatype = std::find(ancestors.begin(), ancestors.end(), type.get(context)) != ancestors.end();
            bool allow_slots = true;
            bool inherited_dictionary = false;
            for (auto base : bases)
            {
                allow_slots &= ready_type(context, base).allow_slots;
                inherited_dictionary |= ready_type(context, base).instance_layout.fields.dictionary;
            }
            bool dictionary = inherited_dictionary || (!metatype && !native_layout && !spec.slots);
            std::vector<std::u32string> own_slots;
            if (spec.slots && !spec.slots->empty() && !allow_slots)
                throw TypeDefinitionError("nonempty slots are not allowed for this native base");
            if (spec.slots)
                for (auto name : *spec.slots)
                {
                    if (!is_identifier(name)) throw TypeDefinitionError("slot names must be identifiers");
                    if (name == U"__dict__")
                    {
                        if (dictionary || metatype) throw TypeDefinitionError("instance dictionary slot already provided");
                        dictionary = true;
                        continue;
                    }
                    if (name == U"__weakref__") throw TypeDefinitionError("weakref slots require the weak-reference contract");
                    if (!allow_slots) throw TypeDefinitionError("nonempty slots are not allowed for this native base");
                    if (name.starts_with(U"__") && !name.ends_with(U"__"))
                    {
                        auto first = spec.name.find_first_not_of(U'_');
                        if (first != std::u32string::npos) name = U"_" + spec.name.substr(first) + name;
                    }
                    if (spec.dictionary && detail::name_lookup(context, spec.dictionary->get(context), name))
                        throw TypeDefinitionError("slot conflicts with a class variable", CoreObject::ValueErrorType);
                    own_slots.push_back(std::move(name));
                }
            std::sort(own_slots.begin(), own_slots.end());
            slot_names.insert(slot_names.end(), own_slots.begin(), own_slots.end());
            layout.fields = {slot_names.size(), dictionary};
            (void)layout.storage_size();

            // Only a fully initialized type is handed to the caller. The staged
            // object is rooted while all host buffers and graph edges are filled.
            ReturnSlot staged(context);
            context.allocate_instance<TypeObject>(metaclass, staged, spec.name, layout);
            auto created = roots.root(staged.result().value());
            auto &payload = context.payload<TypeObject>(created);
            payload.qualname = spec.qualname.value_or(spec.name);
            payload.bootstrap_module = spec.module;
            for (auto base : bases)
                payload.bases.push_back(base.get(context));
            payload.mro.reserve(ancestors.size() + 1);
            payload.mro.push_back(created.get(context));
            payload.mro.insert(payload.mro.end(), ancestors.begin(), ancestors.end());
            payload.solid_base = introduces_layout ? created.get(context) : solid.get(context);
            if (!own_slots.empty()) payload.solid_base = created.get(context);
            payload.base = best_base.get(context);
            payload.slot_names = std::move(slot_names);
            payload.allow_slots = allow_slots;
            context.account_external(created, payload.external_bytes());
            payload.state = TypeState::Structural;
            if (context.core_ready())
                detail::type_ready(context, created, spec.dictionary);
            out.ok(created.get(context));
        }
    }

    void TypeObject::trace(Tracer &tracer) const noexcept
    {
        if (auto value = std::get_if<Value>(&name)) tracer.visit(*value);
        if (auto value = std::get_if<Value>(&qualname)) tracer.visit(*value);
        for (auto base : bases)
            tracer.visit(base);
        for (auto entry : mro)
            tracer.visit(entry);
        if (solid_base)
            tracer.visit(*solid_base);
        if (base) tracer.visit(*base);
        if (dictionary)
            tracer.visit(*dictionary);
    }

    std::size_t TypeObject::external_bytes() const noexcept
    {
        auto string_bytes = [this](const std::u32string &text) {
            const auto address = reinterpret_cast<std::uintptr_t>(text.data());
            const auto start = reinterpret_cast<std::uintptr_t>(this);
            return address >= start && address - start < sizeof(*this) ? 0U : (text.capacity() + 1) * sizeof(char32_t);
        };
        auto bytes = (bases.capacity() + mro.capacity()) * sizeof(Value);
        if (auto text = std::get_if<std::u32string>(&name)) bytes += string_bytes(*text);
        if (auto text = std::get_if<std::u32string>(&qualname)) bytes += string_bytes(*text);
        if (bootstrap_module) bytes += string_bytes(*bootstrap_module);
        bytes += slot_names.capacity() * sizeof(std::u32string);
        for (const auto &name : slot_names)
        {
            auto address = reinterpret_cast<std::uintptr_t>(name.data());
            auto start = reinterpret_cast<std::uintptr_t>(&name);
            if (address < start || address - start >= sizeof(name)) bytes += (name.capacity() + 1) * sizeof(char32_t);
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
    { create(context, spec, nullptr, out); }

    void create_native_type(Attachment &context, const NativeTypeSpec &spec, ReturnSlot &out)
    { create(context, spec.type, &spec.layout, out); }

    bool is_layout_compatible(Attachment &context, Value left, Value right)
    {
        auto &a = context.payload<TypeObject>(left);
        auto &b = context.payload<TypeObject>(right);
        if (a.instance_layout != b.instance_layout || a.slot_names != b.slot_names) return false;
        auto storage_base = [&](Value type) {
            for (;;)
            {
                auto &data = context.payload<TypeObject>(type);
                if (!data.base) return type;
                auto prefix = data.instance_layout;
                auto &parent_type = context.payload<TypeObject>(*data.base);
                auto parent = parent_type.instance_layout;
                prefix.fields = {}; parent.fields = {};
                if (prefix != parent || data.slot_names != parent_type.slot_names) return type;
                type = *data.base;
            }
        };
        left = storage_base(left);
        right = storage_base(right);
        if (left == right) return true;
        auto &left_base = context.payload<TypeObject>(left);
        auto &right_base = context.payload<TypeObject>(right);
        if (!left_base.base || left_base.base != right_base.base ||
            left_base.slot_names != right_base.slot_names || left_base.instance_layout != right_base.instance_layout)
            return false;
        auto prefix = left_base.instance_layout;
        auto parent = context.payload<TypeObject>(*left_base.base).instance_layout;
        prefix.fields = {}; parent.fields = {};
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

    namespace detail
    {
        void type_ready(Attachment &context, Handle type, std::optional<Handle> source)
        {
            auto &payload = ready_type(context, type);
            if (payload.state == TypeState::Ready) return;
            RootScope roots(context);
            ReturnSlot text(context);
            const auto name = std::get<std::u32string>(payload.name);
            const auto qualname = std::get<std::u32string>(payload.qualname);
            make_str(context, name, text);
            payload.name = text.result().value();
            if (qualname != name) make_str(context, qualname, text);
            payload.qualname = text.result().value();
            context.account_external(type, payload.external_bytes());
            initialize_type_dictionary(context, type);
            if (source)
                for (const auto &entry : context.payload<DictObject>(*source).entries)
                    name_store(context, *payload.dictionary, entry.key, entry.value);
            if (name_lookup(context, *payload.dictionary, U"__eq__") &&
                !name_lookup(context, *payload.dictionary, U"__hash__"))
            {
                make_str(context, U"__hash__", text);
                name_store(context, *payload.dictionary, text.result().value(), context.builtin(CoreObject::None));
            }
            if (payload.bootstrap_module && !name_lookup(context, *payload.dictionary, U"__module__"))
            {
                make_str(context, U"__module__", text);
                auto key = roots.root(text.result().value());
                make_str(context, *payload.bootstrap_module, text);
                name_store(context, *payload.dictionary, key.get(context), text.result().value());
            }
            if (payload.instance_layout.fields.dictionary &&
                !std::any_of(payload.bases.begin(), payload.bases.end(), [&](Value base) {
                    return context.payload<TypeObject>(base).instance_layout.fields.dictionary;
                }) && !name_lookup(context, *payload.dictionary, U"__dict__"))
                install_instance_dictionary(context, type);
            const auto inherited = payload.base ? context.payload<TypeObject>(*payload.base).slot_names.size() : 0;
            install_slot_descriptors(context, type, inherited);
            payload.bootstrap_module.reset();
            context.account_external(type, payload.external_bytes());
            payload.state = TypeState::Ready;
        }
        Handle select_metaclass(Attachment &context, Handle metaclass, std::span<const Handle> bases, RootScope &roots)
        {
            if (!is_subtype(context, metaclass.get(context), context.type_type()))
                throw TypeDefinitionError("metaclass must derive from type");
            for (auto base : bases)
            {
                auto candidate = roots.root(context.type_of(base));
                if (is_subtype(context, metaclass, candidate)) continue;
                if (!is_subtype(context, candidate, metaclass)) throw TypeDefinitionError("metaclass conflict");
                metaclass = candidate;
            }
            return metaclass;
        }
        void build_type_seeds(Attachment &context, ReturnSlot &object_out, ReturnSlot &type_out)
        {
            RootScope roots(context);
            context.allocate<TypeObject>(object_out, U"object", LayoutDescriptor::of<InstanceObject>());
            auto object = roots.root(object_out.result().value());
            context.allocate<TypeObject>(type_out, U"type", LayoutDescriptor::of<TypeObject>());
            auto type = roots.root(type_out.result().value());
            context.store(object, context.payload<TypeObject>(object).header.type, type);
            context.store(type, context.payload<TypeObject>(type).header.type, type);

            auto &object_data = context.payload<TypeObject>(object);
            object_data.mro = {object.get(context)};
            object_data.solid_base = object.get(context);
            auto &type_data = context.payload<TypeObject>(type);
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
    }
}
