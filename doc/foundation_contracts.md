# Current Runtime Contracts

The authoritative full objective is
[Plan 2](../cxxpython_foundation_redesign_plan_2.md). Current source organization
is documented in [architecture_design.md](architecture_design.md). The previous
contract/migration ledger is preserved in
[archive/foundation_contracts_2026-10-29.md](archive/foundation_contracts_2026-10-29.md);
it includes superseded serial and migration assumptions and is not the live API.

## Scope And Status

The only implementation is `cxxpython::core`, built as `cxxpython_core` and exposed
by `cxxpython`. Legacy code, standard library, extension API and tests were removed
at the user's request. That removal does not establish M9/N6 acceptance.

N0-N5 and M0-M6 contain experimental slices; complete language, object/protocol,
library, thread/resource, platform/install and performance acceptance remain.
M7-M9/N6 are not complete. No CPython ABI or .pyc compatibility is promised;
the experimental native extension ABI is specified below.

The accepted source subset includes arbitrary integer/float/string literals,
implemented unary/arithmetic/boolean/comparison protocols, name/attribute
operations, if/while/for control flow, calls, function definitions/defaults/variadic
binding, globals/nonlocal, closures, docstrings, tuple expressions, recursive
recursive/starred unpacking and integer/slice/protocol subscripts. Experimental range,
tuple/string iteration, iter(obj), next(it[, default]) and exception construction
use ordinary object protocols. Raise/reraise/chaining, try/except/else/finally and
synchronous with use verified regions and the existing frame scheduler.
Class definitions and decorators use the existing type/descriptor/call machinery
with separate class namespaces, lexical forwarding and real __class__ Cells.
Experimental int/float/tuple constructors use the same ordinary class call and
descriptor boundary, including Python conversion callbacks and native subclasses.
Experimental isinstance/issubclass handle runtime types, nested class tuples,
metaclass checking hooks and isinstance's __class__ fallback through ordinary
descriptor/call/truth protocols. Class-like __bases__ traversal and union
classinfo are still runtime gates, not claimed by the current slice.
Dictionary/list displays and comprehensions, conditional/lambda/named expressions,
assert, expanded calls, power/shift/bitwise protocols, property/attribute builtins,
Unicode identifiers and f-strings use the existing compiler/VM/protocol path.
Import/from/package syntax remains an experimental implemented slice.
Set/bytes/complex execution, full iteration APIs, super(), exception groups,
suspension, annotations/type parameters, patterns, locale formatting and the
remaining Python 3.12 matrix are
unfinished. These are remaining delivery requirements, not a reduction of the
basic-language objective. Existing native VM/type/cleanup interfaces do not imply
source or library support.

The independent parser recognizes the fixed complete Python 3.12.14 PEG grammar,
including those not-yet-executable families. `Grammar/` is the source of truth;
`Tools/peg_generator/generate.py` uses vendored pegen grammar/SCC analysis and
emits checked-in C++ rules. Ordinary parser builds link only the standard library.
It accepts UTF-8 Unicode source; file encoding-cookie decoding is a loader gate.
The CST adapter in `Parser/ast.cpp` is not a grammar recognizer. The previous
handwritten parser and its dedicated parser tests have been removed; compiler,
execution and precise-root tests remain on the new frontend. The native parser's
generic syntax errors do not claim CPython's invalid_* second-pass wording.

## Experimental Module Foundation

The active module/import stage remains incomplete and experimental. A module
uses its ordinary instance dictionary as the single namespace; layout tracing
owns its type/dictionary/tail fields. There is no second attribute map. The
interpreter's managed Modules dictionary is the sole registry, exposed as
sys.modules. Native initialization and managed source use the same publication
and rollback path. Host-only load records contain synchronization/definition
metadata, never untraced Python values or a second cache of completed modules.

