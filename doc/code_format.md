# Native Code Images

`types.CodeType` uses the Python 3.12 constructor argument order. Its byte strings
describe this interpreter's verified VM, not CPython wordcode. Code from another
VM is rejected rather than executed or silently substituted. The format is
implementation-private and versioned; the experimental C extension ABI does not
expose its layout.

## Representation

Every integer below is unsigned little-endian u32 unless specified. Text is a
u32 codepoint count followed by that many u32 codepoints. Python surrogate
codepoints are preserved; values above U+10FFFF are rejected. Counts must fit
within their actual input buffers before storage is allocated.

`co_code` begins with the five bytes `CXXB\x01`, followed by:

1. One byte of CodeKind: function 0, module 1, class body 2.
2. A local-map count and that many indices. The high bit selects `co_cellvars`;
   otherwise an index addresses `co_varnames`. This preserves the VM's fast-local
   numbering, including cells omitted from Python's `co_varnames` view.
3. An instruction count, then one opcode byte and one operand u32 per instruction.
4. A call-site count. Each site contains its positional count, keyword count and
   keyword texts in argument order.
5. One byte indicating a docstring, followed by its text when present.

`co_linetable` begins with `CXXL\x01`, the original first-line number and a position
count. Each position contains line, column, end-line and end-column. Zero line
denotes an absent source position. On reconstruction, nonzero lines shift by the
difference between the supplied `co_firstlineno` and the encoded origin. Thus a
first-line replacement does not require rewriting the image. An empty image
denotes absent source positions for all instructions; execution remains valid.
For legacy Code reconstruction, an even-length delta table is also accepted:
each pair is an unsigned instruction-address delta and a signed line delta, as
returned by `co_lnotab`. Its offsets are native instruction indices. It supplies
line numbers without column information; modern native tables retain columns.

`co_exceptiontable` is empty when there are no regions. Otherwise it begins with
`CXXE\x01` and a region count. Each region contains a kind byte, a parent-present
byte, optional parent index/phase byte, then enter, leave, handler, end-handler,
after, normal-exit, continue, break and depth u32 fields. Region and phase enum
values are declared in `Include/cxxpython/execution.hpp`.

The reflected instruction offsets in `dis`, `co_lines`, `f_lasti` and traceback
objects remain canonical instruction indices. They are not byte positions in
these containers. Existing source-position and descriptor APIs retain that same
offset domain.

## Reconstruction

The constructor validates parameter/name tables, decodes the native images and
reconstructs the lexical region scopes. `VerifiedCode::from_parts` uses the same
opcode, operands, branch, stack-join, source and unwind verifier as compilation.
The supplied stack capacity cannot be smaller than the verified requirement.
No partially constructed Code object is published. Its metadata and image bytes
are immutable, strong GC edges; function globals, defaults and closure cells
remain separate function state.

Malformed or foreign images produce Python ValueError. Argument type errors use
TypeError. Rooted inputs and bounded polls cover decoding and allocation; this
does not serialize other interpreter threads or bypass the GC epoch protocol.

The basic marshal format follows Python's versioned value format. Code payloads
are inherently VM-specific, even when carried in the same marshal record shape.
Native `.pyc` files use a distinct magic number, not CPython's cache identity.
