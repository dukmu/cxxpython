#pragma once

#include "cxxpython/type.hpp"

#include <array>
#include <list>
#include <map>
#include <unordered_map>

namespace cxxpython::core {
// Value/call buffers are immutable after construction. Shared mutable
// references are atomic edges; load them into a caller-owned ReturnSlot.
struct NoneObject {
    ObjectHeader header;
    explicit NoneObject(InitialType head) : header(head) {}
    void trace(Tracer &) const noexcept {}
};
struct StrObject {
    ObjectHeader header;
    const std::u32string value;
    StrObject(InitialType head, std::u32string text) : header(head), value(std::move(text)) {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept;
};
struct IntObject {
    ObjectHeader header;
    const std::int64_t small;
    const std::unique_ptr<const detail::BigInt> large;
    IntObject(InitialType head, std::int64_t value);
    IntObject(InitialType head, std::unique_ptr<detail::BigInt> value);
    ~IntObject() noexcept;
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept;
};
struct TupleObject {
    ObjectHeader header;
    const std::vector<Value> items;
    TupleObject(InitialType head, std::vector<Value> values)
        : header(head), items(std::move(values))
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto item : items)
            tracer.visit(item);
    }
    std::size_t external_bytes() const noexcept
    {
        return items.capacity() * sizeof(Value);
    }
};

struct RangeObject {
    ObjectHeader header;
    const Value start, stop, step, length;
    RangeObject(InitialType head, Value start, Value stop, Value step, Value length)
        : header(head), start(start), stop(stop), step(step), length(length)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        for (auto value : {start, stop, step, length})
            tracer.visit(value);
    }
};
struct RangeIteratorObject {
    ObjectHeader header;
    const Value stop, step;
    AtomicGcEdge current;
    RangeIteratorObject(InitialType head, Value stop, Value step, std::optional<Value> current)
        : header(head), stop(stop), step(step), current(current)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(stop);
        tracer.visit(step);
        tracer.visit(current);
    }
};
struct SequenceIteratorObject {
    ObjectHeader header;
    AtomicGcEdge owner;
    std::atomic<std::size_t> index{0};
    SequenceIteratorObject(InitialType head, Value owner) : header(head), owner(owner) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
    }
};

// Ordered dictionaries share one entry store between Python and name lookup.
struct DictObject {
    struct Entry {
        Value key;
        Value value;
        std::int64_t hash = 0;
        std::uint64_t identity = 0;
    };
    ObjectHeader header;
    AtomicGcEdge namespace_owner;
    using Index = std::map<std::u32string, std::list<Entry>::iterator>;
    using HashIndex = std::multimap<std::int64_t, std::list<Entry>::iterator>;
    struct Storage {
        std::mutex mutex;
        std::list<Entry> names;
        Index index;
        HashIndex hashes;
        std::uint64_t next_identity = 1, keys_version = 0;
        std::size_t bytes = 0;
        std::size_t external_bytes() const noexcept
        {
            return sizeof(Storage) + bytes;
        }
    };
    const std::unique_ptr<Storage> contents;
    explicit DictObject(InitialType head) : header(head), contents(std::make_unique<Storage>()) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(namespace_owner);
        for (const auto &entry : contents->names) {
            tracer.visit(entry.key);
            tracer.visit(entry.value);
        }
    }
    std::size_t external_bytes() const noexcept
    {
        return contents->external_bytes();
    }
};
// Empty means an absent key; Python exceptions remain in the caller's slot.
ResultView dict_lookup(Attachment &, Handle dictionary, Handle key, ReturnSlot &);
ResultView dict_store(Attachment &, Handle dictionary, Handle key, Handle value, ReturnSlot &);
ResultView dict_erase(Attachment &, Handle dictionary, Handle key, ReturnSlot &);
ResultView dict_setdefault(Attachment &, Handle dictionary, Handle key, Handle value, ReturnSlot &);
void dict_clear(Attachment &, Handle dictionary, ReturnSlot &);
enum class DictViewKind { Keys, Values, Items };
struct DictViewObject {
    ObjectHeader header;
    const Value dictionary;
    const DictViewKind kind;
    DictViewObject(InitialType type, Value dictionary, DictViewKind kind)
        : header(type), dictionary(dictionary), kind(kind)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(dictionary);
    }
};
struct DictIteratorObject {
    ObjectHeader header;
    AtomicGcEdge dictionary;
    const DictViewKind kind;
    AtomicGcEdge snapshot;
    // Cursor state is protected by the owning dictionary's storage mutex.
    std::list<DictObject::Entry>::const_iterator cursor;
    std::uint64_t version = 0, last_identity = 0;
    std::size_t expected_size = 0, remaining = 0, position = 0;
    bool invalid = false, exhausted = false;
    DictIteratorObject(InitialType type, Value dictionary, DictViewKind kind)
        : header(type), dictionary(dictionary), kind(kind)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(dictionary);
        tracer.visit(snapshot);
    }
};
struct DictStorage {
    ObjectHeader header;
    const std::vector<DictObject::Entry> entries;
    const std::unordered_map<std::u32string, std::size_t> index;
    const std::size_t index_string_bytes;
    const bool exact_string_keys;
    DictStorage(InitialType head, std::vector<DictObject::Entry> entries,
                std::unordered_map<std::u32string, std::size_t> index, std::size_t string_bytes,
                bool exact_string_keys = true)
        : header(head), entries(std::move(entries)), index(std::move(index)),
          index_string_bytes(string_bytes), exact_string_keys(exact_string_keys)
    {}
    void trace(Tracer &tracer) const noexcept;
    std::size_t external_bytes() const noexcept;
};
class DictView final {
  public:
    DictView(Attachment &context, std::optional<Value> dictionary);
    std::span<const DictObject::Entry> entries() const;
    std::optional<Value> find(const std::u32string &name) const;
    std::optional<Value> storage() const;

