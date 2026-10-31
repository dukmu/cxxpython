#include "cxxpython/genobject.hpp"
#include "internal/errors.hpp"

#include <charconv>

namespace cxxpython::core {
namespace {
bool generator(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<GeneratorObject>(value) &&
        (is_subtype(ctx, ctx.type_of(value), ctx.builtin(CoreObject::GeneratorType)) ||
         ctx.type_of(value) == ctx.builtin(CoreObject::CoroutineType)))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType, U"generator descriptor requires a generator", out);
    return false;
}

bool exception_is(Attachment &ctx, Value value, CoreObject type)
{
    return is_subtype(ctx, ctx.type_of(value), ctx.builtin(type));
}

void set_exception_attribute(Attachment &ctx, Handle error, const char32_t *name, Handle value,
                             ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot key(ctx);
    make_str(ctx, name, key);
    if (key.tag() == ResultTag::Raised) {
        out.raised(key.exception());
        return;
    }
    set_attribute(ctx, error, roots.root(key.value()), value, out);
}

// Normalize the historical throw(type, value, traceback) form as in CPython 3.12.
void normalize_throw(Attachment &ctx, std::span<const Value> args, ReturnSlot &out)
{
    if (args.empty() || args.size() > 3) {
        raise_error(ctx, CoreObject::TypeErrorType, U"throw expected 1 to 3 arguments", out);
        return;
    }
    RootScope roots(ctx);
    for (auto value : args)
        roots.root(value);
    const auto none = ctx.builtin(CoreObject::None);
    if (args.size() == 3 && args[2] != none &&
        (ctx.type_of(args[2]) != ctx.builtin(CoreObject::TracebackType) ||
         !ctx.has_layout<TracebackObject>(args[2]))) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"throw() third argument must be a traceback object", out);
        return;
    }
    const auto type = roots.root(args[0]);
    if (ctx.has_layout<ExceptionObject>(args[0]) &&
        exception_is(ctx, args[0], CoreObject::BaseExceptionType)) {
        if (args.size() > 1 && args[1] != none) {
            raise_error(ctx, CoreObject::TypeErrorType,
                        U"instance exception may not have a separate value", out);
            return;
        }
        out.ok(args[0]);
    } else if (ctx.has_layout<TypeObject>(args[0]) &&
               is_subtype(ctx, ctx.type_of(args[0]), ctx.type_type()) &&
               is_subtype(ctx, args[0], ctx.builtin(CoreObject::BaseExceptionType))) {
        if (args.size() > 1 && args[1] != none && ctx.has_layout<ExceptionObject>(args[1]) &&
            is_subtype(ctx, ctx.type_of(args[1]), args[0])) {
            out.ok(args[1]);
        } else if (args.size() <= 1 || args[1] == none) {
            detail::normalize_exception(ctx, type, out);
        } else if (ctx.has_layout<TupleObject>(args[1]) &&
                   is_subtype(ctx, ctx.type_of(args[1]), ctx.builtin(CoreObject::TupleType))) {
            call(ctx, type, {ctx.payload<TupleObject>(args[1]).items}, out);
        } else {
            const Value values[] = {args[1]};
            call(ctx, type, {values}, out);
        }
        if (out.tag() == ResultTag::Ok &&
            (!ctx.has_layout<ExceptionObject>(out.value()) ||
             !exception_is(ctx, out.value(), CoreObject::BaseExceptionType))) {
            raise_error(ctx, CoreObject::TypeErrorType,
                        U"calling exception class should have returned an instance", out);
            return;
        }
    } else {
        detail::normalize_exception(ctx, type, out);
    }
    if (out.tag() == ResultTag::Raised)
        return;
    if (args.size() == 3 && args[2] != none) {
        auto error = roots.root(out.value());
        ReturnSlot changed(ctx);
        set_exception_attribute(ctx, error, U"__traceback__", roots.root(args[2]), changed);
        if (changed.tag() == ResultTag::Raised)
            out.raised(changed.exception());
        else
            out.ok(error.get(ctx));
    }
}

enum class Operation { Next, Send, Throw, Close };