Only a module's initializing owner may finish its lease. Same-thread cycles may
observe its published partial namespace; different modules may execute in
parallel. Waiters park through NativeWait, with initialization dependency cycles
detected before waiting. No registry lock spans Python, allocation/GC or I/O.
Cache reads and publication use the ordinary hash/equality-aware dictionary
operations, including custom keys and Python exceptions. Publication returns an
internal hash/entry-generation token, not a Python reference or second registry.
Failure removes only that entry generation if it still names the original module;
replacement values and deleted/reinserted bindings survive. Conditional removal
does not allocate or invoke equality/hash, including at zero allocation quota.
C/C++ extension ABI and Python import policy
remain distinct from CPython binary compatibility, per Plan 2 section 11.2.

Current inputs are registered immutable SourceUnits, filesystem source/packages,
statically linked C++ initializers and the C extension inputs specified below.
__import__ handles absolute/relative names, fromlist and
__all__, and binds loaded children onto the parent. The low-level C++
import_module entry loads one full-name transaction. Python importlib.import_module
returns the full leaf through the same transaction; reload preserves the module
and its dictionary, including existing bindings, and retains the canonical entry
on execution failure. Native callbacks must publish None or a
Python exception; their code must outlive registration and every entry they
create. This internal C++ function-pointer interface is not a dynamic C ABI.

Module identity/equality/hash inherit object behavior. The ordinary dictionary
tail traces namespace values; __dict__ is read-only as an attribute, while its
bindings remain mutable. Constructors preserve subtype layouts. __getattribute__
uses generic descriptors/namespace lookup before the module-level __getattr__
callback, which receives only the name. ImportError name/path fields are traced
tail slots with None defaults. The CLI's __main__ and imported __main__ are the
same object/namespace; builtins module/dictionary identities are canonical.

File lookup snapshots the current iterable sys.path or parent's __path__,
skipping non-string entries. Package __init__.py takes precedence over .py;
namespace directory portions are combined unless a concrete module/package wins.
Filesystem reads and native source compilation park through NativeWait with only
native data. Source errors become SyntaxError/IndentationError/TabError; source
read failures become OSError, not C++ errors. Full exception constructor fields,
errno subclasses and formatted source diagnostics remain incomplete.
Source imports/aliases/from/relative/star lower through the same scope/CFG/VM.
IMPORT_NAME consumes level/fromlist and pushes the hook result (2 -> 1);
IMPORT_FROM retains the module and pushes its attribute (1 -> 2);
IMPORT_STAR consumes one object and writes the module namespace (1 -> 0).
The first two have a name-index operand; star has none and is module-code only.
All can raise/call Python, keep inputs traced until success, and use ordinary
unwinding. ImportName reads __import__ from actual builtins, never globals.
Builtin hook reads, RunRequest's __builtins__, module metadata/__getattr__ and
relative/from-import cache fallbacks use the same mapping protocol, preserving
Raised instead of treating a callback error as a missing name.
Hooks receive the actual module/class mapping or a lazy Frame-owned function
locals snapshot refreshed from fast locals/Cells; this snapshot is not execution
state. Star honors __all__ with indexed sequence access or snapshots public
namespace keys. Compiler private-name rules preserve raw fromlist spellings.
CLI script entry uses its resolved directory for imports and publishes __file__.

sys.path and regular package __path__ are real mutable lists; exact-list path
reads take a rooted snapshot before filesystem waits, while other iterables retain
their normal protocol. The CLI publishes a mutable sys.argv for script/-c and
trailing arguments. Its existing --eval inspection option remains reserved;
-- separates trailing Python arguments from those host options.
`Lib/importlib/_bootstrap.py` supplies the actual ModuleSpec class, source/memory/
native/namespace loaders and module_from_spec. A small native `_imp` module owns
source/code execution, source-file reading and transaction entrypoints; it does
not define a parallel Python object model. File loaders reread source on
get_source/get_code/exec_module and on reload, not at discovery. Source retrieval
does not compile the file. Code instances may differ across get_code calls;
their type is the same canonical runtime Code type.

