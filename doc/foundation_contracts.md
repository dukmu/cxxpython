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

VM-facing truth, unary/length/comparison/arithmetic, subscription and iteration
ObjectOps accept borrowed Value inputs as well as Handle inputs. Both forms use
the same owning entry: inputs are validated and copied into precise roots before
an output reset or callback. A Value is not a root and must remain live until
that entry snapshots it; an active Frame's traced stack supplies this lifetime
for VM calls. Handle forms retain arena/generation admission checks and delegate
to the Value form. NativeWait/NoSafepoint, foreign-object rejection, caller-owned
outputs, protocol revalidation and failure semantics are unchanged. This adds no
unchecked execution path or change to the C extension ABI.

N0-N5 and M0-M6 contain experimental slices; complete language, object/protocol,
library, thread/resource, platform/install and performance acceptance remain.
M7-M9/N6 are not complete. No CPython ABI or .pyc compatibility is promised;
the experimental native extension ABI is specified below.

The accepted source subset includes arbitrary integer/float/string literals,
implemented unary/arithmetic/boolean/comparison protocols, name/attribute
operations, if/while/for control flow, calls, function definitions/defaults/variadic
binding, globals/nonlocal, closures, docstrings, tuple expressions, recursive
recursive/starred unpacking and integer/slice/protocol subscripts. Experimental range,
tuple/string iteration, iter(obj[, sentinel]), next(it[, default]) and exception construction
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
Complex execution, full iteration APIs, exception groups,
suspension, annotations/type parameters, locale formatting and the
remaining Python 3.12 matrix are
unfinished. These are remaining delivery requirements, not a reduction of the
basic-language objective. Existing native VM/type/cleanup interfaces do not imply
source or library support.

## Base Builtins, Bytes And Streams

Experimental bytes literals and immutable native BytesObject use ordinary
construction, comparison/hash, repr/str, integer/slice access, iteration,
concatenation/repetition, membership, join and hex/fromhex protocols. __bytes__
precedes index/iterable construction; bytes subclasses retain traced dictionary
tails. Byte iterator claims are atomic and tested with two shared-heap workers
and collection. The immutable payload may be borrowed across NativeWait only
while its precise owner Handle remains live. Native bytearray and byte-oriented
memoryview exports now use synchronized storage and scoped native export tokens.
Native one-element formats preserve byte offsets/strides separately from element
lengths; scalar conversion supports integer, float/binary16, bool and char without
unaligned accesses. Runtime/Python callbacks execute outside storage locks.
The complete buffer-format/exporter/PEP 688 matrix remains a requirement, not
emulated by iterable conversion or an unrooted pointer.

Native BufferExport owns separate precise roots for the underlying data and the
actual exporter, plus a scoped native lease and immutable format/shape metadata.
Direct exports on a memoryview increment its native export counter under a short
per-view commit; release refuses a nonzero counter. Independent view copies do
not count as direct parent exports. The counter's shared native lifetime permits
lease destruction without dereferencing managed storage. PickleBuffer retains
the original exporter and its lease, reusing that boundary for raw views, I/O and
buffer-consuming operations; it does not create a second buffer ownership model.
Its native _pickle module supplies the primitive, not a C++ replacement for the
upstream Python serializer. Multidimensional/general-extension exporters remain
part of the unfinished full buffer matrix.

Native SRE patterns own immutable validated instructions and traced source/group
metadata. Each operation has independent C scratch, bounded GC polling and full
cleanup before propagating errors; no Python RC or execution-wide lock is used.
Scanners claim only their own cursor/cache. Their export token retains mutable
storage and prevents resizing, while versioned snapshots observe same-size
updates without copying immutable subjects at every next step. Match captures
are immutable, with a rooted atomic regs cache. Replacement callbacks run Active
and unlocked. Python regex parsing, flags, caching and replacement policy remain
in the unchanged pinned Lib/re package, not C++ policy substitutes.

The callable/sentinel iterator holds rooted atomic edges, clears both after
exhaustion and preserves retry after other exceptions. Concurrent callbacks are
not one atomic transaction. It is the real iter builtin's two-argument path and
is reused by regex finditer. Full pickle/state, regex/warnings, buffer-release and
platform performance acceptance remain requirements, not implied completion.

Tuple count/index retain the immutable owner and operands across normal equality
and index callbacks. Bounds saturate before normalization; no storage lock covers
callbacks. Warning categories are ordinary traced ExceptionObject instances.
Python warnings policy uses existing synchronized fields/containers and the
current attachment's rooted frame chain, not a second exception or stack system.
Compound filter/context mutations are not advertised as one atomic transaction
or thread-local capture. The sys.version_info native tuple has no mutable tail,
constructor bypass or lifetime authority separate from ordinary tracing.
Int byte conversion snapshots buffer/iterable inputs before native bit import;
__bytes__, element index, signed truth and subtype-constructor callbacks retain
their precise operands. Byte output uses magnitude export and two's-complement
inversion/sign extension without quadratic per-byte whole-integer shifts.

Set/frozenset construction, displays and synchronous set comprehensions use the
same compiler, call and object protocol paths. A SetObject owns a private traced
dictionary table; individual membership/insertion/deletion/pop use its existing
rooted reads and prepared-node commits. No separate hash store or execution lock
is introduced. Frozenset fills its unpublished table before allocating the visible
object and caches its order-independent hash atomically. Subclasses retain their
ordinary native layout and traced tails. Bulk algebra uses rooted snapshots and
short per-key commits, not a promise that an entire multi-operand update is one
atomic transaction. Native weakrefs and generic aliases are integrated; pickle
and the remaining common type APIs are still gates.

Method forms accept iterables, binary operators require set/frozenset, and in-place
operations preserve mutable set identity. Cached hashes are reused only on the
corresponding upstream set/dict paths; ordinary iterable conversion keeps its
observable hash calls and exceptions. Set intersection walks the smaller set
snapshot rather than copying the larger input first. Iterators share the dictionary
cursor implementation under the private table's short lock, returning each key
once to concurrent next calls and detecting size changes.

BUILD_SET's count operand consumes N values and produces one exact set; SET_ADD
and SET_UPDATE require two stack values, consume the item/iterable and retain the
set. All three use normal one-byte opcodes with existing operand encoding, can
allocate/call Python/raise, and are verified and disassembled through the common
opcode definitions. Starred displays and comprehensions do not consult a shadowed
builtins.set. After a starred element, subsequent expressions are inserted before
evaluating the next expression, preserving observable evaluation/hash order.

The native UTF-8/16/32, ASCII, Latin-1 and charmap kernels back str.encode and bytes.decode, with
traced UnicodeEncodeError/UnicodeDecodeError fields and error-handler behavior.
Unknown encodings and handlers are not silently mapped to UTF-8. The traced
per-Interpreter registry backs real Lib/codecs.py and selected encodings modules,
including custom handlers and incremental boundaries. Source-cookie decoding,
other codec families, UnicodeTranslateError and full diagnostic matrices remain.

