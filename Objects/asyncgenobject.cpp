#include "cxxpython/asyncgenobject.hpp"

#include <exception>

namespace cxxpython::core {
namespace {
bool is_exception(Attachment &ctx, Value value, CoreObject type)
{
    return is_subtype(ctx, ctx.type_of(value), ctx.builtin(type));
}
bool check_generator(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<GeneratorObject>(value) &&
        ctx.type_of(value) == ctx.builtin(CoreObject::AsyncGeneratorType))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType,
                U"async generator descriptor requires an async generator", out);
    return false;
}
bool check_request(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.has_layout<AsyncGenRequestObject>(value) &&
        (ctx.type_of(value) == ctx.builtin(CoreObject::AsyncGenASendType) ||
         ctx.type_of(value) == ctx.builtin(CoreObject::AsyncGenAThrowType)))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType,
                U"async generator request descriptor requires a request", out);
    return false;
}
void stop_async(Attachment &ctx, ReturnSlot &out)
{
    RootScope roots(ctx);
    detail::normalize_exception(ctx, roots.root(ctx.builtin(CoreObject::StopAsyncIterationType)),
                                out);
    if (out.tag() == ResultTag::Ok)
        out.raised(out.value());
}
void convert_stop(Attachment &ctx, ReturnSlot &out)
{
    if (out.tag() != ResultTag::Raised)
        return;
    const bool async = is_exception(ctx, out.exception(), CoreObject::StopAsyncIterationType);
    if (!async && !is_exception(ctx, out.exception(), CoreObject::StopIterationType))
        return;
    RootScope roots(ctx);
    const auto cause = roots.root(out.exception());
    ReturnSlot error(ctx), key(ctx), changed(ctx);
    raise_error(ctx, CoreObject::RuntimeErrorType,
                async ? U"async generator raised StopAsyncIteration"
                      : U"async generator raised StopIteration",
                error);
    const auto replacement = roots.root(error.exception());
    for (const auto *name : {U"__cause__", U"__context__"}) {
        make_str(ctx, name, key);
        if (key.tag() == ResultTag::Raised) {
            out.raised(key.exception());
            return;
        }
        set_attribute(ctx, replacement, roots.root(key.value()), cause, changed);
        if (changed.tag() == ResultTag::Raised) {
            out.raised(changed.exception());
            return;
        }
    }
    out.raised(replacement.get(ctx));
}
struct ExecutionGuard {
    std::atomic<bool> &executing;
    ~ExecutionGuard()
    {
        executing.store(false, std::memory_order_release);
    }
};
struct FrameClaim {
    FrameObject &frame;
    FrameState previous;
    bool active = true;
    ~FrameClaim()
    {
        if (active)
            publish(previous);
    }
    void publish(FrameState state)
    {
        frame.owner.store(std::thread::id{}, std::memory_order_relaxed);
        frame.state.store(state, std::memory_order_release);
        active = false;
    }
};
void finish(AsyncGenRequestObject &request, GeneratorObject &gen)
{
    request.state.store(AsyncGenRequestState::Closed, std::memory_order_release);
    gen.async_running.store(false, std::memory_order_release);
}
struct RequestFailureGuard {
    AsyncGenRequestObject &request;
    GeneratorObject &generator;
    const int exceptions = std::uncaught_exceptions();
    ~RequestFailureGuard()
    {
        if (std::uncaught_exceptions() > exceptions &&
            request.state.load(std::memory_order_acquire) == AsyncGenRequestState::Active)
            finish(request, generator);
    }
};