Cold bootstrap loads builtins/sys/_imp and the Python seed through the same module
transactions, then installs their real metadata and loads importlib. Concurrent
callers park only until that one-time bootstrap finishes; module body execution
afterward remains independently leased. Failed bootstrap resets its host state
and preserves successful dependencies for retry without recreating their classes.
There is no native cache of Python spec/factory objects. Initializing flags are
set before publication and cleared on success/failure with pre-rooted keys and
values, including zero-managed-allocation cleanup.

The supported spec fields include name/loader/origin/loader_state, package paths,
parent/has_location/cached properties. Module paths and spec paths share identity;
namespace origin and __file__ are None. No .pyc path is inferred: cached defaults
to None (explicit assignments remain mutable). module_from_spec does not register
the result in sys.modules. Importlib machinery/util export the implemented
classes and helper, not a placeholder full CPython importlib API.

Complete sys/module/reflection methods, general PEP 451 finders/path hooks,
Python-defined loader discovery and the remaining container matrix are still
gates. Namespace-package paths are
currently mutable static list snapshots, not full PEP 420 paths. Auto-refresh after a namespace
parent search path changes, path hooks and bytecode caches remain pending.
This node therefore does not complete the active module-stage goal.

## Dictionary And Namespace Storage

Ordinary Python dicts and module/instance namespaces share one insertion-ordered
entry store; name and hash indexes are accelerators, not second binding maps.
Hash/equality/truth callbacks run outside its per-object commit. Matching entries
are rooted, then revalidated by stable identity after reentry; missing-key commits
validate the key version. Replacement, erase and conditional import rollback do
not allocate. Both ordered indexes use preallocated nodes, including the hash
index: O(log n + collisions) lookup rather than an allocation/rehash under lock.
This is not a claim of CPython's amortized constant-time dictionary performance.
Native canonical-name helpers remain internal and do not invoke Python equality.

The public slice includes constructors from mappings/pairs/keywords, subscripts,
deletion, get/setdefault/pop/clear/copy/update, __missing__, equality and recursive
repr. Known hashes survive exact-dict copy/update/equality. Numeric equal keys
preserve the first key object; identity allows NaN lookup. Dynamic keys/values/items
views trace their owner; iterators reject size/key changes, permit value replacement
and stay exhausted. Shared iterator cursors use the owner's short commit.

Type namespaces retain immutable snapshot/CAS publication, including explicit
attribute reads/updates with custom equal keys. Python mappingproxy is read-only
and reflects later publications; copy returns an ordinary independent dict.
Implicit special-method type lookups still use native name helpers, so arbitrary
key aliases there are not a completed compatibility gate. Remaining canonical
writes in the import/VM setup paths initialize newly private/bootstrap mappings;
they do not read an alternative public module registry.
fromkeys/popitem/union/reversed, view set operations, set and remaining source
remaining mapping APIs are still pending; this slice does not complete M3.

## Mutable Lists And Iteration

ListObject has one contiguous Value buffer, traced only during an acknowledged
collector stop. Every ordinary access uses its per-object mutex. Capacity growth
is geometric: allocate a replacement buffer outside the lock, recheck current
size/capacity, then copy/swap in a no-safepoint commit. Append is amortized O(1);
insertion/deletion are O(n). No Python call or host expansion occurs under that
lock. Rooted snapshots keep copied values alive across callbacks/GC. Comparisons
and representation read each current element into a result root before user
callbacks; compound operations are not globally atomic.

The experimental API covers list construction, integer get/set/delete, length,
equality/order, recursive repr, append/extend/insert/pop/remove/count/index,
clear/copy/reverse, concatenation/repetition and their in-place forms. Constructors
and generic extend share tuple construction's length-hint rules; iterator failures
preserve already appended values. Self-extension snapshots exact lists and
materializes a subclass's overridden iterator before appending. List subclasses
use the normal native-prefix layout and ordinary instance tails.

List iterators trace their owner and share its short commit to claim a cursor.
They see later appends before exhaustion, tolerate mutation rather than enforcing
dictionary key versions, and stay exhausted after their owner edge is cleared.
The general iter protocol also supports the __getitem__ fallback, stopping on
IndexError/StopIteration; explicit __iter__ = None still prevents iteration.