abs/all/any/callable/chr/ord/divmod/min/max/sum use normal type, iterator and
numeric protocols. super uses the current frame's __class__ Cell/first argument
or explicit bindings, C3 lookup and normal descriptors. Its mutable binding is
published as one immutable rooted tuple rather than three independent fields.
locals/globals/vars/dir use real namespaces and current-thread frame snapshots;
fast locals and Cells remain execution authority, not the reflection dictionary.

compile/eval/exec support the exec/eval source and Code path through SourceUnit,
VerifiedCode and RunRequest. Separate locals mappings execute namespace bytecode;
explicit global declarations still access globals. AST/future flags,
positive optimize, closure execution and the remaining compiler API matrix are
not complete and are explicitly rejected, not silently ignored.

The single input mode uses the generated interactive grammar. PRINT_EXPR consumes
one value, dispatches through the current sys.displayhook and produces no value;
it can call Python/allocate/raise and uses the common verifier/disassembler. Only
interactive module-level expression statements display: nested function/class
bodies retain their normal behavior, and a leading string is not a module docstring.
The default hook uses real sys.stdout, clears/restores builtins._ and follows the
UnicodeEncodeError backslashreplace buffer/text paths. SystemExit is a direct
BaseException subtype with a traced code field; sys.exit preserves tuple argument
identity, including tuple subclasses, during native exception normalization.

The process entry accepts scripts, -c, -m, stdin, -i/-q and help/version options.
REPL input uses typed incomplete-input probing in the same tokenizer/PEG parser,
then compiles single mode into the normal VM. Namespace state survives syntax and
runtime errors; each input releases its temporary roots. NativeWait surrounds
host input waits. CLI path[0] changes preserve the configured library path.
The complete pinned runpy.py locates module/package __main__ through real importlib
and executes in the canonical __main__ dictionary. run_module also supports fresh
namespaces, initialization globals and temporary module/argv restoration on both
success and failure. run_path's pkgutil/marshal/cache dependencies are not complete.
Both stdin source execution and interactive line reads use the real
Python-visible streams, not host getline. Optional GNU readline supplies terminal
editing/history/completion without bypassing ordinary replacement streams. Signal
interruption, emergency native diagnostics and installed/isolated path configuration
remain product/I/O gates. Legacy --eval remains an explicit test-only CLI extension.

print uses the dynamically selected sys.stdout or explicit file, str/write in
observable order, sep/end validation and optional flush. input follows ordinary
prompt/write/flush/readline and EOFError behavior, using the same original sys
namespace as print and hooks. That namespace is a precise Interpreter-owned root,
independent of sys.modules replacement or deletion; it is not a second module
table. sys.exception()/exc_info() read the active handled exception and its current
synchronized traceback, including across Frame suspension and nested calls.
Standard streams are actual TextIOWrapper/buffered/FileIO chains created after
import policy installation; the old _StandardTextIO type is removed. Descriptors
are borrowed with closefd=False; absent descriptors produce None. POSIX defaults
are UTF-8, surrogateescape for input/output and backslashreplace for stderr, with
terminal/stderr line buffering. SIGPIPE remains scoped to the writing thread.
Silent post-execution flushing precedes atexit; final stdout/stderr flush failures
produce exit 120 with ordinary unraisable reporting. Full terminal/signal policy, locale/
environment configuration, audit and the remaining platform/shutdown matrix are
gates. IOBase/FileIO, BytesIO/StringIO, buffered binary streams and TextIOWrapper
also back unchanged pinned Lib/io.py and builtin open.
File operations use descriptor leases and NativeWait, not a lock held across
blocking syscalls. Buffered/text streams claim only their own operation state;
competing threads park, same-thread incompatible reentry raises, and Python
callbacks run without internal mutexes. Decoder-state replay cookies preserve
logical text positions independently of raw descriptor offsets. Encoded pending
text obeys write-through, line buffering and explicit operation barriers, distinct
from writes through the binary buffer. Failed pending output is not duplicated by
a later flush. Complete codec,
warning, subclass and configuration matrices are not implied by this slice.

## Network Resources

The experimental POSIX network foundation uses unchanged pinned socket.py,
selectors.py and socketserver.py. Native _socket supplies actual IPv4/IPv6/Unix
socket operations and address resolution; select supplies the real select syscall.
Only select is advertised, so upstream policy chooses SelectSelector rather than
a fabricated poll/epoll implementation. This is not full socket API/platform parity.

Each socket traces an internal resource object. The latter has a native-only
state and a post-STW finalizer, independent of a public subclass's __del__ and
resurrection. Ordinary GC never performs a potentially lingering socket close in
a collector destructor. The collector quarantines every unreachable Python
finalizer graph first, then selects native finalizers still unreachable after
that trace. A dependent native resource cannot be disposed in the same batch as
its potentially resurrecting Python owner. Final interpreter destruction may
still dispose native resources after every attachment has exited; complete
product shutdown remains a gate.
Metadata is immutable per resource; timeout/fd changes use its short native mutex.
Reinitialization publishes a new rooted resource and closes the previous one.
Unlike pinned CPython's overwrite, it does not leak an already-open old descriptor.

Blocking operations acquire a CLOEXEC duplicate under that mutex, then release
the mutex before any I/O, callback or allocation. The duplicate pins the kernel
open-file description independently of public close/detach or fd reuse. Acquisition,
syscalls and duplicate disposal use NativeWait. This deliberately trades an extra
dup/close pair per operation for a small explicit lifetime contract, not a GIL,
active-call registry or another collector. A close in another thread does not
cancel an in-flight receive; use peer data/shutdown or configured timeouts.

Sends snapshot bytes; receives fill native storage before publishing through the
ordinary synchronized buffer writer. BufferExport retains precise exporter/data
roots and storage leases across conversion callbacks and waits. No kernel call
writes unsynchronized managed bytearray storage. Positive timeouts and sendall
retries use a monotonic deadline; EINTR checks ordinary Python signal delivery.
Select roots a snapshot of each input, returns original objects in input order,
preserves duplicates, restores fd sets on EINTR and never owns caller descriptors.

## Library Protocols

The unchanged pinned abc.py uses real _abc weak registries and epoch-tagged
negative caches. Type abstract flags publish with immutable namespace snapshots;
direct subclasses use prepared dictionary nodes with weak targets and conditional
deletion. Bootstrap failure restores these fields rather than retaining an image
through callback/type edges. Mutable Python class/static wrappers use rooted
atomic callable reads; native image descriptors remain read-only.

Low-level _thread locks and start_new_thread execute through the same shared heap
and OS-thread VM. A Transfer retains state for admission without borrowing an
Interpreter C++ lifetime; close rejects new admissions. Admitted threads keep
their state/precise roots alive, and lock waits park for GC. Python exceptions
are reported through the unraisable hook, except quiet SystemExit. Threading
joins, signals, local and complete product shutdown remain requirements. RLock
provides recursive ownership and save/restore using the same GC-aware waits.

## Weak References And Exit Callbacks

