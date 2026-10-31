#include "cxxpython/asyncgenobject.hpp"
#include "cxxpython/bytearrayobject.hpp"
#include "cxxpython/calliterobject.hpp"
#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/genericaliasobject.hpp"
#include "cxxpython/genobject.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/namespaceobject.hpp"
#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "cxxpython/sreobject.hpp"
#include "cxxpython/superobject.hpp"
#include "cxxpython/typealiasobject.hpp"
#include "cxxpython/typevarobject.hpp"
#include "cxxpython/unionobject.hpp"
#include "cxxpython/weakrefobject.hpp"
#include "internal/aiterobject.hpp"
#include "internal/builtinexec.hpp"
#include "internal/codecs.hpp"
#include "internal/errors.hpp"
#include "internal/exceptiongroup.hpp"
#include "internal/exceptions.hpp"
#include "internal/fileio.hpp"
#include "internal/hashmodule.hpp"
#include "internal/iomodule.hpp"
#include "internal/memoryio.hpp"
#include "internal/reversed.hpp"
#include "internal/stdio.hpp"
#include "internal/stringformat.hpp"
#include "internal/structmodule.hpp"
#include "internal/sysmodule.hpp"
#include "internal/unicodeobject.hpp"
#include "internal/weakproxy.hpp"

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
        if (id == CoreObject::ExceptionGroupType)
            bases.push_back(at(CoreObject::ExceptionType));
        TypeSpec spec{name, bases};
        spec.module = U"builtins";
        if (id >= CoreObject::TypeAliasType && id <= CoreObject::GenericType)
            spec.module = U"typing";
        if (id == CoreObject::SrePatternType || id == CoreObject::SreMatchType)
            spec.module = U"re";
        if (id == CoreObject::SreScannerType || id == CoreObject::SreTemplateType)
            spec.module = U"_sre";
        if (id == CoreObject::SrePatternType)
            spec.slots = std::vector<std::u32string>{U"__weakref__"};
        if (id == CoreObject::TypeVarType || id == CoreObject::ParamSpecType ||
            id == CoreObject::TypeVarTupleType)
            spec.slots = std::vector<std::u32string>{U"__dict__", U"__weakref__"};
        if (id == CoreObject::ParamSpecArgsType || id == CoreObject::ParamSpecKwargsType)
            spec.slots = std::vector<std::u32string>{U"__weakref__"};
        if (id == CoreObject::FunctionType || id == CoreObject::ModuleType ||
            id == CoreObject::SimpleNamespaceType || id == CoreObject::BaseExceptionType)
            spec.slots = std::vector<std::u32string>{U"__dict__"};
        if (id == CoreObject::ImportErrorType)
            spec.slots = std::vector<std::u32string>{U"name", U"path", U"msg", U"name_from"};
        if (id == CoreObject::NameErrorType)
            spec.slots = std::vector<std::u32string>{U"name"};
        if (id == CoreObject::SyntaxErrorType)
            spec.slots = std::vector<std::u32string>{
                U"msg",  U"filename",   U"lineno",     U"offset",
                U"text", U"end_lineno", U"end_offset", U"print_file_and_line"};
        if (id == CoreObject::StopIterationType)
            spec.slots = std::vector<std::u32string>{U"value"};
        if (id == CoreObject::SystemExitType)
            spec.slots = std::vector<std::u32string>{U"code"};
        if (id == CoreObject::BaseExceptionGroupType)
            spec.slots = std::vector<std::u32string>{U"exceptions", U"message"};
        if (id == CoreObject::OSErrorType)
            spec.slots = std::vector<std::u32string>{U"errno", U"strerror", U"filename",
                                                     U"filename2", U"characters_written"};
        if (id == CoreObject::UnicodeEncodeErrorType || id == CoreObject::UnicodeDecodeErrorType)
            spec.slots =
                std::vector<std::u32string>{U"encoding", U"object", U"start", U"end", U"reason"};
        create_native_type(context, NativeTypeSpec{spec, layout},
                           *outputs[static_cast<std::size_t>(id)]);
        core.push_back(roots.root(outputs[static_cast<std::size_t>(id)]->result().value()));
        auto collection = CollectionKind::None;
        if (id == CoreObject::TupleType || id == CoreObject::ListType ||
            id == CoreObject::RangeType || id == CoreObject::MemoryViewType)
            collection = CollectionKind::Sequence;
        if (id == CoreObject::DictType || id == CoreObject::MappingProxyType)
            collection = CollectionKind::Mapping;
        context.payload<TypeObject>(core.back())
            .collection.store(collection, std::memory_order_relaxed);
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
    define(CoreObject::GeneratorExitType, "GeneratorExit", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
    define(CoreObject::KeyboardInterruptType, "KeyboardInterrupt",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::BaseExceptionType));
    define(CoreObject::ExceptionType, "Exception", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::BaseExceptionType));
    define(CoreObject::WarningType, "Warning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::UserWarningType, "UserWarning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::WarningType));
    define(CoreObject::DeprecationWarningType, "DeprecationWarning",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::WarningType));
    define(CoreObject::PendingDeprecationWarningType, "PendingDeprecationWarning",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::WarningType));
    define(CoreObject::SyntaxWarningType, "SyntaxWarning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::WarningType));
    define(CoreObject::RuntimeWarningType, "RuntimeWarning",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::WarningType));
    define(CoreObject::FutureWarningType, "FutureWarning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::WarningType));
    define(CoreObject::ImportWarningType, "ImportWarning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::WarningType));
    define(CoreObject::UnicodeWarningType, "UnicodeWarning",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::WarningType));
    define(CoreObject::BytesWarningType, "BytesWarning", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::WarningType));
    define(CoreObject::ResourceWarningType, "ResourceWarning",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::WarningType));
    define(CoreObject::BaseExceptionGroupType, "BaseExceptionGroup",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::BaseExceptionType));
    define(CoreObject::ExceptionGroupType, "ExceptionGroup",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::BaseExceptionGroupType));
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
    define(CoreObject::EOFErrorType, "EOFError", LayoutDescriptor::of<ExceptionObject>(),
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
    define(CoreObject::GeneratorType, "generator", LayoutDescriptor::of<GeneratorObject>());
    define(CoreObject::WeakRefType, "ReferenceType", LayoutDescriptor::of<WeakRefObject>());
    define(CoreObject::UnraisableHookArgsType, "UnraisableHookArgs",
           LayoutDescriptor::of<TupleObject>(), at(CoreObject::TupleType));
    define(CoreObject::ComplexType, "complex", LayoutDescriptor::of<ComplexObject>());
    define(CoreObject::ByteArrayType, "bytearray", LayoutDescriptor::of<ByteArrayObject>());
    define(CoreObject::ByteArrayIteratorType, "bytearray_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::CoroutineType, "coroutine", LayoutDescriptor::of<GeneratorObject>());
    define(CoreObject::CoroutineWrapperType, "coroutine_wrapper",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::PermissionErrorType, "PermissionError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::FileExistsErrorType, "FileExistsError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::IsADirectoryErrorType, "IsADirectoryError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::NotADirectoryErrorType, "NotADirectoryError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::InterruptedErrorType, "InterruptedError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::BlockingIOErrorType, "BlockingIOError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::BrokenPipeErrorType, "BrokenPipeError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::OSErrorType));
    define(CoreObject::ReversedType, "reversed", LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::ListReverseIteratorType, "list_reverseiterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::EnumerateType, "enumerate", LayoutDescriptor::of<EnumObject>());
    define(CoreObject::ZipType, "zip", LayoutDescriptor::of<MultiIteratorObject>());
    define(CoreObject::MapType, "map", LayoutDescriptor::of<MultiIteratorObject>());
    define(CoreObject::FilterType, "filter", LayoutDescriptor::of<MultiIteratorObject>());
    define(CoreObject::SystemErrorType, "SystemError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::BufferErrorType, "BufferError", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::MemoryViewType, "memoryview", LayoutDescriptor::of<MemoryViewObject>());
    define(CoreObject::MemoryViewIteratorType, "memory_iterator",
           LayoutDescriptor::of<SequenceIteratorObject>());
    define(CoreObject::StructType, "Struct", LayoutDescriptor::of<StructObject>());
    define(CoreObject::StructIteratorType, "unpack_iterator",
           LayoutDescriptor::of<StructIteratorObject>());
    define(CoreObject::StructErrorType, "error", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ExceptionType));
    define(CoreObject::HashType, "HASH", LayoutDescriptor::of<HashObject>());
    define(CoreObject::HashXofType, "HASHXOF", LayoutDescriptor::of<HashObject>());
    define(CoreObject::Blake2bType, "blake2b", LayoutDescriptor::of<HashObject>());
    define(CoreObject::Blake2sType, "blake2s", LayoutDescriptor::of<HashObject>());
    for (auto id : {CoreObject::StaticMethodType, CoreObject::ClassMethodType})
        context.payload<TypeObject>(at(id)).instance_layout.fields.dictionary = true;
    define(CoreObject::IOBaseType, "_IOBase", LayoutDescriptor::of<InstanceObject>());
    context.payload<TypeObject>(at(CoreObject::IOBaseType)).instance_layout.fields.dictionary =
        true;
    context.payload<TypeObject>(at(CoreObject::IOBaseType)).instance_layout.fields.weakrefs = true;
    define(CoreObject::RawIOBaseType, "_RawIOBase", LayoutDescriptor::of<InstanceObject>(),
           at(CoreObject::IOBaseType));
    define(CoreObject::BufferedIOBaseType, "_BufferedIOBase",
           LayoutDescriptor::of<InstanceObject>(), at(CoreObject::IOBaseType));
    define(CoreObject::TextIOBaseType, "_TextIOBase", LayoutDescriptor::of<InstanceObject>(),
           at(CoreObject::IOBaseType));
    define(CoreObject::FileIOType, "FileIO", LayoutDescriptor::of<FileIOObject>(),
           at(CoreObject::RawIOBaseType));
    define(CoreObject::BytesIOType, "BytesIO", LayoutDescriptor::of<BytesIOObject>(),
           at(CoreObject::BufferedIOBaseType));
    define(CoreObject::StringIOType, "StringIO", LayoutDescriptor::of<StringIOObject>(),
           at(CoreObject::TextIOBaseType));
    {
        const Handle bases[] = {at(CoreObject::OSErrorType), at(CoreObject::ValueErrorType)};
        TypeSpec spec{U"UnsupportedOperation", bases};
        create_native_type(
            context, {spec, LayoutDescriptor::of<ExceptionObject>()},
            *outputs[static_cast<std::size_t>(CoreObject::UnsupportedOperationType)]);
        core.push_back(roots.root(
            outputs[static_cast<std::size_t>(CoreObject::UnsupportedOperationType)]->value()));
    }
    define(CoreObject::NotImplementedErrorType, "NotImplementedError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::RuntimeErrorType));
    define(CoreObject::SimpleNamespaceType, "SimpleNamespace",
           LayoutDescriptor::of<NamespaceObject>());
    context.payload<TypeObject>(at(CoreObject::SimpleNamespaceType)).allow_weakrefs = false;
    define(CoreObject::GenericAliasType, "GenericAlias",
           LayoutDescriptor::of<GenericAliasObject>());
    define(CoreObject::AsyncGeneratorType, "async_generator",
           LayoutDescriptor::of<GeneratorObject>());
    define(CoreObject::AsyncGenASendType, "async_generator_asend",
           LayoutDescriptor::of<AsyncGenRequestObject>());
    define(CoreObject::AsyncGenAThrowType, "async_generator_athrow",
           LayoutDescriptor::of<AsyncGenRequestObject>());
    define(CoreObject::AsyncGenWrappedValueType, "async_generator_wrapped_value",
           LayoutDescriptor::of<AsyncGenWrappedValueObject>());
    define(CoreObject::StopAsyncIterationType, "StopAsyncIteration",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::GenericAliasIteratorType, "generic_alias_iterator",
           LayoutDescriptor::of<GenericAliasIteratorObject>());
    define(CoreObject::UnionType, "UnionType", LayoutDescriptor::of<UnionObject>());
    define(CoreObject::BinasciiErrorType, "Error", LayoutDescriptor::of<ExceptionObject>(),
           at(CoreObject::ValueErrorType));
    define(CoreObject::BinasciiIncompleteType, "Incomplete",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::WeakProxyType, "ProxyType", LayoutDescriptor::of<WeakRefObject>());
    define(CoreObject::CallableWeakProxyType, "CallableProxyType",
           LayoutDescriptor::of<WeakRefObject>());
    define(CoreObject::ReferenceErrorType, "ReferenceError",
           LayoutDescriptor::of<ExceptionObject>(), at(CoreObject::ExceptionType));
    define(CoreObject::AnextAwaitableType, "anext_awaitable",
           LayoutDescriptor::of<AnextAwaitableObject>());
    define(CoreObject::TypeAliasType, "TypeAliasType", LayoutDescriptor::of<TypeAliasObject>());
    define(CoreObject::TypeVarType, "TypeVar", LayoutDescriptor::of<TypeVarObject>());
    define(CoreObject::ParamSpecType, "ParamSpec", LayoutDescriptor::of<ParamSpecObject>());
    define(CoreObject::TypeVarTupleType, "TypeVarTuple",
           LayoutDescriptor::of<TypeVarTupleObject>());
    define(CoreObject::ParamSpecArgsType, "ParamSpecArgs",
           LayoutDescriptor::of<ParamSpecAttributeObject>());
    define(CoreObject::ParamSpecKwargsType, "ParamSpecKwargs",
           LayoutDescriptor::of<ParamSpecAttributeObject>());
    define(CoreObject::GenericType, "Generic", LayoutDescriptor::of<InstanceObject>());
    define(CoreObject::SrePatternType, "Pattern", LayoutDescriptor::of<SrePatternObject>());
    define(CoreObject::SreMatchType, "Match", LayoutDescriptor::of<SreMatchObject>());
    define(CoreObject::SreScannerType, "SRE_Scanner", LayoutDescriptor::of<SreScannerObject>());
    define(CoreObject::SreTemplateType, "SRE_Template", LayoutDescriptor::of<SreTemplateObject>());
    define(CoreObject::CallIteratorType, "callable_iterator",
           LayoutDescriptor::of<CallIteratorObject>());
    for (auto id :
         {CoreObject::FunctionType, CoreObject::ModuleType, CoreObject::CodeType,
          CoreObject::GeneratorType, CoreObject::CoroutineType, CoreObject::SetType,
          CoreObject::FrozenSetType, CoreObject::GenericAliasType, CoreObject::AsyncGeneratorType})
        context.payload<TypeObject>(at(id)).instance_layout.fields.weakrefs = true;
    for (auto id :
         {CoreObject::TupleType, CoreObject::BytesType, CoreObject::IntType, CoreObject::BoolType})
        context.payload<TypeObject>(at(id)).allow_weakrefs = false;
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
    install_complex(context, core);
    install_protocols(context, core);
    install_dictionaries(context, core);
    install_lists(context, core);
    install_bytes(context, core);
    install_bytearrays(context, core);
    install_memoryviews(context, core);
    install_struct(context, core);
    install_hashes(context, core);
    install_iobases(context, core);
    install_fileio(context, core);
    install_memoryio(context, core);
    install_codecs(context, core);
    install_super(context, core);
    install_sets(context, core);
    install_value_protocols(context, core);
    install_format(context, core);
    install_string_format(context, core);
    install_unicode_methods(context, core);
    install_execution(context, core);
    install_generators(context, core);
    install_async_generators(context, core);
    install_async_iteration(context, core);
    install_namespace(context, core);
    install_genericalias(context, core);
    install_union(context, core);
    install_typealias(context, core);
    install_typevars(context, core);
    install_sre(context, core);
    install_weakrefs(context, core);
    install_weakproxies(context, core);
    install_error_types(context, core);
    install_exception_runtime(context, core);
    install_exception_groups(context, core);
    install_system_exit(context, core);
    install_os_errors(context, core);
    install_ranges(context, core);
    install_slices(context, core);
    install_iteration(context, core);
    install_reversed(context, core);
    install_modules(context, core);
    {
        ReturnSlot name(context), documentation(context);
        context.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)],
                                             name, U"__doc__");
        const auto none = core[static_cast<std::size_t>(CoreObject::None)].get(context);
        for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
            const auto dictionary = *TypeView(context, core[i]).dictionary();
            if (!name_lookup(context, dictionary, U"__doc__"))
                name_store(context, dictionary, name.value(), none);
        }
        for (const auto &[type, text] :
             {std::pair{context.object_type(),
                        U"The base class of the class hierarchy.\n\nWhen called, it accepts "
                        U"no arguments and returns a new featureless\ninstance that has no "
                        U"instance attributes and cannot be given any.\n"},
              std::pair{context.type_type(),
                        U"type(object) -> the object's type\ntype(name, bases, dict, "
                        U"**kwds) -> a new type"},
              std::pair{at(CoreObject::KeyboardInterruptType).get(context),
                        U"Program interrupted by user."}}) {
            context.allocate_instance<StrObject>(
                core[static_cast<std::size_t>(CoreObject::StrType)], documentation, text);
            name_store(context, *TypeView(context, type).dictionary(), name.value(),
                       documentation.value());
        }
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::None); ++i) {
        context.payload<TypeObject>(core[i]).immutable = true;
        context.payload<TypeObject>(core[i]).state = TypeState::Ready;
    }
    context.payload<TypeObject>(at(CoreObject::UnsupportedOperationType)).immutable = false;
    context.payload<TypeObject>(at(CoreObject::ExceptionGroupType)).immutable = false;
    context.payload<TypeObject>(at(CoreObject::GenericType)).immutable = false;
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
                    CoreObject::ByteArrayIteratorType,
                    CoreObject::ListReverseIteratorType,
                    CoreObject::MemoryViewType,
                    CoreObject::MemoryViewIteratorType,
                    CoreObject::StructIteratorType,
                    CoreObject::HashType,
                    CoreObject::HashXofType,
                    CoreObject::Blake2bType,
                    CoreObject::Blake2sType,
                    CoreObject::CoroutineType,
                    CoreObject::CoroutineWrapperType,
                    CoreObject::AsyncGeneratorType,
                    CoreObject::AsyncGenASendType,
                    CoreObject::AsyncGenAThrowType,
                    CoreObject::AsyncGenWrappedValueType,
                    CoreObject::AnextAwaitableType,
                    CoreObject::TypeAliasType,
                    CoreObject::TypeVarType,
                    CoreObject::ParamSpecType,
                    CoreObject::TypeVarTupleType,
                    CoreObject::ParamSpecArgsType,
                    CoreObject::ParamSpecKwargsType,
                    CoreObject::SrePatternType,
                    CoreObject::SreMatchType,
                    CoreObject::SreScannerType,
                    CoreObject::SreTemplateType,
                    CoreObject::CallIteratorType,
                    CoreObject::UnionType,
                    CoreObject::GenericAliasIteratorType,
                    CoreObject::SetIteratorType,
                    CoreObject::GeneratorType})
        context.payload<TypeObject>(at(id)).subclassable = false;
    for (auto id : {CoreObject::WeakProxyType, CoreObject::CallableWeakProxyType})
        context.payload<TypeObject>(at(id)).subclassable = false;
    context.payload<TypeObject>(at(CoreObject::UnraisableHookArgsType)).subclassable = false;
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"typing");
    for (auto id :
         {CoreObject::TypeVarType, CoreObject::ParamSpecType, CoreObject::TypeVarTupleType,
          CoreObject::ParamSpecArgsType, CoreObject::ParamSpecKwargsType, CoreObject::GenericType})
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    for (auto [id, module] : {std::pair{CoreObject::SrePatternType, U"re"},
                              {CoreObject::SreMatchType, U"re"},
                              {CoreObject::SreScannerType, U"_sre"},
                              {CoreObject::SreTemplateType, U"_sre"}}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, module);
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    }
    context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"weakref");
    for (auto id :
         {CoreObject::WeakRefType, CoreObject::WeakProxyType, CoreObject::CallableWeakProxyType})
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    for (auto [id, module] : {std::pair{CoreObject::StructType, U"_struct"},
                              {CoreObject::StructIteratorType, U"_struct"},
                              {CoreObject::StructErrorType, U"struct"},
                              {CoreObject::HashType, U"_hashlib"},
                              {CoreObject::HashXofType, U"_hashlib"},
                              {CoreObject::Blake2bType, U"_blake2"},
                              {CoreObject::Blake2sType, U"_blake2"}}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, module);
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    }
    for (auto id :
         {CoreObject::SimpleNamespaceType, CoreObject::GenericAliasType, CoreObject::UnionType}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"types");
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    }
    for (auto id : {CoreObject::BinasciiErrorType, CoreObject::BinasciiIncompleteType}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text, U"binascii");
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    }
    for (auto id :
         {CoreObject::IOBaseType, CoreObject::RawIOBaseType, CoreObject::BufferedIOBaseType,
          CoreObject::TextIOBaseType, CoreObject::FileIOType, CoreObject::BytesIOType,
          CoreObject::StringIOType, CoreObject::UnsupportedOperationType}) {
        context.allocate_instance<StrObject>(at(CoreObject::StrType), text,
                                             id == CoreObject::UnsupportedOperationType ? U"io"
                                                                                        : U"_io");
        name_store(context, *context.payload<TypeObject>(at(id)).dictionary,
                   module_key.get(context), text.value());
    }

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
                    CoreObject::ComplexType,
                    CoreObject::ByteArrayType,
                    CoreObject::MemoryViewType,
                    CoreObject::BufferErrorType,
                    CoreObject::SystemErrorType,
                    CoreObject::ReversedType,
                    CoreObject::EnumerateType,
                    CoreObject::ZipType,
                    CoreObject::MapType,
                    CoreObject::FilterType,
                    CoreObject::BaseExceptionType,
                    CoreObject::SystemExitType,
                    CoreObject::GeneratorExitType,
                    CoreObject::KeyboardInterruptType,
                    CoreObject::ExceptionType,
                    CoreObject::WarningType,
                    CoreObject::UserWarningType,
                    CoreObject::DeprecationWarningType,
                    CoreObject::PendingDeprecationWarningType,
                    CoreObject::SyntaxWarningType,
                    CoreObject::RuntimeWarningType,
                    CoreObject::FutureWarningType,
                    CoreObject::ImportWarningType,
                    CoreObject::UnicodeWarningType,
                    CoreObject::BytesWarningType,
                    CoreObject::ResourceWarningType,
                    CoreObject::BaseExceptionGroupType,
                    CoreObject::ExceptionGroupType,
                    CoreObject::AssertionErrorType,
                    CoreObject::TypeErrorType,
                    CoreObject::AttributeErrorType,
                    CoreObject::ValueErrorType,
                    CoreObject::UnicodeErrorType,
                    CoreObject::UnicodeEncodeErrorType,
                    CoreObject::UnicodeDecodeErrorType,
                    CoreObject::RuntimeErrorType,
                    CoreObject::NotImplementedErrorType,
                    CoreObject::MemoryErrorType,
                    CoreObject::ReferenceErrorType,
                    CoreObject::RecursionErrorType,
                    CoreObject::ArithmeticErrorType,
                    CoreObject::ZeroDivisionErrorType,
                    CoreObject::OverflowErrorType,
                    CoreObject::LookupErrorType,
                    CoreObject::IndexErrorType,
                    CoreObject::KeyErrorType,
                    CoreObject::StopIterationType,
                    CoreObject::EOFErrorType,
                    CoreObject::StopAsyncIterationType,
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
                    CoreObject::PermissionErrorType,
                    CoreObject::FileExistsErrorType,
                    CoreObject::IsADirectoryErrorType,
                    CoreObject::NotADirectoryErrorType,
                    CoreObject::InterruptedErrorType,
                    CoreObject::BlockingIOErrorType,
                    CoreObject::BrokenPipeErrorType,
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