BUILD_LIST has a count operand and n-input/1-output stack effect, is verified and
disassembled normally, can allocate/raise, and keeps its operands traced until
construction succeeds. List displays, nested lists, function defaults and
assignment/delete/for targets use the existing scope/CFG pipeline. Starred displays
and calls build private list/dict buffers through verified append/extend/merge
instructions; duplicate keyword detection precedes the ordinary call binder.
Slice objects retain three immutable traced fields and follow Python 3.12's
conditional hashability. Sequence conversion callbacks precede length observation;
list replacements materialize inputs before prepared short local commits.
List/dict comprehensions preserve lexical isolation and outer walrus bindings.
Sort/reversed, iterator hints and the complete direct-special-method matrix remain
gates, not placeholder APIs.

## C Extension ABI V1

`Include/cxxpython/extension.h` defines an experimental, versioned C ABI, not the
CPython ABI. An extension exports `CxxPy_Module_v1`, returning immutable metadata
whose version and size are checked before use. Metadata is copied; getter and
entry addresses stay protected by the library lifetime. Native exec/methods use
the same module initialization transaction and ordinary native-call binder/VM.
The first platform loader accepts POSIX `*.cxxpy.so` modules and package
`__init__.cxxpy.so`; arbitrary `.so` and CPython extension binaries are not inputs.

The API table contains C function pointers, fixed-width integers, sizes and
opaque context/handle pointers, never C++ layout, STL, exceptions or raw Python
object addresses. A context belongs to one synchronous callback on its current
OS thread. Handles remain precisely rooted until that callback returns and must
not be cached in native globals, moved to another thread or reused in another
callback. Module state belongs in that invocation's managed module namespace;
method bindings trace their module. Returned objects/exceptions are copied to
the caller's ReturnSlot before callback-local roots disappear. API failure is
sticky for that callback: returning a success handle cannot hide an exception.
Returning NULL without an exception raises RuntimeError. Exec must return None.

V1 methods take a fixed count of positional-only arguments. Construction,
attributes, calls, raising, index-to-int64 conversion, polling and synchronous
GC-aware host waits are explicit table operations. Wait callbacks use only native
data, not handles/API operations. No API runs Python under a registry lock or
while parked. Background callbacks, stored native Python references, custom C
object layouts, keyword method declarations and FFI are not offered by V1.

Dynamic libraries are pinned per Interpreter until its shared State is destroyed
after attachments/transfers retire. Method bindings also retain their definition
and code lease. There is no user unload API; GC sweep never calls dlclose. This
first lifetime policy deliberately does not claim prompt library unloading or
support arbitrary extension-created background activity. Static C/C++ entrypoint
code must outlive its registration and every callable it creates. Dynamic loader
errors, ABI mismatches and module-name mismatches raise ImportError without
publishing a module. Extension code must not throw C++ exceptions across the C ABI.

## Identity And Lifetime

Value is a non-owning, non-null boxed identity. Python inheritance and immutable
native allocation layout are distinct; payload access validates layout/domain.
The nonmoving precise tracing collector is the sole lifetime authority. Value
copies do not adjust Python reference counts. Constructors may allocate host
storage but cannot run Python, collect or publish partial objects.

RootScope owns stable cells in the attachment's thread-private arena, reusing
each cell's existing link for scope membership/free storage. Explicit non-LIFO
release unlinks before reuse. Handle validates interpreter, arena and generation.
No root operation may be used from another attachment/thread or during NativeWait.

ReturnSlot is a noncopyable caller-owned stable root. Empty, Ok and Raised are
distinct; callees publish before dropping their roots. Immediate accessors read
the current cell after owner/tag checks. Saved ResultViews retain revision and
generation checks and reject overwrite/destruction/reuse. Aliased inputs are
captured before resetting outputs. Native critical publication uses only prepared
roots and bounded no-allocation operations; NoSafepoint is not an execution lock.

## Threads And GC