Pinned weakref.py, _weakrefset.py and copyreg.py use the ordinary object/GC path.
Conditional weak-mapping removal snapshots a rooted dictionary binding and
erases only that same entry/value identity after confirming its weak target is
dead. A concurrent live replacement is not removed. Bound-method equality uses
function equality and receiver identity; hashing does not invoke the receiver's
possibly absent hash protocol. Object reduction snapshots ordinary dictionaries
and slots through rooted reads, preserving user constructor/state callbacks.
This is reduction support, not completion of pickle/deepcopy.

The unchanged pinned copy.py now supplies actual shallow/deep-copy policy.
Concrete native types own reconstruction: set/frozenset and bytearray reducers,
tuple/numeric/text/bytes constructor arguments, and range/slice reducers. Exact
payload snapshots bypass subtype conversion hooks, while ordinary constructors,
state hooks and Python memo dictionaries preserve subtype state and graph aliases.
Bytearray snapshots follow the collecting getstate callback. Source graphs may
be read concurrently; compound copying is not an atomic snapshot of concurrent
mutation, and no global graph lock is introduced. Resource/iterator reconstruction
and the complete upstream copy/pickle matrix remain acceptance gates.

BaseException owns an ordinary instance-dictionary tail; __notes__ has no second
storage authority. Concurrent first-use add_note publishes one list through the
existing dictionary commit and appends through normal list synchronization.
Class __set_name__ failures preserve the original exception and attach the
Python 3.12 diagnostic note rather than replacing it with a wrapper exception.

The Interpreter state owns one precise exit-callback root independent of the
replaceable sys.modules and module dictionaries. Atexit registration prepares
nodes before short local commits; unregister compares rooted snapshots outside
the mutex and revalidates stable node identities. Invocation pops each callback
before running Python. A per-state reentrant execution claim prevents concurrent
double invocation without serializing unrelated bytecode or callback execution
under a mutex. Normal product exit invokes these callbacks; joining non-daemon
threads, full embedding finalization and emergency-exit policy remain gates.

The integer-fast itertools.count path reserves its next value under a short
per-object claim and releases before boxing/allocation. Competing OS threads
wait with NativeWait; generic callback-driven count retains an execution claim.
Neither path introduces an interpreter-wide execution lock.

The independent parser recognizes the fixed complete Python 3.12.14 PEG grammar,
including those not-yet-executable families. `Grammar/` is the source of truth;
`Tools/peg_generator/generate.py` uses vendored pegen grammar/SCC analysis and
emits checked-in C++ rules. Ordinary parser builds link only the standard library.
It accepts UTF-8 Unicode source; file encoding-cookie decoding is a loader gate.
The CST adapter in `Parser/ast_public.cpp` is not a grammar recognizer. The previous
handwritten parser and its dedicated parser tests have been removed; compiler,
execution and precise-root tests remain on the new frontend. The native parser's
generic syntax errors do not claim CPython's invalid_* second-pass wording.

An explicit TypeComments parse flag enables comments in the same lexer.
TYPE_IGNORE records are retained separately by the owning Tree, never inserted
into PEG grammar lookahead. CST-to-ASDL mapping supplies type_comment fields and
Module.type_ignores; default parsing remains unchanged. Parser-only flags do not
become inherited future flags or Code traits.

The pinned Python.asdl schema and upstream ASDL reader generate all native AST
node/field/hierarchy metadata. Public Python nodes use ordinary traced instance
dictionaries and attributes. Each owns its immutable canonical type table; the
current type MRO determines its semantic family even after compatible __class__
assignment. Native ASDL nodes contain only owned C++ data. Python/ast.cpp reads
required/optional fields, families, lists, integers, constants and positions while
Active, retaining precise roots through descriptors and collection. Lists use
their normal rooted snapshots; concurrent whole-tree edits are not one atomic
transaction. Only the resulting unmanaged tree crosses NativeWait.

SourceUnit(source) and SourceUnit(native AST) both enter ast_lower.cpp, then the
same scope/CFG/freeze passes. The old CST-to-execution-IR adapter is deleted.
SourceUnit borrows its native AST input only during construction and owns all
resulting graph data. Span retains byte offsets for source input and optionally
owns explicit SourcePosition coordinates for AST input; there is no fabricated
source or ordinal-as-byte-offset conversion. Unknown AST columns use an internal
sentinel and are exposed as None. Scope metadata records a decorated definition's
Code first line separately from its AST node span. The current verified VM still
requires positive source lines: Python AST inputs with non-positive lines raise
an explicit ValueError before bytecode construction, unlike CPython. Removing
that representation limit remains a frontend acceptance gate, not a hidden C++
fault or a fabricated line number. Constant snapshots support signed integers,
complex values, immutable tuples and frozensets without hiding Python identities
in native IR. AST schema/type errors and structural ValueErrors remain distinct
from source/scope SyntaxErrors. Complete type-comment, versioned grammar, error
and complexity matrices remain gates.

The native PEG recursion budget is 1024 active grammar calls. The old 4096
budget could overflow an instrumented native stack before its own check.
This is an explicit implementation complexity limit, not CPython's nesting
limit or a change to the checked-in grammar. Very deeply parenthesized valid
Python can reach it earlier than CPython. AST adaptation iterates through
transparent grammar productions, checks its remaining native recursion before
descent (256 calls) and retains the existing 128-node expression-depth limit.

### Public Tokenization And String Parsing

The current lexer adapts pinned CPython 3.12.14 Parser/tokenizer.c into the
standard-library-only syntax::TokenStream. Compiler token collection and native
_tokenize.TokenizerIter use that one lexical state machine. Stream buffers,
indentation/bracket/f-string state, line snapshots and LexicalDiagnostic are
native data; the standalone parser owns no managed references. ReadLine, Poll
and Warning callbacks are borrowed for one next() call and cleared on every
exit. Direct native users must provide exclusive access to a stream; its public
Python wrapper supplies the execution claim, not an interpreter-wide lock.

The compiler's TokenReader borrows its per-parse TokenStream and source only for
that parse. Generated PEG rules pull tokens lazily; their cache owns token text
and spans across backtracking. A failed grammar does not scan an unrelated suffix
just to collect all tokens. Incomplete-input classification consults the lexer's
actual EOF state. Source Single mode is not the prompt's Interactive mode:
DontImplyDedent and AllowIncompleteInput are independent flags, and only Single
mode synthesizes the permitted EOF terminators for source compilation.

TokenizerIter retains precisely traced reader/encoding/codec-name edges and an
AtomicGcEdge for the managed line cache. It invokes no reader during construction
and refills on demand during iteration, not by eagerly reading all source.
No-encoding readers return str; encoding readers return bytes decoded through the
codec APIs before strict UTF-8 conversion. Reader StopIteration or empty input is
EOF; other callback failures propagate and an exceptional stream exit finishes
that stream. The execution claim spans lexical mutation and Python reader/warning
calls, rejecting concurrent or recursive next() with ValueError. It does not
serialize independent iterators. Calls retain roots across polling, allocation
and callbacks; blocking stream operations use the existing I/O parking boundary.
Character consumption polls every 4096 steps, not inside a lock or a long
NoSafepoint. Native buffer storage is included in external allocation accounting.

