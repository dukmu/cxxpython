#include "builtins.hpp"

#include "../../Objects/object_utils.hpp"

#include <string>
#include <vector>

namespace cxxpython
{
    namespace
    {
        std::string module_name_from_globals(Runtime &runtime, DictObject *globals)
        {
            if (globals)
            {
                if (auto *name = dynamic_cast<StrObject *>(globals->get(runtime.new_str("__name__"))))
                    return name->value;
            }
            return "__main__";
        }
    }

    Object *builtin_build_class(Runtime &runtime, const CallArguments &args)
    {
        if (args.positional.size() < 2)
            raise_type_error(&runtime, "__build_class__() missing function or name");
        auto *function = dynamic_cast<PythonFunctionObject *>(args.positional[0]);
        if (!function)
            raise_type_error(&runtime, "__build_class__() argument 1 must be a function");
        auto *name = dynamic_cast<StrObject *>(args.positional[1]);
        if (!name)
            raise_type_error(&runtime, "__build_class__() argument 2 must be a str");

        std::vector<Object *> bases;
        bases.reserve(args.positional.size() - 2);
        for (std::size_t index = 2; index < args.positional.size(); ++index)
            bases.push_back(args.positional[index]);
        TupleObject *bases_tuple = runtime.new_tuple(std::move(bases));
        auto bases_root = runtime.root(bases_tuple);

        Frame *frame = current_frame_or_error(runtime, "__build_class__");
        return runtime.build_class(ClassBuildRequest{
            .name = name->value,
            .body_code = function->code(),
            .bases = bases_root.get(),
            .keywords = args.keywords,
            .globals = function->globals(),
            .builtins = frame->builtins(),
            .module_fallback = module_name_from_globals(runtime, function->globals()),
            .qualname = name->value,
        });
    }

    Object *builtin_import(Runtime &runtime, const CallArguments &args)
    {
        require_no_keywords(&runtime, "__import__", args);
        require_positional_range(&runtime, "__import__", args, 1, 5);
        auto *name = dynamic_cast<StrObject *>(args.positional[0]);
        if (!name)
            raise_type_error(&runtime, "__import__() name must be str");
        DictObject *globals = nullptr;
        if (args.positional.size() >= 2 && args.positional[1] != runtime.none())
        {
            globals = dynamic_cast<DictObject *>(args.positional[1]);
            if (!globals)
                raise_type_error(&runtime, "__import__() globals must be dict");
        }
        if (!globals)
            globals = current_frame_or_error(runtime, "__import__")->globals();
        std::int64_t level_value = 0;
        if (args.positional.size() >= 5 && args.positional[4] != runtime.none())
        {
            auto *level = dynamic_cast<IntObject *>(args.positional[4]);
            if (!level)
                raise_type_error(&runtime, "__import__() level must be int");
            level_value = level->value;
        }

        Object *fromlist = args.positional.size() >= 4 ? args.positional[3] : runtime.none();
        return runtime.import_name(ImportRequest{
            .name = name->value,
            .globals = globals,
            .locals = nullptr,
            .fromlist = fromlist,
            .level = level_value,
        });
    }
}
