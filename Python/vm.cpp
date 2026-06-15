#include "cxxpython/vm.hpp"

#include "../Objects/object_utils.hpp"
#include "../Objects/type_initialization.hpp"
#include "../Modules/module_registry.hpp"
#include "vm_fastpaths.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <utility>

namespace cxxpython
{
    namespace
    {
        constexpr std::size_t OpcodeCount = static_cast<std::size_t>(Opcode::ReturnValue) + 1;

        void jump_to(Frame &frame, std::size_t instruction_count, std::uint32_t target)
        {
            if (target >= instruction_count)
                throw BytecodeError("jump target out of range");
            frame.jump_to(target);
        }

        BinaryOp decode_binary_op(std::uint32_t operand)
        {
            if (operand > static_cast<std::uint32_t>(BinaryOp::BitOr))
                throw BytecodeError("BINARY_OP operand out of range");
            return static_cast<BinaryOp>(operand);
        }

        UnaryOp decode_unary_op(std::uint32_t operand)
        {
            if (operand > static_cast<std::uint32_t>(UnaryOp::Not))
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
            for (std::size_t index = 0; index < frame->cell_count(); ++index)
                visitor(frame->cell(index));
            frame->visit_fast_locals(visitor);
            frame->visit_stack(visitor);
            if (frame->pending_control_value())
                visitor(frame->pending_control_value());
            if (frame->trace_function())
                visitor(frame->trace_function());
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

        Frame *require_frame_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *frame = dynamic_cast<Frame *>(instance);
            if (!frame)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have frame layout");
            return frame;
        }

        Object *frame_code_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_code")->code();
        }