LexicalToken contains byte spans for compiler use, public row/character-column
metadata and an immutable native line snapshot with a revision. The bridge
converts the line once per revision and roots subsequent cached reads through
the managed atomic edge. Token tuples own normal managed strings/positions;
neither they nor diagnostics borrow reallocatable lexer-buffer pointers.
extra_tokens selects the public token adaptation, including comments/NL and
operator handling. Unchanged Lib/tokenize.py owns encoding detection, TokenInfo,
untokenize and public error translation; the former deferred native import is
gone. TokenStream presence is not acceptance of the complete public interface.

SyntaxError, IndentationError and TabError use ordinary traced exceptions.
SyntaxError msg/location/print_file_and_line fields have independent synchronized
instance slots and support assignment/deletion rather than being derived from
args on each read. Native construction/str adapt pinned Objects/exceptions.c;
source compilation uses the shared raise_source_error conversion and the public
tokenizer uses structured LexicalDiagnostic. Location-in-args and text-presence
flags are distinct from the diagnostic fields. Full error wording, position,
constructor and product-rendering matrices remain acceptance work.

Unchanged Lib/string.py keeps Template and Formatter policy in Python. Native
_string only supplies the upstream-derived formatter_parser and
formatter_field_name_split factories. Their private module-local native types
have empty slots, immutable metadata, no instance dictionary and no direct
construction/subclassing. Factories capture their types through traced native
function captures; iterators trace immutable source strings and inherit ordinary
object identity/equality/hash/repr. Each next() parses native scalar slice/error
records from a cursor snapshot and claims advancement with CAS before allocating
Python output or raising the parsed error. Cursors never move backwards, so a
lost claim retries from the newly observed position without ABA or an execution
flag. Long scans and initial field splitting/decimal conversion poll every 4096
steps with no mutex or NoSafepoint around parsing. Initial field conversion is
eager; remainder parsing and its errors remain lazy. The separate str.format
renderer has not been unified by this addition. Fixed-reference malformed-input,
retry, dependency, shared-heap GC/thread and performance acceptance is not implied
by copying source or adding fixtures; remaining dependencies belong in the
runtime or their actual Python libraries, not compatibility edits to string.py.

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
callback, which receives only the name. ImportError name/path/msg/name_from fields
are traced tail slots; construction accepts the upstream keyword-only fields and
str uses a string msg before falling back to BaseException args. The CLI's __main__ and imported __main__ are the
same object/namespace; builtins module/dictionary identities are canonical.

Python FileFinder/PathFinder lookup snapshots the current iterable sys.path or parent's __path__,
skipping non-string entries. Package __init__.py takes precedence over .py;
namespace directory portions are combined unless a concrete module/package wins.
Filesystem reads and native source compilation park through NativeWait with only
native data. Source errors become SyntaxError/IndentationError/TabError; source
read failures become OSError, not C++ errors. SyntaxError's structured fields and
shared source conversion are described above; full exception/errno-subclass and
formatted product-diagnostic compatibility remain incomplete.
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
transactions, then installs their real metadata and loads importlib. A separate
stream-loading phase admits the owner's installed finder/loader protocol while
ordinary callers still wait. Initial sys.path contains the configured stdlib;
user path[0] is inserted only after codecs and real standard streams are ready.
Concurrent
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

Ordinary imports honor sys.meta_path, sys.path_hooks and sys.path_importer_cache.
Registered modules go through RegistryFinder; clearing meta_path does not enable
an implicit native fallback. The old C++ file-search policy has been deleted.
Only configured seed definitions during cold bootstrap and the script __main__
entry bypass discovery. Python create_module results may be module-like objects;
package paths and child binding use attribute protocols, not native Module casts.
Reload passes the existing object as target and does not call create_module again.
Finder/creator/executor failures propagate through the same per-module transaction;
finder-populated cache entries and user replacements remain authoritative.

FileFinder supports actual custom loader factories/suffixes. Namespace paths
refresh after parent-path changes and invalidate_caches while preserving path
identity. One immutable (parent, epoch, path) snapshot is published per refresh,
and exact-list tuple construction uses rooted synchronized list snapshots.
Importlib util exposes find_spec, resolve_name, spec_from_loader and
spec_from_file_location on those real objects. The fixed-reference tests include
custom objects/reload, missing parent errors and shutdown discovery errors.

Complete sys/module/reflection methods, directory/mtime/case caches, bytecode
caches, deprecated loader compatibility, warning/metadata/resource integration,
encoding and installation configuration remain gates, as does the remaining
builtin/container matrix. Follow the dependency/removal conditions in
[cpython_compatibility.md](cpython_compatibility.md); the active full-route goal
is not complete.

## Dictionary And Namespace Storage

Ordinary Python dicts and module/instance namespaces share one insertion-ordered
entry store; name and hash indexes are accelerators, not second binding maps.
Hash/equality/truth callbacks run outside its per-object commit. Matching entries
are rooted, then revalidated by stable identity after reentry; missing-key commits
validate the key version. Replacement, erase and conditional import rollback do
not allocate. Both ordered indexes link into the same native node owned by the
insertion-order list. The node, including both intrusive tree hooks, is allocated
before the commit; indexes allocate no separate nodes. Lookup remains
O(log n + collisions), with no allocation/rehash under lock.
This is not a claim of CPython's amortized constant-time dictionary performance.
Native canonical-name helpers remain internal and do not invoke Python equality.

Name indexes borrow their own entry's immutable StrObject text: a string pointer
in mutable nodes, or string_view text in immutable snapshots. Tracing the entries owns
that storage; indexes add no second managed edge or copied character allocation.
Prepared nodes borrow the precisely rooted input key until committed. Erasure
removes the index before unlinking its entry, and destruction does not read the
borrowed text. External-byte accounting charges the characters to StrObject only.
Both indexes detach before their owning list is destroyed. Native node addresses
and list iterators stay stable across publication/swap; snapshots copy only the
plain Entry fields, never index links. Those private storage details live in
Include/internal/dict.hpp rather than the public object header.
A uniform node reserves its name hook even for unnamed keys. Fewer allocations
therefore do not imply that every dictionary's reported native size decreases.
An exact-str index hit uses the stored entry hash without recalculating SipHash;
misses and generic-key paths retain hashing, callback and identity revalidation.

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

Managed payloads retain the formal standard-layout/header-at-offset-zero
requirement. Owned native state uses the single-pointer NativeOwner where
std::unique_ptr's implementation would break that requirement; ordinary local
native ownership still uses std::unique_ptr. NativeOwner only destroys native
state and supplies no second managed lifetime authority. The containing payload
traces any managed edges in that state. Constructor failure destroys the owner
without publishing a partial allocation. Clang 21.1.8 acceptance retains the
layout assertions and -Wall -Wextra -Wpedantic -Werror, not warning suppressions.