// The request owns async_running across inner await yields, but never holds a lock
// across a Python call or safepoint. FrameState protects the VM's writer-owned data.
void request_resume(Attachment &ctx, Value self_value, Value sent_value,
                    std::optional<Handle> throw_args, ReturnSlot &out)
{
    if (!check_request(ctx, self_value, out))
        return;
    RootScope roots(ctx);
    const auto self = roots.root(self_value);
    const auto sent = roots.root(sent_value);
    auto &request = ctx.payload<AsyncGenRequestObject>(self);
    bool expected = false;
    if (!request.executing.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"async generator request already executing",
                    out);
        return;
    }
    ExecutionGuard execution{request.executing};
    const auto generator = roots.root(request.generator);
    auto &gen = ctx.payload<GeneratorObject>(generator);
    RequestFailureGuard failure{request, gen};
    auto state = request.state.load(std::memory_order_acquire);
    if (state == AsyncGenRequestState::Closed) {
        raise_error(ctx, CoreObject::RuntimeErrorType,
                    request.throwing ? U"cannot reuse already awaited aclose()/athrow()"
                                     : U"cannot reuse already awaited __anext__()/asend()",
                    out);
        return;
    }
    ReturnSlot loaded(ctx);
    ctx.load(generator.get(ctx), gen.frame, loaded);
    if (loaded.tag() == ResultTag::Empty) {
        request.state.store(AsyncGenRequestState::Closed, std::memory_order_release);
        if (state == AsyncGenRequestState::Active)
            gen.async_running.store(false, std::memory_order_release);
        if (request.throwing)
            detail::generator_stop(ctx, {}, out);
        else
            stop_async(ctx, out);
        return;
    }
    const auto frame = roots.root(loaded.value());
    auto &data = ctx.payload<FrameObject>(frame);
    if (state == AsyncGenRequestState::Initial) {
        if (request.throwing && !throw_args && sent.get(ctx) != ctx.builtin(CoreObject::None)) {
            raise_error(ctx, CoreObject::RuntimeErrorType,
                        U"can't send non-None value to a just-started coroutine", out);
            return;
        }
        expected = false;
        if (!gen.async_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            request.state.store(AsyncGenRequestState::Closed, std::memory_order_release);
            raise_error(ctx, CoreObject::RuntimeErrorType,
                        request.closing    ? U"aclose(): asynchronous generator is already running"
                        : request.throwing ? U"athrow(): asynchronous generator is already running"
                                           : U"anext(): asynchronous generator is already running",
                        out);
            return;
        }
        if (request.throwing && gen.async_closed.load(std::memory_order_acquire)) {
            gen.async_running.store(false, std::memory_order_release);
            request.state.store(AsyncGenRequestState::Closed, std::memory_order_release);
            stop_async(ctx, out);
            return;
        }
        if (request.closing)
            gen.async_closed.store(true, std::memory_order_release);
        request.state.store(AsyncGenRequestState::Active, std::memory_order_release);
    }
    auto frame_state = data.state.load(std::memory_order_acquire);
    for (;;) {
        if (frame_state == FrameState::Running) {
            finish(request, gen);
            raise_error(ctx, CoreObject::ValueErrorType, U"async generator already executing", out);
            return;
        }
        if (frame_state == FrameState::Completed) {
            finish(request, gen);
            if (request.throwing)
                detail::generator_stop(ctx, {}, out);
            else
                stop_async(ctx, out);
            return;
        }
        if (data.state.compare_exchange_weak(frame_state, FrameState::Running,
                                             std::memory_order_acquire, std::memory_order_acquire))
            break;
    }
    data.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    FrameClaim claim{data, frame_state};
    const auto none = ctx.builtin(CoreObject::None);
    Value input = sent.get(ctx);
    if (state == AsyncGenRequestState::Initial && !request.throwing && !throw_args && input == none)
        input = request.argument;
    const auto input_root = roots.root(input);
    if (!throw_args && !(request.throwing && state == AsyncGenRequestState::Initial) &&
        frame_state == FrameState::Created && input != none) {
        finish(request, gen);
        raise_error(ctx, CoreObject::TypeErrorType,
                    U"can't send non-None value to a just-started async generator", out);
        return;
    }
    ReturnSlot thrown(ctx);
    std::optional<Handle> exception;
    std::optional<Handle> arguments = throw_args;
    if (!arguments && request.throwing && state == AsyncGenRequestState::Initial &&
        !request.closing)
        arguments = roots.root(request.argument);
    if (request.closing && state == AsyncGenRequestState::Initial && !arguments) {
        detail::normalize_exception(ctx, roots.root(ctx.builtin(CoreObject::GeneratorExitType)),
                                    thrown);
        if (thrown.tag() == ResultTag::Raised) {
            finish(request, gen);
            out.raised(thrown.exception());
            return;
        }
        exception = roots.root(thrown.value());
    } else if (arguments) {
        // A delegate receives the original throw arguments, just as for generators.
        ReturnSlot delegate(ctx);
        ctx.load(frame.get(ctx), data.delegate, delegate);
        if (delegate.tag() == ResultTag::Empty) {
            detail::normalize_generator_throw(ctx, ctx.payload<TupleObject>(*arguments).items,
                                              thrown);
            if (thrown.tag() == ResultTag::Raised) {
                finish(request, gen);
                out.raised(thrown.exception());
                return;
            }
            exception = roots.root(thrown.value());
        }
    }
    detail::ResumeResult result;
    try {
        result = detail::resume_generator_frame(ctx, frame, input_root, exception, out, arguments);
    } catch (...) {
        claim.publish(FrameState::Completed);
        ctx.store(generator.get(ctx), gen.frame, {});
        finish(request, gen);
        throw;
    }
    const bool suspended =
        result == detail::ResumeResult::Yielded || result == detail::ResumeResult::Rejected;
    claim.publish(suspended ? FrameState::Suspended : FrameState::Completed);
    if (!suspended)
        ctx.store(generator.get(ctx), gen.frame, {});
    if (result == detail::ResumeResult::Yielded &&
        ctx.has_layout<AsyncGenWrappedValueObject>(out.value()) &&
        ctx.type_of(out.value()) == ctx.builtin(CoreObject::AsyncGenWrappedValueType)) {
        const auto wrapped = roots.root(out.value());
        const auto value = roots.root(ctx.payload<AsyncGenWrappedValueObject>(wrapped).value);
        finish(request, gen);
        if (request.closing)
            raise_error(ctx, CoreObject::RuntimeErrorType, U"async generator ignored GeneratorExit",
                        out);
        else
            detail::generator_stop(ctx, value, out);
    } else if (result == detail::ResumeResult::Returned) {
        gen.async_closed.store(true, std::memory_order_release);
        finish(request, gen);
        if (request.closing)
            detail::generator_stop(ctx, {}, out);
        else
            stop_async(ctx, out);
    } else if (result == detail::ResumeResult::Raised || result == detail::ResumeResult::Rejected) {
        if (result == detail::ResumeResult::Raised && out.tag() == ResultTag::Raised &&
            is_exception(ctx, out.exception(), CoreObject::GeneratorExitType))
            gen.async_closed.store(true, std::memory_order_release);
        finish(request, gen);
        if (request.closing && out.tag() == ResultTag::Raised &&
            (is_exception(ctx, out.exception(), CoreObject::GeneratorExitType) ||
             is_exception(ctx, out.exception(), CoreObject::StopAsyncIterationType)))
            detail::generator_stop(ctx, {}, out);
        else if (result == detail::ResumeResult::Raised)
            convert_stop(ctx, out);
    }
}

