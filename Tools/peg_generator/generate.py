"""Generate the standalone C++ PEG parser from fixed CPython grammar inputs.

Uses CPython 3.12.14's pegen grammar reader and left-recursion analysis. Python
AST actions are intentionally replaced with native concrete-syntax nodes. The
generated program has no Python or external parser runtime dependency.
"""

import argparse
import ast
import hashlib
import io
import re
from pathlib import Path
import tokenize

from pegen import grammar as g
from pegen.grammar_parser import GeneratedParser
from pegen.parser_generator import compute_left_recursives
from pegen.tokenizer import Tokenizer

ROOT = Path(__file__).resolve().parents[2]


def rule_id(name):
    return 'default_' if name == 'default' else name


def cpp_text(text):
    return 'U"' + ''.join(
        '\\' + ch if ch in '\\"' else ch if 32 <= ord(ch) < 127
        else f'\\u{ord(ch):04x}' for ch in text) + '"'


def diagnostic(alt, rule):
    # Only unconditional, literal-only actions are safe without evaluating C AST actions.
    action = alt.action or ''
    if rule == 'invalid_def_raw':
        match = re.fullmatch(r'\s*RAISE_INDENTATION_ERROR\s*\(\s*("(?:[^"\\]|\\.)*")\s*,\s*(\w+)\s*->\s*lineno\s*\)\s*', action)
        if match:
            literal, name = match.groups()
            message = ast.literal_eval(literal)
            indexes = {item.name: index for index, item in enumerate(alt.items) if item.name}
            if name in indexes and message.count('%d') == 1 and '%' not in message.replace('%d', ''):
                return '_INDENTATION', message, [indexes[name]]
    match = re.fullmatch(r'RAISE_SYNTAX_ERROR(_KNOWN_LOCATION|_KNOWN_RANGE|_STARTING_FROM|_ON_NEXT_TOKEN)?\s*\(\s*(.*?)\s*\)', action)
    if not match:
        return None
    kind, args = match.groups()
    parts = re.fullmatch(r'(?:(\w+)\s*,\s*)?(?:(\w+)\s*,\s*)?("(?:[^"\\]|\\.)*")', args)
    if not parts:
        return None
    first, last, literal = parts.groups()
    message = ast.literal_eval(literal)
    if '%' in message:
        return None
    count = {None: 0, '_ON_NEXT_TOKEN': 0, '_KNOWN_LOCATION': 1,
             '_STARTING_FROM': 1, '_KNOWN_RANGE': 2}[kind]
    names = [name for name in (first, last) if name]
    if len(names) != count:
        return None
    indexes = {item.name: index for index, item in enumerate(alt.items) if item.name}
    if any(name not in indexes for name in names):
        return None
    return kind, message, [indexes[name] for name in names]