RootScope owns stable cells in the attachment's thread-private arena, reusing
each cell's existing link for scope membership/free storage. Explicit non-LIFO
release unlinks before reuse. Handle validates interpreter, arena and generation.
No root operation may be used from another attachment/thread or during NativeWait.
Whole-scope teardown invalidates cells through the scope marker and returns the
existing chain in one free-list splice. Null scope makes retained object/tag bits
inactive for tracing and handle access; acquisition initializes their final
state before exposing a new generation. ReturnSlot empty-cell initialization and
ResultView generation/revision checks are unchanged.

Attachment::try_payload<T>(Value) performs the same attachment, allocation-domain,
live-publication and native-family checks as has_layout. It returns nullptr only
for a different native family; invalid access still throws the existing native
contract error. The returned pointer is borrowed, like payload's reference: it
does not root or pin the allocation, and the caller retains precise owner roots
through any safepoint or callback. Native-family agreement does not replace a
Python subtype/protocol check. This is a C++ embedding helper, not a change to
the versioned C extension ABI or an unchecked access mode.

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

Allocation cursors index available spans by the complete immutable LayoutDescriptor,
including instance fields, not just the C++ family. A thread-local 64-entry hint
checks exact layout equality before selecting its bucket; misses search layouts,
not every occupied span. Full spans leave the available chain, and existing swept
slots are reused without scanning unrelated storage. The acknowledged collector
rebuilds chains and compacts dead layout buckets without allocation. Empty spans
still release their storage; no new retention policy or lifetime authority is added.
Cache storage is included in native metadata accounting and is retired with its
thread. These cursors are not shared execution caches or another heap lock.

GC coordinates request/ack/completed epochs; attach, retirement and NativeWait
resume share the epoch gate. Only acknowledged frozen roots are scanned. VM
dispatch polls periodically, including allocation-free loops. NativeWait freezes
roots but grants no detached payload access and does not serialize resume.
Blocking host operations must release heap access through that boundary.

Heap inspection uses the same epoch rendezvous, without sweeping. Objects are
published into the requesting attachment's precise root arena before other
mutators resume; no Python callback or managed allocation runs while frozen.
Native allocation failure restores the epoch and leaves already published roots
valid. Objects without a Python type are internal storage, not Python values:
get_objects omits them and referent/referrer queries traverse through them, stopping
at the next typed Python object. Header/type edges are real referents. This may
differ from CPython's RC/cycle-only tracking: all boxed Python objects, including
ints and strings, are tracked here. Inspection is a point-in-time graph view, not
a way to make concurrent subsequent mutations atomic. After core bootstrap,
construction-only types, their owner-marked namespace dictionaries and native
descriptors are omitted until the owner is Ready; these Python staging objects
are not treated as transparent internal storage. Pre-core structural host tests
retain their existing inspection contract.

The collector currently has one full-heap generation, reported as generation 2.
collect(0/1/2) all request full collections; get_objects(0/1) are empty, and only
the last get_stats row contains physical sweep/reclamation totals. get_count's
first item is managed allocations since the last sweep (including internal
storage), not allocations minus RC frees; the other items are zero. get_heap_stats
exposes actual byte budgets, roots, spans, allocation and pause counters. These
are tracing diagnostics, not fabricated CPython generational heuristics. There
are no legacy tp_del uncollectable cycles; garbage is initially empty. Generational
threshold tuning, debug/save-all and permanent-generation freezing remain open.

The Interpreter owns the original gc.callbacks list independently of replaceable
module bindings. Each outer Attachment::collect request emits start/stop in Active
state, with generation 2 and actual physical reclamation returned by that request;
concurrent requests may join the same epoch and their callback pairs may overlap.
Finalizer cleanup sweeps belong to that pair, not additional callback pairs.
Callbacks use rooted live-list reads (including mutation), hold no coordinator
lock, and exceptions are unraisable. Recursive collections on that owner thread
still perform normal epoch collection but do not recursively invoke callbacks.
Other threads retain ordinary collection/CPU progress. No callback run claim or
interpreter execution lock is introduced.

Transfer is a move-only delivery lease backed by a managed, collector-visible
record. Receive roots the payload before unlinking delivery ownership. Handle
and ReturnSlot are never transferred directly between threads. Closing rejects
new admission while existing leases can finish; bootstrap publication alone
requires single-initializer admission.

ThreadRecord owns at most one explicit exit-sentinel root and a native-only
release action. Sweep traces that root even after ordinary scopes drain; a
replacement releases only ownership of the previous notification. Retirement
first freezes/exits the mutator, finishes the current epoch and transfers its
allocation spans. It then releases the native notification under the running
coordinator gate before removing the record. The action must not allocate,
access managed protocols or call Python. Native lock waiters cannot resume heap
access until that gate is released, so Thread.join never observes a still-active
Python Frame/root arena. The bounded strong lease differs from CPython's weak
reference-counted sentinel timing and is not a second thread registry.

The native stack-size setting is atomic per Interpreter; Python worker startup
uses per-launch POSIX attributes. No process-global pthread default or execution
lock is changed. The CLI invokes loaded threading._shutdown before atexit; full
embedding, daemon cancellation and abnormal teardown remain separate gates.

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
TypeSpec declares immutable/subclassable flags before construction can reach a
safepoint. TypeState is atomic; normal creation release-publishes Ready only after
the initial namespace snapshot and builder cleanup are complete. Bootstrap
promotion retains its exclusive admission and rollback contract. Native module
initializers that install further attributes after create_native_type still need
their complete cold-import/reload publication matrix; this change does not claim
those multi-step initializers are atomic transactions.

Code and Function code/globals/builtins/closure identities are immutable. Function
defaults, keyword defaults, name/qualname, annotations, module/doc and type-parameter
references are precise atomic edges. Call binding roots both default snapshots
before allocation or callbacks; independent Python attribute assignments are not
an atomic pair update. Referenced dictionaries and values retain their own
synchronization semantics. Per-thread protocol
caches guard on Type identity, typed protocol and matching publication epoch.
They hold borrowed identities, are cleared before sweep/address reuse and root
entries before a safepoint. Shared Code does not contain mutable execution caches.

All call shapes use the same binder and caller-owned result boundary. Complete
positional native calls borrow spans or prefix a receiver in bounded stack
storage, with a vector for larger arities. Defaults, keyword-only/variadic cases,
recursion guards, callback roots and return validation remain intact.
The internal protocol invocation entry borrows operands already owned by its
ObjectOps caller and shares the resolution scope for binding. It does not create
a second input-root set. Public invoke_protocol still roots caller-supplied
receiver/arguments before resetting output. Both entries revalidate stale slots
and retain the selected descriptor across collecting/replacing callbacks.
The internal caller keeps its resolved snapshot scope alive, including after
stale slot refresh, so it reuses that descriptor root rather than inserting
another copy. Public invocation independently snapshots the descriptor as well
as its operands, so callbacks may release caller-owned input roots.
The checked Value subtype path admits local initialized type metadata before an
identity result; identical operands need no second payload check or MRO scan.

Native sequence slot definitions retain their defining Type as a traced immutable
edge. Arithmetic tries numeric/reflected methods before concatenation/repetition
fallbacks, including in-place operations; __index__ runs only at the sequence
stage. Inherited native sequence definitions retain this role, while assigning
the same callable to an unrelated Python class uses ordinary method semantics.
This classification belongs to the callable definition, not a VM list special case.