  private:
    Attachment &context_;
    RootScope roots_;
    mutable ReturnSlot storage_;
    std::vector<DictObject::Entry> names_;
};
struct ExceptionObject {
    ObjectHeader header;
    AtomicGcEdge args, traceback, cause, context;
    std::atomic<bool> suppress_context{false};
    ExceptionObject(InitialType head, Value arguments) : header(head), args(arguments) {}
    void trace(Tracer &tracer) const noexcept;
};

enum class ParameterKind { PositionalOnly, PositionalOrKeyword, KeywordOnly };
struct Parameter {
    std::u32string name;
    ParameterKind kind = ParameterKind::PositionalOrKeyword;
    std::optional<Value> default_value = {};
};
struct SignatureLayout {
    std::vector<Parameter> parameters;
    std::optional<std::u32string> varargs;
    std::optional<std::u32string> varkwargs;
    void validate() const;
    bool accepts_complete_positional(std::size_t count) const noexcept
    {
        return !varargs && !varkwargs && count == parameters.size() &&
               (parameters.empty() || parameters.back().kind != ParameterKind::KeywordOnly);
    }
    void trace(Tracer &tracer) const noexcept;
    std::size_t external_bytes() const noexcept;
};
struct KeywordArg {
    Value name;
    Value value;
};
struct CallArgsView {
    std::span<const Value> positional = {};
    std::span<const KeywordArg> keywords = {};
};
using NativeEntry = void (*)(Attachment &, Handle function, std::span<const Value>, ReturnSlot &);

struct NativeFunctionObject {
    ObjectHeader header;
    const std::string name;
    const SignatureLayout signature;
    const NativeEntry entry;
    const std::vector<Value> captures;
    NativeFunctionObject(InitialType head, std::string name_in, SignatureLayout signature_in,
                         NativeEntry entry_in, std::vector<Value> captures_in)
        : header(head), name(std::move(name_in)), signature(std::move(signature_in)),
          entry(entry_in), captures(std::move(captures_in))
    {}
    void trace(Tracer &tracer) const noexcept;
    std::size_t external_bytes() const noexcept;
};
struct MethodDescriptorObject {
    ObjectHeader header;
    const Value function;
    const Value owner;
    MethodDescriptorObject(InitialType head, Value function_in, Value owner_in)
        : header(head), function(function_in), owner(owner_in)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(function);
        tracer.visit(owner);
    }
};
struct BoundMethodObject {
    ObjectHeader header;
    const Value function;
    const Value self;
    BoundMethodObject(InitialType head, Value function_in, Value self_in)
        : header(head), function(function_in), self(self_in)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(function);
        tracer.visit(self);
    }
};
using NativeGetter = void (*)(Attachment &, Value, ReturnSlot &);
using NativeSetter = void (*)(Attachment &, Value, std::optional<Value>, ReturnSlot &);
struct GetSetDescriptorObject {
    ObjectHeader header;
    const Value owner;
    const NativeGetter getter;
    const NativeSetter setter;
    GetSetDescriptorObject(InitialType head, Value owner_in, NativeGetter get, NativeSetter set)
        : header(head), owner(owner_in), getter(get), setter(set)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
    }
};
struct MappingProxyObject {
    ObjectHeader header;
    const Value mapping;
    MappingProxyObject(InitialType head, Value dictionary) : header(head), mapping(dictionary) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(mapping);
    }
};
struct StaticMethodObject {
    ObjectHeader header;
    const Value callable;
    StaticMethodObject(InitialType head, Value value) : header(head), callable(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(callable);
    }
};
struct ClassMethodObject {
    ObjectHeader header;
    const Value callable;
    ClassMethodObject(InitialType head, Value value) : header(head), callable(value) {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(callable);
    }
};
struct MemberDescriptorObject {
    ObjectHeader header;
    const Value owner;
    const std::size_t index;
    MemberDescriptorObject(InitialType head, Value type, std::size_t slot)
        : header(head), owner(type), index(slot)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(owner);
    }
};
struct CellObject {
    ObjectHeader header;
    AtomicGcEdge contents;
    explicit CellObject(InitialType head, std::optional<Value> value = {})
        : header(head), contents(value)
    {}
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(contents);
    }
};

