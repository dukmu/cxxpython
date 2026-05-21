#include "module_registry.hpp"

#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cxxpython
{
    namespace
    {
        bool is_identifier(std::string_view text)
        {
            if (text.empty())
                return false;
            const unsigned char first = static_cast<unsigned char>(text.front());
            if (!(std::isalpha(first) || text.front() == '_'))
                return false;
            for (char ch : text)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                if (!(std::isalnum(value) || ch == '_'))
                    return false;
            }
            return true;
        }

        std::string active_module_name(Runtime &runtime)
        {
            VirtualMachine *vm = runtime.active_vm();
            Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
            DictObject *globals = frame ? frame->globals() : nullptr;
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            return "__main__";
        }

        std::vector<std::string> parse_field_names_from_string(const std::string &raw)
        {
            std::vector<std::string> fields;
            std::string token;
            for (char ch : raw)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                if (ch == ',' || std::isspace(value))
                {
                    if (!token.empty())
                    {
                        fields.push_back(token);
                        token.clear();
                    }
                    continue;
                }
                token.push_back(ch);
            }
            if (!token.empty())
                fields.push_back(token);
            return fields;
        }

        std::vector<std::string> namedtuple_fields(Runtime &runtime, Object *field_names)
        {
            std::vector<std::string> fields;
            if (auto *text = dynamic_cast<StrObject *>(field_names))
            {
                fields = parse_field_names_from_string(text->value);
            }
            else
            {
                for (Object *item : collect_iterable_items(&runtime, field_names))
                {
                    auto item_root = runtime.root(item);
                    auto *name = dynamic_cast<StrObject *>(item_root.get());
                    if (!name)
                        raise_type_error(&runtime, "namedtuple() field names must be strings");
                    fields.push_back(name->value);
                }
            }
            if (fields.empty())
                raise_value_error(&runtime, "namedtuple() requires at least one field");
            return fields;
        }

        std::size_t namedtuple_field_count(TypeObject &type)
        {
            Runtime *runtime = type.runtime();
            if (!runtime || !type.dict)
                raise_type_error(runtime, "namedtuple type metadata is missing");
            auto *count = dynamic_cast<IntObject *>(type.dict->get(runtime->new_str("__namedtuple_field_count__")));
            if (!count || count->value < 0)
                raise_type_error(runtime, "namedtuple type metadata is invalid");
            return static_cast<std::size_t>(count->value);
        }

        void validate_namedtuple_ctor(TypeObject &type, const CallArguments &args)
        {
            Runtime *runtime = type.runtime();
            require_no_keywords(runtime, type.name, args);
            const std::size_t expected = namedtuple_field_count(type);
            if (args.positional.size() == expected)
                return;

            std::ostringstream message;
            message << type.name << "() takes exactly " << expected
                    << (expected == 1 ? " argument" : " arguments")
                    << " (" << args.positional.size() << " given)";
            raise_type_error(runtime, message.str());
        }

        Object *namedtuple_new(TypeObject &type, const CallArguments &args)
        {
            validate_namedtuple_ctor(type, args);
            return type.runtime()->allocate<TupleObject>(&type, args.positional);
        }

        void namedtuple_init(Object &object, const CallArguments &args)
        {
            TypeObject *type = object.type();
            if (!type)
                raise_type_error(object.runtime(), "namedtuple instance has no type");
            validate_namedtuple_ctor(*type, args);
        }

        void validate_namedtuple_identifiers(Runtime &runtime,
                                             const std::string &type_name,
                                             const std::vector<std::string> &fields)
        {
            if (!is_identifier(type_name))
                raise_value_error(&runtime, "namedtuple() typename must be a valid identifier");

            std::unordered_set<std::string> seen;
            for (const std::string &field : fields)
            {
                if (!is_identifier(field))
                    raise_value_error(&runtime, "namedtuple() field names must be valid identifiers");
                if (!seen.insert(field).second)
                    raise_value_error(&runtime, "namedtuple() field names must be unique");
            }
        }

        TypeObject *build_namedtuple_type(Runtime &runtime,
                                          const std::string &type_name,
                                          const std::vector<std::string> &fields)
        {
            DictObject *namespace_dict = runtime.new_dict();
            auto namespace_root = runtime.root(namespace_dict);
            TypeObject *type = runtime.new_class(
                type_name,
                std::vector<TypeObject *>{runtime.tuple_type()},
                namespace_root.get(),
                active_module_name(runtime));
            auto type_root = runtime.root(type);

            type_root->slots.new_object = namedtuple_new;
            type_root->slots.init = namedtuple_init;
            type_root->dict->set(runtime.new_str("__namedtuple_field_count__"),
                                 runtime.new_int(static_cast<std::int64_t>(fields.size())));

            std::vector<Object *> field_objects;
            std::vector<Rooted<StrObject>> field_roots;
            field_objects.reserve(fields.size());
            field_roots.reserve(fields.size());
            for (const std::string &field : fields)
            {
                StrObject *text = runtime.new_str(field);
                field_roots.push_back(runtime.root(text));
                field_objects.push_back(text);
            }

            TupleObject *field_tuple = runtime.new_tuple(field_objects);
            auto field_tuple_root = runtime.root(field_tuple);
            type_root->dict->set(runtime.new_str("_fields"), field_tuple_root.get());
            type_root->dict->set(runtime.new_str("__match_args__"), field_tuple_root.get());

            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                const std::string field_name = fields[index];
                GetSetGetter getter = [index, field_name](Runtime &getter_runtime, Object *instance, TypeObject *) -> Object * {
                    auto *tuple = dynamic_cast<TupleObject *>(instance);
                    if (!tuple)
                        raise_type_error(&getter_runtime, "namedtuple field receiver does not have tuple layout");
                    if (index >= tuple->size())
                        raise_attribute_error(&getter_runtime, "'" + type_qualified_name(instance->type()) +
                                                                  "' object has no attribute '" + field_name + "'");
                    return tuple->items()[index];
                };
                GetSetDescriptorObject *descriptor =
                    runtime.new_getset_descriptor(field_name, type_root.get(), std::move(getter), nullptr);
                type_root->dict->set(runtime.new_str(field_name), descriptor);
            }

            return type_root.get();
        }

        Object *collections_namedtuple(Runtime &runtime, const CallArguments &args)
        {
            require_no_keywords(&runtime, "namedtuple", args);
            require_positional_count(&runtime, "namedtuple", args, 2);
            auto *typename_object = dynamic_cast<StrObject *>(args.positional[0]);
            if (!typename_object)
                raise_type_error(&runtime, "namedtuple() typename must be str");

            std::vector<std::string> fields = namedtuple_fields(runtime, args.positional[1]);
            validate_namedtuple_identifiers(runtime, typename_object->value, fields);
            return build_namedtuple_type(runtime, typename_object->value, fields);
        }
    }

    void initialize_collections_module(Runtime &runtime, ModuleObject &module)
    {
        module.dict()->set(runtime.new_str("namedtuple"),
                           runtime.new_native_function("namedtuple", collections_namedtuple));
    }
}