One Interpreter permits multiple Active OS-thread attachments executing Python
bytecode and object protocols in one heap. An attachment owns roots, active
Frames, span allocation/pressure batches and protocol caches. Normal access does
not consult a mutable global heap map or a whole-interpreter execution mutex.

GC coordinates request/ack/completed epochs; attach, retirement and NativeWait
resume share the epoch gate. Only acknowledged frozen roots are scanned. VM
dispatch polls periodically, including allocation-free loops. NativeWait freezes
roots but grants no detached payload access and does not serialize resume.
Blocking host operations must release heap access through that boundary.

Transfer is a move-only delivery lease backed by a managed, collector-visible
record. Receive roots the payload before unlinking delivery ownership. Handle
and ReturnSlot are never transferred directly between threads. Closing rejects
new admission while existing leases can finish; bootstrap publication alone
requires single-initializer admission.

## Shared Fields And Calls

Header/type references, instance field tails, Cell contents and exception graph
edges use atomic rooted operations. Ordinary name dictionaries use local short
critical sections: prepared insertion nodes, in-place value replacement and
non-allocating conditional erasure, preserving insertion order. DictView owns
rooted stable copies; a lazily materialized storage is borrowed from its view.
TypeView retains its own materialization root until it is published. External
byte accounting is a bounded non-GC metadata operation allowed in these commits.
Type metadata/class dictionaries still publish immutable snapshots outside
short commits. Callbacks, managed allocation and
blocking do not occur under internal publication critical sections.
TypeSnapshot includes actual managed bases/MRO tuples, materialized once at first
publication and retained unchanged through name/dictionary updates. __bases__,
__mro__ and __base__ expose that hierarchy, not newly assembled representations.
Dynamic __bases__ replacement remains unsupported.

Code and Function/default/closure identities are immutable; dictionaries and
referenced values retain their own synchronization semantics. Per-thread protocol
caches guard on Type identity, typed protocol and matching publication epoch.
They hold borrowed identities, are cleared before sweep/address reuse and root
entries before a safepoint. Shared Code does not contain mutable execution caches.

All call shapes use the same binder and caller-owned result boundary. Complete
positional native calls borrow spans or prefix a receiver in bounded stack
storage, with a vector for larger arities. Defaults, keyword-only/variadic cases,
recursion guards, callback roots and return validation remain intact.

Native sequence slot definitions retain their defining Type as a traced immutable
edge. Arithmetic tries numeric/reflected methods before concatenation/repetition
fallbacks, including in-place operations; __index__ runs only at the sequence
stage. Inherited native sequence definitions retain this role, while assigning
the same callable to an unrelated Python class uses ordinary method semantics.
This classification belongs to the callable definition, not a VM list special case.

## Code, Frames And Closures

CodeBuilder verifies opcode domains, bounds, stack joins, call shapes, control
regions and source positions before immutable publication, including unreachable
instructions. Shared Code has actual managed name/constant/metadata tuples.
Private bytecode encoding is not a CPython compatibility promise.
Protected normal exits may explicitly target an else block in the parent scope;
handled exits use the after-handler target. Both are checked independently, so
else exceptions cannot be caught by the preceding try's handlers.

Frame state belongs to one invocation/attachment. Its operand storage is fixed
before execution. The scheduler transfers returns into parent traced storage
before releasing the child; ordinary Python calls are non-recursive in C++.
FrameActivation borrows nonmoving Frame/Code payloads under its existing active
root and refreshes them only on frame switches. Polls and atomic instruction
publication for reflection remain active. Cleanup reasons and traceback graphs
are traced, not hidden in native stack state.

Code has an explicit Function/Module/ClassBody kind. Function code declares
disjoint cell/free layouts; class code may own an implicit __class__ Cell and
forward an outer Cell with the same name. The verifier distinguishes those
layouts and rejects namespace opcodes in optimized function code, class-deref
access to local Cells, and mapping code with fast locals/parameters.
FAST access to captured locals is rejected. Frame preparation roots fresh local Cells and existing free Cells
before publishing its const cell-identity vector. Captured parameters have no
second mutable fast-local authority. Siblings share Cells; distinct factory calls
do not. Function/Cell cycles use ordinary tracing, not reference counting.

