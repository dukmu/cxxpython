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
    void FrameObject::trace(Tracer &tracer) const noexcept
    {
        tracer.visit(function); tracer.visit(code); if (back) tracer.visit(*back);
        for (const auto &value : locals) if (value) tracer.visit(*value);
        for (const auto &value : operands) if (value) tracer.visit(*value);
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
        void run(Attachment &context, Handle frame, ReturnSlot &out)
        {
            auto &data = context.payload<FrameObject>(frame); const auto &code = context.payload<CodeObject>(data.code);
            const auto instructions = code.program.instructions(); ReturnSlot result(context);
            for (;;)
            {
                data.last_instruction = data.pc;
                const auto [opcode, operand] = instructions[data.pc++];
                switch (opcode)
                {
                    case Opcode::LoadConst:
                        data.operands[data.sp++] = context.payload<TupleObject>(code.constants).items[operand];
                        break;
                    case Opcode::LoadFast:
                        if (!data.locals[operand]) { unbound(context, code, operand, out); return; }
                        data.operands[data.sp++] = data.locals[operand]; break;
                    case Opcode::StoreFast:
                        data.locals[operand] = data.operands[--data.sp]; data.operands[data.sp].reset(); break;
                    case Opcode::DeleteFast:
                        if (!data.locals[operand]) { unbound(context, code, operand, out); return; }
                        data.locals[operand].reset(); break;
                    case Opcode::PopTop:
                        data.operands[--data.sp].reset(); break;
                    case Opcode::DupTop:
                        data.operands[data.sp] = data.operands[data.sp - 1]; ++data.sp; break;
                    case Opcode::Binary:
                    case Opcode::Compare:
                    {
                        RootScope roots(context);
                        auto left = roots.root(*data.operands[data.sp - 2]); auto right = roots.root(*data.operands[data.sp - 1]);
                        if (opcode == Opcode::Binary) binary(context, left, right, static_cast<BinaryOp>(operand), result);
                        else rich_compare(context, left, right, static_cast<CompareOp>(operand), result);
                        if (raised(result)) { out.raised(result.result().exception()); return; }
                        data.operands[data.sp - 2] = result.result().value(); data.operands[--data.sp].reset(); break;
                    }
                    case Opcode::Jump:
                        data.pc = operand; break;
                    case Opcode::JumpIfFalse:
                    {
                        RootScope roots(context); truth(context, roots.root(*data.operands[data.sp - 1]), result);
                        if (raised(result)) { out.raised(result.result().exception()); return; }
                        data.operands[--data.sp].reset();
                        if (result.result().value() == context.builtin(CoreObject::False)) data.pc = operand;
                        break;
                    }
                    case Opcode::Return:
                        out.ok(*data.operands[data.sp - 1]); return;
                    case Opcode::RaiseInstance:
                    {
                        auto exception = *data.operands[data.sp - 1];
                        if (!kind<ExceptionObject, CoreObject::BaseExceptionType>(context, exception))
                            type_error(context, U"RAISE_INSTANCE requires a BaseException instance", out);
                        else out.raised(exception);
                        return;
                    }
                    case Opcode::Count:
                        throw std::logic_error("unverified opcode reached execution");
                }
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
        class FrameActivation final
        {
        public:
            FrameActivation(Attachment &context, Handle frame) : context_(context), frame_(frame), previous_(context.current_frame_)
            {
                auto &data = context.payload<FrameObject>(frame);
                data.back = previous_; data.state = FrameState::Running; context.current_frame_ = frame.get(context);
            }
            ~FrameActivation()
            {
                auto &data = context_.payload<FrameObject>(frame_);
                for (auto &value : data.operands) value.reset();
                data.sp = 0; data.state = FrameState::Completed; context_.current_frame_ = previous_;
            }
        private:
            Attachment &context_;
            Handle frame_;
            std::optional<Value> previous_;
        };
        void execute_function(Attachment &context, Handle function, CallArgsView args,
                              std::optional<Value> receiver, ReturnSlot &out)
        {
            RootScope roots(context); const auto &callable = context.payload<FunctionObject>(function);
            auto code = roots.root(callable.code);
            auto defaults = roots.root(callable.defaults.value_or(context.builtin(CoreObject::EmptyTuple)));
            std::optional<Value> keywords;
            if (callable.kwdefaults) keywords = roots.root(*callable.kwdefaults).get(context);
            const auto &program = context.payload<CodeObject>(code).program; std::vector<Value> bound;
            FunctionDefaults values{context.payload<TupleObject>(defaults).items, keywords};
            if (!bind_arguments(context, program.signature(), args, receiver, roots, bound, out, values)) return;
            ReturnSlot created(context);
            context.allocate_instance<FrameObject>(roots.root(context.builtin(CoreObject::FrameType)), created,
                function.get(context), code.get(context), program.local_names().size(), program.max_stack());
            auto frame = roots.root(created.result().value()); auto &data = context.payload<FrameObject>(frame);
            for (std::size_t i = 0; i < bound.size(); ++i) data.locals[i] = bound[i];
            FrameActivation activation(context, frame); run(context, frame, out);
        }
        LayoutDescriptor execution_layout(CoreObject type)
        {
            if (type == CoreObject::CodeType) return LayoutDescriptor::of<CodeObject>();
            if (type == CoreObject::FrameType) return LayoutDescriptor::of<FrameObject>();
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
