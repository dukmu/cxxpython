#include "cxxpython/object.hpp"
#include "cxxpython/runtime.hpp"
#include "cxxpython/vm.hpp"

#include "object_utils.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
        std::string function_repr(const Object &object)
        {
            auto *function = static_cast<const PythonFunctionObject *>(&object);
            return "<function " + function->name() + ">";
        }

        std::string method_repr(const Object &object)
        {
            auto *method = static_cast<const PythonMethodObject *>(&object);
            return "<bound method " + method->function()->name() + " of " +
                   object_repr(method->self()) + ">";
        }

        std::int64_t identity_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool identity_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool always_true(const Object &object)
        {
            (void)object;
            return true;
        }

        void function_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *function = static_cast<const PythonFunctionObject *>(&object);
            if (function->code())
                visitor(function->code());
            if (function->globals())
                visitor(function->globals());
        }

        void method_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const PythonMethodObject *>(&object);
            if (method->function())
                visitor(method->function());
            if (method->self())
                visitor(method->self());
        }

        Object *function_call(Object &object, const std::vector<Object *> &args)
        {
            auto *function = static_cast<PythonFunctionObject *>(&object);
            return function->call(args);
        }

        Object *method_call(Object &object, const std::vector<Object *> &args)
        {
            auto *method = static_cast<PythonMethodObject *>(&object);
            std::vector<Object *> bound_args;
            bound_args.reserve(args.size() + 1);
            bound_args.push_back(method->self());
            bound_args.insert(bound_args.end(), args.begin(), args.end());
            return method->function()->call(bound_args);
        }

        Object *function_get(Object &object, Object *instance, TypeObject *owner)
        {
            (void)owner;
            auto *function = static_cast<PythonFunctionObject *>(&object);
            if (!instance)
                return function;
            return function->runtime()->new_method(function, instance);
        }

        std::string positional_count_word(std::size_t count)
        {
            return count == 1 ? "argument" : "arguments";
        }

        std::string positional_count_verb(std::size_t count)
        {
            return count == 1 ? "was" : "were";
        }
    }

    PythonFunctionObject::PythonFunctionObject(TypeObject *type,
                                               std::string name,
                                               CodeObject *code,
                                               DictObject *globals)
        : Object(type), name_(std::move(name)), code_(code), globals_(globals)
    {
    }

    const std::string &PythonFunctionObject::name() const noexcept
    {
        return name_;
    }

    CodeObject *PythonFunctionObject::code() const noexcept
    {
        return code_;
    }

    DictObject *PythonFunctionObject::globals() const noexcept
    {
        return globals_;
    }

    Object *PythonFunctionObject::call(const std::vector<Object *> &args)
    {
        const std::uint32_t expected = code_->arg_count();
        if (args.size() != expected)
        {
            std::ostringstream message;
            message << name_ << "() takes " << expected << " positional "
                    << positional_count_word(expected) << " but "
                    << args.size() << " " << positional_count_word(args.size())
                    << " " << positional_count_verb(args.size()) << " given";
            raise_type_error(runtime(), message.str());
        }
        DictObject *locals = runtime()->new_dict();
        if (code_->varnames().size() < args.size())
            throw BytecodeError("function code object argcount exceeds varnames");
        for (std::size_t index = 0; index < args.size(); ++index)
        {
            auto *name = dynamic_cast<StrObject *>(code_->varnames()[index]);
            if (!name)
                throw BytecodeError("function varname must be a str");
            locals->set(name, args[index]);
        }
        if (VirtualMachine *active_vm = runtime()->active_vm())
            return active_vm->execute(*code_, *locals, *globals_, *runtime()->builtins_dict());
        VirtualMachine vm(*runtime());
        return vm.execute(*code_, *locals, *globals_, *runtime()->builtins_dict());
    }

    PythonMethodObject::PythonMethodObject(TypeObject *type, PythonFunctionObject *function, Object *self)
        : Object(type), function_(function), self_(self)
    {
    }

    PythonFunctionObject *PythonMethodObject::function() const noexcept
    {
        return function_;
    }

    Object *PythonMethodObject::self() const noexcept
    {
        return self_;
    }

    TypeSlots make_function_slots()
    {
        return TypeSlots{
            .repr = function_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = function_traverse,
            .call = function_call,
            .descr_get = function_get,
        };
    }

    TypeSlots make_method_slots()
    {
        return TypeSlots{
            .repr = method_repr,
            .hash = identity_hash,
            .equal = identity_equal,
            .truth = always_true,
            .traverse = method_traverse,
            .call = method_call,
        };
    }
}
