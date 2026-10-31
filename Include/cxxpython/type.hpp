#pragma once

#include "cxxpython/runtime.hpp"

#include <optional>
#include <span>
#include <string>
#include <variant>

namespace cxxpython::core {
// Published type names/namespaces are read through rooted TypeView snapshots.
struct InstanceObject {
    ObjectHeader header;
    explicit InstanceObject(InitialType head) : header(head) {}
    void trace(Tracer &) const noexcept {}
};

enum class TypeState { Building, Structural, Ready };

struct TypeSnapshot {
    ObjectHeader header;
    const Value name, qualname, dictionary, bases, mro;
    const std::optional<Value> storage;
    const std::optional<Value> finalizer;
    const bool abstract;
    TypeSnapshot(InitialType head, Value name, Value qualname, Value dictionary,
                 std::optional<Value> storage, Value bases, Value mro, bool abstract = false);
    void trace(Tracer &tracer) const noexcept
    {
        tracer.visit(name);
        tracer.visit(qualname);
        tracer.visit(dictionary);
        tracer.visit(bases);
        tracer.visit(mro);
        if (storage)
            tracer.visit(*storage);
        if (finalizer)
            tracer.visit(*finalizer);
    }
};

enum class CollectionKind : std::uint8_t { None, Sequence, Mapping };
// Internal TypeReady API; Ready covers the currently implemented protocols.
struct TypeObject {
    ObjectHeader header;
    AtomicGcEdge metadata;
    AtomicGcEdge subclasses;
    // Before metadata publication, names may be native bootstrap text. Publication
    // moves their managed identities to TypeSnapshot and clears these builders.
    std::variant<std::u32string, Value> name;
    std::variant<std::u32string, Value> qualname;
    // Static native types may use __module__ as an instance descriptor. Their
    // declared module remains immutable metadata for class-level reflection.
    std::optional<std::u32string> declared_module = U"builtins";
    // The structural graph and instance layout are frozen after type readiness.
    std::vector<Value> bases;
    std::vector<Value> mro;
    std::optional<Value> base;
    std::optional<Value> solid_base;
    std::optional<Value> dictionary;
    LayoutDescriptor instance_layout;
    std::vector<std::u32string> slot_names;
    TypeState state = TypeState::Building;
    bool immutable = false;
    bool subclassable = true;
    bool allow_slots = true;
    bool allow_weakrefs = true;
    // Pattern classification follows MRO/ABC registration, not __getitem__ duck typing.
    std::atomic<CollectionKind> collection{CollectionKind::None};

    TypeObject(InitialType head, std::u32string name_in, LayoutDescriptor layout)
        : header(head), name(std::move(name_in)), qualname(name), instance_layout(layout)
    {}
    void trace(Tracer &tracer) const noexcept;
    std::size_t external_bytes() const noexcept;
};

class TypeView final {
  public:
    TypeView(Attachment &context, Value type);
    TypeView(Attachment &context, Handle type) : TypeView(context, type.get(context)) {}
    std::variant<std::u32string, Value> name() const;
    std::variant<std::u32string, Value> qualname() const;
    std::optional<Value> dictionary() const;
    std::optional<Value> storage() const;
    std::optional<Value> snapshot() const;

  private:
    Attachment &context_;
    ReturnSlot owner_, metadata_;
    mutable std::optional<ReturnSlot> staged_storage_;
};

struct TypeSpec {
    std::u32string name;
    std::span<const Handle> bases = {};
    std::optional<Handle> metaclass = {};
    std::optional<std::u32string> module = U"__main__";
    std::optional<std::u32string> qualname = {};
    std::optional<Handle> dictionary = {};
    std::optional<std::vector<std::u32string>> slots = {};

    TypeSpec(std::u32string name_in, std::span<const Handle> bases_in = {},
             std::optional<Handle> metaclass_in = {})
        : name(std::move(name_in)), bases(bases_in), metaclass(metaclass_in)
    {}
    // Host-native definitions use ASCII identifiers; Python names take the
    // codepoint overload and never pass through this convenience boundary.
    TypeSpec(std::string name_in, std::span<const Handle> bases_in = {},
             std::optional<Handle> metaclass_in = {})
        : TypeSpec(std::u32string(name_in.begin(), name_in.end()), bases_in, metaclass_in)
    {}
};

struct NativeTypeSpec {
    TypeSpec type;
    LayoutDescriptor layout;
};

class TypeDefinitionError : public std::invalid_argument {
  public:
    explicit TypeDefinitionError(const char *message, CoreObject error = CoreObject::TypeErrorType)
        : std::invalid_argument(message), error_type(error)
    {}
    CoreObject error_type;
};

// Structural host definition faults are distinct from host invariants.
// The Python type.__new__ adapter translates only TypeDefinitionError.
void create_type(Attachment &context, const TypeSpec &spec, ReturnSlot &out);
void create_native_type(Attachment &context, const NativeTypeSpec &spec, ReturnSlot &out);
bool is_subtype(Attachment &context, Handle type, Handle base);
bool is_subtype(Attachment &context, Value type, Value base);
bool is_layout_compatible(Attachment &context, Value left, Value right);
void instance_check(Attachment &, Handle subject, Handle candidate, ReturnSlot &);

namespace detail {
void build_type_seeds(Attachment &context, ReturnSlot &object, ReturnSlot &type);
void initialize_type_dictionary(Attachment &context, Handle type);
void type_ready(Attachment &context, Handle type, std::optional<Handle> dictionary = {});
void publish_type(Attachment &context, Value type);
void register_subclass(Attachment &, Handle base, Handle child);
void direct_subclasses(Attachment &, Handle type, ReturnSlot &out);
void type_name_store(Attachment &context, Value type, Value name, bool qualified);
bool type_namespace_change(Attachment &context, Value type, const std::u32string &name,
                           std::optional<Value> key, std::optional<Value> value,
                           std::optional<bool> abstract = {});
bool publish_type_dictionary(Attachment &, Value type, Value expected, Value storage);
Handle select_metaclass(Attachment &context, Handle metaclass, std::span<const Handle> bases,
                        RootScope &roots);
} // namespace detail
} // namespace cxxpython::core
