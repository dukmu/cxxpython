#ifndef CXXPYTHON_EXTENSION_H
#define CXXPYTHON_EXTENSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXXPY_ABI_VERSION 1u
#if defined(_WIN32)
#define CXXPY_EXPORT __declspec(dllexport)
#else
#define CXXPY_EXPORT __attribute__((visibility("default")))
#endif

typedef struct CxxPyContext CxxPyContext;
typedef struct CxxPyObject *CxxPyHandle;
typedef struct CxxPyApi CxxPyApi;

/* Handles belong to one callback on its current OS thread. NULL means error;
 * every successful handle remains rooted until that callback returns. */
typedef CxxPyHandle (*CxxPyEntry)(const CxxPyApi *, CxxPyContext *, CxxPyHandle module,
                                  const CxxPyHandle *args, size_t nargs);

struct CxxPyApi {
    uint32_t abi_version;
    size_t struct_size;
    CxxPyHandle (*builtin)(CxxPyContext *, const char *name);
    CxxPyHandle (*int_from_i64)(CxxPyContext *, int64_t);
    int (*int_as_i64)(CxxPyContext *, CxxPyHandle, int64_t *);
    CxxPyHandle (*str_from_utf32)(CxxPyContext *, const uint32_t *, size_t);
    CxxPyHandle (*get_attr)(CxxPyContext *, CxxPyHandle, const char *name);
    int (*set_attr)(CxxPyContext *, CxxPyHandle, const char *name, CxxPyHandle);
    CxxPyHandle (*call)(CxxPyContext *, CxxPyHandle, const CxxPyHandle *, size_t);
    CxxPyHandle (*raise)(CxxPyContext *, CxxPyHandle exception);
    int (*poll)(CxxPyContext *);
    /* The host callback must not use Python handles or any API while parked. */
    int (*wait)(CxxPyContext *, void (*callback)(void *), void *data);
};

typedef struct CxxPyMethodDef {
    const char *name;
    uint32_t positional_arity;
    CxxPyEntry entry;
} CxxPyMethodDef;

typedef struct CxxPyModuleDef {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    CxxPyEntry exec;
    const CxxPyMethodDef *methods;
    size_t method_count;
} CxxPyModuleDef;

/* A *.cxxpy.so library exports this getter. Its definition is immutable native
 * metadata. exec runs once per initialization and returns Python None. */
typedef const CxxPyModuleDef *(*CxxPyModuleGetter)(void);
CXXPY_EXPORT const CxxPyModuleDef *CxxPy_Module_v1(void);

#ifdef __cplusplus
}
#endif
#endif
