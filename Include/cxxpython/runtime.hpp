#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Internal runtime storage, roots and attachment API. Not a public extension ABI.
namespace cxxpython::core {
struct ObjectHeader;
class Attachment;
class ReturnSlot;
class Tracer;
class NoSafepoint;
class Transfer;
class Handle;
class RootScope;
struct WeakRefObject;
enum class Protocol : std::size_t;
struct ResolvedSlot;
enum class CoreObject : std::size_t {
    PropertyType,
    NoneType,
    StrType,
    TupleType,
    DictType,
    IntType,
    BoolType,
    FloatType,
    NotImplementedType,
    EllipsisType,
    BaseExceptionType,
    SystemExitType,
    GeneratorExitType,
    ExceptionType,
    BaseExceptionGroupType,
    ExceptionGroupType,
    AssertionErrorType,
    TypeErrorType,
    AttributeErrorType,
    ValueErrorType,
    UnicodeErrorType,
    UnicodeEncodeErrorType,
    UnicodeDecodeErrorType,
    RuntimeErrorType,
    MemoryErrorType,
    RecursionErrorType,
    ArithmeticErrorType,
    ZeroDivisionErrorType,
    OverflowErrorType,
    LookupErrorType,
    IndexErrorType,
    KeyErrorType,
    StopIterationType,
    EOFErrorType,
    NameErrorType,
    UnboundLocalErrorType,
    ImportErrorType,
    ModuleNotFoundErrorType,
    SyntaxErrorType,
    IndentationErrorType,
    TabErrorType,
    OSErrorType,
    FileNotFoundErrorType,
    NativeFunctionType,
    MethodDescriptorType,
    BoundMethodType,
    GetSetDescriptorType,
    MappingProxyType,
    StaticMethodType,
    ClassMethodType,
    MemberDescriptorType,
    CellType,
    CodeType,
    FunctionType,
    FrameType,
    TracebackType,
    RangeType,
    RangeIteratorType,
    TupleIteratorType,
    StrIteratorType,
    ModuleType,
    DictKeysType,
    DictValuesType,
    DictItemsType,
    DictKeyIteratorType,
    DictValueIteratorType,
    DictItemIteratorType,
    ListType,
    ListIteratorType,
    SequenceIteratorType,
    SliceType,
    BytesType,
    SuperType,
    BytesIteratorType,
    SetType,
    FrozenSetType,
    SetIteratorType,
    GeneratorType,
    WeakRefType,
    UnraisableHookArgsType,
    ComplexType,
    ByteArrayType,
    ByteArrayIteratorType,
    CoroutineType,
    CoroutineWrapperType,
    PermissionErrorType,
    FileExistsErrorType,
    IsADirectoryErrorType,
    NotADirectoryErrorType,
    InterruptedErrorType,
    BlockingIOErrorType,
    BrokenPipeErrorType,
    ReversedType,
    ListReverseIteratorType,
    EnumerateType,
    ZipType,
    MapType,
    SystemErrorType,
    BufferErrorType,
    MemoryViewType,
    MemoryViewIteratorType,
    StructType,
    StructIteratorType,
    StructErrorType,
    HashType,
    HashXofType,
    Blake2bType,
    Blake2sType,
    IOBaseType,
    RawIOBaseType,
    BufferedIOBaseType,
    TextIOBaseType,
    FileIOType,
    BytesIOType,
    StringIOType,
    UnsupportedOperationType,
    NotImplementedErrorType,
    SimpleNamespaceType,
    GenericAliasType,
    AsyncGeneratorType,
    AsyncGenASendType,
    AsyncGenAThrowType,
    AsyncGenWrappedValueType,
    StopAsyncIterationType,
    GenericAliasIteratorType,
    UnionType,
    BinasciiErrorType,
    BinasciiIncompleteType,
    WeakProxyType,
    CallableWeakProxyType,
    ReferenceErrorType,
    AnextAwaitableType,
    TypeAliasType,
    None,
    NotImplemented,
    Ellipsis,
    False,
    True,
    IntZero,
    IntOne,
    EmptyTuple,
    MemoryError,
    Builtins,
    Modules,
    Count,
};
namespace detail {
struct State;
struct RootCell;
struct ThreadRecord;
struct TransferRecord;
struct BigInt;
struct ImportState;
class FrameActivation;
std::uint64_t siphash24(std::span<const std::byte> data, std::array<std::uint64_t, 2> key) noexcept;
template <class T> inline constexpr char native_layout_id = 0;
} // namespace detail

// Internal identities must denote live managed allocations. A raw/dangling
// ObjectHeader address is not an embedding handle or a checked pointer API.
class Value final {
  public:
    explicit Value(ObjectHeader &object) noexcept : object_(&object) {}
    ObjectHeader *object() const noexcept
    {
        return object_;
    }
    bool operator==(const Value &) const = default;

