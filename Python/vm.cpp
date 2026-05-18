#include "cxxpython/vm.hpp"

#include <sstream>

namespace cxxpython
{
    namespace
    {
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

    Frame::Frame(TypeObject *type, CodeObject *code)
        : Object(type), code_(code)
    {
        if (!code_)
            throw BytecodeError("frame requires a code object");
    }

    CodeObject *Frame::code() const noexcept
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

    Object *VirtualMachine::execute(CodeObject &code)
    {
        Frame *frame = runtime_.new_frame(&code);
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
            case Opcode::BinaryAdd:
            {
                Object *right = frame->pop();
                Object *left = frame->pop();
                frame->push(binary_add(left, right));
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

    Object *VirtualMachine::binary_add(Object *left, Object *right)
    {
        return object_binary_add(left, right);
    }

    TypeSlots make_frame_slots()
    {
        return TypeSlots{frame_repr, frame_hash, frame_equal, frame_truth, frame_traverse, nullptr};
    }
}
