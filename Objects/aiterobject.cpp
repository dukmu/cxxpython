// Adapted from CPython 3.12.14 Objects/iterobject.c (anextawaitable),
// Objects/abstract.c (PyObject_GetAIter), and Python/bltinmodule.c (anext).
// Copyright Python Software Foundation and contributors; PSF License Version 2.
// GC edges and execution claims replace CPython's reference counting and GIL.
#include "internal/aiterobject.hpp"

namespace cxxpython::core {
namespace {
void lookup_special(Attachment &ctx, Handle type, const std::u32string &name, ReturnSlot &out)
{
    out.reset(ctx);
    if (auto value = detail::type_attribute(ctx, type.get(ctx), name))
        out.ok(*value);
}

std::u32string type_name(Attachment &ctx, Handle value)
{
    TypeView metadata(ctx, ctx.type_of(value));
    auto name = metadata.name();
    if (const auto *text = std::get_if<std::u32string>(&name))
        return *text;
    return ctx.payload<StrObject>(std::get<Value>(name)).value;
}

bool check_awaitable(Attachment &ctx, Value value, ReturnSlot &out)
{
    if (ctx.type_of(value) == ctx.builtin(CoreObject::AnextAwaitableType) &&
        ctx.has_layout<AnextAwaitableObject>(value))
        return true;
    raise_error(ctx, CoreObject::TypeErrorType,
                U"anext_awaitable descriptor requires an anext_awaitable", out);
    return false;
}

void awaitable_iter(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (check_awaitable(ctx, args[0], out))
        out.ok(args[0]);
}

struct ExecutionGuard {
    std::atomic<bool> &executing;
    ~ExecutionGuard()
    {
        executing.store(false, std::memory_order_release);
    }
};

enum class Operation { Next, Send, Throw, Close };
template <Operation operation>
void awaitable_resume(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    if (!check_awaitable(ctx, args[0], out))
        return;
    RootScope roots(ctx);
    const auto self = roots.root(args[0]);
    for (auto value : args.subspan(1))
        roots.root(value);
    auto &data = ctx.payload<AnextAwaitableObject>(self);
    bool expected = false;
    if (!data.executing.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        raise_error(ctx, CoreObject::RuntimeErrorType, U"anext(): awaitable already executing",
                    out);
        return;
    }
    ExecutionGuard execution{data.executing};
    ReturnSlot converted(ctx);
    detail::get_awaitable(ctx, roots.root(data.wrapped), converted);
    if (converted.tag() == ResultTag::Raised) {
        out.raised(converted.exception());
        return;
    }
    const auto iterator = roots.root(converted.value());
    if constexpr (operation == Operation::Next) {
        // iter_next retains StopIteration in out; it is not async exhaustion.
        iter_next(ctx, iterator, out);
    } else {
        ReturnSlot name(ctx), method(ctx);
        constexpr auto label = operation == Operation::Send    ? U"send"
                               : operation == Operation::Throw ? U"throw"
                                                               : U"close";
        make_str(ctx, label, name);
        if (name.tag() == ResultTag::Raised) {
            out.raised(name.exception());
            return;
        }
        get_attribute(ctx, iterator, roots.root(name.value()), method);
        if (method.tag() == ResultTag::Raised) {
            out.raised(method.exception());
        } else {
            const auto callable = roots.root(method.value());
            if constexpr (operation == Operation::Send)
                call(ctx, callable, {args.subspan(1)}, out);
            else
                call(ctx, callable, {ctx.payload<TupleObject>(args[1]).items}, out);
        }
    }
    if (out.tag() == ResultTag::Raised &&
        is_subtype(ctx, ctx.type_of(out.exception()),
                   ctx.builtin(CoreObject::StopAsyncIterationType)))
        detail::generator_stop(ctx, roots.root(data.default_value), out);
}
void async_iter(Attachment &ctx, Handle iterable, bool for_loop, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot method(ctx);
    lookup_special(ctx, roots.root(ctx.type_of(iterable)), U"__aiter__", method);
    if (method.tag() == ResultTag::Empty) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    for_loop
                        ? U"'async for' requires an object with __aiter__ method, got " +
                              type_name(ctx, iterable)
                        : U"'" + type_name(ctx, iterable) + U"' object is not an async iterable",
                    out);
        return;
    }
    detail::invoke_special(ctx, iterable, roots.root(method.value()), {}, out);
    if (out.tag() == ResultTag::Raised)
        return;
    const auto iterator = roots.root(out.value());
    lookup_special(ctx, roots.root(ctx.type_of(iterator)), U"__anext__", method);
    if (method.tag() == ResultTag::Empty)
        raise_error(ctx, CoreObject::TypeErrorType,
                    for_loop ? U"'async for' received an object from __aiter__ that does not "
                               U"implement __anext__: " +
                                   type_name(ctx, iterator)
                             : U"aiter() returned not an async iterator of type '" +
                                   type_name(ctx, iterator) + U"'",
                    out);
}
void async_next(Attachment &ctx, Handle iterator, bool for_loop, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot method(ctx);
    lookup_special(ctx, roots.root(ctx.type_of(iterator)), U"__anext__", method);
    if (method.tag() == ResultTag::Empty) {
        raise_error(ctx, CoreObject::TypeErrorType,
                    for_loop
                        ? U"'async for' requires an iterator with __anext__ method, got " +
                              type_name(ctx, iterator)
                        : U"'" + type_name(ctx, iterator) + U"' object is not an async iterator",
                    out);
        return;
    }
    detail::invoke_special(ctx, iterator, roots.root(method.value()), {}, out);
}
} // namespace