  private:
    ObjectHeader *object_;
};

class GcEdge final {
  public:
    GcEdge() noexcept = default;

  private:
    friend class Attachment;
    friend class Tracer;
    friend struct detail::State;
    ObjectHeader *object_ = nullptr;
};

// Mutable shared edges expose only rooted reads through Attachment.
class AtomicGcEdge final {
  public:
    explicit AtomicGcEdge(std::optional<Value> initial = {}) noexcept
        : object_(initial ? initial->object() : nullptr)
    {}
    AtomicGcEdge(const AtomicGcEdge &) = delete;
    AtomicGcEdge &operator=(const AtomicGcEdge &) = delete;

  private:
    friend class Attachment;
    friend class Tracer;
    friend struct detail::State;
    std::atomic<ObjectHeader *> object_;
};

class InitialType final {
  public:
    InitialType() noexcept = default;

  private:
    friend class Attachment;
    friend struct ObjectHeader;
    explicit InitialType(ObjectHeader *type) noexcept : type_(type) {}
    ObjectHeader *type_ = nullptr;
};

struct ObjectHeader {
    AtomicGcEdge type;
    explicit ObjectHeader(InitialType initial = {}) noexcept
        : type(initial.type_ ? std::optional<Value>{Value(*initial.type_)} : std::optional<Value>{})
    {}
    ObjectHeader(const ObjectHeader &) = delete;
    ObjectHeader &operator=(const ObjectHeader &) = delete;
};

class Tracer final {
  public:
    void visit(const GcEdge &edge) noexcept
    {
        visit_(state_, edge.object_);
    }
    void visit(const AtomicGcEdge &edge) noexcept
    {
        visit_(state_, edge.object_.load(std::memory_order_relaxed));
    }
    void visit(Value value) noexcept
    {
        visit_(state_, value.object());
    }

  private:
    friend struct detail::State;
    Tracer(void *state, void (*visit)(void *, ObjectHeader *) noexcept)
        : state_(state), visit_(visit)
    {}
    void *state_;
    void (*visit_)(void *, ObjectHeader *) noexcept;
};

struct InstanceFieldPlan {
    std::size_t slots = 0;
    bool dictionary = false;
    bool weakrefs = false;
    bool operator==(const InstanceFieldPlan &) const = default;
};

struct LayoutDescriptor final {
    const void *native_family;
    std::size_t size;
    std::size_t alignment;
    void (*trace)(const ObjectHeader &, Tracer &) noexcept;
    void (*destroy)(ObjectHeader *) noexcept;
    InstanceFieldPlan fields;
    void (*finalize)(Attachment &, Handle, ReturnSlot &) = nullptr;
    bool operator==(const LayoutDescriptor &) const = default;

