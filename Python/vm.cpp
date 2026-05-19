#include "cxxpython/vm.hpp"

#include <sstream>
#include <utility>

namespace cxxpython
{
    namespace
    {
        void jump_to(Frame &frame, const CodeObject &code, std::uint32_t target)
        {
            if (target >= code.instructions().size())
                throw BytecodeError("jump target out of range");
            frame.jump_to(target);
        }

        BinaryOp decode_binary_op(std::uint32_t operand)
        {
            if (operand > static_cast<std::uint32_t>(BinaryOp::Power))
                throw BytecodeError("BINARY_OP operand out of range");
            return static_cast<BinaryOp>(operand);
        }

        UnaryOp decode_unary_op(std::uint32_t operand)
        {
            if (operand > static_cast<std::uint32_t>(UnaryOp::Invert))
                throw BytecodeError("UNARY_OP operand out of range");
            return static_cast<UnaryOp>(operand);
        }

        ComparisonOp decode_comparison_op(std::uint32_t operand)
        {
            if (operand > static_cast<std::uint32_t>(ComparisonOp::NotIn))
                throw BytecodeError("COMPARE_OP operand out of range");
            return static_cast<ComparisonOp>(operand);
        }

        Object *exception_instance_from_value(Runtime &runtime, Object *value)
        {
            if (!value)
                raise_type_error(&runtime, "exceptions must derive from BaseException");
            if (auto *exception = dynamic_cast<ExceptionObject *>(value))
                return exception;
            auto *type = dynamic_cast<TypeObject *>(value);
            if (type && is_subclass(type, runtime.base_exception_type()))
                return object_call(type, {});
            raise_type_error(&runtime, "exceptions must derive from BaseException");
        }

        bool exception_matches(Runtime &runtime, Object *exception, Object *matcher)
        {
            auto *exception_object = dynamic_cast<ExceptionObject *>(exception);
            auto *matcher_type = dynamic_cast<TypeObject *>(matcher);
            if (!exception_object || !matcher_type)
                raise_type_error(&runtime, "catching classes that do not inherit from BaseException is not allowed");
            if (!is_subclass(matcher_type, runtime.base_exception_type()))
                raise_type_error(&runtime, "catching classes that do not inherit from BaseException is not allowed");
            return is_subclass(exception_object->type(), matcher_type);
        }

        std::string frame_repr(const Object &object)
        {
            auto *frame = static_cast<const Frame *>(&object);
            std::ostringstream out;
            out << "<frame at " << &object;
            if (frame->code())
                out << ", code=" << frame->code()->repr();
            out << ">";
            return out.str();
        }

        void frame_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *frame = static_cast<const Frame *>(&object);
            if (frame->code())
                visitor(frame->code());
            if (frame->locals())
                visitor(frame->locals());
            if (frame->globals())
                visitor(frame->globals());
            if (frame->builtins())
                visitor(frame->builtins());
            frame->visit_stack(visitor);
            if (frame->pending_control_value())
                visitor(frame->pending_control_value());
        }

        std::int64_t frame_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool frame_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool frame_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        std::string module_name_from_globals(Runtime &runtime, DictObject &globals)
        {
            if (auto *module_name = dynamic_cast<StrObject *>(globals.get(runtime.new_str("__name__"))))
                return module_name->value;
            return "__main__";
        }

        void attach_source_location(const PythonError &error, const CodeObject &code, std::size_t instruction_index)
        {
            ExceptionObject *value = error.value();
            if (!value || value->has_source_location())
                return;
            const std::uint32_t line = code.line_for_instruction(instruction_index);
            if (line != 0)
                value->set_source_location(code.filename(), line, code.name());
        }

        void add_temporary_root(Runtime &runtime, std::vector<Rooted<Object>> &roots, Object *object)
        {
            if (object)
                roots.push_back(runtime.root(object));
        }

