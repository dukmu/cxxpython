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
M7-M9/N6 are not complete. No CPython ABI, .pyc or extension ABI is promised.

The accepted source subset includes arbitrary integer/float/string literals,
implemented unary/arithmetic/boolean/comparison protocols, name/attribute
operations, if/while/for control flow, calls, function definitions/defaults/variadic
binding, globals/nonlocal, closures, docstrings, tuple expressions, recursive
non-starred unpacking and integer/protocol subscripts. Experimental range,
tuple/string iteration, iter(obj), next(it[, default]) and exception construction
use ordinary object protocols. Raise/reraise/chaining, try/except/else/finally and
synchronous with use verified regions and the existing frame scheduler.
Class definitions and decorators use the existing type/descriptor/call machinery
with separate class namespaces, lexical forwarding and real __class__ Cells.
Experimental int/float/tuple constructors use the same ordinary class call and
descriptor boundary, including Python conversion callbacks and native subclasses.
Mutable containers, slices, full iteration APIs, super(),
exception groups, imports, suspension, f-strings, complex/power protocols,
full Unicode source handling and the remaining Python 3.12 matrix are
unfinished. These are remaining delivery requirements, not a reduction of the
basic-language objective. Existing native VM/type/cleanup interfaces do not imply
source or library support.

## Experimental Module Foundation

The active module/import stage remains incomplete and experimental. A module
uses its ordinary instance dictionary as the single namespace; layout tracing
owns its type/dictionary/tail fields. There is no second attribute map. The
interpreter's managed Modules dictionary is the sole registry, later exposed as
sys.modules. Native initialization and managed source use the same publication
and rollback path. Host-only load records contain synchronization/definition
metadata, never untraced Python values or a second cache of completed modules.

Only a module's initializing owner may finish its lease. Same-thread cycles may
observe its published partial namespace; different modules may execute in
parallel. Waiters park through NativeWait, with initialization dependency cycles
detected before waiting. No registry lock spans Python, allocation/GC or I/O.
Failure removes an entry only if it still names that initialization's module;
user replacements must survive. C/C++ extension ABI and Python import policy
remain distinct from CPython binary compatibility, per Plan 2 section 11.2.

Current inputs are registered immutable SourceUnits and statically linked C++
initializers. __import__ handles their absolute/relative names, fromlist and
__all__, and binds loaded children onto the parent. The low-level C++
import_module entry loads a registered full-name transaction; it is not the
unimplemented Python importlib API. Native callbacks must publish None or a
Python exception; their code must outlive registration and every entry they
create. This internal C++ function-pointer interface is not a dynamic C ABI.

Module identity/equality/hash inherit object behavior. The ordinary dictionary
tail traces namespace values; __dict__ is read-only as an attribute, while its
bindings remain mutable. Constructors preserve subtype layouts. __getattribute__
uses generic descriptors/namespace lookup before the module-level __getattr__
callback, which receives only the name. ImportError name/path fields are traced
tail slots with None defaults. The CLI's __main__ and imported __main__ are the
same object/namespace; builtins module/dictionary identities are canonical.

File finders, import/from syntax, complete sys/module/reflection methods,
ModuleSpec/loader objects and Python importlib, general dictionary operations,
mutable list-based path/argv, and dynamic C/extension loading are still gates.
The bootstrap path/argv tuples are experimental, not full sys compatibility.
This node therefore does not complete the active module-stage goal.

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
Complex/power and remaining integer/float method matrices, bytes/buffer numeric
construction, decimal conversion length limits and conversion warnings remain.
The internal name dictionary is still not the general Python dict API; general
dict/list/set construction, storage, protocols and source syntax remain gates.

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
