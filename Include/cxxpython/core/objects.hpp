#pragma once

#include "cxxpython/core/type.hpp"

#include <array>
#include <unordered_map>

namespace cxxpython::core
{
    struct NoneObject
    {
        ObjectHeader header;
        explicit NoneObject(ObjectHeader head) : header(head) {}
        void trace(Tracer &) const noexcept {}
    };
    struct StrObject
    {
        ObjectHeader header;
        std::u32string value;
        StrObject(ObjectHeader head, std::u32string text) : header(head), value(std::move(text)) {}
        void trace(Tracer &) const noexcept {}
        std::size_t external_bytes() const noexcept;
    };
    struct IntObject
    {
        ObjectHeader header;
        std::int64_t small;
        std::unique_ptr<detail::BigInt> large;
        IntObject(ObjectHeader head, std::int64_t value);
        IntObject(ObjectHeader head, std::unique_ptr<detail::BigInt> value);
        ~IntObject() noexcept;
        void trace(Tracer &) const noexcept {}
        std::size_t external_bytes() const noexcept;
    };
    struct TupleObject
    {
        ObjectHeader header;
        std::vector<Value> items;
        TupleObject(ObjectHeader head, std::vector<Value> values) : header(head), items(std::move(values)) {}
        void trace(Tracer &tracer) const noexcept { for (auto item : items) tracer.visit(item); }
        std::size_t external_bytes() const noexcept { return items.capacity() * sizeof(Value); }
    };

    // B3 name dictionaries: managed keys/values, no public general mapping API.
    struct DictObject
    {
        struct Entry { Value key; Value value; };
        ObjectHeader header;
        std::vector<Entry> entries;
        std::unordered_map<std::u32string, std::size_t> index;
        std::size_t index_string_bytes = 0;
        explicit DictObject(ObjectHeader head) : header(head) {}
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept;
    };
    struct ExceptionObject
    {
        ObjectHeader header;
        Value args;
        std::optional<Value> traceback;
        std::optional<Value> cause;
        std::optional<Value> context;
        bool suppress_context = false;
        ExceptionObject(ObjectHeader head, Value arguments) : header(head), args(arguments) {}
        void trace(Tracer &tracer) const noexcept;
    };

    enum class ParameterKind { PositionalOnly, PositionalOrKeyword, KeywordOnly };
    struct Parameter
    {
        std::u32string name;
        ParameterKind kind = ParameterKind::PositionalOrKeyword;
        std::optional<Value> default_value = {};
    };
    struct SignatureLayout
    {
        std::vector<Parameter> parameters;
        std::optional<std::u32string> varargs;
        std::optional<std::u32string> varkwargs;
        void validate() const;
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept;
    };
    struct KeywordArg { Value name; Value value; };
    struct CallArgsView
    {
        std::span<const Value> positional = {};
        std::span<const KeywordArg> keywords = {};
    };
    using NativeEntry = void (*)(Attachment &, Handle function, std::span<const Value>, ReturnSlot &);

