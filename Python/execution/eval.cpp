#include "cxxpython/core/execution.hpp"

#include <charconv>

namespace cxxpython::core
{
    void FunctionObject::trace(Tracer &tracer) const noexcept
    {
        for (auto value : {code, globals, name, qualname}) tracer.visit(value);
        if (defaults) tracer.visit(*defaults);
        if (kwdefaults) tracer.visit(*kwdefaults);
    }
    namespace detail
    {
        class FrameActivation final
        {
        public:
            FrameActivation(Attachment &context, ReturnSlot &active)
                : context_(context), active_(active), root_(active.result().value()), previous_(context.current_frame_),
                  previous_exception_(context.handled_exception_)
            {
                auto &data = context.payload<FrameObject>(root_);
                data.back = previous_; data.handled_exception = previous_exception_;
                data.state = FrameState::Running; context.current_frame_ = root_; sync();
            }
            ~FrameActivation()
            {
                while (current() != root_)
                {
                    auto &data = context_.payload<FrameObject>(current()); const auto parent = *data.back;
                    complete(data); active_.ok(parent); context_.leave_call();
                }
                complete(context_.payload<FrameObject>(root_)); context_.current_frame_ = previous_;
                context_.handled_exception_ = previous_exception_;
            }
            Value current() const { return active_.result().value(); }
            void sync() { context_.handled_exception_ = context_.payload<FrameObject>(current()).handled_exception; }
            bool descend(Handle frame)
            {
                if (!context_.enter_call()) return false;
                auto &child = context_.payload<FrameObject>(frame);
                child.back = current(); child.handled_exception = context_.handled_exception_; child.state = FrameState::Running;
                active_.ok(frame.get(context_)); context_.current_frame_ = current(); sync(); return true;
            }
            bool returned(Value value, ReturnSlot &out)
            {
                if (current() == root_) { out.ok(value); return true; }
                auto &child = context_.payload<FrameObject>(current()); const auto parent_value = *child.back;
                auto &parent = context_.payload<FrameObject>(parent_value); const auto base = *parent.pending_call;
                // Publish into the parent's traced stack before dropping the child root.
                parent.operands[base] = value;
                for (std::size_t i = base + 1; i < parent.sp; ++i) parent.operands[i] = context_.builtin(CoreObject::None);
                parent.sp = base + 1; parent.pending_call.reset(); complete(child);
                active_.ok(parent_value); context_.current_frame_ = parent_value; sync(); context_.leave_call(); return false;
            }
            void raising(Value exception, bool traceback = true)
            {
                auto &data = context_.payload<FrameObject>(current()); data.reason = ExceptionRaise{exception}; data.pending_call.reset();
                if (traceback && exception != context_.builtin(CoreObject::MemoryError))
                {
                    RootScope roots(context_); ReturnSlot out(context_);
                    append_traceback(context_, roots.root(exception), roots.root(current()), out);
                    data.reason = ExceptionRaise{out.result().exception()};
                }
            }
            bool failed(Value exception, ReturnSlot &out)
            {
                if (current() == root_) { out.raised(exception); return true; }
                auto &child = context_.payload<FrameObject>(current()); const auto parent_value = *child.back;
                auto &parent = context_.payload<FrameObject>(parent_value);
                parent.reason = ExceptionRaise{exception}; parent.trim_stack(context_, *parent.pending_call); parent.pending_call.reset();
                complete(child); active_.ok(parent_value); context_.current_frame_ = parent_value; sync(); context_.leave_call();
                raising(exception); return false;
            }
        private:
            void complete(FrameObject &frame) noexcept
            {
                frame.trim_stack(context_, 0); frame.pending_call.reset(); frame.regions.clear(); frame.cleanups.clear();
                frame.reason.reset(); frame.handled_exception.reset(); frame.state = FrameState::Completed;
            }
            Attachment &context_;
            ReturnSlot &active_;
            Value root_;
            std::optional<Value> previous_;
            std::optional<Value> previous_exception_;
        };
    }
    namespace
    {
        void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, std::move(message), out); }
        bool raised(ReturnSlot &out) { return out.result().tag() == ResultTag::Raised; }
        template <class T, CoreObject Kind> bool kind(Attachment &context, Value value)
        { return is_subtype(context, context.type_of(value), context.builtin(Kind)) && context.has_layout<T>(value); }
        void unbound(Attachment &context, const CodeObject &code, std::uint32_t local, ReturnSlot &out)
        {
            raise_error(context, CoreObject::UnboundLocalErrorType,
                        U"local variable '" + code.program.local_names()[local] + U"' is not bound", out);
        }
        void make_frame(Attachment &context, Handle function, CallArgsView args, std::optional<Value> receiver, ReturnSlot &out)
        {
            RootScope roots(context); const auto &callable = context.payload<FunctionObject>(function);
            auto code = roots.root(callable.code); auto defaults = roots.root(callable.defaults.value_or(context.builtin(CoreObject::EmptyTuple)));
            std::optional<Value> keywords;
            if (callable.kwdefaults) keywords = roots.root(*callable.kwdefaults).get(context);
            const auto &program = context.payload<CodeObject>(code).program; std::vector<Value> bound;
            detail::FunctionDefaults values{context.payload<TupleObject>(defaults).items, keywords};
            if (!detail::bind_arguments(context, program.signature(), args, receiver, roots, bound, out, values)) return;
            context.allocate_instance<FrameObject>(roots.root(context.builtin(CoreObject::FrameType)), out,
                function.get(context), code.get(context), program.local_names().size(), program.max_stack(), context.builtin(CoreObject::None), program.max_regions());
            auto &data = context.payload<FrameObject>(out.result().value());
            for (std::size_t i = 0; i < bound.size(); ++i) data.locals[i] = bound[i];
        }
        void run(Attachment &context, detail::FrameActivation &activation, ReturnSlot &out)
        {
            ReturnSlot result(context);
            for (;;)
            {
                auto &data = context.payload<FrameObject>(activation.current()); const auto &code = context.payload<CodeObject>(data.code);
                try
                {
                    if (data.reason)
                    {
                        if (data.unwind(context)) { activation.sync(); continue; }
                        if (const auto *returned = std::get_if<FunctionReturn>(&*data.reason))
                        { if (activation.returned(returned->value, out)) return; }
                        else if (const auto *error = std::get_if<ExceptionRaise>(&*data.reason))
                        { if (activation.failed(error->exception, out)) return; }
                        else throw std::logic_error("unverified control reason escaped its Frame");
                        continue;
                    }
                    data.last_instruction = data.pc;
                    const auto [opcode, operand] = code.program.instructions()[data.pc++];
                    switch (opcode)
                    {
                        case Opcode::LoadConst:
                            data.operands[data.sp++] = context.payload<TupleObject>(code.constants).items[operand];
                            break;
                        case Opcode::LoadFast:
                            if (!data.locals[operand]) { unbound(context, code, operand, result); activation.raising(result.result().exception()); break; }
                            data.operands[data.sp++] = *data.locals[operand]; break;
                        case Opcode::StoreFast:
                            data.locals[operand] = data.operands[--data.sp]; data.operands[data.sp] = context.builtin(CoreObject::None); break;
                        case Opcode::DeleteFast:
                            if (!data.locals[operand]) { unbound(context, code, operand, result); activation.raising(result.result().exception()); break; }
                            data.locals[operand].reset(); break;
                        case Opcode::PopTop:
                            data.operands[--data.sp] = context.builtin(CoreObject::None); break;
                        case Opcode::DupTop:
                            data.operands[data.sp] = data.operands[data.sp - 1]; ++data.sp; break;
                        case Opcode::Binary:
                        case Opcode::Compare:
                        {
                            RootScope roots(context);
                            auto left = roots.root(data.operands[data.sp - 2]); auto right = roots.root(data.operands[data.sp - 1]);
                            if (opcode == Opcode::Binary) binary(context, left, right, static_cast<BinaryOp>(operand), result);
                            else rich_compare(context, left, right, static_cast<CompareOp>(operand), result);
                            if (raised(result)) { activation.raising(result.result().exception()); break; }
                            data.operands[data.sp - 2] = result.result().value(); data.operands[--data.sp] = context.builtin(CoreObject::None); break;
                        }
                        case Opcode::Jump:
                            data.pc = operand; break;
                        case Opcode::JumpIfFalse:
                        {
                            RootScope roots(context); truth(context, roots.root(data.operands[data.sp - 1]), result);
                            if (raised(result)) { activation.raising(result.result().exception()); break; }
                            data.operands[--data.sp] = context.builtin(CoreObject::None);
                            if (result.result().value() == context.builtin(CoreObject::False)) data.pc = operand;
                            break;
                        }
                        case Opcode::Return:
                            result.ok(data.operands[data.sp - 1]); detail::validate_return(context, result);
                            data.reason = FunctionReturn{result.result().value()};
                            break;
                        case Opcode::RaiseInstance:
                        {
                            auto exception = data.operands[data.sp - 1];
                            if (!kind<ExceptionObject, CoreObject::BaseExceptionType>(context, exception))
                            { type_error(context, U"RAISE_INSTANCE requires a BaseException instance", result); exception = result.result().exception(); }
                            else detail::link_exception_context(context, exception);
                            activation.raising(exception); break;
                        }
                        case Opcode::Call:
                        {
                            const auto &layout = code.program.calls()[operand]; const auto base = data.sp - layout.argument_count() - 1;
                            const auto &keywords = context.payload<TupleObject>(context.payload<TupleObject>(code.call_keywords).items[operand]).items;
                            std::vector<KeywordArg> named;
                            for (std::size_t i = 0; i < keywords.size(); ++i)
                                named.push_back({keywords[i], data.operands[base + 1 + layout.positional + i]});
                            CallArgsView args{{data.operands.data() + base + 1, layout.positional}, named};
                            data.pending_call = base; RootScope roots(context);
                            auto prepared = detail::prepare_call(context, roots.root(data.operands[base]), args, roots, result);
                            if (prepared)
                            {
                                ReturnSlot child(context);
                                make_frame(context, prepared->function, {prepared->positional, prepared->keywords},
                                           prepared->receiver ? std::optional<Value>{prepared->receiver->get(context)} : std::optional<Value>{}, child);
                                if (raised(child)) { activation.raising(child.result().exception()); break; }
                                if (!activation.descend(roots.root(child.result().value())))
                                { raise_error(context, CoreObject::RecursionErrorType, U"maximum call depth exceeded", result); activation.raising(result.result().exception()); }
                            }
                            else
                            {
                                if (raised(result)) { activation.raising(result.result().exception()); break; }
                                data.operands[base] = result.result().value();
                                for (std::size_t i = base + 1; i < data.sp; ++i) data.operands[i] = context.builtin(CoreObject::None);
                                data.sp = base + 1; data.pending_call.reset();
                            }
                            break;
                        }
                        case Opcode::EnterRegion:
                            data.regions.push_back({{operand, RegionPhase::Protected}, data.sp, {}}); break;
                        case Opcode::LeaveRegion:
                            data.reason = NormalExit{code.program.regions()[operand].after_pc, code.program.regions()[operand].parent}; break;
                        case Opcode::EndHandler:
                            data.finish_handler(context); activation.sync(); break;
                        case Opcode::BreakLoop:
                            data.reason = LoopBreak{operand}; break;
                        case Opcode::ContinueLoop:
                            data.reason = LoopContinue{operand}; break;
                        case Opcode::LoadException:
                            data.operands[data.sp++] = data.handled_exception.value_or(context.builtin(CoreObject::None)); break;
                        case Opcode::Reraise:
                            if (data.handled_exception) activation.raising(*data.handled_exception, false);
                            else { raise_error(context, CoreObject::RuntimeErrorType, U"no active exception to reraise", result); activation.raising(result.result().exception()); }
                            break;
                        case Opcode::ExceptionMatch:
                            detail::exception_match(context, data.operands[data.sp - 2], data.operands[data.sp - 1], result);
                            if (raised(result)) { activation.raising(result.result().exception()); break; }
                            data.operands[data.sp - 2] = result.result().value(); data.operands[--data.sp] = context.builtin(CoreObject::None); break;
                        case Opcode::Count:
                            throw std::logic_error("unverified opcode reached execution");
                    }
                }
                catch (const std::bad_alloc &) { activation.raising(context.builtin(CoreObject::MemoryError)); }
            }
        }
        void code_name(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<CodeObject>(value).name); }
        void code_qualname(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<CodeObject>(value).qualname); }
        void code_filename(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<CodeObject>(value).filename); }
        void code_constants(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<CodeObject>(value).constants); }
        void code_varnames(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<CodeObject>(value).varnames); }
        void code_stacksize(Attachment &c, Value value, ReturnSlot &out)
        { make_int(c, static_cast<std::int64_t>(c.payload<CodeObject>(value).program.max_stack()), out); }
        void code_firstline(Attachment &c, Value value, ReturnSlot &out)
        { make_int(c, c.payload<CodeObject>(value).program.first_line(), out); }
        void function_code(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<FunctionObject>(value).code); }
        void function_name(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<FunctionObject>(value).name); }
        void function_qualname(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<FunctionObject>(value).qualname); }
        void function_globals(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<FunctionObject>(value).globals); }
        void function_defaults(Attachment &c, Value value, ReturnSlot &out)
        {
            out.ok(c.payload<FunctionObject>(value).defaults.value_or(c.builtin(CoreObject::None)));
        }
        void function_keywords(Attachment &c, Value value, ReturnSlot &out)
        { out.ok(c.payload<FunctionObject>(value).kwdefaults.value_or(c.builtin(CoreObject::None))); }
        void frame_code(Attachment &c, Value value, ReturnSlot &out) { out.ok(c.payload<FrameObject>(value).code); }
        void frame_back(Attachment &c, Value value, ReturnSlot &out)
        { out.ok(c.payload<FrameObject>(value).back.value_or(c.builtin(CoreObject::None))); }
        void frame_lineno(Attachment &c, Value value, ReturnSlot &out)
        {
            const auto &frame = c.payload<FrameObject>(value); const auto &program = c.payload<CodeObject>(frame.code).program;
            auto line = frame.last_instruction ? program.positions()[*frame.last_instruction].line : 0;
            make_int(c, line ? line : program.first_line(), out);
        }
        std::u32string address(Value value)
        {
            char buffer[2 * sizeof(std::uintptr_t)];
            auto end = std::to_chars(std::begin(buffer), std::end(buffer), reinterpret_cast<std::uintptr_t>(value.object()), 16).ptr;
            return U"0x" + std::u32string(buffer, end);
        }
        void execution_repr(Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out)
        {
            const auto owner = c.payload<NativeFunctionObject>(fn).captures[0];
            if (c.type_of(args[0]) != owner) { type_error(c, U"execution repr requires its own object kind", out); return; }
            std::u32string result;
            if (owner == c.builtin(CoreObject::FunctionType))
                result = U"<function " + c.payload<StrObject>(c.payload<FunctionObject>(args[0]).qualname).value;
            else if (owner == c.builtin(CoreObject::CodeType))
                result = U"<code object " + c.payload<StrObject>(c.payload<CodeObject>(args[0]).name).value;
            else result = U"<frame";
            result += U" at " + address(args[0]) + U">"; make_str(c, std::move(result), out);
        }
    }
    void make_function(Attachment &context, const FunctionSpec &spec, ReturnSlot &out)
    {
        out.check_caller(context); RootScope roots(context);
        auto code = roots.root(spec.code.get(context)); auto globals = roots.root(spec.globals.get(context));
        std::optional<Value> defaults, keywords;
        if (spec.defaults) defaults = roots.root(spec.defaults->get(context)).get(context);
        if (spec.kwdefaults) keywords = roots.root(spec.kwdefaults->get(context)).get(context);
        out.reset(context);
        if (context.type_of(code) != context.builtin(CoreObject::CodeType) || !context.has_layout<CodeObject>(code.get(context)))
        { type_error(context, U"function requires verified Code", out); return; }
        if (!kind<DictObject, CoreObject::DictType>(context, globals.get(context)) ||
            (defaults && !kind<TupleObject, CoreObject::TupleType>(context, *defaults)) ||
            (keywords && !kind<DictObject, CoreObject::DictType>(context, *keywords)))
        { type_error(context, U"function requires a globals dict, defaults tuple and optional kwdefaults dict", out); return; }
        const auto &program = context.payload<CodeObject>(code);
        context.allocate_instance<FunctionObject>(roots.root(context.builtin(CoreObject::FunctionType)), out,
            code.get(context), globals.get(context), defaults, keywords, program.name, program.qualname);
    }
    namespace detail
    {
        void execute_function(Attachment &context, Handle function, CallArgsView args,
                              std::optional<Value> receiver, ReturnSlot &out)
        {
            ReturnSlot active(context); make_frame(context, function, args, receiver, active);
            if (raised(active)) { out.raised(active.result().exception()); return; }
            FrameActivation activation(context, active); run(context, activation, out);
        }
        LayoutDescriptor execution_layout(CoreObject type)
        {
            if (type == CoreObject::CodeType) return LayoutDescriptor::of<CodeObject>();
            if (type == CoreObject::FrameType) return LayoutDescriptor::of<FrameObject>();
            if (type == CoreObject::TracebackType) return LayoutDescriptor::of<TracebackObject>();
            if (type == CoreObject::FunctionType)
                return LayoutDescriptor::of<FunctionObject>();
            throw std::invalid_argument("not an execution object kind");
        }
        void install_execution(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            auto field = [&](CoreObject kind, std::u32string name, NativeGetter getter) {
                RootScope roots(context); ReturnSlot out(context);
                context.allocate_instance<GetSetDescriptorObject>(at(CoreObject::GetSetDescriptorType), out, at(kind).get(context), getter, nullptr);
                auto descriptor = roots.root(out.result().value());
                context.allocate_instance<StrObject>(at(CoreObject::StrType), out, std::move(name));
                name_store(context, *context.payload<TypeObject>(at(kind)).dictionary, out.result().value(), descriptor.get(context));
            };
            field(CoreObject::CodeType, U"co_name", code_name); field(CoreObject::CodeType, U"co_qualname", code_qualname);
            field(CoreObject::CodeType, U"co_filename", code_filename); field(CoreObject::CodeType, U"co_consts", code_constants);
            field(CoreObject::CodeType, U"co_varnames", code_varnames); field(CoreObject::CodeType, U"co_stacksize", code_stacksize);
            field(CoreObject::CodeType, U"co_firstlineno", code_firstline);
            field(CoreObject::FunctionType, U"__code__", function_code); field(CoreObject::FunctionType, U"__name__", function_name);
            field(CoreObject::FunctionType, U"__qualname__", function_qualname); field(CoreObject::FunctionType, U"__globals__", function_globals);
            field(CoreObject::FunctionType, U"__defaults__", function_defaults); field(CoreObject::FunctionType, U"__kwdefaults__", function_keywords);
            field(CoreObject::FrameType, U"f_code", frame_code); field(CoreObject::FrameType, U"f_back", frame_back);
            field(CoreObject::FrameType, U"f_lineno", frame_lineno);
            SignatureLayout unary; unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
            for (auto kind : {CoreObject::CodeType, CoreObject::FunctionType, CoreObject::FrameType})
                install_native_attribute(context, core, at(kind), U"__repr__", unary, execution_repr, {at(kind).get(context)});
        }
    }
}
