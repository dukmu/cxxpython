#include "cxxpython/extension_api.h"

CxxPyObject *CxxPyInit_cxxpython_sample_extension(CxxPyRuntime *runtime,
                                                  const CxxPyExtensionApi *api)
{
    if (!api || api->abi_version != CXXPYTHON_EXTENSION_ABI_VERSION)
        return 0;

    CxxPyObject *module = api->create_module(runtime, "cxxpython_sample_extension");
    if (!module)
        return 0;
    if (api->set_attr(runtime, module, "answer", api->create_int(runtime, 42)) != 0)
        return 0;
    if (api->set_attr(runtime, module, "label", api->create_string(runtime, "from-extension")) != 0)
        return 0;
    return module;
}