        Object *frame_locals_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Frame *frame = require_frame_instance(runtime, instance, "f_locals");
            frame->sync_fast_locals_to_dict();
            return frame->locals();
        }

        Object *frame_globals_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_globals")->globals();
        }

        Object *frame_builtins_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return require_frame_instance(runtime, instance, "f_builtins")->builtins();
        }

        Object *frame_lasti_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Frame *frame = require_frame_instance(runtime, instance, "f_lasti");
            return runtime.new_int(static_cast<std::int64_t>(frame->last_instruction_index()));
        }

        Object *frame_lineno_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            Frame *frame = require_frame_instance(runtime, instance, "f_lineno");
            return runtime.new_int(static_cast<std::int64_t>(
                frame->code()->line_for_instruction(frame->last_instruction_index())));
        }

        std::string module_name_from_globals(Runtime &runtime, DictObject &globals)
        {
            if (auto *module_name = dynamic_cast<StrObject *>(globals.get(runtime.new_str("__name__"))))
                return module_name->value;
            return "__main__";
        }

        void attach_source_location(Runtime &runtime,
                                    const PythonError &error,
                                    Frame &frame,
                                    const CodeObject &code,
                                    std::size_t instruction_index)
        {
            ExceptionObject *value = error.value();
            if (!value)
                return;
            const std::uint32_t line = code.line_for_instruction(instruction_index);
            if (line != 0)
            {
                value->add_traceback_entry(code.filename(), line, code.name());
                TracebackObject *current = value->traceback();
                if (current && current->frame() == &frame && current->last_instruction_index() == instruction_index)
                    return;
                TracebackObject *traceback = runtime.new_traceback(current, &frame, instruction_index, line);
                value->prepend_traceback(traceback);
            }
        }

        Object *normalize_trace_result(Runtime &runtime, Object *result)
        {
            if (!result || result == runtime.none())
                return nullptr;
            return result;
        }

        class TraceCallGuard final
        {
        public:
            explicit TraceCallGuard(ThreadState &thread)
                : thread_(thread)
            {
                thread_.enter_tracing();
            }

            ~TraceCallGuard()
            {
                thread_.leave_tracing();
            }

            TraceCallGuard(const TraceCallGuard &) = delete;
            TraceCallGuard &operator=(const TraceCallGuard &) = delete;

        private:
            ThreadState &thread_;
        };

        Object *call_trace_function(Runtime &runtime,
                                    ThreadState &thread,
                                    Object *trace_function,
                                    Frame &frame,
                                    const char *event,
                                    Object *arg)
        {
            if (!trace_function || thread.is_tracing())
                return nullptr;
            auto trace_root = runtime.root(trace_function);
            auto frame_root = runtime.root(&frame);
            auto event_root = runtime.root(runtime.new_str(event));
            Object *resolved_arg = arg ? arg : static_cast<Object *>(runtime.none());
            auto arg_root = runtime.root(resolved_arg);
            TraceCallGuard guard(thread);
            Object *result = object_call(trace_root.get(), {frame_root.get(), event_root.get(), arg_root.get()});
            return normalize_trace_result(runtime, result);
        }

        void set_frame_trace_from_result(Frame &frame, Object *result) noexcept
        {
            frame.set_trace_function(result);
        }

        bool eval_breaker_requested(Runtime &runtime, Frame &frame) noexcept
        {
            return runtime.garbage_collection_requested() ||
                   frame.trace_function() ||
                   pending_signal_delivery_requested();
        }

        void handle_eval_breaker(Runtime &runtime,
                                 ThreadState &thread,
                                 CodeObject &code,
                                 Frame &frame,
                                 std::uint32_t &last_traced_line)
        {
            if (runtime.garbage_collection_requested())
                (void)runtime.collect_garbage_if_requested(thread);

            if (Object *trace_function = frame.trace_function())
            {
                const std::uint32_t current_line =
                    code.line_for_instruction(frame.last_instruction_index());
                if (current_line != 0 && current_line != last_traced_line)
                {
                    last_traced_line = current_line;
                    set_frame_trace_from_result(frame, call_trace_function(runtime,
                                                                           thread,
                                                                           trace_function,
                                                                           frame,
                                                                           "line",
                                                                           runtime.none()));
                }
            }

            if (pending_signal_delivery_requested())
                deliver_pending_signals(runtime);
        }

        void add_temporary_root(Runtime &runtime, std::vector<Rooted<Object>> &roots, Object *object)
        {
            if (object)
                roots.push_back(runtime.root(object));
        }

        bool is_attribute_error(Runtime &runtime, const PythonError &error)
        {
            return error.type() && is_subclass(error.type(), runtime.attribute_error_type());
        }

        Object *try_get_attr(Runtime &runtime, Object *object, const char *name)
        {
            try
            {
                return object_get_attr(object, name);
            }
            catch (const PythonError &error)
            {
                if (is_attribute_error(runtime, error))
                    return nullptr;
                throw;
            }
        }

        void import_public_module_names(ModuleObject &module, DictObject &target)
        {
            for (const DictObject::Entry &entry : module.dict()->entries())
            {
                auto *name = dynamic_cast<StrObject *>(entry.key);
                if (!name || (!name->value.empty() && name->value.front() == '_'))
                    continue;
                target.set(name, entry.value);
            }
        }

        void import_all_names(Runtime &runtime, Object *module, Object *all_names, DictObject &target)
        {
            std::vector<Object *> names = collect_iterable_items(&runtime, all_names);
            std::vector<Rooted<Object>> roots;
            roots.reserve(names.size());
            for (Object *name_object : names)
                add_temporary_root(runtime, roots, name_object);

            for (Object *name_object : names)
            {
                auto *name = dynamic_cast<StrObject *>(name_object);
                if (!name)
                    raise_type_error(&runtime, "module __all__ entries must be str");
                Object *value = object_get_attr(module, name);
                auto value_root = runtime.root(value);
                target.set(name, value_root.get());
            }
        }

        void import_star(Runtime &runtime, Object *module, DictObject &target)
        {
            if (!module)
                raise_import_error(&runtime, "import * received null module");
            auto module_root = runtime.root(module);
            if (Object *all_names = try_get_attr(runtime, module_root.get(), "__all__"))
            {
                auto all_root = runtime.root(all_names);
                import_all_names(runtime, module_root.get(), all_root.get(), target);
                return;
            }
            auto *module_object = dynamic_cast<ModuleObject *>(module_root.get());
            if (!module_object)
                raise_type_error(&runtime, "import * requires a module object without __all__");
            import_public_module_names(*module_object, target);
        }

        class ActiveFrame final
        {
        public:
            ActiveFrame(ThreadState &thread, Frame &frame)
                : thread_(thread),
                  frame_(frame),
                  handled_exception_depth_(thread.handled_exception_depth())
            {
                thread_.push_frame(&frame_);
            }

            ~ActiveFrame()
            {
                thread_.restore_handled_exception_depth(handled_exception_depth_);
                thread_.pop_frame(&frame_);
            }

            ActiveFrame(const ActiveFrame &) = delete;
            ActiveFrame &operator=(const ActiveFrame &) = delete;

        private:
            ThreadState &thread_;
            Frame &frame_;
            std::size_t handled_exception_depth_;
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

    void initialize_frame_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("f_code"),
                        runtime.new_getset_descriptor("f_code",
                                                      type,
                                                      frame_code_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_locals"),
                        runtime.new_getset_descriptor("f_locals",
                                                      type,
                                                      frame_locals_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_globals"),
                        runtime.new_getset_descriptor("f_globals",
                                                      type,
                                                      frame_globals_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_builtins"),
                        runtime.new_getset_descriptor("f_builtins",
                                                      type,
                                                      frame_builtins_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_lasti"),
                        runtime.new_getset_descriptor("f_lasti",
                                                      type,
                                                      frame_lasti_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("f_lineno"),
                        runtime.new_getset_descriptor("f_lineno",
                                                      type,
                                                      frame_lineno_getter,
                                                      nullptr));
    }

    Frame::Frame(TypeObject *type,
                 CodeObject *code,
                 DictObject *locals,
                 DictObject *globals,
                 DictObject *builtins,
                 std::vector<CellObject *> cells)
        : Object(type),
          code_(code),
          locals_(locals),
          globals_(globals),
          builtins_(builtins),
          cells_(std::move(cells)),
          fast_local_names_(code ? code->varnames().size() : 0, nullptr),
          fast_locals_(code ? code->varnames().size() : 0, nullptr)
    {
        if (!code_)
            throw BytecodeError("frame requires a code object");
        if (!locals_)
            throw BytecodeError("frame requires locals");
        if (!globals_)
            throw BytecodeError("frame requires globals");
        if (!builtins_)
            throw BytecodeError("frame requires builtins");
        if (cells_.size() != code_->cellvars().size() + code_->freevars().size())
            throw BytecodeError("frame cell count does not match code metadata");
        code_->incref();
        locals_->incref();
        globals_->incref();
        builtins_->incref();
        for (CellObject *cell : cells_)
        {
            if (cell)
                cell->incref();
        }
        stack_.reserve(code_->stack_size());
        for (std::size_t index = 0; index < code_->varnames().size(); ++index)
        {
            StrObject *name = code_->varnames()[index];
            fast_local_names_[index] = name;
            fast_locals_[index] = locals_->get(name);
            if (fast_locals_[index])
                fast_locals_[index]->incref();
        }
    }

    Frame::~Frame()
    {
        clear_references();
    }

    void Frame::clear_references()
    {
        CXXPYTHON_CLEAR_VECTOR_REF(fast_locals_);
        CXXPYTHON_CLEAR_VECTOR_REF(cells_);
        CXXPYTHON_CLEAR_REF(trace_function_);
        CXXPYTHON_CLEAR_REF(pending_control_value_);
        CXXPYTHON_CLEAR_REF(builtins_);
        CXXPYTHON_CLEAR_REF(globals_);
        CXXPYTHON_CLEAR_REF(locals_);
        CXXPYTHON_CLEAR_REF(code_);
        Object::clear_references();
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

    CellObject *Frame::cell(std::size_t index) const
    {
        if (index >= cells_.size())
            throw BytecodeError("cell index out of range");
        return cells_[index];
    }

    std::size_t Frame::cell_count() const noexcept
    {
        return cells_.size();
    }

    StrObject *Frame::fast_local_name(std::size_t index) const
    {
        if (index >= fast_local_names_.size())
            throw BytecodeError("fast local name index out of range");
        return fast_local_names_[index];
    }

    Object *Frame::fast_local(std::size_t index) const
    {
        if (index >= fast_locals_.size())
            throw BytecodeError("fast local index out of range");
        return fast_locals_[index];
    }

    void Frame::set_fast_local(std::size_t index, Object *value)
    {
        if (index >= fast_locals_.size())
            throw BytecodeError("fast local index out of range");
        if (!value)
            throw BytecodeError("cannot assign null fast local");
        Object *previous = fast_locals_[index];
        value->incref();
        fast_locals_[index] = value;
        if (previous)
            previous->decref();
    }

    bool Frame::delete_fast_local(std::size_t index)
    {
        if (index >= fast_locals_.size())
            throw BytecodeError("fast local index out of range");
        if (!fast_locals_[index])
            return false;
        Object *previous = fast_locals_[index];
        fast_locals_[index] = nullptr;
        previous->decref();
        return true;
    }

    void Frame::sync_fast_locals_to_dict()
    {
        for (std::size_t index = 0; index < fast_locals_.size(); ++index)
        {
            StrObject *name = fast_local_names_[index];
            if (Object *value = fast_locals_[index])
                locals_->set(name, value);
            else
                (void)locals_->erase(name);
        }
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
        if (stack_.size() >= MaxValueStackDepth)
            raise_recursion_error(runtime(), "maximum VM value stack depth exceeded");
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

    Object *Frame::stack_at(std::size_t index) const
    {
        if (index >= stack_.size())
            throw BytecodeError("frame stack index out of range");
        return stack_[index];
    }

    ObjectSpan Frame::stack_slice(std::size_t index, std::size_t count) const
    {
        if (index > stack_.size() || count > stack_.size() - index)
            throw BytecodeError("frame stack slice out of range");
        return ObjectSpan(stack_.data() + index, count);
    }

    void Frame::truncate_stack(std::size_t size) noexcept
    {
        if (size < stack_.size())
            stack_.resize(size);
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

    void Frame::visit_fast_locals(const VisitChildren &visitor) const
    {
        for (Object *object : fast_locals_)
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
        if (value)
            value->incref();
        if (pending_control_value_)
            pending_control_value_->decref();
        pending_control_kind_ = PendingControlKind::Return;
        pending_control_value_ = value;
        pending_control_target_ = 0;
    }

    void Frame::set_pending_exception(ExceptionObject *value)
    {
        if (value)
            value->incref();
        if (pending_control_value_)
            pending_control_value_->decref();
        pending_control_kind_ = PendingControlKind::Exception;
        pending_control_value_ = value;
        pending_control_target_ = 0;
    }

    void Frame::set_pending_break(std::size_t target)
    {
        pending_control_kind_ = PendingControlKind::Break;
        pending_control_value_ = nullptr;
        pending_control_target_ = target;
    }

    void Frame::set_pending_continue(std::size_t target)
    {
        pending_control_kind_ = PendingControlKind::Continue;
        pending_control_value_ = nullptr;
        pending_control_target_ = target;
    }

    PendingControlKind Frame::pending_control_kind() const noexcept
    {
        return pending_control_kind_;
    }

    Object *Frame::pending_control_value() const noexcept
    {
        return pending_control_value_;
    }

    std::size_t Frame::pending_control_target() const noexcept
    {
        return pending_control_target_;
    }

    void Frame::clear_pending_control() noexcept
    {
        if (pending_control_value_)
            pending_control_value_->decref();
        pending_control_kind_ = PendingControlKind::None;
        pending_control_value_ = nullptr;
        pending_control_target_ = 0;
    }

    Object *Frame::trace_function() const noexcept
    {
        return trace_function_;
    }

    void Frame::set_trace_function(Object *trace_function) noexcept
    {
        if (trace_function == trace_function_)
            return;
        if (trace_function)
            trace_function->incref();
        if (trace_function_)
            trace_function_->decref();
        trace_function_ = trace_function;
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
        interpreter_.runtime().register_thread_state(this);
    }

    ThreadState::~ThreadState()
    {
        for (ExceptionObject *exception : handled_exceptions_)
        {
            if (exception)
                exception->decref();
        }
        if (trace_function_)
            trace_function_->decref();
        interpreter_.runtime().unregister_thread_state(this);
    }

    InterpreterState &ThreadState::interpreter() const noexcept
    {
        return interpreter_;
    }

    void ThreadState::push_frame(Frame *frame)
    {
        if (!frame)
            throw BytecodeError("cannot push null frame");
        std::scoped_lock lock(mutex_);
        if (frames_.size() >= MaxFrameDepth)
            raise_recursion_error(&interpreter_.runtime(), "maximum recursion depth exceeded");
        frames_.push_back(frame);
    }

    void ThreadState::pop_frame(Frame *frame)
    {
        std::scoped_lock lock(mutex_);
        if (frames_.empty() || frames_.back() != frame)
            throw BytecodeError("frame stack discipline violation");
        frames_.pop_back();
    }

    Frame *ThreadState::current_frame() const noexcept
    {
        std::scoped_lock lock(mutex_);
        if (frames_.empty())
            return nullptr;
        return frames_.back();
    }

    Frame *ThreadState::frame_at_depth(std::size_t depth) const noexcept
    {
        std::scoped_lock lock(mutex_);
        if (depth >= frames_.size())
            return nullptr;
        return frames_[frames_.size() - 1 - depth];
    }

    std::size_t ThreadState::frame_depth() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return frames_.size();
    }

    void ThreadState::push_handled_exception(ExceptionObject *exception)
    {
        if (!exception)
            throw BytecodeError("cannot push null handled exception");
        std::scoped_lock lock(mutex_);
        exception->incref();
        try
        {
            handled_exceptions_.push_back(exception);
        }
        catch (...)
        {
            exception->decref();
            throw;
        }
    }

    void ThreadState::pop_handled_exception()
    {
        std::scoped_lock lock(mutex_);
        if (handled_exceptions_.empty())
            throw BytecodeError("handled exception stack underflow");
        ExceptionObject *exception = handled_exceptions_.back();
        handled_exceptions_.pop_back();
        exception->decref();
    }

    ExceptionObject *ThreadState::current_exception() const noexcept
    {
        std::scoped_lock lock(mutex_);
        if (handled_exceptions_.empty())
            return nullptr;
        return handled_exceptions_.back();
    }

    std::size_t ThreadState::handled_exception_depth() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return handled_exceptions_.size();
    }

    void ThreadState::restore_handled_exception_depth(std::size_t depth)
    {
        std::scoped_lock lock(mutex_);
        if (depth < handled_exceptions_.size())
        {
            for (std::size_t index = depth; index < handled_exceptions_.size(); ++index)
            {
                if (handled_exceptions_[index])
                    handled_exceptions_[index]->decref();
            }
            handled_exceptions_.resize(depth);
        }
    }

    Object *ThreadState::trace_function() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return trace_function_;
    }

    void ThreadState::set_trace_function(Object *trace_function) noexcept
    {
        std::scoped_lock lock(mutex_);
        if (trace_function == trace_function_)
            return;
        if (trace_function)
            trace_function->incref();
        if (trace_function_)
            trace_function_->decref();
        trace_function_ = trace_function;
    }

    bool ThreadState::is_tracing() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return trace_depth_ != 0;
    }

    void ThreadState::enter_tracing() noexcept
    {
        std::scoped_lock lock(mutex_);
        ++trace_depth_;
    }

    void ThreadState::leave_tracing() noexcept
    {
        std::scoped_lock lock(mutex_);
        if (trace_depth_ != 0)
            --trace_depth_;
    }

    bool ThreadState::at_gc_safepoint() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return gc_state_ != ThreadGCState::Running;
    }

    void ThreadState::set_gc_safepoint(bool at_safepoint) noexcept
    {
        set_gc_state(at_safepoint ? ThreadGCState::OpcodeSafepoint : ThreadGCState::Running);
    }

    ThreadGCState ThreadState::gc_state() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return gc_state_;
    }

    void ThreadState::set_gc_state(ThreadGCState state) noexcept
    {
        {
            std::scoped_lock lock(mutex_);
            gc_state_ = state;
        }
        interpreter_.runtime().notify_gc_safepoint_change();
    }

    ThreadGCStateGuard::ThreadGCStateGuard(ThreadState &thread, ThreadGCState state) noexcept
        : thread_(thread), previous_(thread.gc_state())
    {
        thread_.set_gc_state(state);
    }

    ThreadGCStateGuard::~ThreadGCStateGuard()
    {
        thread_.set_gc_state(previous_);
    }

    std::size_t ThreadState::add_temporary_root(Object *object)
    {
        if (!object)
            return static_cast<std::size_t>(-1);
        temporary_roots_.push_back(object);
        return temporary_roots_.size() - 1;
    }

    void ThreadState::remove_temporary_root(std::size_t index, Object *object)
    {
        if (!object)
            return;
        if (index < temporary_roots_.size() && temporary_roots_[index] == object)
            temporary_roots_[index] = nullptr;
        while (!temporary_roots_.empty() && temporary_roots_.back() == nullptr)
            temporary_roots_.pop_back();
    }

    void ThreadState::visit_frames(const VisitChildren &visitor) const
    {
        std::scoped_lock lock(mutex_);
        for (Frame *frame : frames_)
        {
            if (frame)
                visitor(frame);
        }
        for (ExceptionObject *exception : handled_exceptions_)
        {
            if (exception)
                visitor(exception);
        }
        if (trace_function_)
            visitor(trace_function_);
    }

    void ThreadState::visit_temporary_roots(const VisitChildren &visitor) const
    {
        std::scoped_lock lock(mutex_);
        for (Object *object : temporary_roots_)
        {
            if (object)
                visitor(object);
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
        return execute(ExecutionRequest{
            .code = &code,
            .locals = &globals,
            .globals = &globals,
            .builtins = runtime_.builtins_dict(),
            .closure = {},
            .mode = ExecutionMode::Exec,
        });
    }

    Object *VirtualMachine::execute(CodeObject &code, DictObject &locals, DictObject &globals, DictObject &builtins)
    {
        return execute(ExecutionRequest{
            .code = &code,
            .locals = &locals,
            .globals = &globals,
            .builtins = &builtins,
            .closure = {},
            .mode = ExecutionMode::Exec,
        });
    }

    Object *VirtualMachine::execute(const ExecutionRequest &request)
    {
        ActiveVirtualMachine active_vm(*this);
        if (!request.code)
            throw BytecodeError("execution request requires a code object");
        if (!request.locals)
            throw BytecodeError("execution request requires locals");
        if (!request.globals)
            throw BytecodeError("execution request requires globals");
        DictObject *builtins_dict = request.builtins ? request.builtins : runtime_.builtins_dict();
        if (!builtins_dict)
            throw BytecodeError("execution request requires builtins");
        CodeObject &code = *request.code;
        DictObject &locals = *request.locals;
        DictObject &globals = *request.globals;
        DictObject &builtins = *builtins_dict;
        const std::vector<Instruction> &instructions = code.instructions();
        const std::size_t instruction_count = instructions.size();
        Frame *frame = runtime_.new_frame(&code, &locals, &globals, &builtins, request.closure);
        ActiveFrame active_frame(thread_, *frame);
        if (Object *global_trace = thread_.trace_function())
            set_frame_trace_from_result(*frame, call_trace_function(runtime_,
                                                                    thread_,
                                                                    global_trace,
                                                                    *frame,
                                                                    "call",
                                                                    runtime_.none()));
        std::uint32_t last_traced_line = 0;
        while (frame->instruction_pointer() < instruction_count)
        {
            const Instruction instruction = instructions[frame->instruction_pointer()];
            frame->advance();
            try
            {
            if (eval_breaker_requested(runtime_, *frame))
                handle_eval_breaker(runtime_, thread_, code, *frame, last_traced_line);
#if defined(CXXPYTHON_HAS_COMPUTED_GOTO)
            static void *dispatch_table[OpcodeCount] = {
                &&target_load_const,       // LoadConst
                &&target_load_name,        // LoadName
                &&target_slow,             // StoreName
                &&target_slow,             // DeleteName
                &&target_load_global,      // LoadGlobal
                &&target_slow,             // StoreGlobal
                &&target_slow,             // DeleteGlobal
                &&target_load_fast,        // LoadFast
                &&target_store_fast,       // StoreFast
                &&target_slow,             // DeleteFast
                &&target_slow,             // LoadDeref
                &&target_slow,             // StoreDeref
                &&target_slow,             // DeleteDeref
                &&target_slow,             // LoadClosure
                &&target_slow,             // LoadAttr
                &&target_slow,             // StoreAttr
                &&target_slow,             // DeleteAttr
                &&target_pop_top,          // PopTop
                &&target_slow,             // DupTop
                &&target_slow,             // DupTwo
                &&target_slow,             // RotTwo
                &&target_slow,             // RotThree
                &&target_slow,             // UnaryOp
                &&target_binary_op,        // BinaryOp
                &&target_slow,             // InplaceBinaryOp
                &&target_slow,             // FormatValue
                &&target_slow,             // BinarySubscr
                &&target_slow,             // StoreSubscr
                &&target_slow,             // DeleteSubscr
                &&target_slow,             // BuildList
                &&target_slow,             // BuildTuple
                &&target_slow,             // BuildTupleUnpack
                &&target_slow,             // BuildMap
                &&target_slow,             // BuildKwMapUnpack
                &&target_slow,             // BuildSlice
                &&target_slow,             // CompareOp
                &&target_slow,             // GetIter
                &&target_for_iter,         // ForIter
                &&target_slow,             // BreakLoop
                &&target_slow,             // ContinueLoop
                &&target_jump_absolute,    // JumpAbsolute
                &&target_pop_jump_if_false,// PopJumpIfFalse
                &&target_pop_jump_if_true, // PopJumpIfTrue
                &&target_slow,             // JumpIfFalseOrPop
                &&target_slow,             // MakeFunction
                &&target_slow,             // MakeClass
                &&target_call,             // Call
                &&target_slow,             // CallKw
                &&target_slow,             // CallEx
                &&target_slow,             // ImportStar
                &&target_slow,             // SetupExcept
                &&target_slow,             // SetupFinally
                &&target_slow,             // PopExcept
                &&target_slow,             // PopFinally
                &&target_slow,             // CheckExceptionMatch
                &&target_slow,             // RaiseValue
                &&target_slow,             // WithExit
                &&target_slow,             // EndFinally
                &&target_return_value,     // ReturnValue
            };
            {
                const std::size_t opcode_index = static_cast<std::size_t>(instruction.opcode);
                if (opcode_index >= OpcodeCount)
                    goto target_slow;
                goto *dispatch_table[opcode_index];
            }

target_load_const:
            {
                if (instruction.operand >= code.constants().size())
                    throw BytecodeError("LOAD_CONST operand out of range");
                frame->push(code.constants()[instruction.operand]);
                continue;
            }

target_load_name:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_NAME operand out of range");
                StrObject *name = code.names()[instruction.operand];
                Object *value = locals.get(name);
                if (!value)
                    value = globals.get(name);
                if (!value)
                    value = builtins.get(name);
                if (!value)
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                frame->push(value);
                continue;
            }

target_load_global:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_GLOBAL operand out of range");
                StrObject *name = code.names()[instruction.operand];
                Object *value = globals.get(name);
                if (!value)
                    value = builtins.get(name);
                if (!value)
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                frame->push(value);
                continue;
            }

target_load_fast:
            {
                Object *value = frame->fast_local(instruction.operand);
                if (!value)
                {
                    StrObject *name = frame->fast_local_name(instruction.operand);
                    raise_unbound_local_error(
                        &runtime_,
                        "cannot access local variable '" + name->value +
                            "' where it is not associated with a value");
                }
                frame->push(value);
                continue;
            }

target_store_fast:
            {
                Object *value = frame->pop();
                frame->set_fast_local(instruction.operand, value);
                continue;
            }

target_pop_top:
            {
                (void)frame->pop();
                continue;
            }

target_binary_op:
            {
                const BinaryOp op = decode_binary_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                if (Object *result = vm_fastpath::try_binary_op(runtime_, left, right, op))
                {
                    frame->push(result);
                    continue;
                }
                auto right_root = runtime_.root(right);
                auto left_root = runtime_.root(left);
                frame->push(object_binary_op(left, right, op));
                continue;
            }

target_for_iter:
            {
                Object *iterator = frame->pop();
                if (iterator && iterator->type() == runtime_.range_iterator_type())
                {
                    auto *range_iterator = static_cast<RangeIteratorObject *>(iterator);
                    if (range_iterator->remaining() == 0)
                    {
                        jump_to(*frame, instruction_count, instruction.operand);
                        continue;
                    }
                    const std::int64_t value = range_iterator->consume_value();
                    frame->push(iterator);
                    frame->push(runtime_.new_int(value));
                    continue;
                }
                auto iterator_root = runtime_.root(iterator);
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = runtime_.root(item);
                    frame->push(iterator_root.get());
                    frame->push(item_root.get());
                }
                catch (const PythonError &error)
                {
                    if (error.type() && is_subclass(error.type(), runtime_.stop_iteration_type()))
                    {
                        jump_to(*frame, instruction_count, instruction.operand);
                        continue;
                    }
                    throw;
                }
                continue;
            }

target_jump_absolute:
            {
                jump_to(*frame, instruction_count, instruction.operand);
                continue;
            }

target_pop_jump_if_false:
            {
                Object *condition = frame->pop();
                bool truth = false;
                if (vm_fastpath::try_truth(runtime_, condition, truth))
                {
                    if (!truth)
                        jump_to(*frame, instruction_count, instruction.operand);
                    continue;
                }
                auto condition_root = runtime_.root(condition);
                if (!is_true(condition))
                    jump_to(*frame, instruction_count, instruction.operand);
                continue;
            }

target_pop_jump_if_true:
            {
                Object *condition = frame->pop();
                bool truth = false;
                if (vm_fastpath::try_truth(runtime_, condition, truth))
                {
                    if (truth)
                        jump_to(*frame, instruction_count, instruction.operand);
                    continue;
                }
                auto condition_root = runtime_.root(condition);
                if (is_true(condition))
                    jump_to(*frame, instruction_count, instruction.operand);
                continue;
            }

target_call:
            {
                const std::size_t arg_count = instruction.operand;
                if (frame->stack_size() < arg_count + 1)
                    throw BytecodeError("CALL stack underflow");
                const std::size_t args_start = frame->stack_size() - arg_count;
                ObjectSpan args = frame->stack_slice(args_start, arg_count);
                Object *callable = frame->stack_at(args_start - 1);
                Object *result = nullptr;
                if (Object *fast_result = vm_fastpath::try_builtin_call(runtime_, callable, args))
                {
                    frame->truncate_stack(args_start - 1);
                    frame->push(fast_result);
                    continue;
                }
                result = object_call_rooted(callable, args);
                frame->truncate_stack(args_start - 1);
                frame->push(result);
                continue;
            }

target_return_value:
            {
                Object *value = frame->pop();
                bool jumped_to_finally = false;
                while (frame->has_block_handler())
                {
                    const FrameBlockHandler handler = frame->pop_block_handler();
                    if (handler.kind == FrameBlockKind::Except)
                        continue;
                    frame->clear_stack();
                    frame->set_pending_return(value);
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(handler.target));
                    jumped_to_finally = true;
                    break;
                }
                if (jumped_to_finally)
                    continue;
                if (frame->trace_function())
                {
                    auto value_root = runtime_.root(value);
                    (void)call_trace_function(runtime_,
                                              thread_,
                                              frame->trace_function(),
                                              *frame,
                                              "return",
                                              value_root.get());
                }
                return value;
            }

target_slow:
#endif
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
                StrObject *name = code.names()[instruction.operand];
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
                StrObject *name = code.names()[instruction.operand];
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                locals.set(name, value);
                break;
            }
            case Opcode::DeleteName:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("DELETE_NAME operand out of range");
                StrObject *name = code.names()[instruction.operand];
                if (!locals.erase(name))
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                break;
            }
            case Opcode::LoadGlobal:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_GLOBAL operand out of range");
                StrObject *name = code.names()[instruction.operand];
                Object *value = globals.get(name);
                if (!value)
                    value = builtins.get(name);
                if (!value)
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                frame->push(value);
                break;
            }
            case Opcode::StoreGlobal:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("STORE_GLOBAL operand out of range");
                StrObject *name = code.names()[instruction.operand];
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                globals.set(name, value);
                break;
            }
            case Opcode::DeleteGlobal:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("DELETE_GLOBAL operand out of range");
                StrObject *name = code.names()[instruction.operand];
                if (!globals.erase(name))
                    raise_name_error(&runtime_, "name '" + name->value + "' is not defined");
                break;
            }
            case Opcode::LoadFast:
            {
                Object *value = frame->fast_local(instruction.operand);
                if (!value)
                {
                    StrObject *name = frame->fast_local_name(instruction.operand);
                    raise_unbound_local_error(
                        &runtime_,
                        "cannot access local variable '" + name->value +
                            "' where it is not associated with a value");
                }
                frame->push(value);
                break;
            }
            case Opcode::StoreFast:
            {
                Object *value = frame->pop();
                frame->set_fast_local(instruction.operand, value);
                break;
            }
            case Opcode::DeleteFast:
            {
                if (!frame->delete_fast_local(instruction.operand))
                {
                    StrObject *name = frame->fast_local_name(instruction.operand);
                    raise_unbound_local_error(
                        &runtime_,
                        "cannot delete local variable '" + name->value +
                            "' where it is not associated with a value");
                }
                break;
            }
            case Opcode::LoadDeref:
            {
                CellObject *cell = frame->cell(instruction.operand);
                if (cell->is_empty())
                    raise_name_error(&runtime_, "cannot access free variable");
                frame->push(cell->value());
                break;
            }
            case Opcode::StoreDeref:
            {
                CellObject *cell = frame->cell(instruction.operand);
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                cell->set_value(value);
                if (instruction.operand < code.cellvars().size())
                {
                    StrObject *name = code.cellvars()[instruction.operand];
                    locals.set(name, value);
                }
                break;
            }
            case Opcode::DeleteDeref:
            {
                CellObject *cell = frame->cell(instruction.operand);
                cell->set_value(nullptr);
                if (instruction.operand < code.cellvars().size())
                {
                    StrObject *name = code.cellvars()[instruction.operand];
                    (void)locals.erase(name);
                }
                break;
            }
            case Opcode::LoadClosure:
            {
                frame->push(frame->cell(instruction.operand));
                break;
            }
            case Opcode::LoadAttr:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("LOAD_ATTR operand out of range");
                StrObject *name = code.names()[instruction.operand];
                Object *object = frame->pop();
                auto object_root = runtime_.root(object);
                frame->push(object_get_attr(object, name));
                break;
            }
            case Opcode::StoreAttr:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("STORE_ATTR operand out of range");
                StrObject *name = code.names()[instruction.operand];
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
                StrObject *name = code.names()[instruction.operand];
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
            case Opcode::DupTwo:
            {
                Object *top = frame->pop();
                Object *second = frame->pop();
                frame->push(second);
                frame->push(top);
                frame->push(second);
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
                if (Object *result = vm_fastpath::try_binary_op(runtime_, left, right, op))
                {
                    frame->push(result);
                    break;
                }
                auto right_root = runtime_.root(right);
                auto left_root = runtime_.root(left);
                frame->push(object_binary_op(left, right, op));
                break;
            }
            case Opcode::InplaceBinaryOp:
            {
                const BinaryOp op = decode_binary_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                if (Object *result = vm_fastpath::try_binary_op(runtime_, left, right, op))
                {
                    frame->push(result);
                    break;
                }
                auto right_root = runtime_.root(right);
                auto left_root = runtime_.root(left);
                frame->push(object_inplace_binary_op(left, right, op));
                break;
            }
            case Opcode::FormatValue:
            {
                Object *value = frame->pop();
                auto value_root = runtime_.root(value);
                StrObject *spec = runtime_.new_str("");
                auto spec_root = runtime_.root(spec);
                Object *formatted = object_format(value_root.get(), spec_root.get());
                auto formatted_root = runtime_.root(formatted);
                frame->push(formatted_root.get());
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
            case Opcode::BuildTupleUnpack:
            {
                std::vector<Object *> sources(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(sources.size());
                for (std::size_t index = sources.size(); index > 0; --index)
                {
                    sources[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, sources[index - 1]);
                }

                std::vector<Object *> items;
                std::vector<Rooted<Object>> item_roots;
                for (Object *source : sources)
                {
                    std::vector<Object *> source_items = collect_iterable_items(&runtime_, source);
                    for (Object *item : source_items)
                    {
                        items.push_back(item);
                        add_temporary_root(runtime_, item_roots, item);
                    }
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
            case Opcode::BuildKwMapUnpack:
            {
                std::vector<Object *> sources(instruction.operand);
                std::vector<Rooted<Object>> roots;
                roots.reserve(sources.size());
                for (std::size_t index = sources.size(); index > 0; --index)
                {
                    sources[index - 1] = frame->pop();
                    add_temporary_root(runtime_, roots, sources[index - 1]);
                }

                DictObject *dict = runtime_.new_dict();
                auto dict_root = runtime_.root(dict);
                for (Object *source : sources)
                {
                    auto *source_dict = dynamic_cast<DictObject *>(source);
                    if (!source_dict)
                        raise_type_error(&runtime_, "** argument must be a dict");
                    for (const DictObject::Entry &entry : source_dict->entries())
                    {
                        if (!dynamic_cast<StrObject *>(entry.key))
                            raise_type_error(&runtime_, "keywords must be strings");
                        if (dict->contains(entry.key))
                            raise_type_error(&runtime_,
                                             "got multiple values for keyword argument '" +
                                                 object_str(entry.key) + "'");
                        dict->set(entry.key, entry.value);
                    }
                }
                frame->push(dict);
                break;
            }
            case Opcode::BuildSlice:
            {
                if (instruction.operand != 3)
                    throw BytecodeError("BUILD_SLICE operand must be 3");
                Object *step = frame->pop();
                Object *stop = frame->pop();
                Object *start = frame->pop();
                auto step_root = runtime_.root(step);
                auto stop_root = runtime_.root(stop);
                auto start_root = runtime_.root(start);
                SliceObject *slice = runtime_.new_slice(start, stop, step);
                auto slice_root = runtime_.root(slice);
                frame->push(slice);
                break;
            }
            case Opcode::CompareOp:
            {
                const ComparisonOp op = decode_comparison_op(instruction.operand);
                Object *right = frame->pop();
                Object *left = frame->pop();
                switch (op)
                {
                case ComparisonOp::Equal:
                case ComparisonOp::NotEqual:
                case ComparisonOp::Less:
                case ComparisonOp::LessEqual:
                case ComparisonOp::Greater:
                case ComparisonOp::GreaterEqual:
                    if (Object *result = vm_fastpath::try_compare_op(
                            runtime_, left, right, static_cast<RichCompareOp>(op)))
                    {
                        frame->push(result);
                        break;
                    }
                    {
                        auto right_root = runtime_.root(right);
                        auto left_root = runtime_.root(left);
                        frame->push(object_rich_compare(left, right, static_cast<RichCompareOp>(op)));
                    }
                    break;
                case ComparisonOp::Is:
                    frame->push(left == right ? runtime_.true_value() : runtime_.false_value());
                    break;
                case ComparisonOp::IsNot:
                    frame->push(left != right ? runtime_.true_value() : runtime_.false_value());
                    break;
                case ComparisonOp::In:
                {
                    auto right_root = runtime_.root(right);
                    auto left_root = runtime_.root(left);
                    frame->push(object_contains(right, left) ? runtime_.true_value() : runtime_.false_value());
                    break;
                }
                case ComparisonOp::NotIn:
                {
                    auto right_root = runtime_.root(right);
                    auto left_root = runtime_.root(left);
                    frame->push(object_contains(right, left) ? runtime_.false_value() : runtime_.true_value());
                    break;
                }
                }
                break;
            }
            case Opcode::GetIter:
            {
                Object *iterable = frame->pop();
                auto iterable_root = runtime_.root(iterable);
                frame->push(object_iter(iterable_root.get()));
                break;
            }
            case Opcode::ForIter:
            {
                Object *iterator = frame->pop();
                if (iterator && iterator->type() == runtime_.range_iterator_type())
                {
                    auto *range_iterator = static_cast<RangeIteratorObject *>(iterator);
                    if (range_iterator->remaining() == 0)
                    {
                        jump_to(*frame, instruction_count, instruction.operand);
                        break;
                    }
                    const std::int64_t value = range_iterator->consume_value();
                    frame->push(iterator);
                    frame->push(runtime_.new_int(value));
                    break;
                }
                auto iterator_root = runtime_.root(iterator);
                try
                {
                    Object *item = object_next(iterator_root.get());
                    auto item_root = runtime_.root(item);
                    frame->push(iterator_root.get());
                    frame->push(item_root.get());
                }
                catch (const PythonError &error)
                {
                    if (error.type() && is_subclass(error.type(), runtime_.stop_iteration_type()))
                    {
                        jump_to(*frame, instruction_count, instruction.operand);
                        break;
                    }
                    throw;
                }
                break;
            }
            case Opcode::BreakLoop:
            {
                bool jumped_to_finally = false;
                while (frame->has_block_handler())
                {
                    const FrameBlockHandler handler = frame->pop_block_handler();
                    if (handler.kind == FrameBlockKind::Except)
                        continue;
                    frame->clear_stack();
                    frame->set_pending_break(instruction.operand);
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(handler.target));
                    jumped_to_finally = true;
                    break;
                }
                if (jumped_to_finally)
                    continue;
                jump_to(*frame, instruction_count, instruction.operand);
                break;
            }
            case Opcode::ContinueLoop:
            {
                bool jumped_to_finally = false;
                while (frame->has_block_handler())
                {
                    const FrameBlockHandler handler = frame->pop_block_handler();
                    if (handler.kind == FrameBlockKind::Except)
                        continue;
                    frame->set_pending_continue(instruction.operand);
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(handler.target));
                    jumped_to_finally = true;
                    break;
                }
                if (jumped_to_finally)
                    continue;
                jump_to(*frame, instruction_count, instruction.operand);
                break;
            }
            case Opcode::JumpAbsolute:
            {
                jump_to(*frame, instruction_count, instruction.operand);
                break;
            }
            case Opcode::PopJumpIfFalse:
            {
                Object *condition = frame->pop();
                bool truth = false;
                if (vm_fastpath::try_truth(runtime_, condition, truth))
                {
                    if (!truth)
                        jump_to(*frame, instruction_count, instruction.operand);
                    break;
                }
                auto condition_root = runtime_.root(condition);
                if (!is_true(condition))
                    jump_to(*frame, instruction_count, instruction.operand);
                break;
            }
            case Opcode::PopJumpIfTrue:
            {
                Object *condition = frame->pop();
                bool truth = false;
                if (vm_fastpath::try_truth(runtime_, condition, truth))
                {
                    if (truth)
                        jump_to(*frame, instruction_count, instruction.operand);
                    break;
                }
                auto condition_root = runtime_.root(condition);
                if (is_true(condition))
                    jump_to(*frame, instruction_count, instruction.operand);
                break;
            }
            case Opcode::JumpIfFalseOrPop:
            {
                Object *condition = frame->pop();
                bool truth = false;
                if (vm_fastpath::try_truth(runtime_, condition, truth))
                {
                    if (!truth)
                    {
                        frame->push(condition);
                        jump_to(*frame, instruction_count, instruction.operand);
                    }
                    break;
                }
                auto condition_root = runtime_.root(condition);
                if (!is_true(condition))
                {
                    frame->push(condition);
                    jump_to(*frame, instruction_count, instruction.operand);
                }
                break;
            }
            case Opcode::MakeFunction:
            {
                const std::uint32_t closure_count =
                    instruction.operand & MakeFunctionClosureCountMask;
                const bool has_defaults =
                    (instruction.operand & MakeFunctionHasDefaults) != 0;
                const bool has_kwdefaults =
                    (instruction.operand & MakeFunctionHasKwDefaults) != 0;
                const bool has_annotations =
                    (instruction.operand & MakeFunctionHasAnnotations) != 0;
                Object *code_object = frame->pop();
                auto *function_code = dynamic_cast<CodeObject *>(code_object);
                if (!function_code)
                    throw BytecodeError("MAKE_FUNCTION requires a code object");
                auto code_root = runtime_.root(function_code);
                DictObject *annotations = nullptr;
                std::vector<Rooted<Object>> annotations_roots;
                if (has_annotations)
                {
                    Object *annotations_object = frame->pop();
                    annotations = dynamic_cast<DictObject *>(annotations_object);
                    if (!annotations)
                        throw BytecodeError("MAKE_FUNCTION annotations require a dict");
                    add_temporary_root(runtime_, annotations_roots, annotations);
                }
                DictObject *kwdefaults = nullptr;
                std::vector<Rooted<Object>> kwdefaults_roots;
                if (has_kwdefaults)
                {
                    Object *kwdefaults_object = frame->pop();
                    kwdefaults = dynamic_cast<DictObject *>(kwdefaults_object);
                    if (!kwdefaults)
                        throw BytecodeError("MAKE_FUNCTION keyword defaults require a dict");
                    add_temporary_root(runtime_, kwdefaults_roots, kwdefaults);
                }
                TupleObject *defaults = nullptr;
                std::vector<Rooted<Object>> defaults_roots;
                if (has_defaults)
                {
                    Object *defaults_object = frame->pop();
                    defaults = dynamic_cast<TupleObject *>(defaults_object);
                    if (!defaults)
                        throw BytecodeError("MAKE_FUNCTION defaults require a tuple");
                    add_temporary_root(runtime_, defaults_roots, defaults);
                }
                std::vector<CellObject *> closure;
                std::vector<Rooted<Object>> closure_roots;
                if (closure_count != 0)
                {
                    Object *closure_object = frame->pop();
                    auto *closure_tuple = dynamic_cast<TupleObject *>(closure_object);
                    if (!closure_tuple)
                        throw BytecodeError("MAKE_FUNCTION closure requires a tuple");
                    auto closure_tuple_root = runtime_.root(closure_tuple);
                    closure.reserve(closure_tuple->size());
                    for (Object *item : closure_tuple->items())
                    {
                        auto *cell = dynamic_cast<CellObject *>(item);
                        if (!cell)
                            throw BytecodeError("MAKE_FUNCTION closure entries must be cells");
                        closure.push_back(cell);
                        add_temporary_root(runtime_, closure_roots, cell);
                    }
                }
                PythonFunctionObject *function =
                    runtime_.new_function(
                        function_code->name(),
                        function_code,
                        &globals,
                        defaults,
                        kwdefaults,
                        annotations,
                        std::move(closure));
                auto function_root = runtime_.root(function);
                frame->push(function);
                break;
            }
            case Opcode::MakeClass:
            {
                if (instruction.operand >= code.names().size())
                    throw BytecodeError("MAKE_CLASS operand out of range");
                StrObject *class_name = code.names()[instruction.operand];
                Object *code_object = frame->pop();
                auto *class_code = dynamic_cast<CodeObject *>(code_object);
                if (!class_code)
                    throw BytecodeError("MAKE_CLASS requires a code object");
                Object *keywords_object = frame->pop();
                auto *keywords = dynamic_cast<DictObject *>(keywords_object);
                if (!keywords)
                    throw BytecodeError("MAKE_CLASS requires a keyword dict");
                Object *bases_object = frame->pop();
                auto *bases_tuple = dynamic_cast<TupleObject *>(bases_object);
                if (!bases_tuple)
                    throw BytecodeError("MAKE_CLASS requires a tuple of bases");
                auto class_code_root = runtime_.root(class_code);
                auto keywords_root = runtime_.root(keywords);
                auto bases_tuple_root = runtime_.root(bases_tuple);
                TypeObject *type = runtime_.build_class(ClassBuildRequest{
                    .name = class_name->value,
                    .body_code = class_code_root.get(),
                    .bases = bases_tuple_root.get(),
                    .keywords = keywords_root.get(),
                    .globals = &globals,
                    .builtins = &builtins,
                    .module_fallback = module_name_from_globals(runtime_, globals),
                    .qualname = class_name->value,
                });
                auto type_root = runtime_.root(type);
                frame->push(type);
                break;
            }
            case Opcode::Call:
            {
                const std::size_t arg_count = instruction.operand;
                if (frame->stack_size() < arg_count + 1)
                    throw BytecodeError("CALL stack underflow");
                const std::size_t args_start = frame->stack_size() - arg_count;
                ObjectSpan args = frame->stack_slice(args_start, arg_count);
                Object *callable = frame->stack_at(args_start - 1);
                Object *result = nullptr;
                if (Object *result =
                        vm_fastpath::try_builtin_call(runtime_, callable, args))
                {
                    frame->truncate_stack(args_start - 1);
                    frame->push(result);
                    break;
                }
                result = object_call_rooted(callable, args);
                frame->truncate_stack(args_start - 1);
                frame->push(result);
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
                frame->push(object_call_rooted(callable_root.get(), ObjectSpan(args.data(), args.size()), keywords_root.get()));
                break;
            }
            case Opcode::CallEx:
            {
                Object *keywords_object = frame->pop();
                auto *keywords = dynamic_cast<DictObject *>(keywords_object);
                if (!keywords)
                    throw BytecodeError("CALL_EX requires keyword dict");
                auto keywords_root = runtime_.root(keywords);

                Object *positional_object = frame->pop();
                auto *positional_tuple = dynamic_cast<TupleObject *>(positional_object);
                if (!positional_tuple)
                    throw BytecodeError("CALL_EX requires positional tuple");
                auto positional_root = runtime_.root(positional_tuple);

                Object *callable = frame->pop();
                auto callable_root = runtime_.root(callable);
                std::vector<Object *> args(positional_tuple->items().begin(), positional_tuple->items().end());
                std::vector<Rooted<Object>> roots;
                roots.reserve(args.size());
                for (Object *arg : args)
                    add_temporary_root(runtime_, roots, arg);
                frame->push(object_call_rooted(callable_root.get(), ObjectSpan(args.data(), args.size()), keywords_root.get()));
                break;
            }
            case Opcode::ImportStar:
            {
                Object *module = frame->pop();
                import_star(runtime_, module, *frame->locals());
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
                if (frame->has_exception_handler())
                    (void)frame->pop_exception_handler();
                else
                    thread_.pop_handled_exception();
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
            case Opcode::WithExit:
            {
                Object *exit_callable = frame->pop();
                auto exit_root = runtime_.root(exit_callable);
                std::vector<Object *> exit_args;
                exit_args.reserve(3);
                if (frame->pending_control_kind() == PendingControlKind::Exception)
                {
                    auto *exception = dynamic_cast<ExceptionObject *>(frame->pending_control_value());
                    if (!exception)
                        raise_type_error(&runtime_, "with handler stored non-exception pending state");
                    Object *traceback = exception->traceback()
                                            ? static_cast<Object *>(exception->traceback())
                                            : static_cast<Object *>(runtime_.none());
                    exit_args.push_back(exception->type());
                    exit_args.push_back(exception);
                    exit_args.push_back(traceback);
                }
                else
                {
                    exit_args.push_back(runtime_.none());
                    exit_args.push_back(runtime_.none());
                    exit_args.push_back(runtime_.none());
                }
                Object *result = object_call(
                    exit_root.get(),
                    CallArguments{ObjectSpan(exit_args.data(), exit_args.size()), nullptr});
                auto result_root = runtime_.root(result);
                if (frame->pending_control_kind() == PendingControlKind::Exception &&
                    is_true(result_root.get()))
                {
                    frame->clear_pending_control();
                }
                break;
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
                case PendingControlKind::Break:
                {
                    const std::size_t target = frame->pending_control_target();
                    frame->clear_pending_control();
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(target));
                    continue;
                }
                case PendingControlKind::Continue:
                {
                    const std::size_t target = frame->pending_control_target();
                    frame->clear_pending_control();
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(target));
                    continue;
                }
                }
                break;
            }
            case Opcode::ReturnValue:
            {
                Object *value = frame->pop();
                bool jumped_to_finally = false;
                while (frame->has_block_handler())
                {
                    const FrameBlockHandler handler = frame->pop_block_handler();
                    if (handler.kind == FrameBlockKind::Except)
                        continue;
                    frame->clear_stack();
                    frame->set_pending_return(value);
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(handler.target));
                    jumped_to_finally = true;
                    break;
                }
                if (jumped_to_finally)
                    continue;
                if (frame->trace_function())
                {
                    auto value_root = runtime_.root(value);
                    (void)call_trace_function(runtime_,
                                              thread_,
                                              frame->trace_function(),
                                              *frame,
                                              "return",
                                              value_root.get());
                }
                return value;
            }
            default:
                throw BytecodeError("invalid opcode");
            }
            }
            catch (const PythonError &error)
            {
                attach_source_location(runtime_, error, *frame, code, frame->last_instruction_index());
                if (frame->trace_function() && error.value())
                {
                    ExceptionObject *exception = error.value();
                    auto exception_root = runtime_.root(exception);
                    Object *traceback = exception->traceback() ? static_cast<Object *>(exception->traceback())
                                                               : static_cast<Object *>(runtime_.none());
                    auto traceback_root = runtime_.root(traceback);
                    Object *event_arg = runtime_.new_tuple({exception_root->type(),
                                                            exception_root.get(),
                                                            traceback_root.get()});
                    auto event_arg_root = runtime_.root(event_arg);
                    set_frame_trace_from_result(*frame, call_trace_function(runtime_,
                                                                            thread_,
                                                                            frame->trace_function(),
                                                                            *frame,
                                                                            "exception",
                                                                            event_arg_root.get()));
                }
                if (frame->has_exception_handler())
                {
                    const std::size_t target = frame->pop_exception_handler();
                    frame->clear_stack();
                    ExceptionObject *handled_exception = error.value();
                    if (!handled_exception)
                        handled_exception = runtime_.new_exception(runtime_.exception_type(), "unknown exception");
                    auto handled_root = runtime_.root(handled_exception);
                    thread_.push_handled_exception(handled_exception);
                    frame->push(handled_exception);
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(target));
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
                    jump_to(*frame, instruction_count, static_cast<std::uint32_t>(target));
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
