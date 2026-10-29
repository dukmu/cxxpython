#pragma once

#include "cxxpython/core/runtime.hpp"

#include <optional>
#include <span>
#include <string>
#include <variant>

namespace cxxpython::core
{
    // Type metadata currently relies on transitional serial admission.
    // Plan 2 replaces published mutable metadata with rooted TypeSnapshot reads.
    struct InstanceObject
    {
        ObjectHeader header;
        explicit InstanceObject(ObjectHeader head) : header(head) {}
        void trace(Tracer &) const noexcept {}
    };

    enum class TypeState { Building, Structural, Ready };

    // Internal TypeReady API; Ready covers the currently implemented protocols.
    struct TypeObject
    {
        ObjectHeader header;
        // B2 has no str type yet. B3 replaces bootstrap text with the actual
        // managed strings; reflection and later mutation retain their identity.
        std::variant<std::u32string, Value> name;
        std::variant<std::u32string, Value> qualname;
        std::optional<std::u32string> bootstrap_module = U"builtins";
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

        TypeObject(ObjectHeader head, std::u32string name_in, LayoutDescriptor layout)
            : header(head), name(std::move(name_in)), qualname(name),
              instance_layout(layout) {}
        void trace(Tracer &tracer) const noexcept;
        std::size_t external_bytes() const noexcept;
    };

    struct TypeSpec
    {
        std::u32string name;
        std::span<const Handle> bases = {};
        std::optional<Handle> metaclass = {};
        std::optional<std::u32string> module = U"__main__";
        std::optional<std::u32string> qualname = {};
        std::optional<Handle> dictionary = {};
        std::optional<std::vector<std::u32string>> slots = {};

        TypeSpec(std::u32string name_in, std::span<const Handle> bases_in = {},
                 std::optional<Handle> metaclass_in = {})
            : name(std::move(name_in)), bases(bases_in), metaclass(metaclass_in) {}
        // Host-native definitions use ASCII identifiers; Python names take the
        // codepoint overload and never pass through this convenience boundary.
        TypeSpec(std::string name_in, std::span<const Handle> bases_in = {},
                 std::optional<Handle> metaclass_in = {})
            : TypeSpec(std::u32string(name_in.begin(), name_in.end()), bases_in, metaclass_in) {}
    };

    struct NativeTypeSpec
    {
        TypeSpec type;
        LayoutDescriptor layout;
    };

    class TypeDefinitionError : public std::invalid_argument
    {
    public:
        explicit TypeDefinitionError(const char *message, CoreObject error = CoreObject::TypeErrorType)
            : std::invalid_argument(message), error_type(error) {}
        CoreObject error_type;
    };

    // Structural host definition faults are distinct from host invariants.
    // The Python type.__new__ adapter translates only TypeDefinitionError.
    void create_type(Attachment &context, const TypeSpec &spec, ReturnSlot &out);
    void create_native_type(Attachment &context, const NativeTypeSpec &spec, ReturnSlot &out);
    bool is_subtype(Attachment &context, Handle type, Handle base);
    bool is_subtype(Attachment &context, Value type, Value base);
    bool is_layout_compatible(Attachment &context, Value left, Value right);

    namespace detail
    {
        void build_type_seeds(Attachment &context, ReturnSlot &object, ReturnSlot &type);
        void initialize_type_dictionary(Attachment &context, Handle type);
        void type_ready(Attachment &context, Handle type, std::optional<Handle> dictionary = {});
        Handle select_metaclass(Attachment &context, Handle metaclass, std::span<const Handle> bases,
                                RootScope &roots);
    }
}