    std::size_t storage_size() const
    {
        const auto limit = std::numeric_limits<std::size_t>::max();
        if (fields.dictionary && fields.slots == limit)
            throw std::bad_alloc();
        const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
        if (!count)
            return size;
        constexpr auto padding = alignof(AtomicGcEdge) - 1;
        if (size > limit - padding)
            throw std::bad_alloc();
        const auto offset = (size + padding) & ~padding;
        if (count > (limit - offset) / sizeof(AtomicGcEdge))
            throw std::bad_alloc();
        return offset + count * sizeof(AtomicGcEdge);
    }
    std::size_t storage_alignment() const noexcept
    {
        return alignment < alignof(AtomicGcEdge) ? alignof(AtomicGcEdge) : alignment;
    }
    void *allocate_storage(std::size_t bytes) const
    {
        if (storage_alignment() <= alignof(std::max_align_t))
            return ::operator new(bytes);
        return ::operator new(bytes, std::align_val_t(storage_alignment()));
    }
    void deallocate_storage(void *storage) const noexcept
    {
        if (storage_alignment() <= alignof(std::max_align_t))
            ::operator delete(storage);
        else
            ::operator delete(storage, std::align_val_t(storage_alignment()));
    }
    AtomicGcEdge *field_cells(ObjectHeader *object) const noexcept
    {
        constexpr auto padding = alignof(AtomicGcEdge) - 1;
        const auto offset = (size + padding) & ~padding;
        return reinterpret_cast<AtomicGcEdge *>(reinterpret_cast<std::byte *>(object) + offset);
    }
    void construct_fields(ObjectHeader *object) const noexcept
    {
        const auto count = fields.slots + static_cast<std::size_t>(fields.dictionary);
        if (!count)
            return;
        // Placement array new establishes the array lifetime in the aligned tail.
        ::new (static_cast<void *>(field_cells(object))) AtomicGcEdge[count]();
    }
    void trace_storage(const ObjectHeader &object, Tracer &tracer) const noexcept;
    // End payload/tail lifetimes; only the owning span frees raw storage.
    void destroy_storage(ObjectHeader *object) const noexcept;

    template <class T> static LayoutDescriptor of()
    {
        static_assert(std::is_standard_layout_v<T>);
        static_assert(offsetof(T, header) == 0);
        static_assert(std::is_same_v<decltype(T::header), ObjectHeader>);
        static_assert(std::is_nothrow_destructible_v<T>);
        static_assert(noexcept(std::declval<const T &>().trace(std::declval<Tracer &>())));
        auto finalizer = []() -> void (*)(Attachment &, Handle, ReturnSlot &) {
            if constexpr (requires(Attachment & context, Handle self, ReturnSlot & out) {
                              T::finalize(context, self, out);
                          })
                return T::finalize;
            return nullptr;
        }();
        return {
            &detail::native_layout_id<T>,
            sizeof(T),
            alignof(T),
            [](const ObjectHeader &object, Tracer &tracer) noexcept {
                reinterpret_cast<const T *>(&object)->trace(tracer);
            },
            [](ObjectHeader *object) noexcept { std::destroy_at(reinterpret_cast<T *>(object)); },
            {},
            finalizer,
        };
    }
};

struct HeapConfig {
    std::size_t minimum_gc_budget = 64 * 1024;
    bool collect_every_allocation = false;
    std::size_t max_call_depth = 128;
    std::size_t max_native_depth = 128;
};

struct HeapStats {
    std::size_t objects;
    std::size_t live_bytes;
    std::size_t metadata_bytes;
    std::size_t roots;
    std::size_t root_cells;
    std::size_t collections;
    std::size_t reclaimed_bytes;
    std::uint64_t last_pause_ns;
    std::size_t registered_threads;
    std::uint64_t last_rendezvous_ns, max_rendezvous_ns;
    std::size_t spans = 0, reserved_bytes = 0;
    std::size_t transfer_roots = 0;
    std::size_t reclaimed_objects = 0, allocations_since_gc = 0;
    std::size_t allocated_bytes = 0, gc_budget = 0;
};
enum class HeapInspection { Objects, Referents, Referrers };
enum class MutatorState { PendingAttach, Active, Parked, NativeWait, Collector, Exiting };
enum class GcPhase { Running, Requesting, Collecting };
enum class PublicationDomain { Type, Exception };
struct GcSnapshot {
    GcPhase phase;
    std::uint64_t request_epoch, completed_epoch;
    std::size_t registered, active, parked, native_wait, collections;
    std::size_t transfer_roots = 0;
};

class Interpreter final {
  public:
    explicit Interpreter(HeapConfig config = {});
    ~Interpreter();
    Interpreter(const Interpreter &) = delete;
    Interpreter &operator=(const Interpreter &) = delete;
    void close() noexcept;
    GcSnapshot gc_state() const;