LOAD_DEREF performs one rooted atomic read; STORE_DEREF stores the still-traced
input; DELETE_DEREF performs one exchange, not check-then-clear. Empty local Cells
raise UnboundLocalError, free Cells raise NameError. LOAD_CLOSURE captures Cell
identity even when unbound. MAKE_FUNCTION consumes defaults, kwdefaults, closure
tuple and Code in that order when flags 1/2/4 are present. Shape errors use Python
TypeError/ValueError. co_varnames, co_cellvars, co_freevars and __closure__ expose
actual managed metadata; runtime closure creation reuses the scope's Code.

Several threads may call the same Function/Code/closure. Individual shared Cell
edges are synchronized; compound nonlocal += is not implicitly atomic.

## Class Namespaces And Construction

The native AST has class/decorator nodes. Lexical analysis distinguishes class
namespace bindings from fast locals, skips class attributes when resolving a
method's lexical free variables, and independently forwards outer Cells through
the class scope. Class global declarations do not block a method from capturing
an enclosing function's variable. Private identifiers/attributes/parameters are
mangled in the owning class context; function/class name and qualname metadata
retain their source spellings. Call keyword names are not mangled.

Each class-body invocation owns its namespace mapping and fresh implicit class
Cells; Function does not cache that mutable state. Frame traces its mapping.
LOAD_NAME checks the mapping then globals/builtins; LOAD_CLASS_DEREF checks it
before the corresponding free Cell. Exact internal name dictionaries use their
existing storage API; generic mappings use ordinary item protocols. Only KeyError
means missing. Store/delete use the same mapping boundary, including callbacks
and exceptions. This does not introduce a general public dict implementation.

LOAD_BUILD_CLASS reads the actual builtins mapping, not a shadowing global.
The ordinary __build_class__ builtin resolves __mro_entries__, selects the
metaclass, calls __prepare__, executes the body through the same verified VM and
calls the metaclass. It preserves keywords, __orig_bases__ and user exceptions;
callable non-type metaclasses may return arbitrary values. Default type.__prepare__
returns a new name dictionary. Existing type.__new__ still owns C3/layout checks,
descriptor __set_name__, __init_subclass__ and class-cell filling.

Class prologues store actual module/qualname/docstring values. Normal epilogues
publish/return the implicit Cell when needed. After a type result, build_class
checks that the Cell was propagated and filled with that identity; missing and
wrong values are Python RuntimeError/TypeError, not host faults. A scope's Code
is frozen once and reused by subsequent class/factory invocations, while their
types, Functions and Cells are distinct. Decorators evaluate top-to-bottom
before definition expressions and apply bottom-to-top before name binding.

Zero-/two-argument super and its descriptor object, full locals/globals/vars
reflection, general containers, call/base unpacking, annotation scopes/type
parameters and the rest of the type protocol/method matrix remain delivery
requirements. Class source support here is experimental, not complete M2/M5.

## Iteration And Items

get_iter resolves __iter__ on the type and checks the result's __next__ slot.
iter_next distinguishes Item, Done and Raised; Done retains the original
StopIteration (including subclasses) in the caller's exception slot. Other
exceptions are not exhaustion. None is a valid yielded/default value, not a
termination sentinel. The __getitem__-only sequence fallback and two-argument
iter(callable, sentinel) remain unimplemented.

Range stores immutable normalized start/stop/step and arbitrary-integer length.
It rejects zero step, reports len overflow separately, indexes without narrowing
big integers and compares/hashes sequence content. A range iterator atomically
claims its rooted current value only after preparing the successor. Tuple and
Unicode string iterators share an atomic owner/index layout and claim an index
only after preparing its result. Concurrent next calls return each item once;
no interpreter lock or Python callback is held in the claim. Exhaustion is sticky.
Allocating a result/successor unsuccessfully does not consume the current item.
Integer indexing follows __index__; negative indices are normalized, and invalid
indices raise IndexError. Range slicing, reverse/membership helpers and the full
container method matrices remain gates.