ResolvedSlot retains that derived NativeSlotKind in the existing type-epoch
snapshot/cache; it is scalar metadata, not another managed edge or cache layer.
The lookup's defining owner controls inherited/copied sequence roles. The checked
Value cache overload validates attachment/domain/live state before reading the
cache, and promotion owns fresh type/owner/descriptor roots. Cache hits validate
the actual destination arena's attachment and heap domain once before promoting
these already-admitted local identities without polling.
Each promoted role still has its own cell/generation and scope lifetime, including
when the destination belongs to another active attachment on the same host thread.
Handle-based public resolution keeps result roots independent of its input Handle,
including when both belong to the same RootScope. Internal Value resolution
avoids only the temporary input-type root; it does not borrow the result's lifetime.

ResolvedSlot also retains NativeCallKind: generic descriptor, native function,
binding native descriptor or owner-checked method descriptor. Lookup derives this
scalar from immutable native callable metadata and the existing weak cache retains
it under the same type/epoch identity. Invocation still revalidates the selected
slot and uses the common binder, receiver/depth and return checks. This does not
cache a native entry pointer or add managed edges; public slots default to generic
classification when constructed without this optional dispatch metadata.

Native math/reduction/struct consumers use the shared float_as_double conversion
rather than constructing a boxed float only to read it back. Float subclasses
use their immutable payload; other values honor __float__ then __index__, with
checked returns, deprecation warnings and original errors (struct translates
conversion failure to its own error). Inputs/results survive collecting and
replacing callbacks. The entry snapshots its input before clearing output and
polls explicitly, so allocation-free reduction loops still acknowledge GC epochs.
The Value conversion entry owns the same input snapshot as the Handle entry;
native callers with a traced result/tuple can avoid an intermediate Handle.
The internal iter_next_owned entry borrows an independent live iterator Handle
from its native caller, while public iteration still snapshots inputs. Both
entries use the same checked protocol dispatch, clear the previous output after
owning/validating the iterator, and poll before lookup: even a
native iterator returning permanent objects without allocation must acknowledge
collection requests. Reusable reduction result slots are cleared before the next
callback and after exhaustion; they do not retain the previous item/exception.
Native tuple/string iteration prepares its candidate in the existing output
slot while retaining a separate rooted owner read. A lost atomic index claim
clears that candidate before polling/retrying; allocation failure does not consume
a character. It does not add a per-item temporary result cell.

Native index conversion shares the CPython 3.12.14 Objects/abstract.c distinction
between an integer protocol result and public exact-int normalization. Native
index_as_int64 consumers read int/bool/subclass payloads without copying them or
honoring overridden __index__; non-integers invoke the protocol once. Strict
integer subclass returns warn before use, and public index() normalizes them.
Inputs and protocol results remain rooted across collecting warning hooks.
The native helper snapshots before output reset/poll, leaves out Empty on success,
and supports caller-selected overflow exceptions/messages or saturating clipping.
Its width remains the existing int64 native-conversion contract, not a new
32-bit platform compatibility claim. List/tuple/string/slice, I/O, POSIX, socket
address and deque consumers share it while retaining their own range/synchronization
policies. Conversion warnings now share the Python policy bridge in Python/errors.cpp;
the documented warnings.warn replacement difference remains.

Ordinary native type methods publish owner-bearing MethodDescriptorObjects;
protocol lookup preserves their native/sequence role and invokes the underlying
entry through the normal binder. Native static/class wrappers retain their own
binding rule. Global native functions have traced atomic module-owner and module
metadata edges. Successful native initialization associates newly exported
functions with that actual module before completing its import lease; re-exported
functions keep their original association. Mutable __module__ values are separate
from the fixed module owner, not derived by scanning sys.modules. Callable
reductions use ordinary names/getattr with precise roots, not a pickle-specific
registry of C function addresses. defaultdict forwards its underlying dictionary
initialization through the same binder/receiver/depth/return-checked call path,
using a rooted keyword snapshot; it does not cast a captured method descriptor to
NativeFunctionObject or execute its raw entry. default_factory is updated before
forwarding, including when dictionary initialization raises.

## Code, Frames And Closures

Experimental frame.clear uses the same atomic execution claim as generator close,
not a global execution lock. Running frames reject it; completed frames drop local
slots, private Cell references and their reflection cache without changing shared
Cell contents. Suspended ordinary generators/coroutines execute normal finally/
GeneratorExit cleanup through the existing scheduler. Ignored cleanup exceptions
use unraisable reporting with the originating Frame. Async-generator finalizer
hooks/clear and complete public frame reflection remain acceptance gates.

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
identity even when unbound. MAKE_FUNCTION consumes defaults, kwdefaults, an
annotation dictionary, closure tuple and Code in that order when flags 1/2/8/4
are present. Shape errors use Python
TypeError/ValueError. co_varnames, co_cellvars, co_freevars and __closure__ expose
actual managed metadata; runtime closure creation reuses the scope's Code.
Function parameter/return annotations normally evaluate in the defining scope,
retain private-name mangling, and publish through a traced atomic __annotations__ edge.
Lazy empty dictionaries, replacement, None reset and deletion share that edge.
With future annotations, pure-native AST unparsing supplies canonical strings
before mangling. Temporary annotation scopes validate syntax without introducing
runtime bindings/closures. Local variable annotations still affect symbol/function
classification but do not evaluate or emit nested Code objects.

The `type Alias = expr` path creates a traced
TypeAliasType with a lazy evaluator and a parameter tuple. `_typing`
exports that native type; its constructor takes a name, value and keyword-only
type_params. Name, parameters, evaluator and module are immutable traced edges;
the cached value is an atomic GC edge. A successful evaluation is published by
compare-exchange and a losing reader loads the published value. Concurrent
first readers may execute the evaluator more than once; exceptions propagate and
are not cached. Constructor values are already populated. Alias fields expose
name, module, value and parameters; non-generic aliases reject subscription,
and aliases participate in unions.

Class-local alias evaluators capture a __classdict__ Cell. Class construction
removes __classdictcell__ from the class dictionary and fills the Cell with that
dictionary; name reads consult it before the enclosing Cell or globals/builtins.
MAKE_TYPE_ALIAS consumes three stack values and produces one;
LOAD_FROM_DICT_OR_GLOBALS and LOAD_FROM_DICT_OR_DEREF replace one mapping with
one value. All three can raise and are safepoints. Missing VM names populate
NameError.name; its writable instance slot uses normal traced storage.
Static native types retain declared_module as immutable native metadata so a
type-level __module__ remains available even when the namespace entry is an
instance descriptor. Mutable types use their own namespace publication path.
Native TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs and
Generic types are exported by `_typing`. TypeVar and ParamSpec and TypeVarTuple
instances have native dictionary and weakref storage; the ParamSpec views have
weakref storage without an instance dictionary. The parameter types hide the
`__dict__` descriptor while still permitting instance attributes. TypeVar's lazy
bound and constraints evaluators are traced and their caches are atomic GC edges;
successful values are published atomically, while exceptions are not cached.