  private:
    friend class Attachment;
    std::shared_ptr<detail::State> state_;
};

class Handle final {
  public:
    Handle() noexcept = default;
    Value get(const Attachment &context) const;

  private:
    friend class Attachment;
    friend class RootScope;
    friend class ReturnSlot;
    friend class ResultView;
    detail::State *owner_ = nullptr;
    std::uint64_t arena_id_ = 0;
    detail::RootCell *cell_ = nullptr;
    std::uint64_t generation_ = 0;
};

struct CallArgsView;
using CallExecutor = void (*)(Attachment &, Handle, CallArgsView, std::optional<Value>,
                              ReturnSlot &);
enum class ResultTag : std::uint8_t { Empty, Ok, Raised };

class ResultView final {
  public:
    ResultTag tag() const;
    Value value() const;
    Value exception() const;

  private:
    friend class ReturnSlot;
    ResultView(Attachment &context, Handle root, std::uint64_t revision)
        : context_(&context), root_(root), revision_(revision)
    {}
    detail::RootCell &cell() const;
    Attachment *context_;
    Handle root_;
    std::uint64_t revision_;
};

class RootScope final {
  public:
    explicit RootScope(Attachment &context);
    ~RootScope();
    RootScope(const RootScope &) = delete;
    RootScope &operator=(const RootScope &) = delete;
    Handle root(Value value);
    void release(Handle handle);

  private:
    friend class Attachment;
    Attachment &context_;
    detail::RootCell *head_ = nullptr;
};

class ReturnSlot final {
  public:
    explicit ReturnSlot(Attachment &context);
    ~ReturnSlot();
    ReturnSlot(const ReturnSlot &) = delete;
    ReturnSlot &operator=(const ReturnSlot &) = delete;
    void reset();
    void reset(const Attachment &caller);
    void check_caller(const Attachment &caller) const;
    void ok(Value value);
    void raised(Value exception);
    // Read the current cell; use result() when retaining a versioned view.
    ResultTag tag() const;
    Value value() const;
    Value exception() const;
    ResultView result() const;

  private:
    friend class Attachment;
    void write(ResultTag tag, ObjectHeader *object);
    // Only after validating the output cell and the incoming object's domain.
    void store_root_noexcept(ResultTag tag, ObjectHeader *object) noexcept;
    Attachment &context_;
    Handle root_;
};

// Move-only native delivery lease; the payload record is managed by GC.
// Token access/move/reset requires exclusive host ownership of this token.
class Transfer final {
  public:
    Transfer() noexcept = default;
    ~Transfer();
    Transfer(const Transfer &) = delete;
    Transfer &operator=(const Transfer &) = delete;
    Transfer(Transfer &&other) noexcept;
    Transfer &operator=(Transfer &&other) noexcept;
    explicit operator bool() const noexcept
    {
        return record_ != nullptr;
    }
    void reset() noexcept;

  private:
    friend class Attachment;
    Transfer(std::shared_ptr<detail::State> state, detail::TransferRecord *record) noexcept
        : state_(std::move(state)), record_(record)
    {}
    std::shared_ptr<detail::State> state_;
    detail::TransferRecord *record_ = nullptr;
};

class Attachment final {
  public:
    explicit Attachment(Interpreter &interpreter);
    // The launch transfer retains the heap until admission; close still rejects
    // new attachments. Access to this transfer remains exclusively host-owned.
    explicit Attachment(const Transfer &launch);
    ~Attachment();
    Attachment(const Attachment &) = delete;
    Attachment &operator=(const Attachment &) = delete;

    // Constructors may allocate host memory, but must not run Python or GC.
    // All Value constructor arguments must already be in roots or heap edges.
    template <class T, class... Args> void allocate(ReturnSlot &out, Args &&...args)
    {
        allocate_layout<T>(LayoutDescriptor::of<T>(), {}, out, std::forward<Args>(args)...);
    }

