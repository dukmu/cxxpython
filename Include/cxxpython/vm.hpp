#pragma once

#include <cstddef>
#include <mutex>
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
        Break,
        Continue,
    };

    enum class FrameBlockKind : std::uint8_t
    {
        Except,
        Finally,
    };

    enum class ThreadGCState : std::uint8_t
    {
        Running,
        OpcodeSafepoint,
        NativeWait,
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
        ~Frame() override;
        void clear_references() override;

        CodeObject *code() const noexcept;
        DictObject *locals() const noexcept;
        DictObject *globals() const noexcept;
        DictObject *builtins() const noexcept;
        CellObject *cell(std::size_t index) const;
        std::size_t cell_count() const noexcept;
        StrObject *fast_local_name(std::size_t index) const;
        Object *fast_local(std::size_t index) const;
        void set_fast_local(std::size_t index, Object *value);
        bool delete_fast_local(std::size_t index);
        void sync_fast_locals_to_dict();
        std::size_t instruction_pointer() const noexcept;
        std::size_t last_instruction_index() const noexcept;
        void advance() noexcept;
        void jump_to(std::size_t target) noexcept;

        void push(Object *object);
        Object *pop();
        Object *stack_at(std::size_t index) const;
        ObjectSpan stack_slice(std::size_t index, std::size_t count) const;
        void truncate_stack(std::size_t size) noexcept;
        void clear_stack() noexcept;
        std::size_t stack_size() const noexcept;
        void visit_stack(const VisitChildren &visitor) const;
        void visit_fast_locals(const VisitChildren &visitor) const;
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
        void set_pending_break(std::size_t target);
        void set_pending_continue(std::size_t target);
        PendingControlKind pending_control_kind() const noexcept;
        Object *pending_control_value() const noexcept;
        std::size_t pending_control_target() const noexcept;
        void clear_pending_control() noexcept;
        Object *trace_function() const noexcept;
        void set_trace_function(Object *trace_function) noexcept;

    private:
        CodeObject *code_;
        DictObject *locals_;
        DictObject *globals_;
        DictObject *builtins_;
        std::vector<CellObject *> cells_;
        std::vector<StrObject *> fast_local_names_;
        std::vector<Object *> fast_locals_;
        std::size_t ip_ = 0;
        std::size_t last_ip_ = 0;
        std::vector<Object *> stack_;
        std::vector<FrameBlockHandler> block_handlers_;
        PendingControlKind pending_control_kind_ = PendingControlKind::None;
        Object *pending_control_value_ = nullptr;
        std::size_t pending_control_target_ = 0;
        Object *trace_function_ = nullptr;
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
        ~ThreadState();

        InterpreterState &interpreter() const noexcept;
        void push_frame(Frame *frame);
        void pop_frame(Frame *frame);
        Frame *current_frame() const noexcept;
        Frame *frame_at_depth(std::size_t depth) const noexcept;
        std::size_t frame_depth() const noexcept;
        void push_handled_exception(ExceptionObject *exception);
        void pop_handled_exception();
        ExceptionObject *current_exception() const noexcept;
        std::size_t handled_exception_depth() const noexcept;
        void restore_handled_exception_depth(std::size_t depth);
        Object *trace_function() const noexcept;
        void set_trace_function(Object *trace_function) noexcept;
        bool is_tracing() const noexcept;
        void enter_tracing() noexcept;
        void leave_tracing() noexcept;
        bool at_gc_safepoint() const noexcept;
        void set_gc_safepoint(bool at_safepoint) noexcept;
        ThreadGCState gc_state() const noexcept;
        void set_gc_state(ThreadGCState state) noexcept;
        std::size_t add_temporary_root(Object *object);
        void remove_temporary_root(std::size_t index, Object *object);
        void visit_frames(const VisitChildren &visitor) const;
        void visit_temporary_roots(const VisitChildren &visitor) const;

    private:
        InterpreterState &interpreter_;
        std::vector<Frame *> frames_;
        std::vector<ExceptionObject *> handled_exceptions_;
        std::vector<Object *> temporary_roots_;
        Object *trace_function_ = nullptr;
        std::size_t trace_depth_ = 0;
        std::atomic<ThreadGCState> gc_state_{ThreadGCState::Running};
    };

    class ThreadGCStateGuard final
    {
    public:
        ThreadGCStateGuard(ThreadState &thread, ThreadGCState state) noexcept;
        ~ThreadGCStateGuard();

        ThreadGCStateGuard(const ThreadGCStateGuard &) = delete;
        ThreadGCStateGuard &operator=(const ThreadGCStateGuard &) = delete;

    private:
        ThreadState &thread_;
        ThreadGCState previous_;
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
