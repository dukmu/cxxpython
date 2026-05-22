#include "builtins.hpp"

#include "cxxpython/compiler.hpp"

#include "../../Objects/object_utils.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace cxxpython
{
    namespace
    {
        CodeObject *compile_source(Runtime &runtime,
                                   std::string_view source,
                                   std::string filename,
                                   std::string_view mode)
        {
            if (mode == "eval")
                return compile_eval_input(runtime, source, std::move(filename));
            if (mode == "exec")
                return compile_exec_input(runtime, source, std::move(filename));
            raise_value_error(&runtime, "compile() mode must be 'exec' or 'eval'");
        }

        DictObject *optional_dict_arg(Runtime &runtime,
                                      const CallArguments &args,
                                      std::size_t index,
                                      DictObject *fallback,
                                      const char *function_name,
                                      const char *argument_name)
        {
            if (args.positional.size() <= index || args.positional[index] == runtime.none())
                return fallback;
            auto *dict = dynamic_cast<DictObject *>(args.positional[index]);
            if (!dict)
            {
                raise_type_error(&runtime,
                                 std::string(function_name) + "() " +
                                     argument_name + " must be a dict");
            }
            return dict;
        }

        CodeObject *code_from_source_or_code(Runtime &runtime,
                                             Object *source_or_code,
                                             std::string_view mode,
                                             const char *function_name)
        {
            if (auto *code = dynamic_cast<CodeObject *>(source_or_code))
                return code;
            auto *source = dynamic_cast<StrObject *>(source_or_code);
            if (!source)
                raise_type_error(&runtime, std::string(function_name) + "() argument must be str or code");
            return compile_source(runtime, source->value, "<string>", mode);
        }
    }

    Object *builtin_compile(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "compile", args);
        require_positional_count(&runtime, "compile", args, 3);
        auto *source = dynamic_cast<StrObject *>(args.positional[0]);
        auto *filename = dynamic_cast<StrObject *>(args.positional[1]);
        auto *mode = dynamic_cast<StrObject *>(args.positional[2]);
        if (!source)
            raise_type_error(&runtime, "compile() source must be str");
        if (!filename)
            raise_type_error(&runtime, "compile() filename must be str");
        if (!mode)
            raise_type_error(&runtime, "compile() mode must be str");
        return compile_source(runtime, source->value, filename->value, mode->value);
    }

    Object *builtin_eval(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "eval", args);
        if (args.positional.empty() || args.positional.size() > 3)
            raise_type_error(&runtime, "eval() expected 1 to 3 arguments");
        Frame *frame = current_frame_or_error(runtime, "eval");
        DictObject *globals = optional_dict_arg(runtime, args, 1, frame->globals(), "eval", "globals");
        DictObject *locals = optional_dict_arg(runtime, args, 2, globals, "eval", "locals");
        CodeObject *code = code_from_source_or_code(runtime, args.positional[0], "eval", "eval");
        auto code_root = runtime.root(code);
        VirtualMachine *vm = runtime.active_vm();
        if (!vm)
            raise_type_error(&runtime, "eval() requires an active VM");
        return vm->execute(ExecutionRequest{
            .code = code_root.get(),
            .locals = locals,
            .globals = globals,
            .builtins = runtime.builtins_dict(),
            .closure = {},
            .mode = ExecutionMode::Eval,
        });
    }

    Object *builtin_exec(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "exec", args);
        if (args.positional.empty() || args.positional.size() > 3)
            raise_type_error(&runtime, "exec() expected 1 to 3 arguments");
        Frame *frame = current_frame_or_error(runtime, "exec");
        DictObject *globals = optional_dict_arg(runtime, args, 1, frame->globals(), "exec", "globals");
        DictObject *locals = optional_dict_arg(runtime, args, 2, globals, "exec", "locals");
        CodeObject *code = code_from_source_or_code(runtime, args.positional[0], "exec", "exec");
        auto code_root = runtime.root(code);
        VirtualMachine *vm = runtime.active_vm();
        if (!vm)
            raise_type_error(&runtime, "exec() requires an active VM");
        (void)vm->execute(ExecutionRequest{
            .code = code_root.get(),
            .locals = locals,
            .globals = globals,
            .builtins = runtime.builtins_dict(),
            .closure = {},
            .mode = ExecutionMode::Exec,
        });
        return runtime.none();
    }
}