    template <class T, class... Args>
    void allocate_instance(Handle type, ReturnSlot &out, Args &&...args)
    {
        require_safepoint();
        out.reset(*this);
        auto layout = require_instance_layout(type, LayoutDescriptor::of<T>());
        allocate_layout<T>(layout, type, out, std::forward<Args>(args)...);
    }

    template <class T> T &payload(Handle object)
    {
        Value value = object.get(*this);
        require_layout(value, &detail::native_layout_id<T>);
        return *reinterpret_cast<T *>(value.object());
    }
    template <class T> T &payload(Value object)
    {
        require_layout(object, &detail::native_layout_id<T>);
        return *reinterpret_cast<T *>(object.object());
    }
    template <class T> bool has_layout(Value object) const
    {
        return has_native_layout(object, &detail::native_layout_id<T>);
    }

    AtomicGcEdge *instance_dictionary(Value object);
    AtomicGcEdge &instance_slot(Value object, std::size_t index);

    void store(Handle owner, GcEdge &edge, Handle value);
    void clear(Handle owner, GcEdge &edge);
    void load(Value owner, const AtomicGcEdge &edge, ReturnSlot &out);
    void store(Value owner, AtomicGcEdge &edge, std::optional<Value> value);
    void store(Handle owner, AtomicGcEdge &edge, Handle value)
    {
        store(owner.get(*this), edge, value.get(*this));
    }
    void clear(Handle owner, AtomicGcEdge &edge)
    {
        store(owner.get(*this), edge, {});
    }
    bool compare_exchange(Value owner, AtomicGcEdge &edge, std::optional<Value> expected,
                          std::optional<Value> desired);
    void exchange(Value owner, AtomicGcEdge &edge, std::optional<Value> value,
                  ReturnSlot &previous);
    Transfer make_transfer(Handle value);
    void receive(Transfer &transfer, ReturnSlot &out);
    bool supports_weakrefs(Handle target) const;
    void weakref_lookup(Handle target, Handle type, ReturnSlot &out);
    void register_weakref(Handle self, Handle target, bool cache, ReturnSlot &out);
    void weakrefs(Handle target, RootScope &roots, std::vector<Value> &out);
    void account_external(Handle owner, std::size_t bytes);
    void account_external(Value owner, std::size_t bytes);
    void intern(Handle string, ReturnSlot &out);
    std::size_t object_size(Handle owner) const;
    std::size_t gc_metadata_size(Handle owner) const;
    void bootstrap();
    void bootstrap_core();
    bool core_ready() const;
    // The interpreter, not a replaceable module namespace, owns pending exit callbacks.
    void load_exit_callbacks(ReturnSlot &);
    void publish_exit_callbacks(Handle candidate, ReturnSlot &);
    void load_gc_callbacks(ReturnSlot &);
    void publish_gc_callbacks(Handle candidate, ReturnSlot &);
    void load_sys_dictionary(ReturnSlot &);
    void set_sys_dictionary(Handle dictionary);
    Value builtin(CoreObject object) const;
    std::uint64_t type_epoch() const;
    void invalidate_types();
    std::optional<ResolvedSlot> cached_protocol(Handle type, Protocol protocol, RootScope &roots);
    void cache_protocol(const ResolvedSlot &slot);
    std::uint64_t publication_epoch(PublicationDomain domain) const;
    bool begin_publication(PublicationDomain domain, std::uint64_t epoch);
    void end_publication(PublicationDomain domain, std::uint64_t epoch);
    bool enter_call();
    void leave_call() noexcept;
    bool enter_native_call();
    void leave_native_call() noexcept;
    CallExecutor call_executor() const;
    std::optional<Value> current_frame() const;
    std::optional<Value> handled_exception() const;
    std::uint64_t keyed_hash(std::span<const std::byte> data) const;
    detail::ImportState &imports();
    // Borrowed identities only; callers keep their objects rooted throughout.
    bool enter_repr(Handle object);
    void leave_repr(Value object) noexcept;
    Value object_type() const;
    Value type_type() const;
    Value type_of(Handle object) const;
    Value type_of(Value object) const;
    // Deterministic managed-allocation quota for embedding/failure tests.
    void set_allocation_limit(std::size_t remaining);
    std::size_t collect();
    void poll();
    void set_gc_enabled(bool enabled);
    bool gc_enabled() const;
    bool is_finalized(Handle object) const;
    HeapStats stats() const;
    // Frozen heap inspection publishes precise roots before mutators resume.
    void inspect_heap(HeapInspection, std::span<const Value> targets, RootScope &,
                      std::vector<Value> &out);
    void require_attached() const;

