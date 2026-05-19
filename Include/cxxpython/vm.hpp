#pragma once

#include <cstddef>
#include <vector>

#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"

namespace cxxpython
{
    enum class PendingControlKind : std::uint8_t
    {
        None,
        Return,
        Exception,
    };

    enum class FrameBlockKind : std::uint8_t
    {
        Except,
        Finally,
    };

    struct FrameBlockHandler
    {
        FrameBlockKind kind;
        std::size_t target;
    };

    class Frame final : public Object
    {
    public:
        static constexpr std::size_t MaxValueStackDepth = 1U << 16;

        Frame(TypeObject *type,
              CodeObject *code,
              DictObject *locals,
              DictObject *globals,
              DictObject *builtins,
              std::vector<CellObject *> cells = {});

        CodeObject *code() const noexcept;
        DictObject *locals() const noexcept;
        DictObject *globals() const noexcept;
        DictObject *builtins() const noexcept;
        CellObject *cell(std::size_t index) const;
        std::size_t cell_count() const noexcept;
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
        void push_finally_handler(std::size_t target);
        std::size_t pop_finally_handler();
        bool has_finally_handler() const noexcept;
        void push_block_handler(FrameBlockKind kind, std::size_t target);
        FrameBlockHandler pop_block_handler();
        bool has_block_handler() const noexcept;
        void set_pending_return(Object *value);
        void set_pending_exception(ExceptionObject *value);
        PendingControlKind pending_control_kind() const noexcept;
        Object *pending_control_value() const noexcept;
        void clear_pending_control() noexcept;

    private:
        CodeObject *code_;
        DictObject *locals_;
        DictObject *globals_;
        DictObject *builtins_;
        std::vector<CellObject *> cells_;
        std::size_t ip_ = 0;
        std::size_t last_ip_ = 0;
        std::vector<Object *> stack_;
        std::vector<FrameBlockHandler> block_handlers_;
        PendingControlKind pending_control_kind_ = PendingControlKind::None;
        Object *pending_control_value_ = nullptr;
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
        static constexpr std::size_t MaxFrameDepth = 1000;

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

    enum class ExecutionMode : std::uint8_t
    {
        Eval,
        Exec,
        Module,
        Function,
        ClassBody,
    };

    struct ExecutionRequest
    {
        CodeObject *code = nullptr;
        DictObject *locals = nullptr;
        DictObject *globals = nullptr;
        DictObject *builtins = nullptr;
        ModuleObject *module = nullptr;
        std::vector<CellObject *> closure;
        ExecutionMode mode = ExecutionMode::Exec;
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
        Object *execute(const ExecutionRequest &request);

    private:
        Runtime &runtime_;
        InterpreterState interpreter_;
        ThreadState thread_;
    };
}