// Restore the acquired state if argument preparation fails or throws in C++.
struct FrameClaim {
    FrameObject &frame;
    FrameState previous;
    bool active = true;
    ~FrameClaim()
    {
        if (active) {
            frame.owner.store(std::thread::id{}, std::memory_order_relaxed);
            frame.state.store(previous, std::memory_order_release);
        }
    }
    void publish(FrameState state)
    {
        frame.owner.store(std::thread::id{}, std::memory_order_relaxed);
        frame.state.store(state, std::memory_order_release);
        active = false;
    }
};

void convert_stop_iteration(Attachment &ctx, bool coroutine, ReturnSlot &out)
{
    if (out.tag() != ResultTag::Raised ||
        !exception_is(ctx, out.exception(), CoreObject::StopIterationType))
        return;
    RootScope roots(ctx);
    const auto cause = roots.root(out.exception());
    ReturnSlot error(ctx), changed(ctx);
    raise_error(ctx, CoreObject::RuntimeErrorType,
                coroutine ? U"coroutine raised StopIteration" : U"generator raised StopIteration",
                error);
    const auto replacement = roots.root(error.exception());
    set_exception_attribute(ctx, replacement, U"__context__", cause, changed);
    if (changed.tag() == ResultTag::Raised) {
        out.raised(changed.exception());
        return;
    }
    set_exception_attribute(ctx, replacement, U"__cause__", cause, changed);
    if (changed.tag() == ResultTag::Raised)
        out.raised(changed.exception());
    else
        out.raised(replacement.get(ctx));
}

// Shared by generator.close() and frame.clear(); no generator back-reference or
// extra weakref allocation is needed to run a suspended frame's cleanup.
bool close_frame(Attachment &ctx, Handle frame, bool clearing, ReturnSlot &out)
{
    auto &data = ctx.payload<FrameObject>(frame);
    const bool coroutine = ctx.payload<CodeObject>(data.code).program.is_coroutine();
    auto state = data.state.load(std::memory_order_acquire);
    for (;;) {
        if (state == FrameState::Running) {
            raise_error(ctx, clearing ? CoreObject::RuntimeErrorType : CoreObject::ValueErrorType,
                        clearing    ? U"cannot clear an executing frame"
                        : coroutine ? U"coroutine already executing"
                                    : U"generator already executing",
                        out);
            return false;
        }
        if (state == FrameState::Completed) {
            out.ok(ctx.builtin(CoreObject::None));
            return true;
        }
        if (data.state.compare_exchange_weak(state, FrameState::Running, std::memory_order_acquire))
            break;
    }
    data.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    FrameClaim claim{data, state};
    if (state == FrameState::Created) {
        claim.publish(FrameState::Completed);
        out.ok(ctx.builtin(CoreObject::None));
        return true;
    }
    RootScope roots(ctx);
    ReturnSlot thrown(ctx);
    detail::normalize_exception(ctx, roots.root(ctx.builtin(CoreObject::GeneratorExitType)),
                                thrown);
    if (thrown.tag() == ResultTag::Raised) {
        out.raised(thrown.exception());
        return true;
    }
    const auto exception = roots.root(thrown.value());
    detail::ResumeResult result;
    try {
        result = detail::resume_generator_frame(
            ctx, frame, roots.root(ctx.builtin(CoreObject::None)), exception, out);
    } catch (...) {
        claim.publish(FrameState::Completed);
        throw;
    }
    const bool suspended =
        result == detail::ResumeResult::Yielded || result == detail::ResumeResult::Rejected;
    claim.publish(suspended ? FrameState::Suspended : FrameState::Completed);
    if (result == detail::ResumeResult::Yielded)
        raise_error(ctx, CoreObject::RuntimeErrorType,
                    coroutine ? U"coroutine ignored GeneratorExit"
                              : U"generator ignored GeneratorExit",
                    out);
    else if (result == detail::ResumeResult::Returned ||
             (out.tag() == ResultTag::Raised &&
              exception_is(ctx, out.exception(), CoreObject::GeneratorExitType)))
        out.ok(ctx.builtin(CoreObject::None));
    else if (result == detail::ResumeResult::Raised)
        convert_stop_iteration(ctx, coroutine, out);
    return true;
}

