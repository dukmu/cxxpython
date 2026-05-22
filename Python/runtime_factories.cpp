#include "cxxpython/runtime.hpp"

#include "cxxpython/bytecode.hpp"
#include "cxxpython/vm.hpp"

namespace cxxpython
{
    namespace
    {
        bool is_attribute_error(Runtime &runtime, const PythonError &error)
        {
            return error.type() == runtime.attribute_error_type();
        }

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

        void validate_class_keywords(Runtime &runtime, DictObject *keywords)
        {
            if (!keywords)
                return;
            for (const DictObject::Entry &entry : keywords->entries())
            {
                auto *name = dynamic_cast<StrObject *>(entry.key);
                if (!name)
                    raise_type_error(&runtime, "class keyword names must be strings");
                if (name->value != "metaclass")
                {
                    raise_type_error(&runtime,
                                     "class creation got an unexpected keyword argument '" +
                                         name->value + "'");
                }
            }
        }

        TypeObject *explicit_metaclass_from_keywords(Runtime &runtime, DictObject *keywords)
        {
            if (!keywords)
                return nullptr;
            Object *metaclass = keywords->get(runtime.new_str("metaclass"));
            if (!metaclass)
                return nullptr;
            auto *type = dynamic_cast<TypeObject *>(metaclass);
            if (!type)
                raise_type_error(&runtime, "metaclass must be a type");
            return type;
        }

