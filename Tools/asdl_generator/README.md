# Python AST Schema

`Grammar/Python.asdl` and this directory's `asdl.py` are copied from CPython
3.12.14 `Parser/`. The source license is in
`doc/licenses/python-3.12.14.txt`. `generate.py` uses that upstream ASDL reader
to generate native node kinds, fields, attributes, hierarchy and documentation.
It does not generate or recognize Python grammar.

Run `python3 Tools/asdl_generator/generate.py` to regenerate
`Include/internal/ast_generated.inc`; `--check` compares generated text. Normal
C++ builds require neither host Python nor CPython. The native tree contains
only caller-owned C++ data, not Python object addresses or hidden managed roots.
`Modules/_astmodule.cpp` materializes ordinary traced, subclassable Python nodes
through the generated schema. `Lib/ast.py` retains upstream Python policy.

`Parser/ast_public.cpp` adapts the existing generated CST to ASDL;
`Parser/ast_lower.cpp` lowers that tree to execution IR. SourceUnit and builtin
compile(AST) use the same lowerer, scope analysis, CFG and freeze. The former
CST-to-private-IR adapter has been deleted, not retained as a fallback.

`Python/ast.cpp` snapshots managed nodes while Active, using generated field
families, rooted attribute callbacks and synchronized list snapshots. Only owned
native data crosses the compilation parking boundary. Node identity follows the
current type MRO, including compatible __class__ assignment, not a construction
tag. Host native AST inputs can use explicit SourcePosition coordinates in Span;
no fake source text or CPython bytecode offsets are synthesized.

Public type comments, historical feature-version grammars, complete diagnostic
matrices, non-positive source lines in verified code and frontend
complexity/performance acceptance remain open gates. A
complete ASDL schema and passing executable slices do not close the full goal.