class Generator:
    def __init__(self, grammar, tokens):
        self.grammar = grammar
        self.tokens = tokens
        compute_left_recursives(grammar.rules)
        self.keywords = set()
        self.collect(grammar)

    def collect(self, item):
        if isinstance(item, g.StringLeaf):
            value = ast.literal_eval(item.value)
            if item.value.startswith("'") and value.isidentifier():
                self.keywords.add(value)
        elif isinstance(item, g.Grammar):
            for rule in item.rules.values():
                self.collect(rule)
        elif isinstance(item, (g.Rule, g.Group)):
            self.collect(item.rhs)
        elif isinstance(item, g.Rhs):
            for alt in item.alts:
                self.collect(alt)
        elif isinstance(item, g.Alt):
            for child in item.items:
                self.collect(child.item)
        elif isinstance(item, g.Gather):
            self.collect(item.separator)
            self.collect(item.node)
        elif isinstance(item, (g.Opt, g.Repeat0, g.Repeat1, g.Lookahead, g.Forced)):
            self.collect(item.node)

    def expression(self, item):
        if isinstance(item, g.NameLeaf):
            if item.value in self.grammar.rules:
                return f'rule_{item.value}()'
            if item.value not in self.tokens:
                raise ValueError(f'unknown grammar terminal {item.value}')
            return f'expect(TokenKind::{item.value})'
        if isinstance(item, g.StringLeaf):
            return f'expect({cpp_text(ast.literal_eval(item.value))})'
        if isinstance(item, g.Cut):
            return 'commit(cut)'
        if isinstance(item, g.Group):
            return self.choice(item.rhs, 'Group')
        if isinstance(item, g.Rhs):
            return self.choice(item, 'Group')
        if isinstance(item, g.Opt):
            return f'optional([&] {{ return {self.expression(item.node)}; }})'
        if isinstance(item, (g.Repeat0, g.Repeat1)):
            minimum = int(isinstance(item, g.Repeat1))
            return f'repeat([&] {{ return {self.expression(item.node)}; }}, {minimum})'
        if isinstance(item, g.Gather):
            return (f'gather([&] {{ return {self.expression(item.separator)}; }}, '
                    f'[&] {{ return {self.expression(item.node)}; }})')
        if isinstance(item, g.Lookahead):
            positive = 'true' if isinstance(item, g.PositiveLookahead) else 'false'
            return f'lookahead({positive}, [&] {{ return {self.expression(item.node)}; }})'
        if isinstance(item, g.Forced):
            return f'required([&] {{ return {self.expression(item.node)}; }})'
        raise TypeError(type(item))

    def choice(self, rhs, name):
        alternatives = [(index, alt, diagnostic(alt, name)) for index, alt in enumerate(rhs.alts)]
        if name.startswith('invalid_'):
            alternatives = [item for item in alternatives if item[2]]
        if not alternatives:
            return 'NodePtr{}'
        lines = ['[&]() -> NodePtr {', 'const auto start = mark_;', 'bool cut = false;']
        for index, alt, diag in alternatives:
            lines += ['{', 'std::vector<NodePtr> children;']
            condition = ' &&\n'.join(f'append(children, {self.expression(child.item)})'
                                      for child in alt.items) or 'true'
            if diag:
                kind, message, indexes = diag
                args = ', '.join(f'children[{i}]' for i in indexes)
                args = (', ' + args) if args else ''
                next_token = 'true' if kind == '_ON_NEXT_TOKEN' else 'false'
                if kind == '_INDENTATION':
                    action = f'if (diagnostics_) indentation_diagnostic({cpp_text(message)}{args});'
                else:
                    action = f'if (diagnostics_) diagnostic({cpp_text(message)}, {next_token}{args});'
                if name.startswith('invalid_'):
                    action += ' return {};'
                else:
                    action += f' return make(Rule::{rule_id(name)}, {index}, start, std::move(children));'
                result = '{ ' + action + ' }'
            else:
                result = f'return make(Rule::{rule_id(name)}, {index}, start, std::move(children));'
            lines += [f'if ({condition}) {result}',
                      'mark_ = start;', 'if (cut) return {};', '}']
        lines += ['return {};', '}()']
        return '\n'.join(lines)

    def header(self, digest):
        names = ['Token', 'Group', *self.grammar.rules]
        return ('// Generated by Tools/peg_generator/generate.py; do not edit.\n'
                f'// CPython 3.12.14 grammar SHA-256: {digest}\n'
                '#pragma once\n#include <cstddef>\n#include <string_view>\n'
                'namespace cxxpython::syntax {\n'
                'enum class TokenKind {\n' + ',\n'.join(self.tokens) + '\n};\n'
                'enum class Rule {\n' + ',\n'.join(map(rule_id, names)) + '\n};\n'
                'std::string_view rule_name(Rule) noexcept;\n'
                f'inline constexpr std::size_t grammar_rule_count = {len(self.grammar.rules)};\n'
                '} // namespace cxxpython::syntax\n')

    def source(self, digest):
        lines = ['// Generated by Tools/peg_generator/generate.py; do not edit.',
                 f'// CPython 3.12.14 grammar SHA-256: {digest}',
                 '#include "internal/peg_parser.hpp"',
                 'namespace cxxpython::syntax {',
                 'std::string_view rule_name(Rule rule) noexcept {',
                 'static constexpr std::string_view names[] = {', '"Token", "Group",']
        lines += [f'"{name}",' for name in self.grammar.rules]
        lines += ['};', 'return names[static_cast<std::size_t>(rule)];', '}',
                  'namespace {', 'class GeneratedParser final : public ParserBase {',
                  'public:', 'using ParserBase::ParserBase;',
                  'bool reserved(std::u32string_view text) const override {',
                  'return ' + ' || '.join(f'text == {cpp_text(word)}' for word in sorted(self.keywords)) + ';',
                  '}', 'NodePtr start(InputMode mode) {', 'switch(mode) {',
                  'case InputMode::File: return rule_file();',
                  'case InputMode::Eval: return rule_eval();',
                  'case InputMode::Single:',
                  'case InputMode::Interactive: return rule_interactive();',
                  'case InputMode::FunctionType: return rule_func_type();',
                  '}', 'return {};', '}', 'private:']
        for name, rule in self.grammar.rules.items():
            lines += [f'NodePtr rule_{name}() {{']
            if name.startswith('invalid_'):
                lines += ['if (!diagnostics_) return {};']
            left, leader = str(rule.left_recursive).lower(), str(rule.leader).lower()
            lines += [f'return memo(Rule::{rule_id(name)}, {left}, {leader}, [&]() -> NodePtr {{',
                      f'return {self.choice(rule.rhs, name)};', '});']
            lines += ['}']
        lines += ['};', '} // namespace',
                  'Tree parse(std::string_view source, InputMode mode, ParseFlags flags) {',
                  'const auto run = [&](bool diagnostics) {',
                  'TokenStream stream(false, mode, flags);',
                  'GeneratedParser parser(stream, source_tokens(stream, source, mode, flags), flags, diagnostics);',
                  'auto root = parser.start(mode);',
                  'return parser.finish(std::move(root), mode);',
                  '};',
                  'try {',
                  'return run(false);',
                  '} catch (const Error &first) {',
                  'const std::string_view message = first.what();',
                  'if (!((first.kind == ErrorKind::Syntax && message == "invalid syntax") ||',
                  '      (enabled(flags, ParseFlags::TypeComments) && first.kind == ErrorKind::Indentation &&',
                  '       message == "expected an indented block"))) throw;',
                  'try {',
                  'run(true);',
                  '} catch (const LiteralDiagnostic &) { throw; }',
                  'catch (const Error &) {}',
                  'throw;', '}', '}',
                  '} // namespace cxxpython::syntax', '']
        return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'Grammar/python.gram').read_text()
    reader = GeneratedParser(Tokenizer(tokenize.generate_tokens(io.StringIO(source).readline)))
    grammar = reader.start()
    if grammar is None:
        raise reader.make_syntax_error('invalid grammar input')
    tokens = [line.split()[0] for line in (ROOT / 'Grammar/Tokens').read_text().splitlines()
              if line.strip() and not line.startswith('#')]
    generator = Generator(grammar, tokens)
    digest = hashlib.sha256(source.encode()).hexdigest()
    artifacts = {
        ROOT / 'Include/cxxpython/grammar.hpp': generator.header(digest),
        ROOT / 'Parser/generated_parser.cpp': generator.source(digest),
    }
    for path, content in artifacts.items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit(f'{path.relative_to(ROOT)} is stale; run regen-parser')
        elif not path.exists() or path.read_text() != content:
            path.write_text(content)
    print(f'{len(grammar.rules)} grammar rules; generated artifacts ' + ('verified' if args.check else 'written'))


if __name__ == '__main__':
    main()
