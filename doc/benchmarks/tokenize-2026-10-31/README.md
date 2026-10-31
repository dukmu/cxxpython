# Public Tokenizer Long Lines

`Tools/benchmarks/lexing.py` runs identical source through the product and pinned
CPython. Each line contains 5,000 or 10,000 non-ASCII statement fragments, with
two samples per size and interpreter. Counts are asserted before retaining a
run. Complete-process wall and CPU time include startup and imports.

`manifest.json` retains the source kernel and commands. The text files are raw
captures; `Tools/benchmarks/record.py` generates the JSONL, CSV and Markdown
tables. No table or derived speedup is maintained by hand.

This workload checks the new public-tokenizer path, not VM parallelism or the
existing prime benchmark. The measured doubling is not quadratic, but absolute
performance still falls well short of pinned CPython. It does not close N6.
