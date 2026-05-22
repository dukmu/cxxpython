#include "cxxpython/runtime.hpp"

#include "../Objects/type_initialization.hpp"

namespace cxxpython
{
    void Runtime::initialize_type_descriptors()
    {
        initialize_object_type_descriptors(*this, object_type_);
        initialize_type_type_descriptors(*this, type_type_);
        initialize_code_type_descriptors(*this, code_type_);
        initialize_native_function_type_descriptors(*this, native_function_type_);
        initialize_method_descriptor_type_descriptors(*this, method_descriptor_type_);
        initialize_function_type_descriptors(*this, function_type_);
        initialize_frame_type_descriptors(*this, frame_type_);
        initialize_exception_type_descriptors(*this, base_exception_type_);
    }
}
