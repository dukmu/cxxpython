#include "cxxpython/protocol.hpp"
#include "internal/compiler.hpp"
#include "internal/import.hpp"

#include <deque>
#include <set>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

using namespace cxxpython::core;

struct CxxPyObject {
    CxxPyContext *owner;
    Handle root;
};
struct CxxPyContext {
    explicit CxxPyContext(Attachment &actor) : actor(actor), roots(actor), error(actor) {}
    Attachment &actor;
    RootScope roots;
    ReturnSlot error;
    std::deque<CxxPyObject> handles;
    bool parked = false, invalid_wait_api = false;

    Handle get(CxxPyHandle handle)
    {
        if (!handle || handle->owner != this)
            throw std::invalid_argument("handle belongs to another callback");
        return handle->root;
    }
    CxxPyHandle hold(Value value)
    {
        const auto root = roots.root(value);
        handles.push_back({this, root});
        return &handles.back();
    }
    CxxPyHandle publish(ReturnSlot &result)
    {
        if (result.tag() == ResultTag::Raised) {
            error.raised(result.exception());
            return nullptr;
        }
        return hold(result.value());
    }
};

namespace {
std::u32string text(const char *name)
{
    if (!name)
        throw std::invalid_argument("native name is NULL");
    return detail::decode_filename(SourceBuffer("", name));
}
void failure(CxxPyContext &context, CoreObject type, const char32_t *message) noexcept
{
    try {
        raise_error(context.actor, type, message, context.error);
    } catch (...) {
        context.error.raised(context.actor.builtin(CoreObject::MemoryError));
    }
}
template <class Result, class F>
Result api_call(CxxPyContext *context, Result failed, F &&operation) noexcept
{
    if (context->parked) {
        context->invalid_wait_api = true;
        return failed;
    }
    if (context->error.tag() == ResultTag::Raised)
        return failed;
    try {
        return operation();
    } catch (const std::bad_alloc &) {
        context->error.raised(context->actor.builtin(CoreObject::MemoryError));
    } catch (...) {
        failure(*context, CoreObject::RuntimeErrorType, U"invalid native ABI operation");
    }
    return failed;
}
CxxPyHandle builtin(CxxPyContext *context, const char *name)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        ReturnSlot key(context->actor), result(context->actor);
        make_str(context->actor, text(name), key);
        dict_lookup(context->actor,
                    context->roots.root(context->actor.builtin(CoreObject::Builtins)),
                    context->roots.root(key.value()), result);
        if (result.tag() == ResultTag::Empty) {
            failure(*context, CoreObject::NameErrorType, U"native builtin not found");
            return CxxPyHandle{};
        }
        return context->publish(result);
    });
}
CxxPyHandle from_int(CxxPyContext *context, std::int64_t number)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        ReturnSlot out(context->actor);
        make_int(context->actor, number, out);
        return context->publish(out);
    });
}
int as_int(CxxPyContext *context, CxxPyHandle object, std::int64_t *number)
{
    return api_call(context, -1, [&] {
        ReturnSlot out(context->actor);
        index(context->actor, context->get(object), out);
        if (out.tag() == ResultTag::Raised) {
            context->error.raised(out.exception());
            return -1;
        }
        if (!int_fits_signed(context->actor, out.value(), *number)) {
            failure(*context, CoreObject::OverflowErrorType, U"integer does not fit int64");
            return -1;
        }
        return 0;
    });
}
CxxPyHandle from_string(CxxPyContext *context, const std::uint32_t *points, std::size_t size)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        std::u32string value;
        value.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            if (points[i] > 0x10ffff) {
                failure(*context, CoreObject::ValueErrorType, U"invalid Unicode code point");
                return CxxPyHandle{};
            }
            value.push_back(static_cast<char32_t>(points[i]));
        }
        ReturnSlot out(context->actor);
        make_str(context->actor, std::move(value), out);
        return context->publish(out);
    });
}
CxxPyHandle get_attr(CxxPyContext *context, CxxPyHandle object, const char *name)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        ReturnSlot key(context->actor), out(context->actor);
        make_str(context->actor, text(name), key);
        get_attribute(context->actor, context->get(object), context->roots.root(key.value()), out);
        return context->publish(out);
    });
}
int set_attr(CxxPyContext *context, CxxPyHandle object, const char *name, CxxPyHandle value)
{
    return api_call(context, -1, [&] {
        ReturnSlot key(context->actor), out(context->actor);
        make_str(context->actor, text(name), key);
        set_attribute(context->actor, context->get(object), context->roots.root(key.value()),
                      context->get(value), out);
        if (out.tag() == ResultTag::Raised) {
            context->error.raised(out.exception());
            return -1;
        }
        return 0;
    });
}
CxxPyHandle invoke(CxxPyContext *context, CxxPyHandle function, const CxxPyHandle *args,
                   std::size_t count)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        std::vector<Value> values;
        values.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            values.push_back(context->get(args[i]).get(context->actor));
        ReturnSlot out(context->actor);
        call(context->actor, context->get(function), {values}, out);
        return context->publish(out);
    });
}
CxxPyHandle raised(CxxPyContext *context, CxxPyHandle exception)
{
    return api_call<CxxPyHandle>(context, nullptr, [&] {
        const auto value = context->get(exception).get(context->actor);
        validate_exception(context->actor, value);
        context->error.raised(value);
        return CxxPyHandle{};
    });
}
int poll(CxxPyContext *context)
{
    return api_call(context, -1, [&] {
        context->actor.poll();
        return 0;
    });
}
int wait(CxxPyContext *context, void (*callback)(void *), void *data)
{
    return api_call(context, -1, [&] {
        context->parked = true;
        try {
            NativeWait parked(context->actor);
            callback(data);
        } catch (...) {
            context->parked = false;
            throw;
        }
        context->parked = false;
        if (context->invalid_wait_api) {
            failure(*context, CoreObject::RuntimeErrorType, U"Python API used inside native wait");
            return -1;
        }
        return 0;
    });
}
const CxxPyApi api{CXXPY_ABI_VERSION, sizeof(CxxPyApi), builtin, from_int, as_int, from_string,
                   get_attr,          set_attr,         invoke,  raised,   poll,   wait};

