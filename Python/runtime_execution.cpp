#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

namespace cxxpython
{
    std::string Runtime::active_module_name(std::string fallback)
    {
        VirtualMachine *vm = active_vm();
        Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
        DictObject *globals = frame ? frame->globals() : nullptr;
        if (globals)
        {
            if (auto *name = dynamic_cast<StrObject *>(globals->get(new_str("__name__"))))
                return name->value;
        }
        return fallback;
    }

    Object *Runtime::call_python_function(PythonFunctionObject *function, DictObject *locals)
    {
        if (!function)
            raise_type_error(this, "function call target has invalid layout");
        ExecutionRequest request{
            .code = function->code(),
            .locals = locals,
            .globals = function->globals(),
            .builtins = builtins_dict(),
            .closure = function->closure(),
            .mode = ExecutionMode::Function,
        };
        if (VirtualMachine *active = active_vm())
            return active->execute(request);
        VirtualMachine vm(*this);
        return vm.execute(request);
    }

    SuperObject *Runtime::new_zero_argument_super()
    {
        VirtualMachine *vm = active_vm();
        Frame *frame = vm ? vm->thread_state().current_frame() : nullptr;
        if (!frame)
            raise_type_error(this, "super(): no current frame");
        if (!frame->code())
            raise_type_error(this, "super(): no current frame");

        const std::vector<Object *> &freevars = frame->code()->freevars();
        CellObject *class_cell = nullptr;
        for (std::size_t index = 0; index < freevars.size(); ++index)
        {
            auto *name = dynamic_cast<StrObject *>(freevars[index]);
            if (!name || name->value != "__class__")
                continue;
            class_cell = frame->cell(frame->code()->cellvars().size() + index);
            break;
        }
        if (!class_cell || class_cell->is_empty())
            raise_type_error(this, "super(): __class__ cell not found");
        auto *start_type = dynamic_cast<TypeObject *>(class_cell->value());
        if (!start_type)
            raise_type_error(this, "super(): __class__ cell is not a type");
        if (frame->code()->varnames().empty())
            raise_type_error(this, "super(): no arguments");
        auto *first_arg_name = dynamic_cast<StrObject *>(frame->code()->varnames()[0]);
        if (!first_arg_name)
            throw BytecodeError("super() first argument metadata must be a str");
        Object *first_arg = frame->fast_local(0);
        if (!first_arg)
            raise_type_error(this, "super(): arg[0] deleted");

        TypeObject *object_type = nullptr;
        if (auto *type_object = dynamic_cast<TypeObject *>(first_arg))
        {
            if (!is_subclass(type_object, start_type))
                raise_type_error(this, "super(type, obj): obj must be an instance or subtype of type");
            object_type = type_object;
        }
        else
        {
            object_type = first_arg->type();
            if (!is_subclass(object_type, start_type))
                raise_type_error(this, "super(type, obj): obj must be an instance or subtype of type");
        }
        return new_super(start_type, first_arg, object_type);
    }
}