namespace detail {
void builtin_aiter(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    async_iter(ctx, roots.root(args[0]), false, out);
}
void get_async_iter(Attachment &ctx, Handle iterable, ReturnSlot &out)
{
    async_iter(ctx, iterable, true, out);
}
void get_async_next(Attachment &ctx, Handle iterator, ReturnSlot &out)
{
    RootScope roots(ctx);
    ReturnSlot next(ctx);
    async_next(ctx, iterator, true, next);
    if (next.tag() == ResultTag::Raised) {
        out.raised(next.exception());
        return;
    }
    const auto awaitable = roots.root(next.value());
    get_awaitable(ctx, awaitable, next);
    if (next.tag() != ResultTag::Raised) {
        out.ok(next.value());
        return;
    }
    const auto cause = roots.root(next.exception());
    raise_error(ctx, CoreObject::TypeErrorType,
                U"'async for' received an invalid object from __anext__: " +
                    type_name(ctx, awaitable),
                out);
    ctx.store(out.exception(), ctx.payload<ExceptionObject>(out.exception()).cause, cause.get(ctx));
    ctx.store(out.exception(), ctx.payload<ExceptionObject>(out.exception()).context,
              cause.get(ctx));
    ctx.payload<ExceptionObject>(out.exception())
        .suppress_context.store(true, std::memory_order_release);
}

void builtin_anext(Attachment &ctx, Value, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(ctx);
    const auto iterator = roots.root(args[0]);
    const auto defaults = roots.root(args[1]);
    const auto &values = ctx.payload<TupleObject>(defaults).items;
    if (values.size() > 1) {
        raise_error(ctx, CoreObject::TypeErrorType, U"anext expected at most 2 arguments", out);
        return;
    }
    ReturnSlot awaitable(ctx);
    async_next(ctx, iterator, false, awaitable);
    if (awaitable.tag() == ResultTag::Raised)
        out.raised(awaitable.exception());
    else if (values.empty())
        out.ok(awaitable.value());
    else
        ctx.allocate_instance<AnextAwaitableObject>(
            roots.root(ctx.builtin(CoreObject::AnextAwaitableType)), out, awaitable.value(),
            values[0]);
}

void install_async_iteration(Attachment &ctx, std::span<const Handle> core)
{
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    SignatureLayout pair = unary;
    pair.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    SignatureLayout variable = unary;
    variable.varargs = U"args";
    const auto owner = core[static_cast<std::size_t>(CoreObject::AnextAwaitableType)];
    install_native_attribute(ctx, core, owner, U"__await__", unary, awaitable_iter);
    install_native_attribute(ctx, core, owner, U"__iter__", unary, awaitable_iter);
    install_native_attribute(ctx, core, owner, U"__next__", unary,
                             awaitable_resume<Operation::Next>);
    install_native_attribute(ctx, core, owner, U"send", pair, awaitable_resume<Operation::Send>);
    install_native_attribute(ctx, core, owner, U"throw", variable,
                             awaitable_resume<Operation::Throw>);
    install_native_attribute(ctx, core, owner, U"close", variable,
                             awaitable_resume<Operation::Close>);
}
} // namespace detail
} // namespace cxxpython::core
