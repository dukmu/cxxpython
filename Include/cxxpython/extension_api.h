#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXXPYTHON_EXTENSION_ABI_VERSION 1u

typedef struct CxxPyRuntime CxxPyRuntime;
typedef struct CxxPyObject CxxPyObject;

typedef CxxPyObject *(*CxxPyCreateModuleFunc)(CxxPyRuntime *runtime, const char *name);
typedef CxxPyObject *(*CxxPyCreateStringFunc)(CxxPyRuntime *runtime, const char *value);
typedef CxxPyObject *(*CxxPyCreateIntFunc)(CxxPyRuntime *runtime, int64_t value);
typedef int (*CxxPySetAttrFunc)(CxxPyRuntime *runtime,
                                CxxPyObject *object,
                                const char *name,
                                CxxPyObject *value);
typedef void (*CxxPySetImportErrorFunc)(CxxPyRuntime *runtime, const char *message);

typedef struct CxxPyExtensionApi
{
    uint32_t abi_version;
    CxxPyCreateModuleFunc create_module;
    CxxPyCreateStringFunc create_string;
    CxxPyCreateIntFunc create_int;
    CxxPySetAttrFunc set_attr;
    CxxPySetImportErrorFunc set_import_error;
} CxxPyExtensionApi;

typedef CxxPyObject *(*CxxPyInitFunc)(CxxPyRuntime *runtime, const CxxPyExtensionApi *api);

#ifdef __cplusplus
}
#endif
