#include "cxxpython/vm.hpp"

#include <sstream>

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

    void Frame::advance() noexcept
    {
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

    std::size_t Frame::stack_size() const noexcept
    {
        return stack_.size();
    }

    void Frame::visit_stack(const VisitChildren &visitor) const
    {
        for (Object *object : stack_)
        {
            if (object)
                visitor(object);
        }
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
                locals.set(name, value);
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
                object_set_attr(object, name, value);
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
                frame->push(object_unary_op(operand, op));
                break;
            }
            case Opcode::BinaryOp:
            {
                const BinaryOp op = decode_binary_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                frame->push(object_binary_op(left, right, op));
                break;
            }
            case Opcode::BinarySubscr:
            {
                Object *key = frame->pop();
                Object *object = frame->pop();
                frame->push(object_get_item(object, key));
                break;
            }
            case Opcode::StoreSubscr:
            {
                Object *value = frame->pop();
                Object *key = frame->pop();
                Object *object = frame->pop();
                object_set_item(object, key, value);
                break;
            }
            case Opcode::BuildList:
            {
                std::vector<Object *> items(instruction.operand);
                for (std::size_t index = items.size(); index > 0; --index)
                    items[index - 1] = frame->pop();
                frame->push(runtime_.new_list(std::move(items)));
                break;
            }
            case Opcode::BuildTuple:
            {
                std::vector<Object *> items(instruction.operand);
                for (std::size_t index = items.size(); index > 0; --index)
                    items[index - 1] = frame->pop();
                frame->push(runtime_.new_tuple(std::move(items)));
                break;
            }
            case Opcode::BuildMap:
            {
                std::vector<Object *> keys(instruction.operand);
                std::vector<Object *> values(instruction.operand);
                for (std::size_t index = keys.size(); index > 0; --index)
                {
                    values[index - 1] = frame->pop();
                    keys[index - 1] = frame->pop();
                }
                DictObject *dict = runtime_.new_dict();
                for (std::size_t index = 0; index < keys.size(); ++index)
                    dict->set(keys[index], values[index]);
                frame->push(dict);
                break;
            }
            case Opcode::CompareOp:
            {
                if (instruction.operand > static_cast<std::uint32_t>(RichCompareOp::GreaterEqual))
                    throw BytecodeError("COMPARE_OP operand out of range");
                const auto op = static_cast<RichCompareOp>(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                frame->push(object_rich_compare(left, right, op));
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
                if (!is_true(condition))
                    jump_to(*frame, code, instruction.operand);
                break;
            }
            case Opcode::PopJumpIfTrue:
            {
                Object *condition = frame->pop();
                if (is_true(condition))
                    jump_to(*frame, code, instruction.operand);
                break;
            }
            case Opcode::JumpIfFalseOrPop:
            {
                Object *condition = frame->pop();
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
                frame->push(runtime_.new_function(function_code->name(), function_code, &globals));
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
                std::vector<TypeObject *> bases;
                bases.reserve(bases_tuple->size());
                for (Object *base : bases_tuple->items())
                {
                    auto *base_type = dynamic_cast<TypeObject *>(base);
                    if (!base_type)
                        raise_type_error(&runtime_, "class bases must be types");
                    bases.push_back(base_type);
                }
                DictObject *namespace_dict = runtime_.new_dict();
                namespace_dict->set(runtime_.new_str("__module__"),
                                    runtime_.new_str(module_name_from_globals(runtime_, globals)));
                namespace_dict->set(runtime_.new_str("__qualname__"), runtime_.new_str(class_name->value));
                (void)execute(*class_code, *namespace_dict, globals, builtins);
                frame->push(runtime_.new_class(class_name->value, std::move(bases), namespace_dict));
                break;
            }
            case Opcode::Call:
            {
                std::vector<Object *> args(instruction.operand);
                for (std::size_t index = args.size(); index > 0; --index)
                    args[index - 1] = frame->pop();
                Object *callable = frame->pop();
                frame->push(object_call(callable, args));
                break;
            }
            case Opcode::ReturnValue:
                return frame->pop();
            default:
                throw BytecodeError("invalid opcode");
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
