#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

#include "object_utils.hpp"

namespace cxxpython
{
    namespace
    {
        std::string getset_descriptor_repr(const Object &object)
        {
            auto *descriptor = static_cast<const GetSetDescriptorObject *>(&object);
            return "<attribute '" + descriptor->name() + "' of '" +
                   type_qualified_name(descriptor->owner()) + "' objects>";
        }

        std::int64_t getset_descriptor_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool getset_descriptor_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool getset_descriptor_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void getset_descriptor_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *descriptor = static_cast<const GetSetDescriptorObject *>(&object);
            if (descriptor->owner())
                visitor(descriptor->owner());
        }

        Object *getset_descriptor_get(Object &object, Object *instance, TypeObject *owner)
        {
            auto *descriptor = static_cast<GetSetDescriptorObject *>(&object);
            if (!instance)
                return descriptor;
            return descriptor->get(instance, owner);
        }

        void getset_descriptor_set(Object &object, Object *instance, Object *value)
        {
            auto *descriptor = static_cast<GetSetDescriptorObject *>(&object);
            descriptor->set(instance, value);
        }

        Object *class_getter(Runtime &, Object *instance, TypeObject *)
        {
            return instance->type();
        }

        void class_setter(Runtime &runtime, Object *, Object *)
        {
            raise_type_error(&runtime, "__class__ assignment only supported for mutable types or ModuleType subclasses");
        }

