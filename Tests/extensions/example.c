#include "cxxpython/extension.h"

static CxxPyHandle raise_named(const CxxPyApi *api, CxxPyContext *ctx, const char *name)
{
    CxxPyHandle type = api->builtin(ctx, name);
    CxxPyHandle exception;
    if (!type)
        return NULL;
    exception = api->call(ctx, type, NULL, 0);
    return exception ? api->raise(ctx, exception) : NULL;
}

static CxxPyHandle exec_module(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                               const CxxPyHandle *args, size_t nargs)
{
    CxxPyHandle value = api->int_from_i64(ctx, 42);
    (void)args;
    (void)nargs;
    if (!value || api->set_attr(ctx, module, "value", value) < 0)
        return NULL;
    return api->builtin(ctx, "None");
}

static CxxPyHandle answer(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                          const CxxPyHandle *args, size_t nargs)
{
    (void)args;
    (void)nargs;
    return api->get_attr(ctx, module, "value");
}

static CxxPyHandle add(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                       const CxxPyHandle *args, size_t nargs)
{
    int64_t a, b;
    (void)module;
    (void)nargs;
    if (api->int_as_i64(ctx, args[0], &a) < 0 || api->int_as_i64(ctx, args[1], &b) < 0)
        return NULL;
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return raise_named(api, ctx, "OverflowError");
    return api->int_from_i64(ctx, a + b);
}

static CxxPyHandle fail(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                        const CxxPyHandle *args, size_t nargs)
{
    (void)module;
    (void)args;
    (void)nargs;
    return raise_named(api, ctx, "ValueError");
}

static CxxPyHandle empty(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                         const CxxPyHandle *args, size_t nargs)
{
    (void)api;
    (void)ctx;
    (void)module;
    (void)args;
    (void)nargs;
    return NULL;
}

static CxxPyHandle sticky(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                          const CxxPyHandle *args, size_t nargs)
{
    CxxPyHandle value = api->int_from_i64(ctx, 1);
    (void)args;
    (void)nargs;
    if (!value)
        return NULL;
    (void)api->get_attr(ctx, module, "_missing_for_sticky_test");
    return value;
}

static CxxPyHandle unicode_text(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                                const CxxPyHandle *args, size_t nargs)
{
    const uint32_t text[] = {0x41, 0, 0x1f642, 0xd800};
    (void)module;
    (void)args;
    (void)nargs;
    return api->str_from_utf32(ctx, text, sizeof(text) / sizeof(text[0]));
}

static CxxPyHandle invalid_unicode(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                                   const CxxPyHandle *args, size_t nargs)
{
    const uint32_t text[] = {0x110000};
    (void)module;
    (void)args;
    (void)nargs;
    return api->str_from_utf32(ctx, text, 1);
}

static void increment(void *data)
{
    ++*(int *)data;
}

static CxxPyHandle poll_wait(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                             const CxxPyHandle *args, size_t nargs)
{
    int value = 41;
    (void)module;
    (void)args;
    (void)nargs;
    if (api->poll(ctx) < 0 || api->wait(ctx, increment, &value) < 0)
        return NULL;
    return api->int_from_i64(ctx, value);
}

struct invalid_wait_data {
    const CxxPyApi *api;
    CxxPyContext *ctx;
};

static void poll_while_waiting(void *data)
{
    struct invalid_wait_data *host = (struct invalid_wait_data *)data;
    (void)host->api->poll(host->ctx);
}

static CxxPyHandle invalid_wait(const CxxPyApi *api, CxxPyContext *ctx, CxxPyHandle module,
                                const CxxPyHandle *args, size_t nargs)
{
    struct invalid_wait_data host = {api, ctx};
    (void)module;
    (void)args;
    (void)nargs;
    if (api->wait(ctx, poll_while_waiting, &host) < 0)
        return NULL;
    return api->int_from_i64(ctx, 1);
}

static const CxxPyMethodDef methods[] = {{"answer", 0, answer},
                                         {"add", 2, add},
                                         {"fail", 0, fail},
                                         {"empty", 0, empty},
                                         {"sticky", 0, sticky},
                                         {"unicode_text", 0, unicode_text},
                                         {"invalid_unicode", 0, invalid_unicode},
                                         {"poll_wait", 0, poll_wait},
                                         {"invalid_wait", 0, invalid_wait}};
static const CxxPyModuleDef definition = {CXXPY_ABI_VERSION, sizeof(CxxPyModuleDef),
                                          "_example",        exec_module,
                                          methods,           sizeof(methods) / sizeof(methods[0])};

CXXPY_EXPORT const CxxPyModuleDef *CxxPy_Module_v1(void)
{
    return &definition;
}
