#pragma once

#include <cstddef>
#include <vector>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython
{
    class Frame final : public Object
    {
    public:
        Frame(TypeObject *type, CodeObject *code, DictObject *locals, DictObject *globals, DictObject *builtins);

        CodeObject *code() const noexcept;
        DictObject *locals() const noexcept;
        DictObject *globals() const noexcept;
        DictObject *builtins() const noexcept;
        std::size_t instruction_pointer() const noexcept;
        std::size_t last_instruction_index() const noexcept;
        void advance() noexcept;
        void jump_to(std::size_t target) noexcept;

        void push(Object *object);
        Object *pop();
        void clear_stack() noexcept;
        std::size_t stack_size() const noexcept;
        void visit_stack(const VisitChildren &visitor) const;
        void push_exception_handler(std::size_t target);
        std::size_t pop_exception_handler();
        bool has_exception_handler() const noexcept;

    private:
        CodeObject *code_;
        DictObject *locals_;
        DictObject *globals_;
        DictObject *builtins_;
        std::size_t ip_ = 0;
        std::size_t last_ip_ = 0;
        std::vector<Object *> stack_;
        std::vector<std::size_t> exception_handlers_;
    };

    class InterpreterState final
    {
    public:
        explicit InterpreterState(Runtime &runtime);

        Runtime &runtime() const noexcept;

    private:
        Runtime &runtime_;
    };

    class ThreadState final
    {
    public:
        explicit ThreadState(InterpreterState &interpreter);

        InterpreterState &interpreter() const noexcept;
        void push_frame(Frame *frame);
        void pop_frame(Frame *frame);
        Frame *current_frame() const noexcept;
        std::size_t frame_depth() const noexcept;
        void visit_frames(const VisitChildren &visitor) const;

    private:
        InterpreterState &interpreter_;
        std::vector<Frame *> frames_;
    };

    class VirtualMachine final
    {
    public:
        explicit VirtualMachine(Runtime &runtime);

        InterpreterState &interpreter_state() noexcept;
        ThreadState &thread_state() noexcept;
        Object *execute(CodeObject &code);
        Object *execute(CodeObject &code, DictObject &globals);
        Object *execute(CodeObject &code, DictObject &locals, DictObject &globals, DictObject &builtins);

    private:
        Runtime &runtime_;
        InterpreterState interpreter_;
        ThreadState thread_;
    };
}