  private:
    friend class NoSafepoint;
    friend class NativeWait;
    friend class NativeCallback;
    friend class RootScope;
    friend class ReturnSlot;
    friend class ResultView;
    friend class Handle;
    friend class detail::FrameActivation;
    explicit Attachment(std::shared_ptr<detail::State> state);
    void require_owns(Value value) const;
    void require_safepoint() const;
    void enter_native_wait();
    void leave_native_wait();
    void require_layout(Value value, const void *family) const;
    const LayoutDescriptor &allocation_layout(Value value) const;
    bool has_native_layout(Value value, const void *family) const;
    LayoutDescriptor require_instance_layout(Handle type, const LayoutDescriptor &layout);
    void require_edge(Value owner, const void *edge, std::size_t bytes) const;
    detail::RootCell &cell(Handle handle) const;
    Handle acquire(const void *scope);
    Handle acquire_cell(const void *scope);
    void release(detail::RootCell &cell) noexcept;
    void prepare_allocation(ReturnSlot &out, std::size_t bytes);
    void *reserve_storage(LayoutDescriptor layout);
    void abandon_storage(ObjectHeader *object) noexcept;
    void adopt(ObjectHeader *object, ReturnSlot &out, std::size_t external);
    bool dispatch_finalizers();
    void dispatch_gc_callbacks(Handle callbacks, bool starting, std::size_t collected);

    template <class T, class... Args>
    void allocate_layout(LayoutDescriptor layout, std::optional<Handle> type, ReturnSlot &out,
                         Args &&...args)
    {
        const auto bytes = layout.storage_size();
        prepare_allocation(out, bytes);
        void *storage = reserve_storage(layout);
        allocation_in_progress_ = true;
        T *object = nullptr;
        try {
            object =
                std::construct_at(static_cast<T *>(storage),
                                  type ? InitialType(type->get(*this).object()) : InitialType{},
                                  std::forward<Args>(args)...);
            layout.construct_fields(&object->header);
            std::size_t external = 0;
            if constexpr (requires { object->external_bytes(); })
                external = object->external_bytes();
            adopt(&object->header, out, external);
            allocation_in_progress_ = false;
        } catch (...) {
            if (object)
                layout.destroy_storage(&object->header);
            abandon_storage(reinterpret_cast<ObjectHeader *>(storage));
            allocation_in_progress_ = false;
            throw;
        }
    }

    std::shared_ptr<detail::State> state_;
    std::shared_ptr<detail::ThreadRecord> thread_record_;
    std::size_t call_depth_ = 0;
    std::size_t native_depth_ = 0;
    std::vector<Value> repr_stack_;
    std::optional<Value> current_frame_;
    std::optional<Value> handled_exception_;
    std::size_t no_safepoint_depth_ = 0;
    bool allocation_in_progress_ = false;
};

class NoSafepoint final {
  public:
    explicit NoSafepoint(Attachment &context);
    ~NoSafepoint();
    NoSafepoint(const NoSafepoint &) = delete;
    NoSafepoint &operator=(const NoSafepoint &) = delete;

  private:
    Attachment &context_;
};

class NativeWait final {
  public:
    explicit NativeWait(Attachment &context);
    ~NativeWait();
    NativeWait(const NativeWait &) = delete;
    NativeWait &operator=(const NativeWait &) = delete;

  private:
    friend class NativeCallback;
    Attachment &context_;
};

// Synchronous, owner-thread callback inside an enclosing NativeWait.
class NativeCallback final {
  public:
    explicit NativeCallback(NativeWait &native);
    ~NativeCallback();
    NativeCallback(const NativeCallback &) = delete;
    NativeCallback &operator=(const NativeCallback &) = delete;

  private:
    Attachment &context_;
};
} // namespace cxxpython::core
