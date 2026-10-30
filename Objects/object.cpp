#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"

#include <algorithm>
#include <unordered_set>

namespace cxxpython::core {
bool read_cell(Attachment &context, Value cell, ReturnSlot &out)
{
    context.load(cell, context.payload<CellObject>(cell).contents, out);
    return out.tag() == ResultTag::Ok;
}
void write_cell(Attachment &context, Value cell, std::optional<Value> value)
{
    context.store(cell, context.payload<CellObject>(cell).contents, value);
}

namespace {
#include "unicode_identifiers.inc"

bool in_ranges(std::span<const std::pair<char32_t, char32_t>> ranges, char32_t point)
{
    auto next = std::upper_bound(ranges.begin(), ranges.end(), point,
                                 [](auto value, auto range) { return value < range.first; });
    return next != ranges.begin() && point <= (next - 1)->second;
}
template <class String> std::size_t string_bytes(const String &text) noexcept
{
    auto data = reinterpret_cast<std::uintptr_t>(text.data());
    auto start = reinterpret_cast<std::uintptr_t>(&text);
    return data >= start && data - start < sizeof(text)
               ? 0
               : (text.capacity() + 1) * sizeof(typename String::value_type);
}
} // namespace
bool is_identifier(std::u32string_view text)
{
    if (text.empty() || (text.front() != U'_' && !in_ranges(xid_start, text.front())))
        return false;
    for (auto point : text.substr(1))
        if (!in_ranges(xid_continue, point))
            return false;
    return true;
}
std::size_t StrObject::external_bytes() const noexcept
{
    return string_bytes(value);
}
std::size_t SignatureLayout::external_bytes() const noexcept
{
    auto bytes = parameters.capacity() * sizeof(Parameter);
    for (const auto &parameter : parameters)
        bytes += string_bytes(parameter.name);
    if (varargs)
        bytes += string_bytes(*varargs);
    if (varkwargs)
        bytes += string_bytes(*varkwargs);
    return bytes;
}
std::size_t NativeFunctionObject::external_bytes() const noexcept
{
    return string_bytes(name) + signature.external_bytes() + captures.capacity() * sizeof(Value);
}
void ExceptionObject::trace(Tracer &tracer) const noexcept
{
    tracer.visit(args);
    tracer.visit(traceback);
    tracer.visit(cause);
    tracer.visit(context);
}
void SignatureLayout::trace(Tracer &tracer) const noexcept
{
    for (const auto &parameter : parameters)
        if (parameter.default_value)
            tracer.visit(*parameter.default_value);
}
void NativeFunctionObject::trace(Tracer &tracer) const noexcept
{
    signature.trace(tracer);
    if (sequence_owner)
        tracer.visit(*sequence_owner);
    if (bound_self)
        tracer.visit(*bound_self);
    for (auto value : captures)
        tracer.visit(value);
}
void SignatureLayout::validate() const
{
    std::unordered_set<std::u32string> names;
    ParameterKind previous = ParameterKind::PositionalOnly;
    bool default_seen = false;
    for (const auto &parameter : parameters) {
        if (parameter.name.empty() || !names.insert(parameter.name).second ||
            parameter.kind < previous)
            throw std::invalid_argument("invalid native signature parameter order or name");
        if (parameter.kind != ParameterKind::KeywordOnly) {
            if (default_seen && !parameter.default_value)
                throw std::invalid_argument("required positional parameter follows a default");
            default_seen |= parameter.default_value.has_value();
        }
        previous = parameter.kind;
    }
    for (const auto *name : {&varargs, &varkwargs})
        if (*name && ((**name).empty() || !names.insert(**name).second))
            throw std::invalid_argument("duplicate or empty variadic parameter name");
}

void make_str(Attachment &context, std::u32string value, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(context.builtin(CoreObject::StrType));
    for (auto point : value)
        if (point > 0x10ffff)
            throw std::invalid_argument("invalid Unicode code point");
    context.allocate_instance<StrObject>(type, out, std::move(value));
}
void make_tuple(Attachment &context, std::span<const Value> values, ReturnSlot &out)
{
    RootScope roots(context);
    for (auto value : values)
        roots.root(value);
    auto type = roots.root(context.builtin(CoreObject::TupleType));
    context.allocate_instance<TupleObject>(type, out,
                                           std::vector<Value>(values.begin(), values.end()));
}
void validate_exception(Attachment &context, Value exception)
{
    if (!context.has_layout<ExceptionObject>(exception) ||
        !is_subtype(context, context.type_of(exception),
                    context.builtin(CoreObject::BaseExceptionType)))
        throw std::logic_error("Raised requires a BaseException instance");
}
void raise_error(Attachment &context, CoreObject type, std::u32string message, ReturnSlot &out)
{
    out.reset(context);
    try {
        RootScope roots(context);
        ReturnSlot temporary(context);
        make_str(context, std::move(message), temporary);
        auto text = roots.root(temporary.value());
        std::array args{text.get(context)};
        cxxpython::core::make_tuple(context, args, temporary);
        auto arguments = roots.root(temporary.value());
        auto exception_type = roots.root(context.builtin(type));
        context.allocate_instance<ExceptionObject>(exception_type, temporary,
                                                   arguments.get(context));
        detail::link_exception_context(context, temporary.value());
        out.raised(temporary.value());
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
}
void make_native_function(Attachment &context, std::string name, SignatureLayout signature,
                          NativeEntry entry, std::span<const Value> captures, ReturnSlot &out,
                          NativeFunctionBinding binding)
{
    RootScope roots(context);
    for (const auto &parameter : signature.parameters)
        if (parameter.default_value)
            roots.root(*parameter.default_value);
    for (auto capture : captures)
        roots.root(capture);
    auto type = roots.root(context.builtin(CoreObject::NativeFunctionType));
    detail::allocate_function(context, type, std::move(name), std::move(signature), entry,
                              {captures.begin(), captures.end()}, out, {}, binding);
}
void make_method_descriptor(Attachment &context, Handle owner, Handle function, ReturnSlot &out)
{
    RootScope roots(context);
    auto type = roots.root(context.builtin(CoreObject::MethodDescriptorType));
    (void)context.payload<TypeObject>(owner);
    if (context.type_of(function) != context.builtin(CoreObject::NativeFunctionType))
        throw std::invalid_argument("method descriptor requires an exact native function");
    context.allocate_instance<MethodDescriptorObject>(type, out, function.get(context),
                                                      owner.get(context));
}

namespace detail {
void allocate_function(Attachment &context, Handle type, std::string name,
                       SignatureLayout signature, NativeEntry entry, std::vector<Value> captures,
                       ReturnSlot &out, std::optional<Value> sequence_owner,
                       NativeFunctionBinding binding, std::optional<Value> bound_self)
{
    out.reset(context);
    signature.validate();
    if (!entry)
        throw std::invalid_argument("native function requires an entrypoint");
    context.allocate_instance<NativeFunctionObject>(
        type, out, std::move(name), std::move(signature), entry, std::move(captures),
        sequence_owner, binding, bound_self);
}
} // namespace detail

} // namespace cxxpython::core
