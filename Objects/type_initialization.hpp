#pragma once

#include "cxxpython/object.hpp"

namespace cxxpython
{
    void initialize_object_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_type_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_code_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_frame_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_native_function_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_method_descriptor_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_function_type_descriptors(Runtime &runtime, TypeObject *type);
    void initialize_exception_type_descriptors(Runtime &runtime, TypeObject *type);

    void initialize_object_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_type_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_int_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_str_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_list_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_slice_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_code_type_methods(Runtime &runtime, TypeObject *type);
    void initialize_property_type_methods(Runtime &runtime, TypeObject *type);

    void add_slot_wrapper(TypeObject *type,
                          const char *name,
                          SlotWrapperKind kind,
                          BinaryOp binary_op = BinaryOp::Add,
                          UnaryOp unary_op = UnaryOp::Positive,
                          RichCompareOp rich_compare_op = RichCompareOp::Equal);
    void add_common_object_slot_wrappers(TypeObject *type, bool hashable);
    void add_order_slot_wrappers(TypeObject *type);
    void add_numeric_slot_wrappers(TypeObject *type);
    void add_sequence_slot_wrappers(TypeObject *type, bool is_mutable);

    void initialize_object_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_none_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_not_implemented_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_int_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_bool_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_str_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_list_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_tuple_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_slice_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_dict_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_mappingproxy_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_iterator_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_range_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_range_iterator_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_enumerate_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_zip_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_map_slot_wrappers(Runtime &runtime, TypeObject *type);
    void initialize_filter_slot_wrappers(Runtime &runtime, TypeObject *type);
}
