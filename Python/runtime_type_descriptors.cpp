#include "cxxpython/runtime.hpp"
#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

#include "../Objects/type_initialization.hpp"

namespace cxxpython
{
    namespace
    {
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

    void initialize_code_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("co_name"),
                        runtime.new_getset_descriptor("co_name",
                                                      type,
                                                      code_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_filename"),
                        runtime.new_getset_descriptor("co_filename",
                                                      type,
                                                      code_filename_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_argcount"),
                        runtime.new_getset_descriptor("co_argcount",
                                                      type,
                                                      code_argcount_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_flags"),
                        runtime.new_getset_descriptor("co_flags",
                                                      type,
                                                      code_flags_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_stacksize"),
                        runtime.new_getset_descriptor("co_stacksize",
                                                      type,
                                                      code_stacksize_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_consts"),
                        runtime.new_getset_descriptor("co_consts",
                                                      type,
                                                      code_consts_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_names"),
                        runtime.new_getset_descriptor("co_names",
                                                      type,
                                                      code_names_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_varnames"),
                        runtime.new_getset_descriptor("co_varnames",
                                                      type,
                                                      code_varnames_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_cellvars"),
                        runtime.new_getset_descriptor("co_cellvars",
                                                      type,
                                                      code_cellvars_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_freevars"),
                        runtime.new_getset_descriptor("co_freevars",
                                                      type,
                                                      code_freevars_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_firstlineno"),
                        runtime.new_getset_descriptor("co_firstlineno",
                                                      type,
                                                      code_firstlineno_getter,
                                                      nullptr));
    }

    void initialize_frame_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("f_code"),
                        runtime.new_getset_descriptor("f_code",
                                                      type,
                                                      frame_code_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_locals"),
                        runtime.new_getset_descriptor("f_locals",
                                                      type,
                                                      frame_locals_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_globals"),
                        runtime.new_getset_descriptor("f_globals",
                                                      type,
                                                      frame_globals_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_builtins"),
                        runtime.new_getset_descriptor("f_builtins",
                                                      type,
                                                      frame_builtins_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_lasti"),
                        runtime.new_getset_descriptor("f_lasti",
                                                      type,
                                                      frame_lasti_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_lineno"),
                        runtime.new_getset_descriptor("f_lineno",
                                                      type,
                                                      frame_lineno_getter,
                                                      nullptr));
    }

    void Runtime::initialize_type_descriptors()
    {
        initialize_object_type_descriptors(*this, object_type_);
        initialize_type_type_descriptors(*this, type_type_);
        initialize_code_type_descriptors(*this, code_type_);
        initialize_function_type_descriptors(*this, function_type_);
        initialize_frame_type_descriptors(*this, frame_type_);
    }
}
