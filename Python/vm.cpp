#include "cxxpython/vm.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
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
    }

    Frame::Frame(const CodeObject *code)
        : code_(code)
    {
        if (!code_)
            throw BytecodeError("frame requires a code object");
    }

    const CodeObject *Frame::code() const noexcept
    {
        return code_;
    }

    std::size_t Frame::instruction_pointer() const noexcept
    {
        return ip_;
    }

    void Frame::advance() noexcept
    {
        ++ip_;
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
                frame->visit_stack(visitor);
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

    Object *VirtualMachine::execute(const CodeObject &code)
    {
        Frame frame(&code);
        ActiveFrame active_frame(thread_, frame);
        while (frame.instruction_pointer() < code.instructions().size())
        {
            const Instruction instruction = code.instructions()[frame.instruction_pointer()];
            frame.advance();
            switch (instruction.opcode)
            {
            case Opcode::LoadConst:
            {
                if (instruction.operand >= code.constants().size())
                    throw BytecodeError("LOAD_CONST operand out of range");
                frame.push(code.constants()[instruction.operand]);
                break;
            }
            case Opcode::BinaryAdd:
            {
                Object *right = frame.pop();
                Object *left = frame.pop();
                frame.push(binary_add(left, right));
                break;
            }
            case Opcode::ReturnValue:
                return frame.pop();
            default:
                throw BytecodeError("invalid opcode");
            }
        }
        throw BytecodeError("code object terminated without RETURN_VALUE");
    }

    Object *VirtualMachine::binary_add(Object *left, Object *right)
    {
        if (auto *left_int = dynamic_cast<IntObject *>(left))
        {
            if (auto *right_int = dynamic_cast<IntObject *>(right))
                return runtime_.new_int(left_int->value + right_int->value);
        }

        if (auto *left_str = dynamic_cast<StrObject *>(left))
        {
            if (auto *right_str = dynamic_cast<StrObject *>(right))
                return runtime_.new_str(left_str->value + right_str->value);
        }

        std::ostringstream message;
        message << "unsupported operand type(s) for +: '"
                << type_qualified_name(left ? left->type() : nullptr)
                << "' and '"
                << type_qualified_name(right ? right->type() : nullptr)
                << "'";
        raise_type_error(&runtime_, message.str());
    }
}
