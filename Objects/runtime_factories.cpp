#include "cxxpython/runtime.hpp"

#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

namespace cxxpython
{
    namespace
    {
        std::string exception_message_from_args(const TupleObject *args)
        {
            if (!args || args->size() == 0)
                return "";
            if (args->size() == 1)
            {
                if (auto *message = dynamic_cast<StrObject *>(args->items()[0]))
                    return message->value;
                return object_repr(args->items()[0]);
            }
            return object_repr(args);
        }
    }

    IntObject *Runtime::new_int(std::int64_t value)
    {
        return allocate<IntObject>(int_type_, value);
    }

    StrObject *Runtime::new_str(std::string value)
    {
        return allocate<StrObject>(str_type_, std::move(value));
    }

    ListObject *Runtime::new_list(std::vector<Object *> items)
    {
        return allocate<ListObject>(list_type_, std::move(items));
    }

    TupleObject *Runtime::new_tuple(std::vector<Object *> items)
    {
        return allocate<TupleObject>(tuple_type_, std::move(items));
    }

    DictObject *Runtime::new_dict()
    {
        return allocate<DictObject>(dict_type_);
    }

    MappingProxyObject *Runtime::new_mappingproxy(DictObject *mapping)
    {
        if (!mapping)
            raise_type_error(this, "mappingproxy mapping cannot be null");
        return allocate<MappingProxyObject>(mappingproxy_type_, mapping);
    }

    ModuleObject *Runtime::new_module(std::string name, std::string filename)
    {
        DictObject *dict = new_dict();
        if (!dict)
            raise_type_error(this, "module dict cannot be null");
        dict->set(new_str("__name__"), new_str(name));
        dict->set(new_str("__file__"), new_str(filename));
        if (builtins_module_)
            dict->set(new_str("__builtins__"), builtins_module_);
        return allocate<ModuleObject>(module_type_, std::move(name), std::move(filename), dict);
    }

    NativeFunctionObject *Runtime::new_native_function(std::string name, NativeFunctionCallback callback)
    {
        return allocate<NativeFunctionObject>(native_function_type_, std::move(name), std::move(callback));
    }

    PythonFunctionObject *Runtime::new_function(std::string name, CodeObject *code, DictObject *globals)
    {
        if (!code)
            raise_type_error(this, "function requires a code object");
        if (!globals)
            raise_type_error(this, "function requires globals");
        return allocate<PythonFunctionObject>(function_type_, std::move(name), code, globals);
    }

    PythonMethodObject *Runtime::new_method(PythonFunctionObject *function, Object *self)
    {
        if (!function || !self)
            raise_type_error(this, "method requires function and self");
        return allocate<PythonMethodObject>(method_type_, function, self);
    }

    MethodDescriptorObject *Runtime::new_method_descriptor(std::string name,
                                                           TypeObject *owner,
                                                           MethodDescriptorCallback callback)
    {
        if (!owner)
            raise_type_error(this, "method descriptor owner cannot be null");
        return allocate<MethodDescriptorObject>(method_descriptor_type_,
                                                std::move(name),
                                                owner,
                                                std::move(callback));
    }

    BoundMethodObject *Runtime::new_bound_method(MethodDescriptorObject *method, Object *self)
    {
        if (!method || !self)
            raise_type_error(this, "bound method requires method and self");
        return allocate<BoundMethodObject>(native_function_type_, method, self);
    }

    GetSetDescriptorObject *Runtime::new_getset_descriptor(std::string name,
                                                           TypeObject *owner,
                                                           GetSetGetter getter,
                                                           GetSetSetter setter)
    {
        if (!owner)
            raise_type_error(this, "getset descriptor owner cannot be null");
        if (!getter)
            raise_type_error(this, "getset descriptor getter cannot be null");
        return allocate<GetSetDescriptorObject>(getset_descriptor_type_,
                                                std::move(name),
                                                owner,
                                                std::move(getter),
                                                std::move(setter));
    }

    CodeObject *Runtime::new_code(std::vector<Object *> constants, std::vector<Instruction> instructions)
    {
        return new_code("<module>", "<string>", std::move(constants), {}, std::move(instructions));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::vector<Object *> constants,
                                  std::vector<Instruction> instructions)
    {
        return new_code(std::move(name), std::move(filename), std::move(constants), {}, std::move(instructions));
    }