bool read_cell(Attachment &context, Value cell, ReturnSlot &out);
void write_cell(Attachment &context, Value cell, std::optional<Value> value);

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
ResultView set_attribute(Attachment &context, Handle object, Handle name, Handle value,
                         ReturnSlot &out);
ResultView del_attribute(Attachment &context, Handle object, Handle name, ReturnSlot &out);

namespace detail {
constexpr auto core_count = static_cast<std::size_t>(CoreObject::Count);
void build_core_objects(Attachment &context, std::span<ReturnSlot *const> outputs);
std::optional<Value> name_lookup(Attachment &context, Value dictionary, const std::u32string &name);
void name_store(Attachment &context, Value dictionary, Value name, Value value);
bool name_erase_if(Attachment &context, Value dictionary, const std::u32string &name,
                   Value expected);
void discard_name_table(Attachment &context, Value dictionary);
std::int64_t name_hash(Attachment &, const std::u32string &);
void install_dictionaries(Attachment &, std::span<const Handle>);
void copy_dictionary_entries(Attachment &, Value destination, std::span<const DictObject::Entry>);
void make_dict_iterator(Attachment &, Handle dictionary, DictViewKind, ReturnSlot &);
void next_dict_item(Attachment &, Handle iterator, ReturnSlot &);
std::size_t dictionary_size(Attachment &, Value dictionary);
ResultView dict_lookup_hash(Attachment &, Handle, Handle, std::int64_t, ReturnSlot &);
ResultView dict_store_hash(Attachment &, Handle, Handle, Handle, std::int64_t, ReturnSlot &);
bool name_erase(Attachment &context, Value dictionary, const std::u32string &name);
bool copy_name_storage(Attachment &context, std::optional<Value> source, const std::u32string &name,
                       std::optional<Value> key, std::optional<Value> value, ReturnSlot &out);
void copy_dictionary_storage(Attachment &, std::span<const DictObject::Entry>, ReturnSlot &);
std::optional<Value> type_attribute(Attachment &context, Value type, const std::u32string &name);
struct TypeAttribute {
    Value owner;
    Value value;
};
std::optional<TypeAttribute> find_type_attribute(Attachment &context, Value type,
                                                 const std::u32string &name);
void bind_descriptor(Attachment &context, Value descriptor, std::optional<Value> receiver,
                     Value owner, ReturnSlot &out);
void special(Attachment &context, Value receiver, const std::u32string &name, CallArgsView args,
             ReturnSlot &out);
void invoke_special(Attachment &context, Handle receiver, Handle descriptor, CallArgsView args,
                    ReturnSlot &out);
void install_native_attribute(Attachment &context, std::span<const Handle> core, Handle owner,
                              std::u32string name, SignatureLayout signature, NativeEntry entry,
                              std::vector<Value> captures = {});
void install_native_field(Attachment &context, std::span<const Handle> core, Handle owner,
                          std::u32string name, NativeGetter getter, NativeSetter setter = nullptr);
void install_iteration(Attachment &context, std::span<const Handle> core);
void install_ranges(Attachment &context, std::span<const Handle> core);
void install_iterator_builtins(Attachment &context, std::span<const Handle> core, Value mapping);
void install_class_builtins(Attachment &context, std::span<const Handle> core, Value mapping);
void install_import_builtins(Attachment &context, std::span<const Handle> core, Value mapping);
void stop_iteration(Attachment &context, ReturnSlot &out);
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
void execute_class_body(Attachment &context, Handle function, Handle mapping, ReturnSlot &out);
struct FunctionDefaults {
    std::span<const Value> positional;
    std::optional<Value> keywords;
};
struct PreparedPythonCall {
    Handle function;
    std::optional<Handle> receiver;
    std::vector<Value> positional;
    std::vector<KeywordArg> keywords;
};
// A prepared target borrows the caller's RootScope; otherwise out owns
// the completed value/exception. No prepared target is a Python value.
std::optional<PreparedPythonCall> prepare_call(Attachment &context, Handle callable,
                                               CallArgsView args, RootScope &roots,
                                               ReturnSlot &out);
void validate_return(Attachment &context, ReturnSlot &out);
void link_exception_context(Attachment &context, Value exception);
void append_traceback(Attachment &context, Handle exception, Handle frame, ReturnSlot &out);
void install_exception_runtime(Attachment &context, std::span<const Handle> core);
bool bind_arguments(Attachment &context, const SignatureLayout &signature, CallArgsView args,
                    std::optional<Value> receiver, RootScope &roots, std::vector<Value> &bound,
                    ReturnSlot &out, std::optional<FunctionDefaults> defaults = {});
void install_instance_dictionary(Attachment &context, Handle type);
void install_slot_descriptors(Attachment &context, Handle type, std::size_t first);
void get_native_field(Attachment &context, Value descriptor, std::optional<Value> receiver,
                      ReturnSlot &out);
void get_native_member(Attachment &context, Value descriptor, std::optional<Value> receiver,
                       ReturnSlot &out);
void allocate_function(Attachment &context, Handle type, std::string name,
                       SignatureLayout signature, NativeEntry entry, std::vector<Value> captures,
                       ReturnSlot &out);
} // namespace detail
} // namespace cxxpython::core