void resume(Attachment &ctx, Value self_value, Operation operation, std::span<const Value> args,
            ReturnSlot &out, std::optional<Handle> throw_args = {})
{
    if (!generator(ctx, self_value, out))
        return;
    if (operation == Operation::Throw && (args.empty() || args.size() > 3)) {
        raise_error(ctx, CoreObject::TypeErrorType, U"throw expected 1 to 3 arguments", out);
        return;
    }
    RootScope roots(ctx);
    const auto self = roots.root(self_value);
    const bool coroutine = ctx.type_of(self) == ctx.builtin(CoreObject::CoroutineType);
    for (auto value : args)
        roots.root(value);
    ReturnSlot loaded(ctx), thrown(ctx);
    ctx.load(self_value, ctx.payload<GeneratorObject>(self).frame, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        if (coroutine && operation != Operation::Close) {
            raise_error(ctx, CoreObject::RuntimeErrorType,
                        U"cannot reuse already awaited coroutine", out);
            return;
        }
        if (operation == Operation::Throw) {
            normalize_throw(ctx, args, thrown);
            if (thrown.tag() == ResultTag::Raised)
                out.raised(thrown.exception());
            else
                out.raised(thrown.value());
        } else if (operation == Operation::Close)
            out.ok(ctx.builtin(CoreObject::None));
        else
            detail::generator_stop(ctx, {}, out);
        return;
    }
    const auto frame = roots.root(loaded.value());
    auto &data = ctx.payload<FrameObject>(frame);
    if (operation == Operation::Close) {
        try {
            close_frame(ctx, frame, false, out);
        } catch (...) {
            if (data.state.load(std::memory_order_acquire) == FrameState::Completed)
                ctx.store(self.get(ctx), ctx.payload<GeneratorObject>(self).frame, {});
            throw;
        }
        if (data.state.load(std::memory_order_acquire) == FrameState::Completed)
            ctx.store(self.get(ctx), ctx.payload<GeneratorObject>(self).frame, {});
        return;
    }
    auto state = data.state.load(std::memory_order_acquire);
    for (;;) {
        if (state == FrameState::Running) {
            raise_error(ctx, CoreObject::ValueErrorType,
                        coroutine ? U"coroutine already executing" : U"generator already executing",
                        out);
            return;
        }
        if (state == FrameState::Completed) {
            if (coroutine && operation != Operation::Close) {
                raise_error(ctx, CoreObject::RuntimeErrorType,
                            U"cannot reuse already awaited coroutine", out);
                return;
            }
            if (operation == Operation::Throw) {
                normalize_throw(ctx, args, thrown);
                if (thrown.tag() == ResultTag::Raised)
                    out.raised(thrown.exception());
                else
                    out.raised(thrown.value());
            } else if (operation == Operation::Close)
                out.ok(ctx.builtin(CoreObject::None));
            else
                detail::generator_stop(ctx, {}, out);
            return;
        }
        if (data.state.compare_exchange_weak(state, FrameState::Running, std::memory_order_acquire,
                                             std::memory_order_acquire))
            break;
    }
    data.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    FrameClaim claim{data, state};
    const auto none = ctx.builtin(CoreObject::None);
    if (operation == Operation::Send && state == FrameState::Created && args[0] != none) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    coroutine ? U"can't send non-None value to a just-started coroutine"
                              : U"can't send non-None value to a just-started generator",
                    out);
        return;
    }
    std::optional<Handle> exception;
    if (operation == Operation::Throw) {
        ReturnSlot delegate(ctx);
        ctx.load(frame.get(ctx), data.delegate, delegate);
        if (delegate.tag() == ResultTag::Empty) {
            normalize_throw(ctx, args, thrown);
            if (thrown.tag() == ResultTag::Raised) {
                out.raised(thrown.exception());
                return;
            }
            exception = roots.root(thrown.value());
        }
    }
    const auto sent = roots.root(operation == Operation::Send ? args[0] : none);
    detail::ResumeResult result;
    try {
        result = detail::resume_generator_frame(ctx, frame, sent, exception, out, throw_args);
    } catch (...) {
        claim.publish(FrameState::Completed);
        ctx.store(self.get(ctx), ctx.payload<GeneratorObject>(self).frame, {});
        throw;
    }
    const bool suspended =
        result == detail::ResumeResult::Yielded || result == detail::ResumeResult::Rejected;
    claim.publish(suspended ? FrameState::Suspended : FrameState::Completed);
    if (!suspended)
        ctx.store(self.get(ctx), ctx.payload<GeneratorObject>(self).frame, {});
    if (result == detail::ResumeResult::Returned) {
        const auto value = roots.root(out.value());
        detail::generator_stop(ctx, value, out);
    } else if (result == detail::ResumeResult::Raised)
        convert_stop_iteration(ctx, coroutine, out);
}

