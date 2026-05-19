#include "cxxpython/runtime.hpp"

#include <ostream>
#include <sstream>

namespace cxxpython
{
    namespace
    {
        void reject_keywords(Runtime &runtime, const std::string &name, const CallArguments &args)
        {
            if (args.keywords && args.keywords->size() != 0)
                raise_type_error(&runtime, name + "() takes no keyword arguments");
        }

        Object *builtin_print(Runtime &runtime, const CallArguments &args)
        {
            reject_keywords(runtime, "print", args);
            std::ostream &out = runtime.stdout_stream();
            for (std::size_t index = 0; index < args.positional.size(); ++index)
            {
                if (index != 0)
                    out << ' ';
                out << object_str(args.positional[index]);
            }
            out << '\n';
            return runtime.none();
        }

        Object *builtin_len(Runtime &runtime, const CallArguments &args)
        {
            reject_keywords(runtime, "len", args);
            if (args.positional.size() != 1)
            {
                std::ostringstream message;
                message << "len() takes exactly one argument (" << args.positional.size() << " given)";
                raise_type_error(&runtime, message.str());
            }
            return runtime.new_int(static_cast<std::int64_t>(object_length(args.positional[0])));
        }
    }

    void Runtime::initialize_builtins()
    {
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str("builtins"));
        dict->set(new_str("__file__"), new_str(""));

        builtins_module_ = allocate<ModuleObject>(module_type_, "builtins", "", dict);
        dict->set(new_str("__builtins__"), builtins_module_);
        dict->set(new_str("NotImplemented"), not_implemented_);
        dict->set(new_str("object"), object_type_);
        dict->set(new_str("type"), type_type_);
        dict->set(new_str("str"), str_type_);
        dict->set(new_str("list"), list_type_);
        dict->set(new_str("property"), property_type_);
        dict->set(new_str("staticmethod"), staticmethod_type_);
        dict->set(new_str("classmethod"), classmethod_type_);
        dict->set(new_str("print"), new_native_function("print", builtin_print));
        dict->set(new_str("len"), new_native_function("len", builtin_len));
    }
}