void execute(Attachment &actor, CxxPyEntry entry, Handle module, std::span<const Value> arguments,
             ReturnSlot &out)
{
    CxxPyContext context(actor);
    try {
        const auto owner = context.hold(module.get(actor));
        std::vector<CxxPyHandle> args;
        args.reserve(arguments.size());
        for (auto value : arguments)
            args.push_back(context.hold(value));
        const auto result = entry(&api, &context, owner, args.data(), args.size());
        if (context.error.tag() == ResultTag::Raised)
            out.raised(context.error.exception());
        else if (!result)
            raise_error(actor, CoreObject::RuntimeErrorType,
                        U"native callback returned NULL without an exception", out);
        else
            out.ok(context.get(result).get(actor));
    } catch (const std::bad_alloc &) {
        out.raised(actor.builtin(CoreObject::MemoryError));
    } catch (...) {
        raise_error(actor, CoreObject::RuntimeErrorType, U"exception crossed native C ABI", out);
    }
}
struct NativeBinding {
    ObjectHeader header;
    const Value module;
    const std::shared_ptr<const detail::NativeModule> definition;
    const std::size_t method;
    NativeBinding(InitialType type, Value module,
                  std::shared_ptr<const detail::NativeModule> definition, std::size_t method)
        : header(type), module(module), definition(std::move(definition)), method(method)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(module);
    }
};
void bound_method(Attachment &actor, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    const auto capture = actor.payload<NativeFunctionObject>(function).captures[0];
    const auto &binding = actor.payload<NativeBinding>(capture);
    RootScope roots(actor);
    execute(actor, binding.definition->methods[binding.method].entry, roots.root(binding.module),
            args, out);
}
std::shared_ptr<detail::NativeModule> copy_definition(const CxxPyModuleDef &definition)
{
    if (definition.abi_version != CXXPY_ABI_VERSION ||
        definition.struct_size < sizeof(CxxPyModuleDef))
        throw std::invalid_argument("incompatible cxxpython extension ABI");
    if (!definition.exec || (definition.method_count && !definition.methods))
        throw std::invalid_argument("invalid native module definition");
    auto result = std::make_shared<detail::NativeModule>();
    result->name = text(definition.name);
    if (result->name.empty())
        throw std::invalid_argument("empty native module name");
    for (std::size_t begin = 0; begin < result->name.size();) {
        const auto end = result->name.find(U'.', begin);
        if (!is_identifier(
                result->name.substr(begin, end == result->name.npos ? end : end - begin)))
            throw std::invalid_argument("invalid native module name");
        if (end == result->name.npos)
            break;
        begin = end + 1;
        if (begin == result->name.size())
            throw std::invalid_argument("invalid native module name");
    }
    result->exec = definition.exec;
    std::set<std::u32string> names;
    for (std::size_t i = 0; i < definition.method_count; ++i) {
        const auto &method = definition.methods[i];
        auto name = text(method.name);
        if (!method.entry || !is_identifier(name) || !names.insert(name).second)
            throw std::invalid_argument("invalid or duplicate native method");
        result->methods.push_back({std::move(name), method.positional_arity, method.entry});
    }
    return result;
}
} // namespace

