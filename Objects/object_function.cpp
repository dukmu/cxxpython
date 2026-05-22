#include "cxxpython/object.hpp"
#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"

#include "object_utils.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

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
            if (PythonFunctionObject *function = method->function())
            {
                return "<bound method " + function->name() + " of " +
                       object_repr(method->self()) + ">";
            }
            return "<bound method " + object_repr(method->callable()) + " of " +
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
            if (function->defaults())
                visitor(function->defaults());
            for (CellObject *cell : function->closure())
            {
                if (cell)
                    visitor(cell);
            }
        }

        void method_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *method = static_cast<const PythonMethodObject *>(&object);
            if (method->callable())
                visitor(method->callable());
            if (method->self())
                visitor(method->self());
        }

        Object *function_call(Object &object, const CallArguments &args)
        {
            auto *function = static_cast<PythonFunctionObject *>(&object);
            return function->call(args);
        }

        Object *method_call(Object &object, const CallArguments &args)
        {
            auto *method = static_cast<PythonMethodObject *>(&object);
            std::vector<Object *> bound_args;
            bound_args.reserve(args.positional.size() + 1);
            bound_args.push_back(method->self());
            bound_args.insert(bound_args.end(), args.positional.begin(), args.positional.end());
            return object_call(method->callable(), CallArguments{std::move(bound_args), args.keywords});
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

        StrObject *varname_at(CodeObject *code, std::uint32_t index)
        {
            if (index >= code->varnames().size())
                throw BytecodeError("function code object argument metadata exceeds varnames");
            auto *name = dynamic_cast<StrObject *>(code->varnames()[index]);
            if (!name)
                throw BytecodeError("function varname must be a str");
            return name;
        }

        void raise_positional_count_error(Runtime *runtime,
                                          const std::string &name,
                                          std::uint32_t expected,
                                          std::size_t given)
        {
            std::ostringstream message;
            message << name << "() takes " << expected << " positional "
                    << positional_count_word(expected) << " but "
                    << given << " " << positional_count_word(given)
                    << " " << positional_count_verb(given) << " given";
            raise_type_error(runtime, message.str());
        }

        std::uint32_t argument_slot_count(const CodeObject *code)
        {
            return code->arg_count() +
                   (code->has_varargs() ? 1U : 0U) +
                   (code->has_varkeywords() ? 1U : 0U);
        }

        PythonFunctionObject *require_function_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *function = dynamic_cast<PythonFunctionObject *>(instance);
            if (!function)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have function layout");
            return function;
        }

        Object *function_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_function_instance(runtime, instance, "__name__")->name());
        }

        Object *function_qualname_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_function_instance(runtime, instance, "__qualname__")->name());
        }

        Object *function_code_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_function_instance(runtime, instance, "__code__")->code();
        }

        Object *function_defaults_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            PythonFunctionObject *function = require_function_instance(runtime, instance, "__defaults__");
            return function->defaults() ? static_cast<Object *>(function->defaults()) : static_cast<Object *>(runtime.none());
        }

        void function_defaults_setter(Runtime &runtime, Object *instance, Object *value)
        {
            PythonFunctionObject *function = require_function_instance(runtime, instance, "__defaults__");
            if (!value || value == runtime.none())
            {
                function->set_defaults(nullptr);
                return;
            }
            auto *defaults = dynamic_cast<TupleObject *>(value);
            if (!defaults)
                raise_type_error(&runtime, "__defaults__ must be set to a tuple or None");
            function->set_defaults(defaults);
        }

        Object *function_globals_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_function_instance(runtime, instance, "__globals__")->globals();
        }

        Object *function_module_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            PythonFunctionObject *function = require_function_instance(runtime, instance, "__module__");
            if (auto *module = dynamic_cast<StrObject *>(function->globals()->get(runtime.new_str("__name__"))))
                return module;
            return runtime.none();
        }

        Object *function_doc_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Object *doc = require_function_instance(runtime, instance, "__doc__")->doc();
            return doc ? doc : runtime.none();
        }

        Object *function_text_signature_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_function_instance(runtime, instance, "__text_signature__")->text_signature());
        }
    }

    PythonFunctionObject::PythonFunctionObject(TypeObject *type,
                                               std::string name,
                                               CodeObject *code,
                                               DictObject *globals,
                                               TupleObject *defaults,
                                               std::vector<CellObject *> closure)
        : Object(type),
          name_(std::move(name)),
          code_(code),
          globals_(globals),
          defaults_(defaults),
          doc_(nullptr),
          closure_(std::move(closure))
    {
        if (code_ && !code_->constants().empty())
        {
            if (dynamic_cast<StrObject *>(code_->constants().front()))
                doc_ = code_->constants().front();
        }
    }

    const std::string &PythonFunctionObject::name() const noexcept
    {
        return name_;
    }

    Object *PythonFunctionObject::doc() const noexcept
    {
        return doc_;
    }

    std::string PythonFunctionObject::text_signature() const
    {
        if (!code_)
            return "()";

        std::ostringstream signature;
        signature << "(";
        bool first = true;
        auto append_name = [&signature, &first](StrObject *name, std::string_view prefix = {})
        {
            if (!first)
                signature << ", ";
            first = false;
            signature << prefix << name->value;
        };

        for (std::uint32_t index = 0; index < code_->arg_count(); ++index)
        {
            append_name(varname_at(code_, index));
            if (defaults_ && index >= code_->arg_count() - defaults_->size())
                signature << "=...";
        }
        if (code_->has_varargs())
            append_name(varname_at(code_, code_->varargs_index()), "*");
        if (code_->has_varkeywords())
            append_name(varname_at(code_, code_->varkeywords_index()), "**");
        signature << ")";
        return signature.str();
    }

    CodeObject *PythonFunctionObject::code() const noexcept
    {
        return code_;
    }

    DictObject *PythonFunctionObject::globals() const noexcept
    {
        return globals_;
    }

    TupleObject *PythonFunctionObject::defaults() const noexcept
    {
        return defaults_;
    }

    void PythonFunctionObject::set_defaults(TupleObject *defaults)
    {
        if (defaults && defaults->size() > code_->arg_count())
            raise_type_error(runtime(), "__defaults__ length exceeds positional arguments");
        defaults_ = defaults;
    }

    const std::vector<CellObject *> &PythonFunctionObject::closure() const noexcept
    {
        return closure_;
    }

    Object *PythonFunctionObject::call(const CallArguments &args)
    {
        DictObject *locals = runtime()->new_dict();
        const std::uint32_t expected = code_->arg_count();
        if (code_->varnames().size() < argument_slot_count(code_))
            throw BytecodeError("function code object argument metadata exceeds varnames");

        std::vector<bool> bound(expected, false);
        const std::size_t direct_count = std::min<std::size_t>(args.positional.size(), expected);
        for (std::size_t index = 0; index < direct_count; ++index)
        {
            locals->set(varname_at(code_, static_cast<std::uint32_t>(index)), args.positional[index]);
            bound[index] = true;
        }

        if (args.positional.size() > expected)
        {
            if (!code_->has_varargs())
                raise_positional_count_error(runtime(), name_, expected, args.positional.size());
            std::vector<Object *> extras;
            extras.reserve(args.positional.size() - expected);
            for (std::size_t index = expected; index < args.positional.size(); ++index)
                extras.push_back(args.positional[index]);
            locals->set(varname_at(code_, code_->varargs_index()), runtime()->new_tuple(std::move(extras)));
        }
        else if (code_->has_varargs())
        {
            locals->set(varname_at(code_, code_->varargs_index()), runtime()->new_tuple({}));
        }

        DictObject *keyword_locals = code_->has_varkeywords() ? runtime()->new_dict() : nullptr;
        if (args.keywords)
        {
            for (const DictObject::Entry &entry : args.keywords->entries())
            {
                auto *keyword_name = dynamic_cast<StrObject *>(entry.key);
                if (!keyword_name)
                    raise_type_error(runtime(), name_ + "() keywords must be strings");

                bool matched = false;
                for (std::uint32_t index = 0; index < expected; ++index)
                {
                    StrObject *parameter = varname_at(code_, index);
                    if (parameter->value != keyword_name->value)
                        continue;
                    if (bound[index])
                    {
                        raise_type_error(runtime(),
                                         name_ + "() got multiple values for argument '" +
                                             keyword_name->value + "'");
                    }
                    locals->set(parameter, entry.value);
                    bound[index] = true;
                    matched = true;
                    break;
                }

                if (!matched)
                {
                    if (!keyword_locals)
                    {
                        raise_type_error(runtime(),
                                         name_ + "() got an unexpected keyword argument '" +
                                             keyword_name->value + "'");
                    }
                    keyword_locals->set(entry.key, entry.value);
                }
            }
        }

        for (std::uint32_t index = 0; index < expected; ++index)
        {
            if (bound[index])
                continue;
            StrObject *parameter = varname_at(code_, index);
            const std::size_t default_count = defaults_ ? defaults_->size() : 0;
            const std::uint32_t first_default_index =
                expected - static_cast<std::uint32_t>(default_count);
            if (index >= first_default_index)
            {
                Object *value = defaults_->items()[index - first_default_index];
                locals->set(parameter, value);
                continue;
            }
            raise_type_error(runtime(),
                             name_ + "() missing required positional argument: '" +
                                 parameter->value + "'");
        }

        if (keyword_locals)
        {
            locals->set(varname_at(code_, code_->varkeywords_index()), keyword_locals);
        }

        return runtime()->call_python_function(this, locals);
    }

    PythonMethodObject::PythonMethodObject(TypeObject *type, Object *callable, Object *self)
        : Object(type), callable_(callable), self_(self)
    {
    }

    PythonFunctionObject *PythonMethodObject::function() const noexcept
    {
        return dynamic_cast<PythonFunctionObject *>(callable_);
    }

    Object *PythonMethodObject::callable() const noexcept
    {
        return callable_;
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

    void initialize_function_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("__name__"),
                        runtime.new_getset_descriptor("__name__",
                                                      type,
                                                      function_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__qualname__"),
                        runtime.new_getset_descriptor("__qualname__",
                                                      type,
                                                      function_qualname_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__code__"),
                        runtime.new_getset_descriptor("__code__",
                                                      type,
                                                      function_code_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__defaults__"),
                        runtime.new_getset_descriptor("__defaults__",
                                                      type,
                                                      function_defaults_getter,
                                                      function_defaults_setter));
        type->dict->set(runtime.new_str("__globals__"),
                        runtime.new_getset_descriptor("__globals__",
                                                      type,
                                                      function_globals_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__module__"),
                        runtime.new_getset_descriptor("__module__",
                                                      type,
                                                      function_module_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__doc__"),
                        runtime.new_getset_descriptor("__doc__",
                                                      type,
                                                      function_doc_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("__text_signature__"),
                        runtime.new_getset_descriptor("__text_signature__",
                                                      type,
                                                      function_text_signature_getter,
                                                      nullptr));
    }
}