void aiter(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (check_generator(ctx, args[0], out))
        out.ok(args[0]);
}
template <bool throwing, bool closing>
void make_request(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_generator(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto gen = roots.root(args[0]);
    const auto argument =
        roots.root(closing || args.size() == 1 ? ctx.builtin(CoreObject::None) : args[1]);
    ctx.allocate_instance<AsyncGenRequestObject>(
        roots.root(
            ctx.builtin(throwing ? CoreObject::AsyncGenAThrowType : CoreObject::AsyncGenASendType)),
        out, gen.get(ctx), argument.get(ctx), throwing, closing);
}
void request_iter(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (check_request(ctx, args[0], out))
        out.ok(args[0]);
}
void request_next(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    request_resume(ctx, args[0], ctx.builtin(CoreObject::None), {}, out);
}
void request_send(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    request_resume(ctx, args[0], args[1], {}, out);
}
void request_throw(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto tuple = roots.root(args[1]);
    request_resume(ctx, args[0], ctx.builtin(CoreObject::None), tuple, out);
}
void request_close(Attachment &ctx, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_request(ctx, args[0], out))
        return;
    auto &request = ctx.payload<AsyncGenRequestObject>(args[0]);
    bool expected = false;
    if (!request.executing.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"async generator request already executing",
                    out);
        return;
    }
    ExecutionGuard execution{request.executing};
    if (request.state.exchange(AsyncGenRequestState::Closed, std::memory_order_acq_rel) ==
        AsyncGenRequestState::Active)
        ctx.payload<GeneratorObject>(request.generator)
            .async_running.store(false, std::memory_order_release);
    out.ok(ctx.builtin(CoreObject::None));
}
enum class Field { Code, Frame, Running, Await, Name, Qualname };
template <Field field> void get_field(Attachment &ctx, Value self, ReturnSlot &out)
{
    if (!check_generator(ctx, self, out))
        return;
    RootScope roots(ctx);
    const auto gen_root = roots.root(self);
    auto &gen = ctx.payload<GeneratorObject>(gen_root);
    if constexpr (field == Field::Code)
        out.ok(gen.code);
    else if constexpr (field == Field::Name || field == Field::Qualname)
        ctx.load(self, field == Field::Name ? gen.name : gen.qualname, out);
    else if constexpr (field == Field::Running)
        out.ok(ctx.builtin(gen.async_running.load(std::memory_order_acquire) ? CoreObject::True
                                                                             : CoreObject::False));
    else {
        ReturnSlot frame(ctx);
        ctx.load(self, gen.frame, frame);
        if (frame.tag() == ResultTag::Empty) {
            out.ok(ctx.builtin(CoreObject::None));
            return;
        }
        const auto frame_root = roots.root(frame.value());
        auto &data = ctx.payload<FrameObject>(frame_root);
        if (data.state.load(std::memory_order_acquire) == FrameState::Completed)
            out.ok(ctx.builtin(CoreObject::None));
        else if constexpr (field == Field::Frame)
            out.ok(frame_root.get(ctx));
        else {
            ctx.load(frame_root.get(ctx), data.delegate, out);
            if (out.tag() == ResultTag::Empty)
                out.ok(ctx.builtin(CoreObject::None));
            else if (ctx.type_of(out.value()) == ctx.builtin(CoreObject::CoroutineWrapperType)) {
                const auto wrapper = roots.root(out.value());
                ctx.load(wrapper.get(ctx), ctx.payload<SequenceIteratorObject>(wrapper).owner, out);
            }
        }
    }
}
template <Field field>
void set_name(Attachment &ctx, Value self, std::optional<Value> value, ReturnSlot &out)
{
    if (!check_generator(ctx, self, out))
        return;
    if (!value || !ctx.has_layout<StrObject>(*value) ||
        !is_subtype(ctx, ctx.type_of(*value), ctx.builtin(CoreObject::StrType))) {
        raise_error(ctx, CoreObject::TypeErrorType, U"async generator name must be a string", out);
        return;
    }
    auto &gen = ctx.payload<GeneratorObject>(self);
    ctx.store(self, field == Field::Name ? gen.name : gen.qualname, *value);
    out.ok(ctx.builtin(CoreObject::None));
}
} // namespace