    struct NativeFunctionObject
    {
        ObjectHeader header;
        std::string name;
        SignatureLayout signature;
        NativeEntry entry;
        std::vector<Value> captures;
        NativeFunctionObject(ObjectHeader head, std::string name_in, SignatureLayout signature_in,
                             NativeEntry entry_in, std::vector<Value> captures_in)
            : header(head), name(std::move(name_in)), signature(std::move(signature_in)),
              entry(entry_in), captures(std::move(captures_in)) {}
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept;
    };
    struct MethodDescriptorObject
    {
        ObjectHeader header;
        Value function;
        Value owner;
        MethodDescriptorObject(ObjectHeader head, Value function_in, Value owner_in)
            : header(head), function(function_in), owner(owner_in) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(function); tracer.visit(owner); }
    };
    struct BoundMethodObject
    {
        ObjectHeader header;
        Value function;
        Value self;
        BoundMethodObject(ObjectHeader head, Value function_in, Value self_in)
            : header(head), function(function_in), self(self_in) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(function); tracer.visit(self); }
    };
    using NativeGetter = void (*)(Attachment &, Value, ReturnSlot &);
    using NativeSetter = void (*)(Attachment &, Value, std::optional<Value>, ReturnSlot &);
    struct GetSetDescriptorObject
    {
        ObjectHeader header;
        Value owner;
        NativeGetter getter;
        NativeSetter setter;
        GetSetDescriptorObject(ObjectHeader head, Value owner_in, NativeGetter get, NativeSetter set)
            : header(head), owner(owner_in), getter(get), setter(set) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(owner); }
    };
    struct MappingProxyObject
    {
        ObjectHeader header;
        Value mapping;
        MappingProxyObject(ObjectHeader head, Value dictionary) : header(head), mapping(dictionary) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(mapping); }
    };
    struct StaticMethodObject
    {
        ObjectHeader header;
        Value callable;
        StaticMethodObject(ObjectHeader head, Value value) : header(head), callable(value) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(callable); }
    };
    struct ClassMethodObject
    {
        ObjectHeader header;
        Value callable;
        ClassMethodObject(ObjectHeader head, Value value) : header(head), callable(value) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(callable); }
    };
    struct MemberDescriptorObject
    {
        ObjectHeader header;
        Value owner;
        std::size_t index;
        MemberDescriptorObject(ObjectHeader head, Value type, std::size_t slot)
            : header(head), owner(type), index(slot) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(owner); }
    };
    struct CellObject
    {
        ObjectHeader header;
        std::optional<Value> contents;
        explicit CellObject(ObjectHeader head, std::optional<Value> value = {}) : header(head), contents(value) {}
        void trace(Tracer &tracer) const noexcept { if (contents) tracer.visit(*contents); }
    };

    void make_str(Attachment &context, std::u32string value, ReturnSlot &out);
    void make_int(Attachment &context, std::int64_t value, ReturnSlot &out);
    void make_int_decimal(Attachment &context, std::string_view value, ReturnSlot &out);
    bool is_int(Attachment &context, Value value);
    bool int_fits_signed(Attachment &context, Value value, std::int64_t &result);
    int int_sign(Attachment &context, Value value);
    std::string int_decimal(Attachment &context, Value value);
    void copy_int(Attachment &context, Value value, ReturnSlot &out);
    void make_tuple(Attachment &context, std::span<const Value> values, ReturnSlot &out);
    void make_native_function(Attachment &context, std::string name, SignatureLayout signature,
                              NativeEntry entry, std::span<const Value> captures, ReturnSlot &out);
    void make_method_descriptor(Attachment &context, Handle owner, Handle function, ReturnSlot &out);
    void raise_error(Attachment &context, CoreObject type, std::u32string message, ReturnSlot &out);
    void validate_exception(Attachment &context, Value exception);
    bool is_identifier(std::u32string_view text);
    ResultView call(Attachment &context, Handle callable, CallArgsView args, ReturnSlot &out);
    ResultView get_attribute(Attachment &context, Handle object, Handle name, ReturnSlot &out);
    ResultView set_attribute(Attachment &context, Handle object, Handle name, Handle value, ReturnSlot &out);
    ResultView del_attribute(Attachment &context, Handle object, Handle name, ReturnSlot &out);

    namespace detail
    {
        constexpr auto core_count = static_cast<std::size_t>(CoreObject::Count);
        void build_core_objects(Attachment &context, std::span<ReturnSlot *const> outputs);
        std::optional<Value> name_lookup(Attachment &context, Value dictionary, const std::u32string &name);
        void name_store(Attachment &context, Value dictionary, Value name, Value value);
        bool name_erase(Attachment &context, Value dictionary, const std::u32string &name);
        std::optional<Value> type_attribute(Attachment &context, Value type, const std::u32string &name);
        struct TypeAttribute { Value owner; Value value; };
        std::optional<TypeAttribute> find_type_attribute(Attachment &context, Value type, const std::u32string &name);
        void bind_descriptor(Attachment &context, Value descriptor, std::optional<Value> receiver,
                             Value owner, ReturnSlot &out);
        void special(Attachment &context, Value receiver, const std::u32string &name,
                     CallArgsView args, ReturnSlot &out);
        void invoke_special(Attachment &context, Handle receiver, Handle descriptor, CallArgsView args, ReturnSlot &out);
        void install_native_attribute(Attachment &context, std::span<const Handle> core, Handle owner,
                                      std::u32string name, SignatureLayout signature, NativeEntry entry,
                                      std::vector<Value> captures = {});
        void install_call_descriptors(Attachment &context, std::span<const Handle> core);
        void install_attribute_descriptors(Attachment &context, std::span<const Handle> core);
        void install_construction(Attachment &context, std::span<const Handle> core);
        void install_numbers(Attachment &context, std::span<const Handle> core);
        void install_protocols(Attachment &context, std::span<const Handle> core);
        void install_value_protocols(Attachment &context, std::span<const Handle> core);
        std::int64_t int_hash_value(Attachment &context, Value value);
        LayoutDescriptor execution_layout(CoreObject type);
        void install_execution(Attachment &context, std::span<const Handle> core);
        void execute_function(Attachment &context, Handle function, CallArgsView args,
                              std::optional<Value> receiver, ReturnSlot &out);
        struct FunctionDefaults
        {
            std::span<const Value> positional;
            std::optional<Value> keywords;
        };
        struct PreparedPythonCall
        {
            Handle function;
            std::optional<Handle> receiver;
            std::vector<Value> positional;
            std::vector<KeywordArg> keywords;
        };
        // A prepared target borrows the caller's RootScope; otherwise out owns
        // the completed value/exception. No prepared target is a Python value.
        std::optional<PreparedPythonCall> prepare_call(Attachment &context, Handle callable,
            CallArgsView args, RootScope &roots, ReturnSlot &out);
        void validate_return(Attachment &context, ReturnSlot &out);
        bool bind_arguments(Attachment &context, const SignatureLayout &signature, CallArgsView args,
                            std::optional<Value> receiver, RootScope &roots, std::vector<Value> &bound,
                            ReturnSlot &out, std::optional<FunctionDefaults> defaults = {});
        void install_instance_dictionary(Attachment &context, Handle type);
        void install_slot_descriptors(Attachment &context, Handle type, std::size_t first);
        void get_native_field(Attachment &context, Value descriptor, std::optional<Value> receiver, ReturnSlot &out);
        void get_native_member(Attachment &context, Value descriptor, std::optional<Value> receiver, ReturnSlot &out);
        void allocate_function(Attachment &context, Handle type, std::string name, SignatureLayout signature,
                               NativeEntry entry, std::vector<Value> captures, ReturnSlot &out);
    }
}