namespace cxxpython::core::detail {
struct NativeLibrary {
    void *handle;
    explicit NativeLibrary(void *handle) : handle(handle) {}
    ~NativeLibrary()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (handle)
            dlclose(handle);
#endif
    }
};
std::shared_ptr<const NativeModule> load_extension(Attachment &actor, const std::string &path,
                                                   const std::u32string &name, ReturnSlot &out)
{
    auto &state = actor.imports();
    try {
#if defined(__unix__) || defined(__APPLE__)
        NativeWait parked(actor);
        auto library = std::make_shared<NativeLibrary>(nullptr);
        library->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library->handle)
            throw std::invalid_argument(dlerror());
        const auto getter =
            reinterpret_cast<CxxPyModuleGetter>(dlsym(library->handle, "CxxPy_Module_v1"));
        if (!getter)
            throw std::invalid_argument("extension does not export CxxPy_Module_v1");
        const auto definition = getter();
        if (!definition)
            throw std::invalid_argument("extension returned NULL module definition");
        auto result = copy_definition(*definition);
        if (result->name != name)
            throw std::invalid_argument("extension module name does not match import name");
        result->library = library;
        std::list<std::shared_ptr<const NativeLibrary>> prepared{std::move(library)};
        {
            std::lock_guard lock(state.mutex);
            state.libraries.splice(state.libraries.end(), prepared);
        }
        return result;
#else
        (void)state;
        (void)path;
        (void)name;
        throw std::invalid_argument("native extension loading requires a POSIX build");
#endif
    } catch (const std::bad_alloc &) {
        out.raised(actor.builtin(CoreObject::MemoryError));
    } catch (const std::exception &error) {
        raise_error(actor, CoreObject::ImportErrorType, text(error.what()), out);
    } catch (...) {
        raise_error(actor, CoreObject::ImportErrorType, U"exception crossed native module getter",
                    out);
    }
    return {};
}
void exec_extension(Attachment &actor, const std::shared_ptr<const NativeModule> &definition,
                    Handle module, ReturnSlot &out)
{
    RootScope roots(actor);
    ReturnSlot binding(actor), function(actor), key(actor);
    for (std::size_t i = 0; i < definition->methods.size(); ++i) {
        const auto &method = definition->methods[i];
        actor.allocate<NativeBinding>(binding, module.get(actor), definition, i);
        SignatureLayout signature;
        for (std::uint32_t n = 0; n < method.arity; ++n) {
            const auto digits = std::to_string(n);
            signature.parameters.push_back({U"arg" + std::u32string(digits.begin(), digits.end()),
                                            ParameterKind::PositionalOnly});
        }
        const std::array captures{binding.value()};
        make_native_function(actor, source_utf8(method.name), std::move(signature), bound_method,
                             captures, function);
        make_str(actor, method.name, key);
        dict_store(actor, roots.root(module_dict(actor, module.get(actor))),
                   roots.root(key.value()), roots.root(function.value()), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    execute(actor, definition->exec, module, {}, out);
}
} // namespace cxxpython::core::detail

namespace cxxpython::core {
ResultView register_c_module(Attachment &actor, const CxxPyModuleDef &definition, ReturnSlot &out)
{
    out.reset(actor);
    try {
        auto native = copy_definition(definition);
        auto name = native->name;
        detail::register_definition(
            actor, std::move(name),
            std::make_shared<const detail::ModuleDefinition>(
                detail::ModuleDefinition{std::move(native), false, "built-in", {}}));
        out.ok(actor.builtin(CoreObject::None));
    } catch (const std::bad_alloc &) {
        out.raised(actor.builtin(CoreObject::MemoryError));
    } catch (const std::exception &error) {
        raise_error(actor, CoreObject::ImportErrorType, text(error.what()), out);
    }
    return out.result();
}
} // namespace cxxpython::core