namespace detail {
void install_async_generators(Attachment &ctx, std::span<const Handle> core)
{
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    SignatureLayout variable = unary;
    variable.varargs = U"args";
    const auto gen = core[static_cast<std::size_t>(CoreObject::AsyncGeneratorType)];
    install_native_attribute(ctx, core, gen, U"__aiter__", unary, aiter);
    install_native_attribute(ctx, core, gen, U"__anext__", unary, make_request<false, false>);
    install_native_attribute(ctx, core, gen, U"asend", pair, make_request<false, false>);
    install_native_attribute(ctx, core, gen, U"athrow", variable, make_request<true, false>);
    install_native_attribute(ctx, core, gen, U"aclose", unary, make_request<true, true>);
    install_native_field(ctx, core, gen, U"ag_code", get_field<Field::Code>);
    install_native_field(ctx, core, gen, U"ag_frame", get_field<Field::Frame>);
    install_native_field(ctx, core, gen, U"ag_running", get_field<Field::Running>);
    install_native_field(ctx, core, gen, U"ag_await", get_field<Field::Await>);
    install_native_field(ctx, core, gen, U"__name__", get_field<Field::Name>,
                         set_name<Field::Name>);
    install_native_field(ctx, core, gen, U"__qualname__", get_field<Field::Qualname>,
                         set_name<Field::Qualname>);
    for (auto type : {CoreObject::AsyncGenASendType, CoreObject::AsyncGenAThrowType}) {
        const auto owner = core[static_cast<std::size_t>(type)];
        install_native_attribute(ctx, core, owner, U"__await__", unary, request_iter);
        install_native_attribute(ctx, core, owner, U"__iter__", unary, request_iter);
        install_native_attribute(ctx, core, owner, U"__next__", unary, request_next);
        install_native_attribute(ctx, core, owner, U"send", pair, request_send);
        install_native_attribute(ctx, core, owner, U"throw", variable, request_throw);
        install_native_attribute(ctx, core, owner, U"close", unary, request_close);
    }
}
} // namespace detail
} // namespace cxxpython::core
