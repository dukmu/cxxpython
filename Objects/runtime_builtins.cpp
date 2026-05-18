#include "cxxpython/runtime.hpp"

#include <ostream>

namespace cxxpython
{
    namespace
    {
        std::string print_text(Object *object)
        {
            if (auto *text = dynamic_cast<StrObject *>(object))
                return text->value;
            return object_repr(object);
        }

        Object *builtin_print(Runtime &runtime, const std::vector<Object *> &args)
        {
            std::ostream &out = runtime.stdout_stream();
            for (std::size_t index = 0; index < args.size(); ++index)
            {
                if (index != 0)
                    out << ' ';
                out << print_text(args[index]);
            }
            out << '\n';
            return runtime.none();
        }
    }

    void Runtime::initialize_builtins()
    {
        DictObject *dict = new_dict();
        dict->set(new_str("__name__"), new_str("builtins"));
        dict->set(new_str("__file__"), new_str(""));

        builtins_module_ = allocate<ModuleObject>(module_type_, "builtins", "", dict);
        dict->set(new_str("__builtins__"), builtins_module_);
        dict->set(new_str("print"), new_native_function("print", builtin_print));
    }
}
