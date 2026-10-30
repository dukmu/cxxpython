# Standalone Python Grammar Generation

`Grammar/python.gram`, `Grammar/Tokens`, and the `pegen/` Python modules are
unmodified copies from CPython 3.12.14. The upstream license and notices are in
`doc/licenses/python-3.12.14.txt`. No CPython library or parser is linked into the
product. Host Python is needed only to regenerate or check generated files.

`generate.py` is this project's C++ output backend. It uses pegen's own grammar
reader and SCC/left-recursion analysis, preserves ordered choice, lookahead,
repetition, gathering, forced tokens and cuts, and replaces CPython-specific C
AST actions with immutable native concrete-syntax nodes. `invalid_*` diagnostic
rules never accept input; the ordinary grammar decides recognition. Detailed
CPython second-pass error wording is not claimed.

`Parser/generated_parser.cpp` and `Include/cxxpython/grammar.hpp` are checked in.
Run `python3 Tools/peg_generator/generate.py` to regenerate; `--check` detects
drift without editing. The parser library depends only on the C++ standard
library. Each parse owns its token stream, memo table and syntax tree.

The public input is UTF-8 Unicode source (the equivalent of `ast.parse(str)`).
Decoding a file's encoding cookie belongs to the source loader, not this API.
Independent build: `cmake -S Parser -B .cache/build-parser` followed by
`cmake --build .cache/build-parser`. `parser_probe` reads source on stdin and
can print a CST with `--dump`; it does not embed or invoke another interpreter.

Grammar recognition is distinct from compiler support: the AST adapter and
scope/VM passes must explicitly implement each executable feature. A successfully
recognized generator, coroutine or pattern is not evidence of its execution.