        Object *call_prepare(Runtime &runtime,
                             TypeObject *metaclass,
                             StrObject *name,
                             TupleObject *bases)
        {
            Object *prepare = nullptr;
            try
            {
                prepare = object_get_attr(metaclass, "__prepare__");
            }
            catch (const PythonError &error)
            {
                if (!is_attribute_error(runtime, error))
                    throw;
            }
            if (!prepare)
                return runtime.new_dict();

            auto prepare_root = runtime.root(prepare);
            if (dynamic_cast<MethodDescriptorObject *>(prepare_root.get()))
                return object_call(prepare_root.get(), {metaclass, name, bases});
            return object_call(prepare_root.get(), {name, bases});
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

    SliceObject *Runtime::new_slice(Object *start, Object *stop, Object *step)
    {
        return allocate<SliceObject>(
            slice_type_,
            start ? start : none_,
            stop ? stop : none_,
            step ? step : none_);
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

    SequenceIteratorObject *Runtime::new_sequence_iterator(Object *sequence)
    {
        if (!sequence)
            raise_type_error(this, "sequence iterator requires a sequence");
        return allocate<SequenceIteratorObject>(sequence_iterator_type_, sequence);
    }

    DictKeyIteratorObject *Runtime::new_dict_key_iterator(DictObject *dict)
    {
        if (!dict)
            raise_type_error(this, "dict key iterator requires a dict");
        return allocate<DictKeyIteratorObject>(dict_key_iterator_type_, dict);
    }

    RangeObject *Runtime::new_range(std::int64_t start, std::int64_t stop, std::int64_t step)
    {
        if (step == 0)
            raise_value_error(this, "range() arg 3 must not be zero");
        return allocate<RangeObject>(range_type_, start, stop, step);
    }

    RangeIteratorObject *Runtime::new_range_iterator(std::int64_t current,
                                                     std::int64_t step,
                                                     std::size_t remaining)
    {
        return allocate<RangeIteratorObject>(range_iterator_type_, current, step, remaining);
    }

    EnumerateObject *Runtime::new_enumerate(Object *iterator, std::int64_t next_index)
    {
        if (!iterator)
            raise_type_error(this, "enumerate iterator cannot be null");
        return allocate<EnumerateObject>(enumerate_type_, iterator, next_index);
    }

    ZipObject *Runtime::new_zip(std::vector<Object *> iterators)
    {
        return allocate<ZipObject>(zip_type_, std::move(iterators));
    }

    MapObject *Runtime::new_map(Object *callable, std::vector<Object *> iterators)
    {
        if (!callable)
            raise_type_error(this, "map callable cannot be null");
        return allocate<MapObject>(map_type_, callable, std::move(iterators));
    }

    FilterObject *Runtime::new_filter(Object *callable, Object *iterator)
    {
        if (!callable || !iterator)
            raise_type_error(this, "filter requires callable and iterator");
        return allocate<FilterObject>(filter_type_, callable, iterator);
    }

    TextIOObject *Runtime::new_text_io(std::string name,
                                       std::istream *input,
                                       std::ostream *output,
                                       bool owns_streams,
                                       std::unique_ptr<std::iostream> owned_stream)
    {
        return allocate<TextIOObject>(
            text_io_type_,
            std::move(name),
            input,
            output,
            owns_streams,
            std::move(owned_stream));
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
        ModuleObject *module = allocate<ModuleObject>(module_type_, name, std::move(filename), dict);
        if (modules_)
            modules_->set(new_str(name), module);
        return module;
    }

    NativeFunctionObject *Runtime::new_native_function(std::string name,
                                                       NativeFunctionCallback callback,
                                                       std::string doc,
                                                       std::string text_signature)
    {
        return allocate<NativeFunctionObject>(native_function_type_,
                                              std::move(name),
                                              std::move(callback),
                                              std::move(doc),
                                              std::move(text_signature));
    }

    PythonFunctionObject *Runtime::new_function(std::string name,
                                                CodeObject *code,
                                                DictObject *globals,
                                                std::vector<CellObject *> closure)
    {
        if (!code)
            raise_type_error(this, "function requires a code object");
        if (!globals)
            raise_type_error(this, "function requires globals");
        if (closure.size() != code->freevars().size())
            raise_type_error(this, "function closure size does not match code free variables");
        return allocate<PythonFunctionObject>(function_type_, std::move(name), code, globals, std::move(closure));
    }

    CellObject *Runtime::new_cell(Object *value)
    {
        return allocate<CellObject>(cell_type_, value);
    }

    SuperObject *Runtime::new_super(TypeObject *start_type, Object *object, TypeObject *object_type)
    {
        if (!start_type || !object || !object_type)
            raise_type_error(this, "super requires type, object, and object type");
        return allocate<SuperObject>(super_type_, start_type, object, object_type);
    }

    PythonMethodObject *Runtime::new_method(Object *callable, Object *self)
    {
        if (!callable || !self)
            raise_type_error(this, "method requires callable and self");
        return allocate<PythonMethodObject>(method_type_, callable, self);
    }

    MethodDescriptorObject *Runtime::new_method_descriptor(std::string name,
                                                           TypeObject *owner,
                                                           MethodDescriptorCallback callback,
                                                           std::string doc,
                                                           std::string text_signature)
    {
        if (!owner)
            raise_type_error(this, "method descriptor owner cannot be null");
        return allocate<MethodDescriptorObject>(method_descriptor_type_,
                                                std::move(name),
                                                owner,
                                                std::move(callback),
                                                std::move(doc),
                                                std::move(text_signature));
    }

    BoundMethodObject *Runtime::new_bound_method(MethodDescriptorObject *method, Object *self)
    {
        if (!method || !self)
            raise_type_error(this, "bound method requires method and self");
        return allocate<BoundMethodObject>(native_function_type_, method, self);
    }

    SlotWrapperObject *Runtime::new_slot_wrapper(std::string name,
                                                 TypeObject *owner,
                                                 SlotWrapperKind kind,
                                                 BinaryOp binary_op,
                                                 UnaryOp unary_op,
                                                 RichCompareOp rich_compare_op)
    {
        if (!owner)
            raise_type_error(this, "slot wrapper owner cannot be null");
        return allocate<SlotWrapperObject>(slot_wrapper_type_,
                                           std::move(name),
                                           owner,
                                           kind,
                                           binary_op,
                                           unary_op,
                                           rich_compare_op);
    }

    BoundSlotWrapperObject *Runtime::new_bound_slot_wrapper(SlotWrapperObject *wrapper, Object *self)
    {
        if (!wrapper || !self)
            raise_type_error(this, "bound slot wrapper requires wrapper and self");
        return allocate<BoundSlotWrapperObject>(bound_slot_wrapper_type_, wrapper, self);
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

    PropertyObject *Runtime::new_property(Object *getter, Object *setter, Object *deleter, Object *doc)
    {
        return allocate<PropertyObject>(property_type_, getter, setter, deleter, doc);
    }

    StaticMethodObject *Runtime::new_staticmethod(Object *callable)
    {
        if (!callable)
            raise_type_error(this, "staticmethod requires callable");
        return allocate<StaticMethodObject>(staticmethod_type_, callable);
    }

    ClassMethodObject *Runtime::new_classmethod(Object *callable)
    {
        if (!callable)
            raise_type_error(this, "classmethod requires callable");
        return allocate<ClassMethodObject>(classmethod_type_, callable);
    }

    CodeObject *Runtime::new_code(std::vector<Object *> constants, std::vector<Instruction> instructions)
    {
        return new_code("<module>", "<string>", std::move(constants), {}, std::move(instructions));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::vector<Object *> constants,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return new_code(std::move(name),
                        std::move(filename),
                        std::move(constants),
                        {},
                        std::move(instructions),
                        std::move(lines));
    }

    CodeObject *Runtime::new_code(std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return new_code("<module>",
                        "<string>",
                        std::move(constants),
                        std::move(names),
                        std::move(instructions),
                        std::move(lines));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return new_code(std::move(name),
                        std::move(filename),
                        0,
                        0,
                        {},
                        std::move(constants),
                        std::move(names),
                        std::move(instructions),
                        std::move(lines));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::uint32_t arg_count,
                                  std::vector<Object *> varnames,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return new_code(std::move(name),
                        std::move(filename),
                        arg_count,
                        0,
                        std::move(varnames),
                        std::move(constants),
                        std::move(names),
                        {},
                        {},
                        std::move(instructions),
                        std::move(lines));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::uint32_t arg_count,
                                  std::uint32_t flags,
                                  std::vector<Object *> varnames,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return new_code(std::move(name),
                        std::move(filename),
                        arg_count,
                        flags,
                        std::move(varnames),
                        std::move(constants),
                        std::move(names),
                        {},
                        {},
                        std::move(instructions),
                        std::move(lines));
    }

    CodeObject *Runtime::new_code(std::string name,
                                  std::string filename,
                                  std::uint32_t arg_count,
                                  std::uint32_t flags,
                                  std::vector<Object *> varnames,
                                  std::vector<Object *> constants,
                                  std::vector<Object *> names,
                                  std::vector<Object *> cellvars,
                                  std::vector<Object *> freevars,
                                  std::vector<Instruction> instructions,
                                  std::vector<std::uint32_t> lines)
    {
        return allocate<CodeObject>(code_type_,
                                    std::move(name),
                                    std::move(filename),
                                    arg_count,
                                    flags,
                                    std::move(varnames),
                                    std::move(constants),
                                    std::move(names),
                                    std::move(cellvars),
                                    std::move(freevars),
                                    std::move(instructions),
                                    std::move(lines));
    }

    Frame *Runtime::new_frame(CodeObject *code,
                              DictObject *locals,
                              DictObject *globals,
                              DictObject *builtins,
                              std::vector<CellObject *> closure)
    {
        if (!code)
            throw BytecodeError("frame requires a code object");
        if (closure.size() != code->freevars().size())
            throw BytecodeError("frame closure size does not match code free variables");
        DictObject *resolved_globals = globals ? globals : new_dict();
        DictObject *resolved_locals = locals ? locals : resolved_globals;
        DictObject *resolved_builtins = builtins ? builtins : builtins_dict();
        if (!resolved_builtins)
            raise_type_error(this, "runtime builtins are not initialized");

        std::vector<CellObject *> cells;
        std::vector<Rooted<CellObject>> cell_roots;
        cells.reserve(code->cellvars().size() + closure.size());
        cell_roots.reserve(code->cellvars().size() + closure.size());
        for (Object *name_object : code->cellvars())
        {
            auto *name = dynamic_cast<StrObject *>(name_object);
            if (!name)
                throw BytecodeError("cell variable name must be a str");
            CellObject *cell = new_cell(resolved_locals->get(name));
            cells.push_back(cell);
            cell_roots.push_back(root(cell));
        }
        for (CellObject *cell : closure)
        {
            if (!cell)
                throw BytecodeError("frame closure cannot contain null cells");
            cells.push_back(cell);
            cell_roots.push_back(root(cell));
        }

        return allocate<Frame>(frame_type_,
                               code,
                               resolved_locals,
                               resolved_globals,
                               resolved_builtins,
                               std::move(cells));
    }

    TypeObject *Runtime::new_heap_type(std::string name, TypeObject *base, TypeSlots slots)
    {
        TypeObject *type = allocate_type(std::move(name));
        type->set_type(type_type_);
        type->flags = TypeFlagHeapType | TypeFlagHasInstanceDict;
        TypeObject *resolved_base = base ? base : object_type_;
        type->slots = inherit_slots(resolved_base, slots);
        initialize_type(type, resolved_base);
        type->solid_base = resolved_base->solid_base ? resolved_base->solid_base : resolved_base;
        initialize_type_dict(type);
        return type;
    }

    Object *Runtime::new_plain_instance(TypeObject *type)
    {
        if (!type)
            raise_type_error(this, "plain instance requires a type");
        return allocate<Object>(type);
    }

    DictObject *Runtime::prepare_class_namespace(const ClassConstructionRequest &request)
    {
        DictObject *namespace_dict = request.namespace_dict ? request.namespace_dict : new_dict();
        if (!namespace_dict->contains(new_str("__module__")))
            namespace_dict->set(new_str("__module__"), new_str(request.module_fallback));
        const std::string qualname = request.qualname.empty() ? request.name : request.qualname;
        if (!namespace_dict->contains(new_str("__qualname__")))
            namespace_dict->set(new_str("__qualname__"), new_str(qualname));
        return namespace_dict;
    }

    TypeObject *Runtime::new_class(ClassConstructionRequest request)
    {
        if (!request.namespace_dict)
            raise_type_error(this, "class namespace cannot be null");
        std::vector<TypeObject *> bases = std::move(request.bases);
        if (bases.empty())
            bases.push_back(object_type_);
        for (TypeObject *base : bases)
        {
            if (!base)
                raise_type_error(this, "class bases cannot contain null");
        }

        validate_base_layouts(bases);
        TypeObject *metaclass = select_metaclass(request.explicit_metaclass, bases);
        TypeObject *best_base = best_base_for_layout(bases);
        auto *namespace_module = dynamic_cast<StrObject *>(request.namespace_dict->get(new_str("__module__")));
        std::string class_name = std::move(request.name);
        std::string qualname = request.qualname.empty() ? class_name : std::move(request.qualname);
        TypeObject *type = allocate_type(std::move(class_name));
        type->set_type(metaclass);
        type->flags = TypeFlagHeapType | TypeFlagHasInstanceDict;
        type->slots = inherit_slots(best_base, {});
        initialize_type(type, bases);
        type->base = best_base;
        type->solid_base = best_base && best_base->solid_base ? best_base->solid_base : object_type_;
        initialize_type_dict(type);
        for (const DictObject::Entry &entry : request.namespace_dict->entries())
        {
            auto *key = dynamic_cast<StrObject *>(entry.key);
            if (key && key->value == "__classcell__")
                continue;
            if (entry.key && entry.value)
                type->dict->set(entry.key, entry.value);
        }
        if (namespace_module)
            type->module = namespace_module->value;
        else
            type->module = std::move(request.module_fallback);
        type->dict->set(new_str("__name__"), new_str(type->name));
        type->dict->set(new_str("__module__"), new_str(type->module));
        if (!type->dict->contains(new_str("__qualname__")))
            type->dict->set(new_str("__qualname__"), new_str(std::move(qualname)));
        std::vector<Object *> base_objects;
        base_objects.reserve(type->bases.size());
        for (TypeObject *base : type->bases)
            base_objects.push_back(base);
        type->dict->set(new_str("__bases__"), new_tuple(std::move(base_objects)));
        return type;
    }

    TypeObject *Runtime::new_class(std::string name,
                                   std::vector<TypeObject *> bases,
                                   DictObject *namespace_dict,
                                   std::string module_fallback)
    {
        ClassConstructionRequest request;
        request.name = std::move(name);
        request.bases = std::move(bases);
        request.namespace_dict = namespace_dict;
        request.module_fallback = std::move(module_fallback);
        request.qualname = request.name;
        return new_class(std::move(request));
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

    TypeObject *Runtime::build_class(const ClassBuildRequest &request)
    {
        if (!request.body_code)
            raise_type_error(this, "__build_class__ requires a class body code object");
        if (!request.bases)
            raise_type_error(this, "__build_class__ requires a bases tuple");
        if (!request.globals)
            raise_type_error(this, "__build_class__ requires globals");
        if (!request.builtins)
            raise_type_error(this, "__build_class__ requires builtins");
        VirtualMachine *vm = active_vm();
        if (!vm)
            raise_type_error(this, "__build_class__ requires an active virtual machine");

        validate_class_keywords(*this, request.keywords);
        auto bases_root = root(request.bases);
        auto code_root = root(request.body_code);
        auto globals_root = root(request.globals);
        auto builtins_root = root(request.builtins);

        std::vector<TypeObject *> base_types;
        base_types.reserve(bases_root->size());
        for (Object *base : bases_root->items())
        {
            auto *base_type = dynamic_cast<TypeObject *>(base);
            if (!base_type)
                raise_type_error(this, "class bases must be types");
            base_types.push_back(base_type);
        }

        TypeObject *explicit_metaclass = explicit_metaclass_from_keywords(*this, request.keywords);
        TypeObject *metaclass = select_metaclass(explicit_metaclass, base_types);
        StrObject *name = new_str(request.name);
        auto name_root = root(name);
        Object *prepared = call_prepare(*this, metaclass, name_root.get(), bases_root.get());
        auto prepared_root = root(prepared);
        auto *namespace_dict = dynamic_cast<DictObject *>(prepared_root.get());
        if (!namespace_dict)
            raise_type_error(this, "__prepare__() must return a dict");

        ClassConstructionRequest namespace_request;
        namespace_request.name = request.name;
        namespace_request.bases = base_types;
        namespace_request.namespace_dict = namespace_dict;
        namespace_request.module_fallback = request.module_fallback;
        namespace_request.qualname = request.qualname.empty() ? request.name : request.qualname;
        namespace_request.explicit_metaclass = metaclass;
        prepare_class_namespace(namespace_request);

        (void)vm->execute(ExecutionRequest{
            .code = code_root.get(),
            .locals = namespace_dict,
            .globals = globals_root.get(),
            .builtins = builtins_root.get(),
            .closure = {},
            .mode = ExecutionMode::ClassBody,
        });

        CellObject *class_cell = nullptr;
        if (Object *class_cell_object = namespace_dict->get(new_str("__classcell__")))
        {
            class_cell = dynamic_cast<CellObject *>(class_cell_object);
            if (!class_cell)
                raise_type_error(this, "__classcell__ must be a cell");
        }

        Object *result = object_call(metaclass, {name_root.get(), bases_root.get(), namespace_dict});
        auto result_root = root(result);
        auto *type = dynamic_cast<TypeObject *>(result_root.get());
        if (!type)
            raise_type_error(this, "__build_class__() must return a type");
        if (class_cell)
            class_cell->set_value(type);
        return type;
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

    ExceptionObject *Runtime::new_value_error(std::string message)
    {
        return new_exception(value_error_type_, std::move(message));
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

    ExceptionObject *Runtime::new_unbound_local_error(std::string message)
    {
        return new_exception(unbound_local_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_recursion_error(std::string message)
    {
        return new_exception(recursion_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_attribute_error(std::string message)
    {
        return new_exception(attribute_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_import_error(std::string message)
    {
        return new_exception(import_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_os_error(std::string message)
    {
        return new_exception(os_error_type_, std::move(message));
    }

    ExceptionObject *Runtime::new_stop_iteration()
    {
        return new_exception(stop_iteration_type_, std::vector<Object *>{});
    }
}