tuple() consumes an iterator obtained through get_iter. It returns an exact tuple
argument unchanged, preserves native tuple subclass allocation, and otherwise
roots yielded values before later callbacks/allocation. It requests the original
iterable's length after obtaining the iterator; __len__ can return an __index__
object. A TypeError from length or length-hint invocation declines that hint;
length-hint descriptor binding errors and other exceptions propagate. The type
is looked up again after length callbacks, which may replace __class__.
__length_hint__ accepts only an integer or NotImplemented, checks signed-size
overflow and rejects negative hints. Iterator
errors are not mistaken for exhaustion. Exact empty results reuse EmptyTuple.

FOR_ITER has distinct taken/fallthrough stack effects, both checked by the Code
verifier; exhaustion retains the iterator until explicit cleanup outside the
loop's protected region. Source loops use the same verified Loop regions as
native code. Exhaustion removes the iterator before else; break removes it without
executing else; continue preserves it, including transfers across with/finally.
UNPACK_SEQUENCE collects and roots
the required values, probes for surplus and only then exposes target stores.
Subscript augmented assignment evaluates owner/key once, using the same typed
getitem/setitem dispatch as the public runtime API. List/dict syntax and starred
targets are not approximated by tuple behavior.

BaseException construction uses the native exception layout and a managed args
tuple; args replacement consumes an iterable before atomically publishing its
tuple. StopIteration.value is an independent traced atomic tail field: assigning
args or calling BaseException.__init__ does not overwrite it, while calling
StopIteration.__init__ resets it. Complete exception subtype-specific constructor
matrices remain a gate.
KeyError's one-argument str uses repr of its key; explicitly calling
BaseException.__str__ retains the base behavior. The ordinary str constructor
supports empty/object conversion, object/encoding/errors argument validation
and native-layout-preserving subclasses. Byte buffers/codecs and the complete
Unicode API remain requirements; no existing non-buffer type is silently decoded.

## Numeric Operations

int() accepts decimal Unicode strings and base-0/2-36 text parsing, with ordinary
__index__ conversion of the base, underscore/prefix/sign validation and arbitrary
integer precision. Without an explicit base, conversions prefer __int__, then
__index__, then the Python 3.12 __trunc__ fallback. Conversion results are
validated rather than reinterpreted as native payloads. Native int subclasses
retain their type and instance tail; bool keeps its separate truth constructor.

FloatObject is an immutable native double payload, traced and destroyed through
the same allocation metadata as every object. It has no outgoing payload edges;
its type and optional subclass fields are traced by the allocation layout.
float() accepts integer, Unicode text, __float__ and __index__ inputs through the
ordinary binder/protocols. Exact float-to-float preserves identity; subclass
construction allocates the requested native layout, including ordinary slots.
Large integers round to nearest-even and raise OverflowError outside double
range. Text parsing preserves signed zero, underflow, infinity and NaN; malformed
signs/underscores raise ValueError. repr/str use round-trippable decimal formatting.
Equality with integers compares exact numeric values rather than lossy converted
doubles; hashes agree for equal integers/floats, and NaN is not equal to itself.
int(float) truncates finite values and reports NaN/infinity as Python errors.
Descriptors validate native receivers; no mutable shared numeric cache is added.

Float literals share the pure native text parser with float construction. The
native AST/CFG stores their binary64 value; each attached freeze materializes
ordinary managed constants in immutable Code. Decimal/exponent/underscore forms
and malformed literals are checked without allocating Python objects in parsing.
Signs remain ordinary unary operators, preserving negative zero.

