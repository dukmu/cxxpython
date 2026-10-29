#include "cxxpython/core/objects.hpp"

#include <algorithm>

namespace cxxpython::core
{
    namespace
    {
        void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, std::move(message), out); }
        void definition_error(Attachment &context, const TypeDefinitionError &error, ReturnSlot &out)
        {
            const std::string message(error.what());
            raise_error(context, error.error_type, {message.begin(), message.end()}, out);
        }
        bool raised(ReturnSlot &out) { return out.result().tag() == ResultTag::Raised; }
        bool is_type(Attachment &context, Value value)
        {
            return context.has_layout<TypeObject>(value) &&
                   is_subtype(context, context.type_of(value), context.type_type()) &&
                   context.payload<TypeObject>(value).state == TypeState::Ready;
        }
        bool is_instance(Attachment &context, Value value, CoreObject type)
        { return is_subtype(context, context.type_of(value), context.builtin(type)); }
        std::vector<KeywordArg> keywords(Attachment &context, Value dictionary)
        {
            std::vector<KeywordArg> result;
            for (const auto &entry : context.payload<DictObject>(dictionary).entries)
                result.push_back({entry.key, entry.value});
            return result;
        }
        bool excess(Attachment &context, std::span<const Value> args)
        {
            return !context.payload<TupleObject>(args[1]).items.empty() ||
                   !context.payload<DictObject>(args[2]).entries.empty();
        }
        Value unwrapped(Attachment &context, Value value)
        {
            return context.type_of(value) == context.builtin(CoreObject::StaticMethodType)
                ? context.payload<StaticMethodObject>(value).callable : value;
        }
        bool object_default(Attachment &context, Value type, const std::u32string &name)
        {
            auto method = detail::type_attribute(context, type, name);
            return method && unwrapped(context, *method) ==
                             unwrapped(context, *detail::type_attribute(context, context.object_type(), name));
        }
        void object_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0])) { type_error(context, U"object.__new__ requires a type", out); return; }
            if (excess(context, args) && (!object_default(context, args[0], U"__new__") ||
                                          object_default(context, args[0], U"__init__")))
            { type_error(context, U"object.__new__ received excess arguments", out); return; }
            auto prefix = context.payload<TypeObject>(args[0]).instance_layout;
            prefix.fields = {};
            if (prefix != LayoutDescriptor::of<InstanceObject>())
            { type_error(context, U"object.__new__ cannot allocate this native layout", out); return; }
            RootScope roots(context);
            context.allocate_instance<InstanceObject>(roots.root(args[0]), out);
        }
        void object_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            auto type = context.type_of(args[0]);
            if (excess(context, args) && (!object_default(context, type, U"__init__") ||
                                          object_default(context, type, U"__new__")))
            { type_error(context, U"object.__init__ received excess arguments", out); return; }
            out.ok(context.builtin(CoreObject::None));
        }
        void type_call(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0])) { type_error(context, U"type.__call__ requires a type", out); return; }
            auto &positional = context.payload<TupleObject>(args[1]).items;
            auto named = keywords(context, args[2]);
            if (args[0] == context.type_type())
            {
                if (positional.size() == 1 && named.empty()) { out.ok(context.type_of(positional[0])); return; }
                if (positional.size() != 3) { type_error(context, U"type() takes 1 or 3 arguments", out); return; }
            }
            RootScope roots(context);
            auto cls = roots.root(args[0]);
            auto constructor = detail::type_attribute(context, args[0], U"__new__");
            if (!constructor) { type_error(context, U"type has no __new__", out); return; }
            auto original = roots.root(*constructor);
            ReturnSlot target(context);
            // Builtin allocation entries do not run metaclass attribute hooks.
            // User __new__ uses ordinary class lookup, including those hooks.
            auto builtin_new = detail::type_attribute(context, context.type_type(), U"__new__");
            if (object_default(context, args[0], U"__new__") || *constructor == *builtin_new)
                detail::bind_descriptor(context, original.get(context), {}, args[0], target);
            else
            {
                ReturnSlot name(context); make_str(context, U"__new__", name);
                get_attribute(context, cls, roots.root(name.result().value()), target);
            }
            if (raised(target)) { out.raised(target.result().exception()); return; }
            std::vector<Value> new_args{args[0]};
            new_args.insert(new_args.end(), positional.begin(), positional.end());
            ReturnSlot created(context);
            call(context, roots.root(target.result().value()), {new_args, named}, created);
            if (raised(created)) { out.raised(created.result().exception()); return; }
            auto self = roots.root(created.result().value());
            if (!is_subtype(context, context.type_of(self), args[0])) { out.ok(self.get(context)); return; }
            ReturnSlot initialized(context);
            detail::special(context, self.get(context), U"__init__", {positional, named}, initialized);
            if (raised(initialized)) { out.raised(initialized.result().exception()); return; }
            if (initialized.result().value() != context.builtin(CoreObject::None))
            { type_error(context, U"__init__ must return None", out); return; }
            out.ok(self.get(context));
        }
        void static_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_instance(context, args[0], CoreObject::StaticMethodType))
            { type_error(context, U"staticmethod descriptor requires a staticmethod", out); return; }
            if (args[1] == context.builtin(CoreObject::None) && args[2] == context.builtin(CoreObject::None))
            { type_error(context, U"__get__(None, None) is invalid", out); return; }
            out.ok(context.payload<StaticMethodObject>(args[0]).callable);
        }
        void class_get(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_instance(context, args[0], CoreObject::ClassMethodType))
            { type_error(context, U"classmethod descriptor requires a classmethod", out); return; }
            if (args[1] == context.builtin(CoreObject::None) && args[2] == context.builtin(CoreObject::None))
            { type_error(context, U"__get__(None, None) is invalid", out); return; }
            auto owner = args[2];
            if (owner == context.builtin(CoreObject::None)) owner = context.type_of(args[1]);
            auto callable = context.payload<ClassMethodObject>(args[0]).callable;
            if (detail::type_attribute(context, context.type_of(callable), U"__get__"))
            { detail::bind_descriptor(context, callable, owner, owner, out); return; }
            RootScope roots(context);
            context.allocate_instance<BoundMethodObject>(roots.root(context.builtin(CoreObject::BoundMethodType)), out, callable, owner);
        }
        void static_call(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_instance(context, args[0], CoreObject::StaticMethodType))
            { type_error(context, U"staticmethod call requires a staticmethod", out); return; }
            RootScope roots(context);
            auto named = keywords(context, args[2]);
            call(context, roots.root(context.payload<StaticMethodObject>(args[0]).callable),
                 {context.payload<TupleObject>(args[1]).items, named}, out);
        }
        void static_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0]) || !is_subtype(context, args[0], context.builtin(CoreObject::StaticMethodType)))
            { type_error(context, U"staticmethod.__new__ requires a staticmethod subtype", out); return; }
            RootScope roots(context);
            context.allocate_instance<StaticMethodObject>(roots.root(args[0]), out, args[1]);
        }
        void class_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0]) || !is_subtype(context, args[0], context.builtin(CoreObject::ClassMethodType)))
            { type_error(context, U"classmethod.__new__ requires a classmethod subtype", out); return; }
            RootScope roots(context);
            context.allocate_instance<ClassMethodObject>(roots.root(args[0]), out, args[1]);
        }
        void cell_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[0] != context.builtin(CoreObject::CellType))
            { type_error(context, U"cell.__new__ requires cell", out); return; }
            const auto &items = context.payload<TupleObject>(args[1]).items;
            if (items.size() > 1) { type_error(context, U"cell accepts at most one argument", out); return; }
            RootScope roots(context);
            context.allocate_instance<CellObject>(roots.root(args[0]), out,
                                                  items.empty() ? std::optional<Value>{} : items[0]);
        }
        void wrap_definition(Attachment &context, Handle cls, const std::u32string &name, bool class_method)
        {
            auto dictionary = *context.payload<TypeObject>(cls).dictionary;
            auto function = detail::name_lookup(context, dictionary, name);
            if (!function || context.type_of(*function) != context.builtin(CoreObject::NativeFunctionType)) return;
            RootScope roots(context);
            auto callable = roots.root(*function);
            ReturnSlot out(context);
            if (class_method)
                context.allocate_instance<ClassMethodObject>(roots.root(context.builtin(CoreObject::ClassMethodType)), out, callable.get(context));
            else
                context.allocate_instance<StaticMethodObject>(roots.root(context.builtin(CoreObject::StaticMethodType)), out, callable.get(context));
            auto wrapper = roots.root(out.result().value());
            make_str(context, name, out);
            detail::name_store(context, dictionary, out.result().value(), wrapper.get(context));
        }
        void finish_class(Attachment &context, Handle cls, std::span<const KeywordArg> named, ReturnSlot &out)
        {
            // Callbacks may resize/replace the namespace and collect removed
            // entries. Snapshot and root the original key/value pairs first.
            auto entries = context.payload<DictObject>(*context.payload<TypeObject>(cls).dictionary).entries;
            RootScope roots(context);
            for (const auto &entry : entries) { roots.root(entry.key); roots.root(entry.value); }
            ReturnSlot callback(context);
            for (const auto &entry : entries)
            {
                if (!detail::type_attribute(context, context.type_of(entry.value), U"__set_name__")) continue;
                std::array args{cls.get(context), entry.key};
                detail::special(context, entry.value, U"__set_name__", {args, {}}, callback);
                if (raised(callback))
                {
                    const auto cause = callback.result().exception();
                    raise_error(context, CoreObject::RuntimeErrorType, U"error calling __set_name__", out);
                    if (context.type_of(out.result().exception()) == context.builtin(CoreObject::RuntimeErrorType))
                    {
                        auto &error = context.payload<ExceptionObject>(out.result().exception());
                        error.cause = cause; error.context = cause; error.suppress_context = true;
                    }
                    return;
                }
            }
            const auto &mro = context.payload<TypeObject>(cls).mro;
            for (auto base : std::span(mro).subspan(1))
            {
                const auto dictionary = context.payload<TypeObject>(base).dictionary;
                auto hook = dictionary ? detail::name_lookup(context, *dictionary, U"__init_subclass__") : std::optional<Value>{};
                if (!hook) continue;
                detail::bind_descriptor(context, *hook, cls.get(context), cls.get(context), callback);
                if (raised(callback)) { out.raised(callback.result().exception()); return; }
                call(context, roots.root(callback.result().value()), {{}, named}, out);
                if (!raised(out)) out.ok(cls.get(context));
                return;
            }
            throw std::logic_error("object has no __init_subclass__");
        }
        void type_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0]) || !is_subtype(context, args[0], context.type_type()) ||
                !is_instance(context, args[1], CoreObject::StrType) ||
                !is_instance(context, args[2], CoreObject::TupleType) ||
                !is_instance(context, args[3], CoreObject::DictType))
            { type_error(context, U"type.__new__ requires a metaclass, str, tuple and dict", out); return; }
            auto named = keywords(context, args[4]);
            RootScope roots(context);
            std::vector<Handle> bases;
            for (auto base : context.payload<TupleObject>(args[2]).items)
                bases.push_back(roots.root(base));
            Handle winner;
            try { winner = detail::select_metaclass(context, roots.root(args[0]), bases, roots); }
            catch (const TypeDefinitionError &error) { definition_error(context, error, out); return; }
            auto constructor = detail::type_attribute(context, winner.get(context), U"__new__");
            if (winner.get(context) != args[0] && constructor != detail::type_attribute(context, context.type_type(), U"__new__"))
            {
                ReturnSlot bound(context);
                detail::bind_descriptor(context, *constructor, {}, winner.get(context), bound);
                if (raised(bound)) { out.raised(bound.result().exception()); return; }
                std::array values{winner.get(context), args[1], args[2], args[3]};
                call(context, roots.root(bound.result().value()), {values, named}, out);
                return;
            }
            for (auto base : bases)
                if (!is_type(context, base.get(context))) { type_error(context, U"bases must be types", out); return; }
            const auto &name = context.payload<StrObject>(args[1]).value;
            if (std::find(name.begin(), name.end(), U'\0') != name.end())
            { raise_error(context, CoreObject::ValueErrorType, U"type name must not contain null characters", out); return; }
            auto space = roots.root(args[3]);
            TypeSpec spec{name, bases, winner};
            spec.module.reset();
            spec.dictionary = space;
            if (auto qualname = detail::name_lookup(context, args[3], U"__qualname__"))
            {
                if (!is_instance(context, *qualname, CoreObject::StrType))
                { type_error(context, U"__qualname__ must be a str", out); return; }
                spec.qualname = context.payload<StrObject>(*qualname).value;
            }
            if (auto slots = detail::name_lookup(context, args[3], U"__slots__"))
            {
                spec.slots.emplace();
                auto append = [&](Value value) {
                    if (!is_instance(context, value, CoreObject::StrType)) return false;
                    spec.slots->push_back(context.payload<StrObject>(value).value);
                    return true;
                };
                bool valid = true;
                if (is_instance(context, *slots, CoreObject::StrType)) valid = append(*slots);
                else if (is_instance(context, *slots, CoreObject::TupleType))
                {
                    for (auto item : context.payload<TupleObject>(*slots).items) valid &= append(item);
                }
                else if (is_instance(context, *slots, CoreObject::DictType))
                {
                    for (auto entry : context.payload<DictObject>(*slots).entries) valid &= append(entry.key);
                }
                else valid = false;
                if (!valid) { type_error(context, U"__slots__ requires string names from str, tuple or dict", out); return; }
            }
            auto classcell = detail::name_lookup(context, args[3], U"__classcell__");
            if (classcell && !is_instance(context, *classcell, CoreObject::CellType))
            { type_error(context, U"__classcell__ must be a cell", out); return; }
            ReturnSlot created(context);
            try
            {
                create_type(context, spec, created);
            }
            catch (const TypeDefinitionError &error)
            {
                definition_error(context, error, out);
                return;
            }
            auto cls = roots.root(created.result().value());
            auto &payload = context.payload<TypeObject>(cls);
            payload.name = args[1];
            payload.qualname = detail::name_lookup(context, args[3], U"__qualname__").value_or(args[1]);
            context.account_external(cls, payload.external_bytes());
            detail::name_erase(context, *context.payload<TypeObject>(cls).dictionary, U"__qualname__");
            wrap_definition(context, cls, U"__new__", false);
            wrap_definition(context, cls, U"__init_subclass__", true);
            wrap_definition(context, cls, U"__class_getitem__", true);
            if (classcell)
            {
                context.payload<CellObject>(*classcell).contents = cls.get(context);
                detail::name_erase(context, *payload.dictionary, U"__classcell__");
            }
            if (auto cell = detail::name_lookup(context, *payload.dictionary, U"__classdictcell__"))
            {
                if (!is_instance(context, *cell, CoreObject::CellType))
                { type_error(context, U"__classdictcell__ must be a cell", out); return; }
                context.payload<CellObject>(*cell).contents = *payload.dictionary;
                detail::name_erase(context, *payload.dictionary, U"__classdictcell__");
            }
            finish_class(context, cls, named, out);
        }
        void type_init(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            auto size = context.payload<TupleObject>(args[1]).items.size();
            if (!is_type(context, args[0]) || (size != 1 && size != 3) ||
                (size == 1 && !context.payload<DictObject>(args[2]).entries.empty()))
            { type_error(context, U"type.__init__ takes 1 or 3 arguments", out); return; }
            out.ok(context.builtin(CoreObject::None));
        }
        void object_subclass(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!is_type(context, args[0])) { type_error(context, U"__init_subclass__ requires a type", out); return; }
            out.ok(context.builtin(CoreObject::None));
        }
    }
    namespace detail
    {
        void install_construction(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            RootScope roots(context);
            auto object = roots.root(context.object_type()); auto type = roots.root(context.type_type());
            enum class Binding { Plain, Static, Class };
            auto install = [&](Handle owner, std::u32string name, SignatureLayout signature, NativeEntry entry,
                               Binding binding = Binding::Plain) {
                install_native_attribute(context, core, owner, name, std::move(signature), entry);
                if (binding == Binding::Plain) return;
                ReturnSlot out(context);
                auto function = *name_lookup(context, *context.payload<TypeObject>(owner).dictionary, name);
                if (binding == Binding::Static) context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out, function);
                else context.allocate_instance<ClassMethodObject>(at(CoreObject::ClassMethodType), out, function);
                auto wrapper = roots.root(out.result().value());
                context.allocate_instance<StrObject>(at(CoreObject::StrType), out, std::move(name));
                name_store(context, *context.payload<TypeObject>(owner).dictionary, out.result().value(), wrapper.get(context));
            };
            SignatureLayout variadic;
            variadic.parameters = {{U"self", ParameterKind::PositionalOnly}};
            variadic.varargs = U"args"; variadic.varkwargs = U"kwargs";
            install(object, U"__new__", variadic, object_new, Binding::Static);
            install(object, U"__init__", variadic, object_init);
            install(type, U"__call__", variadic, type_call);
            install(type, U"__init__", variadic, type_init);
            SignatureLayout definition;
            definition.parameters = {{U"cls", ParameterKind::PositionalOnly}, {U"name", ParameterKind::PositionalOnly},
                                     {U"bases", ParameterKind::PositionalOnly}, {U"dict", ParameterKind::PositionalOnly}};
            definition.varkwargs = U"kwargs";
            install(type, U"__new__", definition, type_new, Binding::Static);
            SignatureLayout subclass;
            subclass.parameters = {{U"cls", ParameterKind::PositionalOnly}};
            install(object, U"__init_subclass__", subclass, object_subclass, Binding::Class);
            SignatureLayout get;
            get.parameters = {{U"self", ParameterKind::PositionalOnly}, {U"instance", ParameterKind::PositionalOnly},
                              {U"owner", ParameterKind::PositionalOnly, at(CoreObject::None).get(context)}};
            install(at(CoreObject::StaticMethodType), U"__get__", get, static_get);
            install(at(CoreObject::ClassMethodType), U"__get__", get, class_get);
            install(at(CoreObject::StaticMethodType), U"__call__", variadic, static_call);
            SignatureLayout wrapped;
            wrapped.parameters = {{U"cls", ParameterKind::PositionalOnly}, {U"function", ParameterKind::PositionalOnly}};
            install(at(CoreObject::StaticMethodType), U"__new__", wrapped, static_new, Binding::Static);
            install(at(CoreObject::ClassMethodType), U"__new__", wrapped, class_new, Binding::Static);
            SignatureLayout cell;
            cell.parameters = {{U"cls", ParameterKind::PositionalOnly}};
            cell.varargs = U"contents";
            install(at(CoreObject::CellType), U"__new__", cell, cell_new, Binding::Static);
        }
    }
}