void iter(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (generator(ctx, args[0], out))
        out.ok(args[0]);
}
void next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    resume(ctx, args[0], Operation::Next, {}, out);
}
void send(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    resume(ctx, args[0], Operation::Send, args.subspan(1), out);
}
void throw_(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!generator(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto arguments = roots.root(args[1]);
    resume(ctx, args[0], Operation::Throw, ctx.payload<TupleObject>(arguments).items, out,
           arguments);
}
void close(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    resume(ctx, args[0], Operation::Close, {}, out);
}

enum class Field { Code, Frame, Running, Suspended, YieldFrom, Name, Qualname };
template <Field field> void get_field(Attachment &ctx, Value self, ReturnSlot &out)
{
    if (!generator(ctx, self, out))
        return;
    RootScope roots(ctx);
    roots.root(self);
    auto &gen = ctx.payload<GeneratorObject>(self);
    if constexpr (field == Field::Code) {
        out.ok(gen.code);
    } else if constexpr (field == Field::Name || field == Field::Qualname) {
        ctx.load(self, field == Field::Name ? gen.name : gen.qualname, out);
    } else {
        ReturnSlot frame(ctx);
        ctx.load(self, gen.frame, frame);
        if (frame.tag() == ResultTag::Empty) {
            out.ok(ctx.builtin(field == Field::Running || field == Field::Suspended
                                   ? CoreObject::False
                                   : CoreObject::None));
            return;
        }
        const auto value = roots.root(frame.value());
        const auto &data = ctx.payload<FrameObject>(value);
        const auto state = data.state.load(std::memory_order_acquire);
        if constexpr (field == Field::Running || field == Field::Suspended)
            out.ok(ctx.builtin(
                state == (field == Field::Running ? FrameState::Running : FrameState::Suspended)
                    ? CoreObject::True
                    : CoreObject::False));
        else if constexpr (field == Field::Frame)
            out.ok(state == FrameState::Completed ? ctx.builtin(CoreObject::None) : value.get(ctx));
        else {
            if (state == FrameState::Completed) {
                out.ok(ctx.builtin(CoreObject::None));
                return;
            }
            ctx.load(value.get(ctx), data.delegate, out);
            if (out.tag() == ResultTag::Empty)
                out.ok(ctx.builtin(CoreObject::None));
            else if (ctx.type_of(self) == ctx.builtin(CoreObject::CoroutineType) &&
                     ctx.type_of(out.value()) == ctx.builtin(CoreObject::CoroutineWrapperType)) {
                const auto delegate = roots.root(out.value());
                ctx.load(delegate.get(ctx), ctx.payload<SequenceIteratorObject>(delegate).owner,
                         out);
            }
        }
    }
}
template <Field field>
void set_name(Attachment &ctx, Value self, std::optional<Value> value, ReturnSlot &out)
{
    if (!generator(ctx, self, out))
        return;
    if (!value || !ctx.has_layout<StrObject>(*value) ||
        !is_subtype(ctx, ctx.type_of(*value), ctx.builtin(CoreObject::StrType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"generator name must be a string", out);
        return;
    }
    auto &gen = ctx.payload<GeneratorObject>(self);
    ctx.store(self, field == Field::Name ? gen.name : gen.qualname, *value);
    out.ok(ctx.builtin(CoreObject::None));
}
void repr_generator(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!generator(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    roots.root(args[0]);
    ReturnSlot name(ctx);
    get_field<Field::Qualname>(ctx, args[0], name);
    char buffer[2 * sizeof(std::uintptr_t)];
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer),
                                         reinterpret_cast<std::uintptr_t>(args[0].object()), 16);
    make_str(ctx,
             (ctx.type_of(args[0]) == ctx.builtin(CoreObject::CoroutineType)
                  ? U"<coroutine object "
                  : U"<generator object ") +
                 ctx.payload<StrObject>(name.value()).value + U" at 0x" +
                 std::u32string(buffer, converted.ptr) + U">",
             out);
}
void await_wrapper(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!generator(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    ctx.allocate_instance<SequenceIteratorObject>(
        roots.root(ctx.builtin(CoreObject::CoroutineWrapperType)), out, args[0]);
}
void wrapper_iter(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
{
    out.ok(args[0]);
}
template <Operation op>
void wrapper_resume(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    ReturnSlot owner(ctx);
    ctx.load(self.get(ctx), ctx.payload<SequenceIteratorObject>(self).owner, owner);
    const auto coroutine = roots.root(owner.value());
    if constexpr (op == Operation::Throw) {
        const auto tuple = roots.root(args[1]);
        resume(ctx, coroutine.get(ctx), op, ctx.payload<TupleObject>(tuple).items, out, tuple);
    } else if constexpr (op == Operation::Next) {
        const Value none[] = {ctx.builtin(CoreObject::None)};
        resume(ctx, coroutine.get(ctx), Operation::Send, none, out);
    } else
        resume(ctx, coroutine.get(ctx), op, args.subspan(1), out);
}
} // namespace
void GeneratorObject::finalize(Attachment &context, Handle self, ReturnSlot &out)
{
    // Async-generator finalizer hooks and event-loop shutdown are not installed yet.
    if (context.type_of(self) == context.builtin(CoreObject::AsyncGeneratorType)) {
        out.ok(context.builtin(CoreObject::None));
        return;
    }
    resume(context, self.get(context), Operation::Close, {}, out);
}
} // namespace cxxpython::core

namespace cxxpython::core::detail {
// Based on CPython 3.12.14 Objects/genobject.c (PSF license).
void normalize_generator_throw(Attachment &context, std::span<const Value> args, ReturnSlot &out)
{
    normalize_throw(context, args, out);
}

void make_generator(Attachment &context, Handle frame, ReturnSlot &out)
{
    RootScope roots(context);
    const auto rooted_frame = roots.root(frame.get(context));
    const auto code = roots.root(context.payload<FrameObject>(rooted_frame).code);
    const auto &metadata =
        context.payload<FunctionObject>(context.payload<FrameObject>(rooted_frame).function);
    ReturnSlot loaded(context);
    context.load(context.payload<FrameObject>(rooted_frame).function, metadata.name, loaded);
    const auto name = roots.root(loaded.value());
    context.load(context.payload<FrameObject>(rooted_frame).function, metadata.qualname, loaded);
    const auto qualname = roots.root(loaded.value());
    const auto &program = context.payload<CodeObject>(code).program;
    const auto type =
        program.is_coroutine()
            ? (program.is_generator() ? CoreObject::AsyncGeneratorType : CoreObject::CoroutineType)
            : CoreObject::GeneratorType;
    context.allocate_instance<GeneratorObject>(roots.root(context.builtin(type)), out,
                                               rooted_frame.get(context), code.get(context),
                                               name.get(context), qualname.get(context));
}

void clear_generator_frame(Attachment &context, Handle frame, ReturnSlot &out)
{
    // Reuse close's actual execution claim, cleanup and exception protocol.
    // A competing resume is an ordinary clear() error, not an ignored finalizer.
    if (close_frame(context, frame, true, out) && out.tag() == ResultTag::Raised) {
        RootScope roots(context);
        write_unraisable(context, roots.root(out.exception()), frame);
        out.ok(context.builtin(CoreObject::None));
    }
}

void get_awaitable(Attachment &context, Handle value, ReturnSlot &out)
{
    RootScope roots(context);
    const auto method = type_attribute(context, context.type_of(value), U"__await__");
    if (!method) {
        raise_error(context, CoreObject::TypeErrorType,
                    U"object cannot be used in 'await' expression", out);
        return;
    }
    invoke_special(context, value, roots.root(*method), {}, out);
    if (out.tag() == ResultTag::Raised)
        return;
    const auto iterator = roots.root(out.value());
    const auto next =
        resolve_protocol(context, roots.root(context.type_of(iterator)), Protocol::Next, roots);
    if (next.state == SlotState::Missing ||
        context.type_of(iterator) == context.builtin(CoreObject::CoroutineType))
        raise_error(context, CoreObject::TypeErrorType, U"__await__() returned non-iterator", out);
}

void generator_stop(Attachment &context, std::optional<Handle> value, ReturnSlot &out)
{
    if (!value || value->get(context) == context.builtin(CoreObject::None)) {
        stop_iteration(context, out);
        return;
    }
    RootScope roots(context);
    const auto item = roots.root(value->get(context));
    const Value arguments[] = {item.get(context)};
    call(context, roots.root(context.builtin(CoreObject::StopIterationType)), {arguments}, out);
    if (out.tag() == ResultTag::Ok)
        out.raised(out.value());
}

void install_generators(Attachment &context, std::span<const Handle> core)
{
    const auto owner = core[static_cast<std::size_t>(CoreObject::GeneratorType)];
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    SignatureLayout variable = unary;
    variable.varargs = U"args";
    install_native_attribute(context, core, owner, U"__iter__", unary, iter);
    install_native_attribute(context, core, owner, U"__next__", unary, next);
    install_native_attribute(context, core, owner, U"send", pair, send);
    install_native_attribute(context, core, owner, U"throw", variable, throw_);
    install_native_attribute(context, core, owner, U"close", unary, close);
    install_native_attribute(context, core, owner, U"__repr__", unary, repr_generator);
    install_native_field(context, core, owner, U"gi_code", get_field<Field::Code>);
    install_native_field(context, core, owner, U"gi_frame", get_field<Field::Frame>);
    install_native_field(context, core, owner, U"gi_running", get_field<Field::Running>);
    install_native_field(context, core, owner, U"gi_suspended", get_field<Field::Suspended>);
    install_native_field(context, core, owner, U"gi_yieldfrom", get_field<Field::YieldFrom>);
    install_native_field(context, core, owner, U"__name__", get_field<Field::Name>,
                         set_name<Field::Name>);
    install_native_field(context, core, owner, U"__qualname__", get_field<Field::Qualname>,
                         set_name<Field::Qualname>);
    const auto coroutine = core[static_cast<std::size_t>(CoreObject::CoroutineType)];
    install_native_attribute(context, core, coroutine, U"__await__", unary, await_wrapper);
    install_native_attribute(context, core, coroutine, U"send", pair, send);
    install_native_attribute(context, core, coroutine, U"throw", variable, throw_);
    install_native_attribute(context, core, coroutine, U"close", unary, close);
    install_native_attribute(context, core, coroutine, U"__repr__", unary, repr_generator);
    install_native_field(context, core, coroutine, U"cr_code", get_field<Field::Code>);
    install_native_field(context, core, coroutine, U"cr_frame", get_field<Field::Frame>);
    install_native_field(context, core, coroutine, U"cr_running", get_field<Field::Running>);
    install_native_field(context, core, coroutine, U"cr_suspended", get_field<Field::Suspended>);
    install_native_field(context, core, coroutine, U"cr_await", get_field<Field::YieldFrom>);
    install_native_field(context, core, coroutine, U"__name__", get_field<Field::Name>,
                         set_name<Field::Name>);
    install_native_field(context, core, coroutine, U"__qualname__", get_field<Field::Qualname>,
                         set_name<Field::Qualname>);
    const auto wrapper = core[static_cast<std::size_t>(CoreObject::CoroutineWrapperType)];
    install_native_attribute(context, core, wrapper, U"__iter__", unary, wrapper_iter);
    install_native_attribute(context, core, wrapper, U"__next__", unary,
                             wrapper_resume<Operation::Next>);
    install_native_attribute(context, core, wrapper, U"send", pair,
                             wrapper_resume<Operation::Send>);
    install_native_attribute(context, core, wrapper, U"throw", variable,
                             wrapper_resume<Operation::Throw>);
    install_native_attribute(context, core, wrapper, U"close", unary,
                             wrapper_resume<Operation::Close>);
}
} // namespace cxxpython::core::detail