        Object *dict_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            if (auto *type = dynamic_cast<TypeObject *>(instance))
                return runtime.new_mappingproxy(type->dict);
            if (auto *module = dynamic_cast<ModuleObject *>(instance))
                return module->dict();
            if ((instance->type()->flags & TypeFlagHasInstanceDict) == 0)
            {
                raise_attribute_error(&runtime,
                                      "'" + type_qualified_name(instance->type()) +
                                          "' object has no attribute '__dict__'");
            }
            if (!instance->instance_dict())
                instance->set_instance_dict(runtime.new_dict());
            return instance->instance_dict();
        }

        void dict_setter(Runtime &runtime, Object *, Object *)
        {
            raise_attribute_error(&runtime, "attribute '__dict__' is read-only");
        }

        TypeObject *require_type_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *type = dynamic_cast<TypeObject *>(instance);
            if (!type)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have type layout");
            return type;
        }

        Object *type_base_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__base__");
            return type->base ? static_cast<Object *>(type->base) : static_cast<Object *>(runtime.none());
        }

        Object *type_bases_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__bases__");
            std::vector<Object *> bases;
            bases.reserve(type->bases.size());
            for (TypeObject *base : type->bases)
                bases.push_back(base);
            return runtime.new_tuple(std::move(bases));
        }

        Object *type_mro_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__mro__");
            std::vector<Object *> mro;
            mro.reserve(type->mro.size());
            for (TypeObject *item : type->mro)
                mro.push_back(item);
            return runtime.new_tuple(std::move(mro));
        }

        Object *type_qualname_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            TypeObject *type = require_type_instance(runtime, instance, "__qualname__");
            if (type->dict)
            {
                if (auto *qualname = dynamic_cast<StrObject *>(type->dict->get(runtime.new_str("__qualname__"))))
                    return qualname;
            }
            return runtime.new_str(type->name);
        }

        Object *doc_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            if (auto *type = dynamic_cast<TypeObject *>(instance))
            {
                if (type->dict)
                {
                    if (Object *doc = type->dict->get(runtime.new_str("__doc__")))
                        return doc;
                }
            }
            return runtime.none();
        }

        CodeObject *require_code_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *code = dynamic_cast<CodeObject *>(instance);
            if (!code)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have code layout");
            return code;
        }

        Object *code_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_code_instance(runtime, instance, "co_name")->name());
        }

        Object *code_filename_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_code_instance(runtime, instance, "co_filename")->filename());
        }

        Object *code_argcount_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_argcount")->arg_count()));
        }

        Object *code_flags_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_flags")->flags()));
        }

        Object *code_stacksize_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_stacksize")->stack_size()));
        }

        Object *tuple_from_object_vector(Runtime &runtime, const std::vector<Object *> &items)
        {
            std::vector<Object *> copied(items.begin(), items.end());
            return runtime.new_tuple(std::move(copied));
        }

        Object *code_consts_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_consts")->constants());
        }

        Object *code_names_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_names")->names());
        }

        Object *code_varnames_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_varnames")->varnames());
        }

        Object *code_cellvars_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_cellvars")->cellvars());
        }

        Object *code_freevars_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_freevars")->freevars());
        }

        Object *code_firstlineno_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            CodeObject *code = require_code_instance(runtime, instance, "co_firstlineno");
            std::uint32_t line = 0;
            const std::vector<std::uint32_t> &lines = code->lines();
            if (!lines.empty())
                line = lines.front();
            return runtime.new_int(static_cast<std::int64_t>(line));
        }

        Frame *require_frame_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *frame = dynamic_cast<Frame *>(instance);
            if (!frame)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have frame layout");
            return frame;
        }

        Object *frame_code_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_code")->code();
        }

        Object *frame_locals_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_locals")->locals();
        }

        Object *frame_globals_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_globals")->globals();
        }

        Object *frame_builtins_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_builtins")->builtins();
        }

        Object *frame_lasti_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Frame *frame = require_frame_instance(runtime, instance, "f_lasti");
            return runtime.new_int(static_cast<std::int64_t>(frame->last_instruction_index()));
        }

        Object *frame_lineno_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Frame *frame = require_frame_instance(runtime, instance, "f_lineno");
            return runtime.new_int(static_cast<std::int64_t>(
                frame->code()->line_for_instruction(frame->last_instruction_index())));
        }
    }

    GetSetDescriptorObject::GetSetDescriptorObject(TypeObject *type,
                                                   std::string name,
                                                   TypeObject *owner,
                                                   GetSetGetter getter,
                                                   GetSetSetter setter)
        : Object(type),
          name_(std::move(name)),
          owner_(owner),
          getter_(std::move(getter)),
          setter_(std::move(setter))
    {
    }

    const std::string &GetSetDescriptorObject::name() const noexcept
    {
        return name_;
    }

    TypeObject *GetSetDescriptorObject::owner() const noexcept
    {
        return owner_;
    }

    Object *GetSetDescriptorObject::get(Object *instance, TypeObject *owner)
    {
        if (!getter_)
            raise_attribute_error(runtime(), "attribute '" + name_ + "' is not readable");
        Object *result = getter_(*runtime(), instance, owner);
        if (!result)
            raise_type_error(runtime(), "getset descriptor returned null");
        return result;
    }

    void GetSetDescriptorObject::set(Object *instance, Object *value)
    {
        if (!setter_)
            raise_attribute_error(runtime(), "attribute '" + name_ + "' is read-only");
        setter_(*runtime(), instance, value);
    }

    TypeSlots make_getset_descriptor_slots()
    {
        return TypeSlots{
            .repr = getset_descriptor_repr,
            .hash = getset_descriptor_hash,
            .equal = getset_descriptor_equal,
            .truth = getset_descriptor_truth,
            .traverse = getset_descriptor_traverse,
            .descr_get = getset_descriptor_get,
            .descr_set = getset_descriptor_set,
        };
    }

    void Runtime::initialize_type_descriptors()
    {
        object_type_->dict->set(new_str("__class__"),
                                new_getset_descriptor("__class__",
                                                      object_type_,
                                                      class_getter,
                                                      class_setter));
        object_type_->dict->set(new_str("__dict__"),
                                new_getset_descriptor("__dict__",
                                                      object_type_,
                                                      dict_getter,
                                                      dict_setter));
        object_type_->dict->set(new_str("__doc__"),
                                new_getset_descriptor("__doc__",
                                                      object_type_,
                                                      doc_getter,
                                                      nullptr));
        type_type_->dict->set(new_str("__base__"),
                              new_getset_descriptor("__base__",
                                                    type_type_,
                                                    type_base_getter,
                                                    nullptr));
        type_type_->dict->set(new_str("__bases__"),
                              new_getset_descriptor("__bases__",
                                                    type_type_,
                                                    type_bases_getter,
                                                    nullptr));
        type_type_->dict->set(new_str("__mro__"),
                              new_getset_descriptor("__mro__",
                                                    type_type_,
                                                    type_mro_getter,
                                                    nullptr));
        type_type_->dict->set(new_str("__qualname__"),
                              new_getset_descriptor("__qualname__",
                                                    type_type_,
                                                    type_qualname_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_name"),
                              new_getset_descriptor("co_name",
                                                    code_type_,
                                                    code_name_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_filename"),
                              new_getset_descriptor("co_filename",
                                                    code_type_,
                                                    code_filename_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_argcount"),
                              new_getset_descriptor("co_argcount",
                                                    code_type_,
                                                    code_argcount_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_flags"),
                              new_getset_descriptor("co_flags",
                                                    code_type_,
                                                    code_flags_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_stacksize"),
                              new_getset_descriptor("co_stacksize",
                                                    code_type_,
                                                    code_stacksize_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_consts"),
                              new_getset_descriptor("co_consts",
                                                    code_type_,
                                                    code_consts_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_names"),
                              new_getset_descriptor("co_names",
                                                    code_type_,
                                                    code_names_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_varnames"),
                              new_getset_descriptor("co_varnames",
                                                    code_type_,
                                                    code_varnames_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_cellvars"),
                              new_getset_descriptor("co_cellvars",
                                                    code_type_,
                                                    code_cellvars_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_freevars"),
                              new_getset_descriptor("co_freevars",
                                                    code_type_,
                                                    code_freevars_getter,
                                                    nullptr));
        code_type_->dict->set(new_str("co_firstlineno"),
                              new_getset_descriptor("co_firstlineno",
                                                    code_type_,
                                                    code_firstlineno_getter,
                                                    nullptr));
        frame_type_->dict->set(new_str("f_code"),
                               new_getset_descriptor("f_code",
                                                     frame_type_,
                                                     frame_code_getter,
                                                     nullptr));
        frame_type_->dict->set(new_str("f_locals"),
                               new_getset_descriptor("f_locals",
                                                     frame_type_,
                                                     frame_locals_getter,
                                                     nullptr));
        frame_type_->dict->set(new_str("f_globals"),
                               new_getset_descriptor("f_globals",
                                                     frame_type_,
                                                     frame_globals_getter,
                                                     nullptr));
        frame_type_->dict->set(new_str("f_builtins"),
                               new_getset_descriptor("f_builtins",
                                                     frame_type_,
                                                     frame_builtins_getter,
                                                     nullptr));
        frame_type_->dict->set(new_str("f_lasti"),
                               new_getset_descriptor("f_lasti",
                                                     frame_type_,
                                                     frame_lasti_getter,
                                                     nullptr));
        frame_type_->dict->set(new_str("f_lineno"),
                               new_getset_descriptor("f_lineno",
                                                     frame_type_,
                                                     frame_lineno_getter,
                                                     nullptr));
    }
}
