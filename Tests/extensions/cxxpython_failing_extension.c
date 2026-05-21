#include "cxxpython/extension_api.h"

CxxPyObject *CxxPyInit_cxxpython_failing_extension(CxxPyRuntime *runtime,
                                                   const CxxPyExtensionApi *api)
{
    if (api)
        api->set_import_error(runtime, "sample extension failed during init");
    return 0;
}