    CodeObject *Runtime::new_code(std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions)
    {
        return new_code("<module>", "<string>", std::move(constants), std::move(names), std::move(instructions));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions)
    {
        return new_code(std::move(name),
                        std::move(filename),
                        0,
                        {},
                        std::move(constants),
                        std::move(names),
                        std::move(instructions));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::uint32_t arg_count,
                                  std::vector<Object *> varnames,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions)
    {
        return allocate<CodeObject>(code_type_,
                                    std::move(name),
                                    std::move(filename),
                                    arg_count,
                                    std::move(varnames),
                                    std::move(constants),
                                    std::move(names),
                                    std::move(instructions));
    }

    Frame *Runtime::new_frame(CodeObject *code, DictObject *locals, DictObject *globals, DictObject *builtins)
    {
        DictObject *resolved_globals = globals ? globals : new_dict();
        DictObject *resolved_locals = locals ? locals : resolved_globals;
        DictObject *resolved_builtins = builtins ? builtins : builtins_dict();
        if (!resolved_builtins)
            raise_type_error(this, "runtime builtins are not initialized");
        return allocate<Frame>(frame_type_, code, resolved_locals, resolved_globals, resolved_builtins);
    }

    TypeObject *Runtime::new_heap_type(std::string name, TypeObject *base, TypeSlots slots)
    {
        TypeObject *type = allocate_type(std::move(name));
        type->set_type(type_type_);
        type->flags = TypeFlagHeapType | TypeFlagHasInstanceDict;
        TypeObject *resolved_base = base ? base : object_type_;
        type->slots = inherit_slots(resolved_base, slots);
        initialize_type(type, resolved_base);
        initialize_type_dict(type);
        return type;
    }

    TypeObject *Runtime::new_class(std::string name,
                                   std::vector<TypeObject *> bases,
                                   DictObject *namespace_dict,
                                   std::string module_fallback)
    {
        if (!namespace_dict)
            raise_type_error(this, "class namespace cannot be null");
        if (bases.empty())
            bases.push_back(object_type_);
        for (TypeObject *base : bases)
        {
            if (!base)
                raise_type_error(this, "class bases cannot contain null");
        }

        auto *namespace_module = dynamic_cast<StrObject *>(namespace_dict->get(new_str("__module__")));
        TypeObject *type = allocate_type(std::move(name));
        type->set_type(type_type_);
        type->flags = TypeFlagHeapType | TypeFlagHasInstanceDict;
        type->slots = inherit_slots(bases[0], {});
        initialize_type(type, bases);
        initialize_type_dict(type);
        for (const DictObject::Entry &entry : namespace_dict->entries())
        {
            if (entry.key && entry.value)
                type->dict->set(entry.key, entry.value);
        }
        if (namespace_module)
            type->module = namespace_module->value;
        else
            type->module = std::move(module_fallback);
        type->dict->set(new_str("__name__"), new_str(type->name));
        type->dict->set(new_str("__module__"), new_str(type->module));
        if (!type->dict->contains(new_str("__qualname__")))
            type->dict->set(new_str("__qualname__"), new_str(type->name));
        std::vector<Object *> base_objects;
        base_objects.reserve(type->bases.size());
        for (TypeObject *base : type->bases)
            base_objects.push_back(base);
        type->dict->set(new_str("__bases__"), new_tuple(std::move(base_objects)));
        return type;
    }

    TypeObject *Runtime::new_class(std::string name,
                                   TypeObject *base,
                                   DictObject *namespace_dict,
                                   std::string module_fallback)
    {
        std::vector<TypeObject *> bases;
        if (base)
            bases.push_back(base);
        return new_class(std::move(name), std::move(bases), namespace_dict, std::move(module_fallback));
    }

    ExceptionObject *Runtime::new_exception(TypeObject *type, std::string message)
    {
        return new_exception(type, {new_str(std::move(message))});
    }

    ExceptionObject *Runtime::new_exception(TypeObject *type, std::vector<Object *> args)
    {
        TypeObject *resolved_type = type ? type : exception_type_;
        TupleObject *args_tuple = new_tuple(std::move(args));
        return allocate<ExceptionObject>(resolved_type, args_tuple, exception_message_from_args(args_tuple));
    }

    ExceptionObject *Runtime::new_type_error(std::string message)
    {
        return new_exception(type_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_index_error(std::string message)
    {
        return new_exception(index_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_key_error(Object *key)
    {
        Object *resolved_key = key ? key : new_str("<null>");
        TupleObject *args_tuple = new_tuple({resolved_key});
        return allocate<ExceptionObject>(key_error_type_, args_tuple, object_repr(resolved_key));
    }

    ExceptionObject *Runtime::new_syntax_error(std::string message)
    {
        return new_exception(syntax_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_name_error(std::string message)
    {
        return new_exception(name_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_attribute_error(std::string message)
    {
        return new_exception(attribute_error_type_, std::move(message));
    }
}
