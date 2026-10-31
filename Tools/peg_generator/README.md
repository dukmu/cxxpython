# Standalone Python Grammar Generation

`Grammar/python.gram`, `Grammar/Tokens`, and the `pegen/` Python modules are
unmodified copies from CPython 3.12.14. The upstream license and notices are in
`doc/licenses/python-3.12.14.txt`. No CPython library or parser is linked into the
product. Host Python is needed only to regenerate or check generated files.

`generate.py` is this project's C++ output backend. It uses pegen's own grammar
reader and SCC/left-recursion analysis, preserves ordered choice, lookahead,
repetition, gathering, forced tokens and cuts, and replaces CPython-specific C
AST actions with immutable native concrete-syntax nodes. `invalid_*` diagnostic
rules never accept input; the ordinary grammar decides recognition. After a
generic first-pass syntax failure, a separate parser evaluates safe literal
diagnostic actions extracted from the fixed grammar, including named-node ranges.
Valid input does not incur that pass or token-vector copying. Dynamic formatting,
conditional C actions and complete CPython diagnostic wording remain work; when
no extracted diagnostic applies, the original failure and location are preserved.

`Parser/generated_parser.cpp` and `Include/cxxpython/grammar.hpp` are checked in.
Run `python3 Tools/peg_generator/generate.py` to regenerate; `--check` detects
drift without editing. The parser library depends only on the C++ standard
library. Each parse owns its token stream, memo table and syntax tree.

The lexical state machine in `Parser/tokenizer.cpp` is adapted from pinned
CPython `Parser/tokenizer.c`, without CPython object APIs. Its incremental
`syntax::TokenStream` serves both lazy PEG token requests and `_tokenize`;
public comment/NL/position adaptation belongs to the native module bridge.
Literal decoding is shared in `Parser/string_parser.cpp`, not duplicated in
the lexer. The generated grammar retains semantic literal/conversion checks
that public lexical tokenization intentionally does not apply.

The compiler caches only tokens requested by PEG lookahead/backtracking. It does
not scan later invalid text after an earlier grammar failure. Source Single and
actual prompt Interactive modes are distinct; DontImplyDedent and
AllowIncompleteInput are independent parse flags, matching codeop's native
requirements without embedding Python-library policy in the recognizer.

Native parsing is bounded to 1024 active grammar calls, independently of the
grammar's recognition coverage. This bound is lower than the earlier 4096,
which could exhaust sanitizer-expanded native stack frames before reporting an
error. Highly nested valid inputs can therefore hit this implementation limit
before CPython does. AST adaptation walks transparent productions iteratively,
bounds remaining recursion to 256 and retains its existing 128-node expression
depth limit. Resource-limit errors are not additional language grammar rules.

The public input is UTF-8 Unicode source (the equivalent of `ast.parse(str)`).
Decoding a file's encoding cookie belongs to the source loader, not this API.
Independent build: `cmake -S Parser -B .cache/build-parser` followed by
`cmake --build .cache/build-parser`. `parser_probe` reads source on stdin and
can print a CST with `--dump`; it does not embed or invoke another interpreter.

Grammar recognition is distinct from compiler support: the AST adapter and
scope/VM passes must explicitly implement each executable feature. A successfully
recognized generator, coroutine or pattern is not evidence of its execution.