Generic functions, aliases and classes use an independent type-parameter scope
identified by AnnotationRole. Function defaults evaluate in the outer scope;
annotations evaluate in the parameter scope. Bounds and constraints have their
own annotation roles, and class-local annotation scopes can read the captured
class dictionary. The compiler emits MAKE_TYPE_VAR, MAKE_PARAM_SPEC,
MAKE_TYPE_VAR_TUPLE, MAKE_GENERIC_BASE and SET_FUNCTION_TYPE_PARAMS for this
path. Generic class lowering is connected, not evidence of working library
closure or acceptance.

Generic policy is delegated through actual imports of `typing`, including the
classmethod hooks for Generic, generic base construction, non-None native
TypeVar bounds, substitution, unions and TypeVarTuple unpacking. Import and
policy errors propagate; no synthetic typing module is supplied. The current
typing/re/warnings dependency chain is not closed. Reference-tested native
constructors and generic function/alias scopes do not complete the whole gate.

Variable annotations use ordinary assignment/item/attribute protocols and retain
the reference's RHS/target/annotation order. Simple module/class names store into
__annotations__; parenthesized names and complex targets do not. Bare attributes
and subscripts evaluate the required receiver/index expressions without doing an
item or attribute read. SETUP_ANNOTATIONS is a raising/safepoint-bearing 0->0 Name
instruction restricted to mapping Code. Module/class prologues initialize the
local annotations mapping even if a declaration is in an untaken branch. Exact
dict initialization uses existing atomic setdefault; custom mappings use their
Python item protocols. Type.__annotations__ consults only the class's own
namespace, binds descriptor values and lazily publishes an empty dict through
the existing metadata CAS path, not an independent TypeObject edge.

SourceUnit accepts explicit compiler flags and exposes its effective future mask;
VerifiedCode freezes it along with actual nested/generator/coroutine/argument
traits. co_flags uses Python bit assignments for those real traits, without
claiming CPython instruction encodings. compile handles supported historical and
annotation flags/dont_inherit; exec/eval inherit only for source strings, not
precompiled Code. Imports do not inherit the importer's flags. REPL state retains
future directives across input units and ordinary -i scripts. Barry comparison
syntax, other compile modes/options, full type-parameter scope/typing acceptance and -i
after -m flag handoff remain separate gates.

The parser-only flags 0x200 (DontImplyDedent) and 0x4000
(AllowIncompleteInput) never leak into co_flags or future inheritance. Builtin
compile snapshots source/filename before NativeWait; native SourceBuffer,
parsing, scope analysis and CFG construction access no managed objects while
parked. The attachment resumes before freezing Code or raising a Python source
exception. Unchanged pinned codeop.py owns command classification and future
flag memory; unchanged code.py owns InteractiveInterpreter/InteractiveConsole.

ASYNC_YIELD_VALUE has the same one-slot resumption effect as YIELD_VALUE, may
allocate/raise and is a safepoint. Verification requires both generator and
coroutine code flags. It boxes only user async yield values; an inner await uses
the existing YIELD_FROM delegation and does not box its suspension. Async request
objects own their generator and input arguments through precise tracing. Their
generator's running claim survives await suspension; no execution-wide lock or
event-loop implementation is used as a substitute for OS-thread safety.

PREPARE_ASYNC_WITH has the same one-input/two-output preparation effect as
PREPARE_WITH, may raise and is a safepoint. The existing With region retains an
asynchronous flag; entry and every cleanup path await through the normal
GET_AWAITABLE/YIELD_FROM continuation. Verification checks operand domains,
coroutine flags, stack joins and the exact awaited cleanup template. No native
stack local carries a cleanup reason across suspension. aiter/anext use class
special-method lookup; the default wrapper traces both operands and rejects
concurrent/reentrant execution using its own atomic claim, not a GIL.

GET_AITER replaces one input with its class-protocol async iterator. It may
call/allocate/raise and is a safepoint; it is also legal in ordinary code when
constructing an async generator expression. FOR_AWAIT requires coroutine code,
has one protected iterator input, pops none, and pushes one result on success or
none on the exhausted branch. It is a raising safepoint with a verified target.
It fuses next/await/exhaustion using the existing Frame delegate and resume state,
not a temporary local, sentinel or new scheduler. Only exceptions from obtaining
or awaiting the next item can terminate iteration via StopAsyncIteration; target
binding and loop-body exceptions unwind normally. Break/continue/else and cleanup
retain existing Loop regions. Async collection comprehensions implicitly await
their child function; async generator expressions stay lazy after eager outer
iterator acquisition. Nested async comprehensions propagate coroutine scope
requirements without leaking comprehension bindings.

GNU readline is a native external-resource boundary, not execution serialization.
The C++ embedding boundary pairs NativeWait with NativeCallback(NativeWait &).
NativeCallback is owner-thread-only: it waits for a pending GC epoch before
admitting Active managed work, then republishes frozen roots on scope exit,
including native exception unwind. Callback-local roots must end before that
exit; caller-owned result slots may retain callback results. The surrounding
NativeWait must outlive the callback. This neither admits a foreign OS thread nor
creates a second Attachment; foreign callbacks still need ordinary admission.

_curses uses the platform ncursesw/terminfo library, with unchanged pinned Python
curses policy. Python argument conversion and result/exception allocation happen
Active and outside the process-native resource mutex. Whole ncurses calls and
waits for that mutex use NativeWait, including blocking input/output. Window
objects precisely trace their exception type and own a separate native Resource;
parked calls access that native storage, not managed payload fields. Native
window disposal uses the existing post-STW finalizer mechanism, never a blocking
C++ collector destructor. Constructor failure releases newly acquired windows.
The mutex coordinates ncurses' process-global terminal state, not Python
execution; unrelated threads can execute and collect while a reader waits.
The upstream wrapper restores keypad/echo/cbreak/screen state on ordinary and
exceptional exits. newterm reports initialization failure without exiting the
process. The process-native screen cache adopts its terminal and releases the
temporary setupterm object; repeated wrappers reuse the screen rather than
losing terminfo allocations. Product LC_CTYPE is initialized from the environment before
creating interpreter threads; embedders retain responsibility for libc locale.
Subwindows/pads, panels, full character/cell/resize APIs, textpad and multi-screen
session policy remain open, not supported merely because Python files exist.
The versioned C11 extension ABI is unchanged by this C++ scope API.
Readline's module-owned typed state precisely traces callback edges and owns a separate
native history snapshot. A process-local lease swaps that state into GNU's globals
and coordinates competing readline operations with GC-aware parking. Reentrant
configuration calls during callbacks remain valid; recursive terminal reads are
rejected. Whole GNU calls park, callbacks execute Active, and tty/callback/stream state
is restored on exit. GNU longjmp signal handling is disabled. Shared-heap Python
workers can collect/compute during input waits and output backpressure. Full signal integration,
display hooks, automatic rlcompleter and reload/history policy remain gates;
the specific remaining curses gaps are listed above.

