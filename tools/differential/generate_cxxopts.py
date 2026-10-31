#!/usr/bin/env python3
"""Generate a differential replay from the cxxopts runtime capture."""
import argparse
import gzip
import json
import os
import re
import sys
from collections import Counter

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))


def _open_text(path):
    if path.endswith('.gz'):
        return gzip.open(path, 'rt', encoding='utf-8')
    return open(path, encoding='utf-8')


def _resolve_paths(default_suite):
    parser = argparse.ArgumentParser(
        description='Generate the cli.hpp replay for an upstream capture.')
    parser.add_argument(
        'input', nargs='?',
        default=os.path.join(TOOL_DIR, 'corpus', default_suite + '_all.jsonl.gz'),
        help='capture file (.jsonl or .jsonl.gz)')
    parser.add_argument(
        'output', nargs='?', default=default_suite + '_replay.cpp',
        help='generated C++ replay')
    args = parser.parse_args()
    return args.input, args.output


SRC, OUT = _resolve_paths('cxxopts')

rows = [json.loads(line) for line in _open_text(SRC)]
for r in rows:
    r['args_b'] = [a.encode('latin1') for a in r['args']]

INT_CODES = {'a': 8, 's': 16, 'i': 32, 'l': 64, 'x': 64}
UINT_CODES = {'h': 8, 't': 16, 'j': 32, 'm': 64, 'y': 64}
FLOAT_CODES = {'f': 'float', 'd': 'double', 'e': 'long double'}
ALL_CODES = set(INT_CODES) | set(UINT_CODES) | set(FLOAT_CODES) | {'b', 'c'}


def cpp_bytes(b):
    out = '"'
    for byte in b:
        ch = chr(byte)
        if 32 <= byte < 127 and ch not in '"\\':
            out += ch
        else:
            out += '\\%03o' % byte
    out += '"'
    return out


def cpp_str(s):
    return cpp_bytes(s.encode('latin1'))


def cpp_string_expr(b):
    lit = cpp_bytes(b)
    if all(32 <= x < 127 and chr(x) not in '"\\' for x in b):
        return lit
    return f'std::string({lit}, {len(b)})'


def vector_element(tid):
    for marker in ('St6vectorI',):
        idx = tid.find(marker)
        if idx >= 0:
            return tid[idx + len(marker):]
    return None


def scalar_code(tid):
    if not tid:
        return None
    if len(tid) == 1:
        return tid if tid in ALL_CODES else None
    for marker in ('6vectorI', '6atomicI', '8optionalI'):
        idx = tid.find(marker)
        if idx >= 0:
            rest = tid[idx + len(marker):]
            if rest and rest[0] in ALL_CODES:
                return rest[0]
    return None


def resolve_type(o):
    """Return (cpp_type, is_vector, tags)."""
    tid = o.get('type_id', '')
    tags = set()
    if not tid:
        return None, False, {'untyped_option'}
    if '6vectorI' in tid and not o.get('is_container'):
        return 'std::string', False, {'vector_proxy'}
    if '6vectorI' in tid:
        code = scalar_code(tid)
        if code is None:
            if 'basic_string' in tid:
                return 'std::string', True, tags
            return None, False, {'unknown_vector_element'}
        if code in INT_CODES:
            width = INT_CODES[code]
            cpp = 'int' if width == 32 else f'std::int{width}_t'
        elif code in UINT_CODES:
            width = UINT_CODES[code]
            cpp = 'unsigned' if width == 32 else f'std::uint{width}_t'
        elif code in FLOAT_CODES:
            cpp = FLOAT_CODES[code]
        elif code == 'b':
            cpp = 'bool'
        elif code == 'c':
            cpp = 'char'
        else:
            return None, False, {'unknown_vector_element'}
        return cpp, True, tags
    if 'basic_string' in tid:
        cpp = 'std::string'
    elif '5tupleI' in tid or '4pairI' in tid:
        cpp = 'std::string'
        tags.add('tuple_proxy')
    elif '8optionalI' in tid:
        cpp = 'std::string'
        tags.add('optional_type')
    elif '4pathE' in tid or 'filesystem' in tid:
        cpp = 'std::filesystem::path'
        tags.add('path_type')
    else:
        code = scalar_code(tid)
        if code is None:
            return None, False, {'unknown_type'}
        if code in INT_CODES:
            width = INT_CODES[code]
            cpp = 'int' if width == 32 else f'std::int{width}_t'
        elif code in UINT_CODES:
            width = UINT_CODES[code]
            cpp = 'unsigned' if width == 32 else f'std::uint{width}_t'
        elif code in FLOAT_CODES:
            cpp = FLOAT_CODES[code]
        elif code == 'b':
            cpp = 'bool'
        elif code == 'c':
            cpp = 'char'
        else:
            return None, False, {'unknown_type'}
    return cpp, False, tags