Float +, -, *, /, // and % accept native float/int payloads, including subclasses,
not arbitrary objects with __float__. Comparison handles all six rich relations
against native floats/integers; integers are not narrowed to doubles. NaN is
unordered, infinity compares outside every finite integer, and fractional floats
are ordered against exact integers. Reflected/subclass/inplace dispatch uses the
existing protocol engine; builtins return NotImplemented for incompatible types.
Division/modulo by zero raises ZeroDivisionError. Floor division and remainder
keep divisor-sign/rounding/signed-zero behavior distinct from C++ truncation.

Integer true division rounds the exact integer ratio to nearest-even binary64,
including subnormals, signed-zero underflow and overflow. It does not separately
convert huge operands to double, so finite ratios of enormous integers work.
The / and /= source forms use the existing BINARY/INPLACE instructions and their
2-input/1-result stack effect, with the verified TrueDivide operand and ordinary
__truediv__/__rtruediv__/__itruediv__ slots. No new VM execution path is introduced.

These are experimental numeric slices, not complete numeric support.
Complex values, modular power and remaining integer/float method matrices, bytes/buffer numeric
construction, decimal conversion length limits and conversion warnings remain.
The experimental dictionary slice is specified above. Its remaining API matrix,
set construction/protocols and remaining source container features remain gates.

## Source Exceptions And Cleanup

RAISE takes one exception or an exception plus cause; its operand, stack effect
and exceptional control edge are verified. Values may be BaseException instances
or derived types constructed through the ordinary call path; invalid values or
constructor results raise TypeError. Both source operands are evaluated before
normalization. RERAISE retains the active exception without adding a synthetic
traceback node. Called functions inherit and restore handled state.

The compiler lays out CFG blocks in lexical region-phase order and replays the
same CodeBuilder region operations used by native tests. It does not recreate
unwind in generated Python calls or hidden native state. Except clauses test
ordinary expressions against the real exception hierarchy; alias binding uses
the normal local/global/Cell classification. A small finally region clears an
alias even when the handler deletes it, returns, raises, breaks or continues.
Handlers restore the previous handled exception; else runs only on protected
normal completion; finally runs for every transfer and may replace its reason.

Multiple with items lower to nested With regions. The region is registered after
enter succeeds and before target binding; exit is already cached by the existing preparation
template. Binding failure invokes exit. Exits run in reverse order, receive the
actual type/exception/traceback on errors and may suppress only exceptions, not
return/break/continue. Allocation, calls and suppression truth conversion use the
same rooted and scheduled protocol paths as native with code.

BaseException exposes its actual __context__, __cause__ and __suppress_context__
fields. Links accept instances/None, not classes, and cannot be deleted; explicit
cause assignment enables suppression. The suppression flag accepts only bool.
References and flags remain atomic; explicit graph setters use the existing
exception publication epoch shared with implicit context-cycle repair. These
individual field operations are safe but not a multi-field transaction.
The CLI displays cause/context chains in order, honors suppression and terminates
on explicit graph cycles. This does not promise byte-for-byte CPython diagnostic
formatting, exception-group rendering or the complete traceback library.

## Frontend And Measurement

SourceUnit owns immutable native Source/ScopeInfo/CFG, contains no hidden Python
identities and can be shared by parallel compilation tasks. Attached freeze roots
all managed materialization. RunRequest and the single CLI use this same service,
not a second language path. Native SourceError preserves source spans and category;
runtime faults use Raised and real tracebacks. Globals use actual name dictionaries;
builtins accept a module or dictionary and normalize to its canonical dictionary.
Arbitrary RunRequest namespace mappings and Python compile/exec/eval remain absent.

Native contracts live in Tests/cpp; actual executable differential drivers live
in Tests/python and use fixed CPython 3.12.14 when configured. Lib is reserved for
the interpreter's Python standard library. Benchmark source kernels
are captured verbatim for the paired reference; raw runs retain version/build/hash
provenance and generate tables. Same-Function source, closure, for-loop, exception and method probes
exercise CPU parallelism, but the large absolute CPython cost remains outside the
user's limited-regression acceptance. Scaling or a native-only probe cannot
substitute for measured cost and complexity. Plan 2 section 12.2 permits limited
explained regressions rather than requiring parity for every foundation slice.