Native _opcode metadata reads immutable Code data into rooted results. Python
opcode/dis supply tuple protocols, instruction iteration, recursive formatting,
source positions and traceback/current-offset selection. Public offsets are VM
instruction indexes. Source columns retain zero-based UTF-8 byte offsets. Code-
aware Bytecode.stack_effect accounts for actual call-site layouts; standalone
effects must not interpret call-table indexes as arities. No CPython co_code,
cache bytes or specialization records are fabricated. Full upstream reflection
and control-region inspection remain work.

Code.co_positions() returns one immutable tuple per real instruction;
co_lines() merges consecutive same-line instruction ranges. Their snapshot
iterators keep the precise owner rooted while allocating and polling. Readonly
Frame.f_lasti uses the existing atomic instruction cursor. Lib/traceback.py
adapts only its private position lookup from CPython wordcode offsets to these
instruction indexes. Focused exception/console formatting works; public ast,
unicodedata, frame.clear and the complete traceback/reflection matrix remain
dependencies rather than fabricated metadata or empty native modules.

BaseExceptionGroup and ExceptionGroup use the existing exception storage plus
readonly message/child slots. Construction validates children and group families;
derive/subgroup/split retain ordered structure and normal metadata/notes rules.
ExceptionGroup is a mutable heap type, while BaseExceptionGroup is immutable.
Unchanged pinned contextlib.py exercises synchronous/asynchronous managers and
stacks plus group suppression. except* uses existing Except/Finally regions and
traced operand-stack state for splitting, handler cleanup and recombination;
nested group diagnostics and complete serialization remain separate gates.

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

Base super and namespace reflection are specified above. Full reflection,
general containers, call/base unpacking, annotation scopes/type parameters and
the rest of the type protocol/method matrix remain delivery requirements.
Class source support here is experimental, not complete M2/M5.

## Structural Patterns

match/case lowers from the generated grammar through typed AST/scope analysis
into verified CFG blocks. Literal/singleton/value, capture/wildcard, sequence,
mapping/rest, class, OR and AS patterns share normal expression and name-store
semantics. Guards run after binding, including yield/await suspension. Capture
values and saved subjects remain precise operand-stack roots; partial structural
failure discards temporary captures. Python does not specify whether bindings
from a failed pattern survive, so this implementation does not publish them.
Duplicate names/constant keys/attributes, inconsistent OR bindings, multiple stars and
unreachable irrefutable alternatives are compile-time errors. Pattern attribute
keywords keep their literal names; binding comparisons use source identifiers,
while captured private names use ordinary mangling only for the final store.

MATCH_SEQUENCE and MATCH_MAPPING retain one operand and append a boolean using
the type's atomic collection classification. GET_LEN retains the subject and
appends its protocol length. MATCH_KEYS retains subject/keys and appends an exact
values tuple or None. MATCH_CLASS consumes subject/class/keyword names and
produces an exact attribute tuple or None; its count operand does not alter the
fixed 3->1 stack effect. COPY_DICT_WITHOUT_KEYS consumes subject/keys and produces
a new dict. Raising operations are safepoints; type-flag checks cannot callback.

Collection classification follows MRO priority, ABC initialization and explicit
ABC registration (including existing descendants). Immutable native types keep
their declared flags; strings/bytes/bytearrays are not sequence patterns. Atomic
publication and post-subclass-link inheritance close the registration/creation
handoff without serializing Python execution. Mapping patterns use get(key,
sentinel), not __missing__; rest copies use normal mapping protocols. Class
patterns use the same instance-check and attribute protocols as the builtins,
including AttributeError-as-mismatch and exact __match_args__ tuple/str checks.
Wildcard-only sequences avoid item reads, and wildcard stars avoid unnecessary
iteration, matching the fixed reference's observable callback behavior.

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
and native-layout-preserving subclasses. Complete buffer/codec and Unicode APIs
remain requirements beyond the bytes and codec kernels specified above; no
existing non-buffer type is silently decoded.

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
The experimental dictionary slice is specified above. Its remaining APIs and
source container features remain gates. Set slices
and their remaining common-type integration are specified above.

## Source Exceptions And Cleanup

RAISE takes one exception or an exception plus cause; its operand, stack effect
and exceptional control edge are verified. Values may be BaseException instances
or derived types constructed through the ordinary call path; invalid values or
constructor results raise TypeError. Both source operands are evaluated before
normalization. RERAISE retains the active exception without adding a synthetic
traceback node. Called functions inherit and restore handled state.

except* lowers to the ordinary Except/Finally region engine with the original
exception, raised-result list and remainder on the traced operand stack, not
hidden Python locals or a separate Frame scheduler. CHECK_EG_MATCH consumes
(remainder, matcher) and produces (rest, match), publishing a non-None match as
the handled exception. Partial group matches honor overridden split methods;
subgroup creation and projection use derive. Both results remain rooted before
any following instruction. The compiler uses identity comparison with None,
not a subgroup's user-defined truth protocol. Handler-body failures are caught
by a nested ordinary Except region after alias-finally cleanup, appended in
order, and do not skip later starred clauses.

PREP_RERAISE_STAR consumes (original, results) and produces an exception or None.
It classifies raised versus reraised entries by actual traceback/cause/context
identity, projects duplicate leaf identities back into the original group tree,
and preserves ordering of genuinely new exceptions. Python __notes__ copies are
not CPython 3.12's unused native notes field and do not determine reraising.
RERAISE 0 uses the active exception; RERAISE 1 consumes an explicit exception
without adding a traceback or relinking its context. Arity, handler boundaries,
protected stack prefixes and exceptional edges are verified; opcode/dis expose
these real effects and operands. A naked exception's wrapper gets the current
handler frame traceback rather than borrowing the child's traceback chain.
All suspension/handoff state remains in existing Frame roots and handled-state
restoration. return and escaping break/continue are rejected inside starred
handlers, while nested loops/functions keep their ordinary control contracts.

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
RunRequest accepts distinct globals/locals, with Python compile/exec/eval and
their remaining option/mode gates specified above.

Native contracts live in Tests/cpp; actual executable differential drivers live
in Tests/python and use fixed CPython 3.12.14 when configured. Portable fixtures
are Tests/cases/<name>/test.py with optional real stdin.bin input and byte-exact
stdout/stderr expectations. Tests/nogil_cases uses the same driver for explicitly
different concurrent contracts: concurrent atexit execution must invoke each
callback at most once, unlike the pinned reference's private-function behavior.
These are separate from, not skipped inside, the reference compatibility suite.
Lib is reserved for
the interpreter's Python standard library. Benchmark source kernels
are captured verbatim for the paired reference; raw runs retain version/build/hash
provenance and generate tables. Same-Function source, closure, for-loop, exception and method probes
exercise CPU parallelism, but the large absolute CPython cost remains outside the
user's limited-regression acceptance. Scaling or a native-only probe cannot
substitute for measured cost and complexity. Plan 2 section 12.2 permits limited
explained regressions rather than requiring parity for every foundation slice.