        class ActiveFrame final
        {
        public:
            ActiveFrame(ThreadState &thread, Frame &frame)
                : thread_(thread), frame_(frame)
            {
                thread_.push_frame(&frame_);
            }

            ~ActiveFrame()
            {
                thread_.pop_frame(&frame_);
            }

            ActiveFrame(const ActiveFrame &) = delete;
            ActiveFrame &operator=(const ActiveFrame &) = delete;

        private:
            ThreadState &thread_;
            Frame &frame_;
        };

        class ActiveVirtualMachine final
        {
        public:
            explicit ActiveVirtualMachine(VirtualMachine &vm)
                : runtime_(vm.interpreter_state().runtime()),
                  previous_(runtime_.active_vm())
            {
                runtime_.set_active_vm(&vm);
            }

            ~ActiveVirtualMachine()
            {
                runtime_.set_active_vm(previous_);
            }

            ActiveVirtualMachine(const ActiveVirtualMachine &) = delete;
            ActiveVirtualMachine &operator=(const ActiveVirtualMachine &) = delete;

        private:
            Runtime &runtime_;
            VirtualMachine *previous_;
        };
    }

    Frame::Frame(TypeObject *type, CodeObject *code, DictObject *locals, DictObject *globals, DictObject *builtins)
        : Object(type), code_(code), locals_(locals), globals_(globals), builtins_(builtins)
    {
        if (!code_)
            throw BytecodeError("frame requires a code object");
        if (!locals_)
            throw BytecodeError("frame requires locals");
        if (!globals_)
            throw BytecodeError("frame requires globals");
        if (!builtins_)
            throw BytecodeError("frame requires builtins");
    }

    CodeObject *Frame::code() const noexcept
    {
        return code_;
    }

    DictObject *Frame::locals() const noexcept
    {
        return locals_;
    }

    DictObject *Frame::globals() const noexcept
    {
        return globals_;
    }

    DictObject *Frame::builtins() const noexcept
    {
        return builtins_;
    }

    std::size_t Frame::instruction_pointer() const noexcept
    {
        return ip_;
    }

    std::size_t Frame::last_instruction_index() const noexcept
    {
        return last_ip_;
    }

    void Frame::advance() noexcept
    {
        last_ip_ = ip_;
        ++ip_;
    }

    void Frame::jump_to(std::size_t target) noexcept
    {
        ip_ = target;
    }

    void Frame::push(Object *object)
    {
        if (!object)
            throw BytecodeError("cannot push null object onto frame stack");
        stack_.push_back(object);
    }

    Object *Frame::pop()
    {
        if (stack_.empty())
            throw BytecodeError("frame stack underflow");
        Object *object = stack_.back();
        stack_.pop_back();
        return object;
    }

    void Frame::clear_stack() noexcept
    {
        stack_.clear();
    }

    std::size_t Frame::stack_size() const noexcept
    {
        return stack_.size();
    }

    void Frame::push_exception_handler(std::size_t target)
    {
        push_block_handler(FrameBlockKind::Except, target);
    }

    std::size_t Frame::pop_exception_handler()
    {
        if (block_handlers_.empty() || block_handlers_.back().kind != FrameBlockKind::Except)
            throw BytecodeError("exception handler stack underflow");
        const std::size_t target = block_handlers_.back().target;
        block_handlers_.pop_back();
        return target;
    }

    bool Frame::has_exception_handler() const noexcept
    {
        return !block_handlers_.empty() && block_handlers_.back().kind == FrameBlockKind::Except;
    }

    void Frame::visit_stack(const VisitChildren &visitor) const
    {
        for (Object *object : stack_)
        {
            if (object)
                visitor(object);
        }
    }

    void Frame::push_finally_handler(std::size_t target)
    {
        push_block_handler(FrameBlockKind::Finally, target);
    }

    std::size_t Frame::pop_finally_handler()
    {
        if (block_handlers_.empty() || block_handlers_.back().kind != FrameBlockKind::Finally)
            throw BytecodeError("finally handler stack underflow");
        const std::size_t target = block_handlers_.back().target;
        block_handlers_.pop_back();
        return target;
    }

    bool Frame::has_finally_handler() const noexcept
    {
        return !block_handlers_.empty() && block_handlers_.back().kind == FrameBlockKind::Finally;
    }

    void Frame::push_block_handler(FrameBlockKind kind, std::size_t target)
    {
        block_handlers_.push_back(FrameBlockHandler{kind, target});
    }

    FrameBlockHandler Frame::pop_block_handler()
    {
        if (block_handlers_.empty())
            throw BytecodeError("block handler stack underflow");
        const FrameBlockHandler handler = block_handlers_.back();
        block_handlers_.pop_back();
        return handler;
    }

    bool Frame::has_block_handler() const noexcept
    {
        return !block_handlers_.empty();
    }

    void Frame::set_pending_return(Object *value)
    {
        pending_control_kind_ = PendingControlKind::Return;
        pending_control_value_ = value;
    }

    void Frame::set_pending_exception(ExceptionObject *value)
    {
        pending_control_kind_ = PendingControlKind::Exception;
        pending_control_value_ = value;
    }

    PendingControlKind Frame::pending_control_kind() const noexcept
    {
        return pending_control_kind_;
    }

    Object *Frame::pending_control_value() const noexcept
    {
        return pending_control_value_;
    }

    void Frame::clear_pending_control() noexcept
    {
        pending_control_kind_ = PendingControlKind::None;
        pending_control_value_ = nullptr;
    }

    InterpreterState::InterpreterState(Runtime &runtime)
        : runtime_(runtime)
    {
    }

    Runtime &InterpreterState::runtime() const noexcept
    {
        return runtime_;
    }

    ThreadState::ThreadState(InterpreterState &interpreter)
        : interpreter_(interpreter)
    {
    }

    InterpreterState &ThreadState::interpreter() const noexcept
    {
        return interpreter_;
    }

    void ThreadState::push_frame(Frame *frame)
    {
        if (!frame)
            throw BytecodeError("cannot push null frame");
        frames_.push_back(frame);
    }

    void ThreadState::pop_frame(Frame *frame)
    {
        if (frames_.empty() || frames_.back() != frame)
            throw BytecodeError("frame stack discipline violation");
        frames_.pop_back();
    }

    Frame *ThreadState::current_frame() const noexcept
    {
        if (frames_.empty())
            return nullptr;
        return frames_.back();
    }

    std::size_t ThreadState::frame_depth() const noexcept
    {
        return frames_.size();
    }

    void ThreadState::visit_frames(const VisitChildren &visitor) const
    {
        for (Frame *frame : frames_)
        {
            if (frame)
                visitor(frame);
        }
    }

    VirtualMachine::VirtualMachine(Runtime &runtime)
        : runtime_(runtime), interpreter_(runtime), thread_(interpreter_)
    {
    }

    InterpreterState &VirtualMachine::interpreter_state() noexcept
    {
        return interpreter_;
    }

    ThreadState &VirtualMachine::thread_state() noexcept
    {
        return thread_;
    }

    Object *VirtualMachine::execute(CodeObject &code)
    {
        DictObject *globals = runtime_.new_dict();
        return execute(code, *globals);
    }

    Object *VirtualMachine::execute(CodeObject &code, DictObject &globals)
    {
        return execute(code, globals, globals, *runtime_.builtins_dict());
    }

    Object *VirtualMachine::execute(CodeObject &code, DictObject &locals, DictObject &globals, DictObject &builtins)
    {
        ActiveVirtualMachine active_vm(*this);
        Frame *frame = runtime_.new_frame(&code, &locals, &globals, &builtins);
        ActiveFrame active_frame(thread_, *frame);
        while (frame->instruction_pointer() < code.instructions().size())
        {
            const Instruction instruction = code.instructions()[frame->instruction_pointer()];
            frame->advance();
            try
            {
            switch (instruction.opcode)
            {
            case Opcode::LoadConst:
            {
                if (instruction.operand >= code.constants().size())
                    throw BytecodeError("LOAD_CONST operand out of range");
                frame->push(code.constants()[instruction.operand]);
                break;
            }
            case Opcode::LoadName:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_NAME operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("LOAD_NAME requires a str name");
                Object *value = locals.get(name);
                if (!value)
                    value = globals.get(name);
                if (!value)
                    value = builtins.get(name);
                if (!value)
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                frame->push(value);
                break;
            }
            case Opcode::StoreName:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("STORE_NAME operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("STORE_NAME requires a str name");
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                locals.set(name, value);
                break;
            }
            case Opcode::DeleteName:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("DELETE_NAME operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("DELETE_NAME requires a str name");
                if (!locals.erase(name))
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                break;
            }
            case Opcode::LoadFast:
            {
                if (instruction.operand >= code.varnames().size())
                    throw BytecodeError("LOAD_FAST operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.varnames()[instruction.operand]);
                if (!name)
                    throw BytecodeError("LOAD_FAST requires a str varname");
                Object *value = locals.get(name);
                if (!value)
                    raise_unbound_local_error(
                        &runtime_,
                        "cannot access local variable '" + name->value +
                            "' where it is not associated with a value");
                frame->push(value);
                break;
            }
            case Opcode::StoreFast:
            {
                if (instruction.operand >= code.varnames().size())
                    throw BytecodeError("STORE_FAST operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.varnames()[instruction.operand]);
                if (!name)
                    throw BytecodeError("STORE_FAST requires a str varname");
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                locals.set(name, value);
                break;
            }
            case Opcode::DeleteFast:
            {
                if (instruction.operand >= code.varnames().size())
                    throw BytecodeError("DELETE_FAST operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.varnames()[instruction.operand]);
                if (!name)
                    throw BytecodeError("DELETE_FAST requires a str varname");
                if (!locals.erase(name))
                    raise_unbound_local_error(
                        &runtime_,
                        "cannot delete local variable '" + name->value +
                            "' where it is not associated with a value");
                break;
            }
            case Opcode::LoadAttr:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_ATTR operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("LOAD_ATTR requires a str name");
                Object *object = frame->pop();
                auto object_root = runtime_.root(object);
                frame->push(object_get_attr(object, name));
                break;
            }
            case Opcode::StoreAttr:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("STORE_ATTR operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("STORE_ATTR requires a str name");
                Object *value = frame->pop();
                Object *object = frame->pop();
                auto value_root = runtime_.root(value);
                auto object_root = runtime_.root(object);
                object_set_attr(object, name, value);
                break;
            }
            case Opcode::DeleteAttr:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("DELETE_ATTR operand out of range");
                auto *name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!name)
                    throw BytecodeError("DELETE_ATTR requires a str name");
                Object *object = frame->pop();
                auto object_root = runtime_.root(object);
                object_delete_attr(object, name);
                break;
            }
            case Opcode::PopTop:
                (void)frame->pop();
                break;
            case Opcode::DupTop:
            {
                Object *top = frame->pop();
                frame->push(top);
                frame->push(top);
                break;
            }
            case Opcode::RotTwo:
            {
                Object *top = frame->pop();
                Object *second = frame->pop();
                frame->push(top);
                frame->push(second);
                break;
            }
            case Opcode::RotThree:
            {
                Object *top = frame->pop();
                Object *second = frame->pop();
                Object *third = frame->pop();
                frame->push(top);
                frame->push(third);
                frame->push(second);
                break;
            }
            case Opcode::UnaryOp:
            {
                const UnaryOp op = decode_unary_op(instruction.operand);
                Object *operand = frame->pop();
                auto operand_root = runtime_.root(operand);
                frame->push(object_unary_op(operand, op));
                break;
            }
            case Opcode::BinaryOp:
            {
                const BinaryOp op = decode_binary_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                auto right_root = runtime_.root(right);
                auto left_root = runtime_.root(left);
                frame->push(object_binary_op(left, right, op));
                break;
            }
            case Opcode::BinarySubscr:
            {
                Object *key = frame->pop();
                Object *object = frame->pop();
                auto key_root = runtime_.root(key);
                auto object_root = runtime_.root(object);
                frame->push(object_get_item(object, key));
                break;
            }
            case Opcode::StoreSubscr:
            {
                Object *value = frame->pop();
                Object *key = frame->pop();
                Object *object = frame->pop();
                auto value_root = runtime_.root(value);
                auto key_root = runtime_.root(key);
                auto object_root = runtime_.root(object);
                object_set_item(object, key, value);
                break;
            }
            case Opcode::DeleteSubscr:
            {
                Object *key = frame->pop();
                Object *object = frame->pop();
                auto key_root = runtime_.root(key);
                auto object_root = runtime_.root(object);
                object_delete_item(object, key);
                break;
            }
            case Opcode::BuildList:
            {
                std::vector<Object *> items(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(items.size());
                for (std::size_t index = items.size(); index > 0; --index)
                {
                    items[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, items[index - 1]);
                }
                ListObject *list = runtime_.new_list(std::move(items));
                auto list_root = runtime_.root(list);
                frame->push(list);
                break;
            }
            case Opcode::BuildTuple:
            {
                std::vector<Object *> items(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(items.size());
                for (std::size_t index = items.size(); index > 0; --index)
                {
                    items[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, items[index - 1]);
                }
                TupleObject *tuple = runtime_.new_tuple(std::move(items));
                auto tuple_root = runtime_.root(tuple);
                frame->push(tuple);
                break;
            }
            case Opcode::BuildMap:
            {
                std::vector<Object *> keys(instruction.operand);
                std::vector<Object *> values(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(keys.size() * 2);
                for (std::size_t index = keys.size(); index > 0; --index)
                {
                    values[index - 1] = frame->pop();
                    keys[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, values[index - 1]);
                    add_temporary_root(runtime_, roots, keys[index - 1]);
                }
                DictObject *dict = runtime_.new_dict();
                auto dict_root = runtime_.root(dict);
                for (std::size_t index = 0; index < keys.size(); ++index)
                    dict->set(keys[index], values[index]);
                frame->push(dict);
                break;
            }
            case Opcode::CompareOp:
            {
                const ComparisonOp op = decode_comparison_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                auto right_root = runtime_.root(right);
                auto left_root = runtime_.root(left);
                switch (op)
                {
                case ComparisonOp::Equal:
                case ComparisonOp::NotEqual:
                case ComparisonOp::Less:
                case ComparisonOp::LessEqual:
                case ComparisonOp::Greater:
                case ComparisonOp::GreaterEqual:
                    frame->push(object_rich_compare(left, right, static_cast<RichCompareOp>(op)));
                    break;
                case ComparisonOp::Is:
                    frame->push(left == right ? runtime_.true_value() : runtime_.false_value());
                    break;
                case ComparisonOp::IsNot:
                    frame->push(left != right ? runtime_.true_value() : runtime_.false_value());
                    break;
                case ComparisonOp::In:
                    frame->push(object_contains(right, left) ? runtime_.true_value() : runtime_.false_value());
                    break;
                case ComparisonOp::NotIn:
                    frame->push(object_contains(right, left) ? runtime_.false_value() : runtime_.true_value());
                    break;
                }
                break;
            }
            case Opcode::JumpAbsolute:
            {
                jump_to(*frame, code, instruction.operand);
                break;
            }
            case Opcode::PopJumpIfFalse:
            {
                Object *condition = frame->pop();
                auto condition_root = runtime_.root(condition);
                if (!is_true(condition))
                    jump_to(*frame, code, instruction.operand);
                break;
            }
            case Opcode::PopJumpIfTrue:
            {
                Object *condition = frame->pop();
                auto condition_root = runtime_.root(condition);
                if (is_true(condition))
                    jump_to(*frame, code, instruction.operand);
                break;
            }
            case Opcode::JumpIfFalseOrPop:
            {
                Object *condition = frame->pop();
                auto condition_root = runtime_.root(condition);
                if (!is_true(condition))
                {
                    frame->push(condition);
                    jump_to(*frame, code, instruction.operand);
                }
                break;
            }
            case Opcode::MakeFunction:
            {
                Object *code_object = frame->pop();
                auto *function_code = dynamic_cast<CodeObject *>(code_object);
                if (!function_code)
                    throw BytecodeError("MAKE_FUNCTION requires a code object");
                auto code_root = runtime_.root(function_code);
                PythonFunctionObject *function = runtime_.new_function(function_code->name(), function_code, &globals);
                auto function_root = runtime_.root(function);
                frame->push(function);
                break;
            }
            case Opcode::MakeClass:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("MAKE_CLASS operand out of range");
                auto *class_name = dynamic_cast<StrObject *>(code.names()[instruction.operand]);
                if (!class_name)
                    throw BytecodeError("MAKE_CLASS requires a str name");
                Object *code_object = frame->pop();
                auto *class_code = dynamic_cast<CodeObject *>(code_object);
                if (!class_code)
                    throw BytecodeError("MAKE_CLASS requires a code object");
                Object *bases_object = frame->pop();
                auto *bases_tuple = dynamic_cast<TupleObject *>(bases_object);
                if (!bases_tuple)
                    throw BytecodeError("MAKE_CLASS requires a tuple of bases");
                auto class_code_root = runtime_.root(class_code);
                auto bases_tuple_root = runtime_.root(bases_tuple);
                std::vector<TypeObject *> bases;
                bases.reserve(bases_tuple->size());
                for (Object *base : bases_tuple->items())
                {
                    auto *base_type = dynamic_cast<TypeObject *>(base);
                    if (!base_type)
                        raise_type_error(&runtime_, "class bases must be types");
                    bases.push_back(base_type);
                }
                ClassConstructionRequest request;
                request.name = class_name->value;
                request.bases = std::move(bases);
                request.module_fallback = module_name_from_globals(runtime_, globals);
                request.qualname = class_name->value;
                DictObject *namespace_dict = runtime_.prepare_class_namespace(request);
                auto namespace_root = runtime_.root(namespace_dict);
                (void)execute(*class_code, *namespace_dict, globals, builtins);
                request.namespace_dict = namespace_dict;
                TypeObject *type = runtime_.new_class(std::move(request));
                auto type_root = runtime_.root(type);
                frame->push(type);
                break;
            }
            case Opcode::Call:
            {
                std::vector<Object *> args(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(args.size());
                for (std::size_t index = args.size(); index > 0; --index)
                {
                    args[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, args[index - 1]);
                }
                Object *callable = frame->pop();
                auto callable_root = runtime_.root(callable);
                frame->push(object_call(callable, args));
                break;
            }
            case Opcode::CallKw:
            {
                Object *keywords_object = frame->pop();
                auto *keywords = dynamic_cast<DictObject *>(keywords_object);
                if (!keywords)
                    throw BytecodeError("CALL_KW requires keyword dict");
                auto keywords_root = runtime_.root(keywords);
                std::vector<Object *> args(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(args.size());
                for (std::size_t index = args.size(); index > 0; --index)
                {
                    args[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, args[index - 1]);
                }
                Object *callable = frame->pop();
                auto callable_root = runtime_.root(callable);
                frame->push(object_call(callable, CallArguments{std::move(args), keywords}));
                break;
            }
            case Opcode::SetupExcept:
            {
                if (instruction.operand >= code.instructions().size())
                    throw BytecodeError("SETUP_EXCEPT target out of range");
                frame->push_exception_handler(instruction.operand);
                break;
            }
            case Opcode::SetupFinally:
            {
                if (instruction.operand >= code.instructions().size())
                    throw BytecodeError("SETUP_FINALLY target out of range");
                frame->push_finally_handler(instruction.operand);
                break;
            }
            case Opcode::PopExcept:
            {
                (void)frame->pop_exception_handler();
                break;
            }
            case Opcode::PopFinally:
            {
                (void)frame->pop_finally_handler();
                break;
            }
            case Opcode::CheckExceptionMatch:
            {
                Object *matcher = frame->pop();
                Object *exception = frame->pop();
                auto matcher_root = runtime_.root(matcher);
                auto exception_root = runtime_.root(exception);
                frame->push(exception);
                frame->push(exception_matches(runtime_, exception, matcher)
                                ? runtime_.true_value()
                                : runtime_.false_value());
                break;
            }
            case Opcode::RaiseValue:
            {
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                Object *exception = exception_instance_from_value(runtime_, value_root.get());
                auto exception_root = runtime_.root(exception);
                auto *exception_object = dynamic_cast<ExceptionObject *>(exception_root.get());
                if (!exception_object)
                    raise_type_error(&runtime_, "exceptions must derive from BaseException");
                throw PythonError(exception_object);
            }
            case Opcode::EndFinally:
            {
                switch (frame->pending_control_kind())
                {
                case PendingControlKind::None:
                    break;
                case PendingControlKind::Return:
                {
                    Object *value = frame->pending_control_value();
                    frame->clear_pending_control();
                    return value;
                }
                case PendingControlKind::Exception:
                {
                    Object *value = frame->pending_control_value();
                    auto *exception = dynamic_cast<ExceptionObject *>(value);
                    frame->clear_pending_control();
                    if (!exception)
                        raise_type_error(&runtime_, "finally handler stored non-exception pending state");
                    throw PythonError(exception);
                }
                }
                break;
            }
            case Opcode::ReturnValue:
            {
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                bool jumped_to_finally = false;
                while (frame->has_block_handler())
                {
                    const FrameBlockHandler handler = frame->pop_block_handler();
                    if (handler.kind == FrameBlockKind::Except)
                        continue;
                    frame->clear_stack();
                    frame->set_pending_return(value);
                    jump_to(*frame, code, static_cast<std::uint32_t>(handler.target));
                    jumped_to_finally = true;
                    break;
                }
                if (jumped_to_finally)
                    continue;
                return value;
            }
            default:
                throw BytecodeError("invalid opcode");
            }
            }
            catch (const PythonError &error)
            {
                attach_source_location(error, code, frame->last_instruction_index());
                if (frame->has_exception_handler())
                {
                    const std::size_t target = frame->pop_exception_handler();
                    frame->clear_stack();
                    if (error.value())
                        frame->push(error.value());
                    else
                        frame->push(runtime_.new_exception(runtime_.exception_type(), "unknown exception"));
                    jump_to(*frame, code, static_cast<std::uint32_t>(target));
                    continue;
                }
                if (frame->has_finally_handler())
                {
                    const std::size_t target = frame->pop_finally_handler();
                    frame->clear_stack();
                    ExceptionObject *pending_exception = error.value();
                    if (!pending_exception)
                        pending_exception = runtime_.new_exception(runtime_.exception_type(), "unknown exception");
                    frame->set_pending_exception(pending_exception);
                    jump_to(*frame, code, static_cast<std::uint32_t>(target));
                    continue;
                }
                throw;
            }
        }
        throw BytecodeError("code object terminated without RETURN_VALUE");
    }

    TypeSlots make_frame_slots()
    {
        return TypeSlots{
            .repr = frame_repr,
            .hash = frame_hash,
            .equal = frame_equal,
            .truth = frame_truth,
            .traverse = frame_traverse,
        };
    }
}