def option_names(o):
    names = ['--' + n for n in o.get('long', [])]
    if o.get('short'):
        names.append('-' + o['short'])
    return names


def option_by_name(schema, name):
    for o in schema['options']:
        if name in o.get('long', []) or name == o.get('short'):
            return o
    return None


def count_name(o, args):
    names = option_names(o)
    count = 0
    for tok in args:
        if tok in names or any(tok.startswith(n + '=') for n in names):
            count += 1
        elif o.get('short') and any(tok.startswith('-' + o['short']) and len(tok) > 1 + len(o['short'])
                                    for tok in args if tok.startswith('-' + o['short'])):
            count += 1
    return count


def tags_for_case(r):
    schema = r['schema']
    args = r['args']
    tags = set()
    if schema.get('allow_unrecognised'):
        tags.add('allow_unrecognised')
    for o in schema['options']:
        _, _, ttags = resolve_type(o)
        tags |= ttags
        if o.get('has_implicit') and not o.get('is_boolean'):
            tags.add('implicit_value')
    if not schema['positional']:
        non_options = [a for a in args if a == '--' or not a.startswith('-')]
        if non_options:
            tags.add('unmatched_positionals')
    for pos in schema['positional']:
        o = option_by_name(schema, pos)
        if o is None:
            tags.add('invalid_positional')
            continue
        if any(tok in option_names(o) or any(tok.startswith(n + '=') for n in option_names(o))
               for tok in args):
            tags.add('named_positional')
    return tags


def emit_case(r):
    schema = r['schema']
    lines = []
    lines.append('    cli::App app("app");')
    emitted = set()
    for o in schema['options']:
        cpp, is_vec, tags = resolve_type(o)
        if cpp is None:
            continue
        names = option_names(o)
        if not names:
            continue
        first = names[0]
        if o.get('is_boolean') and o.get('has_implicit'):
            expr = f'app.add_flag({cpp_str(first)})'
        else:
            typ = f'std::vector<{cpp}>' if is_vec else cpp
            expr = f'app.add<{typ}>({cpp_str(first)})'
        for n in names[1:]:
            expr += f'.alias({cpp_str(n)})'
        if is_vec and not o.get('is_boolean'):
            expr += ".delimiter(',')"
        lines.append(f'    (void){expr};')
        emitted.add(id(o))
    for pos in schema['positional']:
        o = option_by_name(schema, pos)
        if o is None:
            continue
        cpp, is_vec, tags = resolve_type(o)
        if cpp is None:
            continue
        typ = f'std::vector<{cpp}>' if is_vec else cpp
        expr = f'app.add_positional<{typ}>({cpp_str(pos)}).required(false)'
        lines.append(f'    (void){expr};')
    if schema.get('allow_unrecognised'):
        lines.append('    app.allow_extras();')
    elif not schema['positional'] and all(a == '--' or not a.startswith('-') for a in r['args']):
        lines.append('    app.allow_extras();')
    arglist = ', '.join(cpp_string_expr(x) for x in r['args_b'])
    lines.append(f'    auto res = app.parse(std::vector<std::string>{{{arglist}}});')
    expected = 'true' if r['outcome'] == 'ok' else 'false'
    lines.append(f'    return res.has_value() == {expected};')
    return lines


valid = []
blocked = Counter()
for r in rows:
    schema = r['schema']
    bad = None
    for o in schema['options']:
        if not option_names(o):
            bad = 'unnamed-option'
        cpp, _, _ = resolve_type(o)
        if cpp is None and not bad:
            bad = 'type:' + o.get('type_id', '')
    if bad:
        blocked[bad] += 1
        continue
    r['tags'] = tags_for_case(r)
    valid.append(r)

print(f'replayable {len(valid)} / {len(rows)}; blocked {sum(blocked.values())}: {dict(blocked)}')
print('tagged cases:', sum(1 for r in valid if r['tags']), file=sys.stderr)

lines = ['#include <cli.hpp>', '#include <cstdio>', '#include <cstdint>',
         '#include <string>', '#include <vector>', '']
for i, r in enumerate(valid):
    lines.append(f'static bool case_{i}() {{')
    lines.extend(emit_case(r))
    lines.append('}')
    lines.append('')
lines.append('int main() {')
lines.append('    int failures = 0;')
for i, r in enumerate(valid):
    tags = ','.join(sorted(r['tags']))
    args = ' '.join(r['args'])
    lines.append(
        f'    if (!case_{i}()) {{ std::printf("FAIL\\t{i}\\t%s\\t%s\\t%s\\n", '
        f'{cpp_str(tags)}, {cpp_str(args)}, {cpp_str(r.get("error", ""))}); ++failures; }}')
lines.append(f'    std::printf("SUMMARY\\t%d\\t{len(valid)}\\n", failures);')
lines.append('    return failures == 0 ? 0 : 1;')
lines.append('}')
open(OUT, 'w').write('\n'.join(lines))
print('wrote', OUT)
