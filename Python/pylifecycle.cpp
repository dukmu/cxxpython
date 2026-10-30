#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "cxxpython/superobject.hpp"
#include "internal/builtinexec.hpp"
#include "internal/codecs.hpp"
#include "internal/stdio.hpp"
#include "internal/stringformat.hpp"
#include "internal/sysmodule.hpp"
#include "internal/unicodeobject.hpp"

#include <algorithm>

namespace cxxpython::core::detail {
namespace {
template <Protocol Operation>
void builtin_protocol(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    auto value = roots.root(args[0]);
    if constexpr (Operation == Protocol::Len)
        length(context, value, out);
    else if constexpr (Operation == Protocol::Repr)
        repr(context, value, out);
    else if constexpr (Operation == Protocol::Hash)
        hash(context, value, out);
}
} // namespace
void build_core_objects(Attachment &context, std::span<ReturnSlot *const> outputs)
{
    RootScope roots(context);
    std::vector<Handle> core;
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto define = [&](CoreObject id, const char *name, LayoutDescriptor layout,
                      std::optional<Handle> base = {}) {
        std::vector<Handle> bases;
        if (base)
            bases.push_back(*base);
        TypeSpec spec{name, bases};
        spec.module = U"builtins";
        if (id == CoreObject::FunctionType || id == CoreObject::ModuleType)
            spec.slots = std::vector<std::u32string>{U"__dict__"};
        if (id == CoreObject::ImportErrorType)
            spec.slots = std::vector<std::u32string>{U"name", U"path", U"msg", U"name_from"};
        if (id == CoreObject::StopIterationType)
            spec.slots = std::vector<std::u32string>{U"value"};
        if (id == CoreObject::SystemExitType)
            spec.slots = std::vector<std::u32string>{U"code"};
        if (id == CoreObject::UnicodeEncodeErrorType || id == CoreObject::UnicodeDecodeErrorType)
            spec.slots =
                std::vector<std::u32string>{U"encoding", U"object", U"start", U"end", U"reason"};
        create_native_type(context, NativeTypeSpec{spec, layout},
                           *outputs[static_cast<std::size_t>(id)]);
        core.push_back(roots.root(outputs[static_cast<std::size_t>(id)]->result().value()));
    };
    define(CoreObject::PropertyType, "property", LayoutDescriptor::of<PropertyObject>());
    define(CoreObject::NoneType, "NoneType", LayoutDescriptor::of<NoneObject>());
    define(CoreObject::StrType, "str", LayoutDescriptor::of<StrObject>());
    define(CoreObject::TupleType, "tuple", LayoutDescriptor::of<TupleObject>());
    define(CoreObject::DictType, "dict", LayoutDescriptor::of<DictObject>());
    define(CoreObject::IntType, "int", LayoutDescriptor::of<IntObject>());
    define(CoreObject::BoolType, "bool", LayoutDescriptor::of<IntObject>(),
           at(CoreObject::IntType));
    define(CoreObject::FloatType, "float", LayoutDescriptor::of<FloatObject>());
    define(CoreObject::NotImplementedType, "NotImplementedType",
           LayoutDescriptor::of<NoneObject>());
    define(CoreObject::EllipsisType, "ellipsis", LayoutDescriptor::of<NoneObject>());
    define(CoreObject::BaseExceptionType, "BaseException", LayoutDescriptor::of<ExceptionObject>());
    define(CoreObject::SystemExitType, "SystemExit", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
    define(CoreObject::ExceptionType, "Exception", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
    define(CoreObject::AssertionErrorType, "AssertionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::TypeErrorType, "TypeError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::AttributeErrorType, "AttributeError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::ValueErrorType, "ValueError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::UnicodeErrorType, "UnicodeError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ValueErrorType));
    define(CoreObject::UnicodeEncodeErrorType, "UnicodeEncodeError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::UnicodeErrorType));
    define(CoreObject::UnicodeDecodeErrorType, "UnicodeDecodeError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::UnicodeErrorType));
    define(CoreObject::RuntimeErrorType, "RuntimeError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::MemoryErrorType, "MemoryError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::RecursionErrorType, "RecursionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::RuntimeErrorType));
    define(CoreObject::ArithmeticErrorType, "ArithmeticError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::ZeroDivisionErrorType, "ZeroDivisionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ArithmeticErrorType));
    define(CoreObject::OverflowErrorType, "OverflowError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ArithmeticErrorType));
    define(CoreObject::LookupErrorType, "LookupError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::IndexErrorType, "IndexError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::LookupErrorType));
    define(CoreObject::KeyErrorType, "KeyError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::LookupErrorType));
    define(CoreObject::StopIterationType, "StopIteration", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::NameErrorType, "NameError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::UnboundLocalErrorType, "UnboundLocalError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::NameErrorType));
    define(CoreObject::ImportErrorType, "ImportError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::ModuleNotFoundErrorType, "ModuleNotFoundError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ImportErrorType));
    define(CoreObject::SyntaxErrorType, "SyntaxError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::IndentationErrorType, "IndentationError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::SyntaxErrorType));
    define(CoreObject::TabErrorType, "TabError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::IndentationErrorType));
    define(CoreObject::OSErrorType, "OSError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::FileNotFoundErrorType, "FileNotFoundError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::NativeFunctionType, "builtin_function_or_method",
           LayoutDescriptor::of<NativeFunctionObject>());
    define(CoreObject::MethodDescriptorType, "method_descriptor",
           LayoutDescriptor::of<MethodDescriptorObject>());
    define(CoreObject::BoundMethodType, "method", LayoutDescriptor::of<BoundMethodObject>());
    define(CoreObject::GetSetDescriptorType, "getset_descriptor",
           LayoutDescriptor::of<GetSetDescriptorObject>());
    define(CoreObject::MappingProxyType, "mappingproxy",
           LayoutDescriptor::of<MappingProxyObject>());
    define(CoreObject::StaticMethodType, "staticmethod",
           LayoutDescriptor::of<StaticMethodObject>());
    define(CoreObject::ClassMethodType, "classmethod", LayoutDescriptor::of<ClassMethodObject>());
    define(CoreObject::MemberDescriptorType, "member_descriptor",
           LayoutDescriptor::of<MemberDescriptorObject>());
    define(CoreObject::CellType, "cell", LayoutDescriptor::of<CellObject>());
    define(CoreObject::CodeType, "code", execution_layout(CoreObject::CodeType));
    define(CoreObject::FunctionType, "function", execution_layout(CoreObject::FunctionType));
    define(CoreObject::FrameType, "frame", execution_layout(CoreObject::FrameType));
    define(CoreObject::TracebackType, "traceback", execution_layout(CoreObject::TracebackType));
    define(CoreObject::RangeType, "range", LayoutDescriptor::of<RangeObject>());
    define(CoreObject::RangeIteratorType, "range_iterator",
           LayoutDescriptor::of<RangeIteratorObject>());
    define(CoreObject::TupleIteratorType, "tuple_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::StrIteratorType, "str_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::ModuleType, "module", LayoutDescriptor::of<ModuleObject>());
    define(CoreObject::DictKeysType, "dict_keys", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictValuesType, "dict_values", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictItemsType, "dict_items", LayoutDescriptor::of<DictViewObject>());
    define(CoreObject::DictKeyIteratorType, "dict_keyiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::DictValueIteratorType, "dict_valueiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::DictItemIteratorType, "dict_itemiterator",
           LayoutDescriptor::of<DictIteratorObject>());
    define(CoreObject::ListType, "list", LayoutDescriptor::of<ListObject>());
    define(CoreObject::ListIteratorType, "list_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::SequenceIteratorType, "iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::SliceType, "slice", LayoutDescriptor::of<SliceObject>());
    define(CoreObject::BytesType, "bytes", LayoutDescriptor::of<BytesObject>());
    define(CoreObject::SuperType, "super", LayoutDescriptor::of<SuperObject>());
    define(CoreObject::BytesIteratorType, "bytes_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::SetType, "set", LayoutDescriptor::of<SetObject>());
    define(CoreObject::FrozenSetType, "frozenset", LayoutDescriptor::of<SetObject>());
    define(CoreObject::SetIteratorType, "set_iterator", LayoutDescriptor::of<DictIteratorObject>());
    context.payload<TypeObject>(at(CoreObject::BytesType)).allow_slots = false;
    context.payload<TypeObject>(at(CoreObject::TupleType)).allow_slots = false;
    context.payload<TypeObject>(at(CoreObject::IntType)).allow_slots = false;
    for (auto type : core) {
        ReturnSlot dictionary(context);
        context.allocate_instance<DictObject>(at(CoreObject::DictType), dictionary);
        context.payload<TypeObject>(type).dictionary = dictionary.value();
    }
    for (auto seed : {context.object_type(), context.type_type()}) {
        ReturnSlot dictionary(context);
        context.allocate_instance<DictObject>(at(CoreObject::DictType), dictionary);
        context.payload<TypeObject>(seed).dictionary = dictionary.value();
    }
    auto &none_out = *outputs[static_cast<std::size_t>(CoreObject::None)];
    context.allocate_instance<NoneObject>(at(CoreObject::NoneType), none_out);
    core.push_back(roots.root(none_out.value()));
    auto &not_implemented = *outputs[static_cast<std::size_t>(CoreObject::NotImplemented)];
    context.allocate_instance<NoneObject>(at(CoreObject::NotImplementedType), not_implemented);
    core.push_back(roots.root(not_implemented.value()));
    auto &ellipsis = *outputs[static_cast<std::size_t>(CoreObject::Ellipsis)];
    context.allocate_instance<NoneObject>(at(CoreObject::EllipsisType), ellipsis);
    core.push_back(roots.root(ellipsis.value()));
    for (auto id : {CoreObject::False, CoreObject::True, CoreObject::IntZero, CoreObject::IntOne}) {
        auto &output = *outputs[static_cast<std::size_t>(id)];
        const bool boolean = id == CoreObject::False || id == CoreObject::True;
        const std::int64_t value = id == CoreObject::True || id == CoreObject::IntOne ? 1 : 0;
        context.allocate_instance<IntObject>(
            at(boolean ? CoreObject::BoolType : CoreObject::IntType), output, value);
        core.push_back(roots.root(output.value()));
    }
    auto &tuple_out = *outputs[static_cast<std::size_t>(CoreObject::EmptyTuple)];
    context.allocate_instance<TupleObject>(at(CoreObject::TupleType), tuple_out,
                                           std::vector<Value>{});
    core.push_back(roots.root(tuple_out.value()));
    auto &error_out = *outputs[static_cast<std::size_t>(CoreObject::MemoryError)];
    context.allocate_instance<ExceptionObject>(at(CoreObject::MemoryErrorType), error_out,
                                               tuple_out.value());
    core.push_back(roots.root(error_out.value()));
    ReturnSlot text(context);
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__module__");
    auto module_key = roots.root(text.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"builtins");
    auto module_value = roots.root(text.value());
    auto initialize_names = [&](Value type) {
        auto &payload = context.payload<TypeObject>(type);
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text,
                                             std::get<std::u32string>(payload.name));
        payload.name = text.value();
        payload.qualname = text.value();
        payload.bootstrap_module.reset();
        context.account_external(type, payload.external_bytes());
    };
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        initialize_names(core[i].get(context));
        name_store(context, *context.payload<TypeObject>(core[i]).dictionary,
                   module_key.get(context), module_value.get(context));
    }
    for (auto type : {context.object_type(), context.type_type()}) {
        initialize_names(type);
        name_store(context, *context.payload<TypeObject>(type).dictionary, module_key.get(context),
                   module_value.get(context));
    }
    install_call_descriptors(context, core);
    install_attribute_descriptors(context, core);
    install_construction(context, core);
    install_numbers(context, core);
    install_floats(context, core);
    install_protocols(context, core);
    install_dictionaries(context, core);
    install_lists(context, core);
    install_bytes(context, core);
    install_codecs(context, core);
    install_super(context, core);
    install_sets(context, core);
    install_value_protocols(context, core);
    install_format(context, core);
    install_string_format(context, core);
    install_unicode_methods(context, core);
    install_execution(context, core);
    install_exception_runtime(context, core);
    install_system_exit(context, core);
    install_ranges(context, core);
    install_slices(context, core);
    install_iteration(context, core);
    install_modules(context, core);
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        context.payload<TypeObject>(core[i]).immutable = true;
        context.payload<TypeObject>(core[i]).state = TypeState::Ready;
    }
    for (auto type : {context.object_type(), context.type_type()})
        context.payload<TypeObject>(type).state = TypeState::Ready;
    for (auto id : {CoreObject::NoneType,
                    CoreObject::BoolType,
                    CoreObject::NotImplementedType,
                    CoreObject::EllipsisType,
                    CoreObject::NativeFunctionType,
                    CoreObject::MethodDescriptorType,
                    CoreObject::BoundMethodType,
                    CoreObject::GetSetDescriptorType,
                    CoreObject::MappingProxyType,
                    CoreObject::MemberDescriptorType,
                    CoreObject::CellType,
                    CoreObject::CodeType,
                    CoreObject::FunctionType,
                    CoreObject::FrameType,
                    CoreObject::TracebackType,
                    CoreObject::RangeType,
                    CoreObject::RangeIteratorType,
                    CoreObject::TupleIteratorType,
                    CoreObject::StrIteratorType,
                    CoreObject::DictKeysType,
                    CoreObject::DictValuesType,
                    CoreObject::DictItemsType,
                    CoreObject::DictKeyIteratorType,
                    CoreObject::DictValueIteratorType,
                    CoreObject::DictItemIteratorType,
                    CoreObject::ListIteratorType,
                    CoreObject::SequenceIteratorType,
                    CoreObject::SliceType,
                    CoreObject::BytesIteratorType,
                    CoreObject::SetIteratorType})
        context.payload<TypeObject>(at(id)).subclassable = false;

    auto &builtins = *outputs[static_cast<std::size_t>(CoreObject::Builtins)];
    context.allocate_instance<DictObject>(at(CoreObject::DictType), builtins);
    core.push_back(roots.root(builtins.value()));
    const auto mapping = builtins.value();
    auto publish = [&](Value key, Value value) { name_store(context, mapping, key, value); };
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"builtins");
    const auto module_name = roots.root(text.value());
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"__name__");
    publish(text.value(), module_name.get(context));
    for (auto id : {CoreObject::StrType,
                    CoreObject::TupleType,
                    CoreObject::ListType,
                    CoreObject::DictType,
                    CoreObject::IntType,
                    CoreObject::BoolType,
                    CoreObject::FloatType,
                    CoreObject::BaseExceptionType,
                    CoreObject::SystemExitType,
                    CoreObject::ExceptionType,
                    CoreObject::AssertionErrorType,
                    CoreObject::TypeErrorType,
                    CoreObject::AttributeErrorType,
                    CoreObject::ValueErrorType,
                    CoreObject::UnicodeErrorType,
                    CoreObject::UnicodeEncodeErrorType,
                    CoreObject::UnicodeDecodeErrorType,
                    CoreObject::RuntimeErrorType,
                    CoreObject::MemoryErrorType,
                    CoreObject::RecursionErrorType,
                    CoreObject::ArithmeticErrorType,
                    CoreObject::ZeroDivisionErrorType,
                    CoreObject::OverflowErrorType,
                    CoreObject::LookupErrorType,
                    CoreObject::IndexErrorType,
                    CoreObject::KeyErrorType,
                    CoreObject::StopIterationType,
                    CoreObject::RangeType,
                    CoreObject::SliceType,
                    CoreObject::BytesType,
                    CoreObject::SuperType,
                    CoreObject::SetType,
                    CoreObject::FrozenSetType,
                    CoreObject::NameErrorType,
                    CoreObject::UnboundLocalErrorType,
                    CoreObject::ImportErrorType,
                    CoreObject::ModuleNotFoundErrorType,
                    CoreObject::SyntaxErrorType,
                    CoreObject::IndentationErrorType,
                    CoreObject::TabErrorType,
                    CoreObject::OSErrorType,
                    CoreObject::FileNotFoundErrorType,
                    CoreObject::PropertyType,
                    CoreObject::StaticMethodType,
                    CoreObject::ClassMethodType})
        publish(std::get<Value>(context.payload<TypeObject>(at(id)).name), at(id).get(context));
    for (auto type : {context.object_type(), context.type_type()})
        publish(std::get<Value>(context.payload<TypeObject>(type).name), type);
    for (auto [name, id] : {std::pair{U"None", CoreObject::None},
                            {U"Ellipsis", CoreObject::Ellipsis},
                            {U"True", CoreObject::True},
                            {U"False", CoreObject::False},
                            {U"NotImplemented", CoreObject::NotImplemented}}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, name);
        publish(text.value(), at(id).get(context));
    }
    auto operation = [&](const char *name, std::u32string key, NativeEntry entry) {
        SignatureLayout signature;
        signature.parameters = {{U"object", ParameterKind::PositionalOnly}};
        ReturnSlot function(context);
        context.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType),
                                                        function, name, std::move(signature), entry,
                                                        std::vector<Value>{});
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, std::move(key));
        publish(text.value(), function.value());
    };
    operation("len", U"len", builtin_protocol<Protocol::Len>);
    operation("repr", U"repr", builtin_protocol<Protocol::Repr>);
    operation("hash", U"hash", builtin_protocol<Protocol::Hash>);
    install_iterator_builtins(context, core, mapping);
    install_class_builtins(context, core, mapping);
    install_exec_builtins(context, core, mapping);
    {
        SignatureLayout signature;
        signature.parameters = {
            {U"sep", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)},
            {U"end", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)},
            {U"file", ParameterKind::KeywordOnly, at(CoreObject::None).get(context)},
            {U"flush", ParameterKind::KeywordOnly, at(CoreObject::False).get(context)}};
        signature.varargs = U"objects";
        ReturnSlot function(context);
        context.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType),
                                                        function, "print", signature, print_builtin,
                                                        std::vector<Value>{});
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"print");
        publish(text.value(), function.value());
    }
    install_import_builtins(context, core, mapping);
    auto &modules = *outputs[static_cast<std::size_t>(CoreObject::Modules)];
    context.allocate_instance<DictObject>(at(CoreObject::DictType), modules);
    core.push_back(roots.root(modules.value()));
}
} // namespace cxxpython::core::detail
