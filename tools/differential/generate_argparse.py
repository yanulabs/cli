#!/usr/bin/env python3
"""Generate a differential replay from the argparse runtime capture.

Every public parse_args/parse_known_args call in the upstream argparse test
suite is captured with its full parser tree.  This script translates each
schema into cli.hpp, emits a replay executable and tags schemas that exercise
semantics cli.hpp intentionally does not implement.
"""
import argparse
import ast
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


SRC, OUT = _resolve_paths('argparse')

INT_CODES = {'a': 'signed char', 's': 'short', 'i': 'int', 'l': 'long',
             'x': 'long long'}
UINT_CODES = {'h': 'unsigned char', 't': 'unsigned short',
              'j': 'unsigned', 'm': 'unsigned long',
              'y': 'unsigned long long'}
FLOAT_CODES = {'f': 'float', 'd': 'double', 'e': 'long double'}
ALL_CODES = set(INT_CODES) | set(UINT_CODES) | set(FLOAT_CODES) | {'b', 'c'}

CONTAINER_MARKERS = ('6vectorI', '3setI', '4listI', '5dequeI')


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


def scalar_cpp(code):
    if code in INT_CODES:
        return INT_CODES[code]
    if code in UINT_CODES:
        return UINT_CODES[code]
    if code in FLOAT_CODES:
        return FLOAT_CODES[code]
    if code == 'b':
        return 'bool'
    if code == 'c':
        return 'char'
    return None


def container_marker(tid):
    for marker in CONTAINER_MARKERS:
        idx = tid.find(marker)
        if idx >= 0:
            return marker, tid[idx + len(marker):]
    return None, None


def resolve_type(tid):
    """Return (cpp element type, is_container, tags)."""
    tags = set()
    if not tid:
        return 'std::string', False, tags
    if 'basic_string' in tid:
        return 'std::string', False, tags
    if '4pathE' in tid or 'filesystem' in tid:
        tags.add('path_type')
        return 'std::string', False, tags
    if '8optionalI' in tid:
        tags.add('optional_type')
        return 'std::string', False, tags
    if '5tupleI' in tid or '4pairI' in tid:
        tags.add('tuple_proxy')
        return 'std::string', False, tags
    marker, rest = container_marker(tid)
    if marker:
        if marker != '6vectorI':
            tags.add('container_' + {'3setI': 'set', '4listI': 'list',
                                     '5dequeI': 'deque'}[marker])
        if 'basic_string' in rest:
            return 'std::string', True, tags
        code = rest[:1]
        cpp = scalar_cpp(code)
        if cpp is None:
            tags.add('unknown_container_element')
            return None, True, tags
        return cpp, True, tags
    cpp = scalar_cpp(tid)
    if cpp is None:
        tags.add('unknown_type')
        return None, False, tags
    return cpp, False, tags


def arg_type(a):
    """Resolve the storage/parsing type recorded for an argument."""
    tid = a.get('scan_type') or a.get('default_type') or ''
    cpp, is_container, tags = resolve_type(tid)
    return cpp, is_container, tags


def builtin_names(a):
    return a.get('builtin', False)


def is_flag(a):
    return (not a['positional'] and a['nargs_min'] == 0 and
            a['nargs_max'] == 0 and a['has_implicit'] and
            a.get('implicit_type') == 'b')


def is_implicit_nonbool(a):
    return (not a['positional'] and a['nargs_min'] == 0 and
            a['nargs_max'] == 0 and a['has_implicit'] and
            a.get('implicit_type') not in ('', 'b'))


def is_optional_value(a):
    return (not a['positional'] and a['nargs_min'] == 0 and
            a['nargs_max'] == 1)


def is_zero_or_more(a):
    return a['nargs_max'] is None and a['nargs_min'] == 0


def is_zero_min_range(a):
    return a['nargs_min'] == 0 and a['nargs_max'] is not None and a['nargs_max'] > 1


def decode_string_repr(repr_text):
    text = repr_text
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        try:
            return ast.literal_eval(text)
        except Exception:
            body = text[1:-1]
            body = body.replace('\\"', '"').replace('\\\\', '\\')
            return body
    return text


def split_repr_tokens(body):
    tokens = []
    i = 0
    while i < len(body):
        while i < len(body) and body[i].isspace():
            i += 1
        if i >= len(body):
            break
        if body[i] == '"':
            j = i + 1
            value = []
            while j < len(body):
                if body[j] == '\\' and j + 1 < len(body):
                    value.append(body[j:j + 2])
                    j += 2
                    continue
                if body[j] == '"':
                    break
                value.append(body[j])
                j += 1
            raw = ''.join(value)
            try:
                raw = ast.literal_eval('"' + raw + '"')
            except Exception:
                raw = raw.replace('\\"', '"').replace('\\\\', '\\')
            tokens.append(('str', raw))
            i = j + 1
        else:
            j = i
            while j < len(body) and not body[j].isspace():
                j += 1
            tokens.append(('bare', body[i:j]))
            i = j
    return tokens


def default_expr(a, cpp, is_container):
    if not a['has_default']:
        return None
    repr_text = a.get('default_repr', '')
    if is_container:
        body = repr_text.strip()
        if body.startswith('{') and body.endswith('}'):
            body = body[1:-1]
        tokens = split_repr_tokens(body)
        parts = []
        for kind, value in tokens:
            if cpp == 'std::string':
                parts.append(cpp_str(value) if kind == 'str' else cpp_str(value))
            else:
                parts.append(value)
        return '{' + ', '.join(parts) + '}'
    if cpp == 'std::string':
        value = decode_string_repr(repr_text)
        return cpp_str(value)
    if cpp == 'bool':
        return 'true' if repr_text == 'true' else 'false'
    if cpp == 'char':
        if len(repr_text) == 1:
            return f'static_cast<char>({ord(repr_text)})'
        m = re.match(r'\\x([0-9a-fA-F]+)', repr_text)
        if m:
            return f'static_cast<char>({int(m.group(1), 16)})'
        return f"static_cast<char>({ord(repr_text[0])})"
    if cpp == 'signed char':
        if repr_text.startswith('\\x'):
            return f'static_cast<signed char>({int(repr_text[2:], 16)})'
        if len(repr_text) == 1:
            return f'static_cast<signed char>({ord(repr_text)})'
    if cpp == 'unsigned char':
        if repr_text.startswith('\\x'):
            return f'static_cast<unsigned char>({int(repr_text[2:], 16)})'
        if len(repr_text) == 1:
            return f'static_cast<unsigned char>({ord(repr_text)})'
    return repr_text


def cpp_choices(a, cpp):
    choices = a.get('choices')
    if choices is None:
        return None
    parts = []
    for value in choices:
        if cpp == 'std::string':
            parts.append(cpp_str(value))
        elif cpp in ('char', 'signed char', 'unsigned char'):
            parts.append(f'static_cast<{cpp}>({ord(value[0])})')
        elif cpp == 'bool':
            parts.append('true' if value.lower() == 'true' else 'false')
        else:
            parts.append(value)
    return '{' + ', '.join(parts) + '}'


def arg_vector(a, cpp, is_container):
    if a.get('_force_vector'):
        return True
    if is_flag(a) or is_implicit_nonbool(a):
        return False
    if a['positional']:
        return (is_container or a['nargs_max'] is None or
                (a['nargs_max'] is not None and a['nargs_max'] > 1))
    return (is_container or a['repeatable'] or a['nargs_max'] is None or
            (a['nargs_max'] is not None and a['nargs_max'] > 1))


def is_argparse_decimal_literal(s):
    """Mirror argparse's Argument::is_decimal_literal for the text after '-'.

    The grammar intentionally rejects leading-zero integers unless they carry a
    fraction or exponent (for example -00 and -0777 are option-like), while
    accepting -0, -0., -00e-0 and -003.
    """
    i = 0
    n = len(s)

    def digits():
        nonlocal i
        start = i
        while i < n and s[i].isdigit():
            i += 1
        return i > start

    def exponent():
        nonlocal i
        if i >= n or s[i] not in 'eE':
            return False
        i += 1
        if i < n and s[i] in '+-':
            i += 1
        return digits() and i == n

    if not s:
        return False
    c = s[0]
    if c == '0':
        i = 1
        if i == n:
            return True
        digits()
    elif c in '123456789':
        digits()
        if i == n:
            return True
    elif c == '.':
        i = 1
        if not digits():
            return False
        return i == n or exponent()
    else:
        return False

    if i < n and s[i] == '.':
        i += 1
        if i < n and s[i].isdigit():
            digits()
        return i == n or exponent()
    if i < n and s[i] in 'eE':
        i += 1
        if i < n and s[i] in '+-':
            i += 1
        return digits() and i == n
    return False


def collect_tags(rec):
    tags = set()
    root = rec['parsers'][0]
    args = [bytes.fromhex(h).decode('latin1') for h in rec['args_hex']]
    all_names = set()
    for parser in rec['parsers']:
        for a in parser['positionals'] + parser['optionals']:
            all_names.update(a['names'])
    for parser in rec['parsers']:
        if parser['prefix'] != '-':
            tags.add('custom_prefix')
        if parser['assign'] != '=':
            tags.add('custom_assign')
        if parser['suppress']:
            tags.add('suppressed_help')
        for a in parser['positionals'] + parser['optionals']:
            if a['builtin']:
                continue
            if a.get('user_action'):
                tags.add('user_callback')
            if a.get('scan_shape') and a['scan_shape'] not in ('i', 'd', 'u'):
                tags.add('scan_format_' + a['scan_shape'].lower())
            _, _, ttags = arg_type(a)
            tags |= ttags
            if a['accepts_optional_like']:
                tags.add('accepts_optional_like')
            if is_implicit_nonbool(a):
                tags.add('implicit_non_bool')
            if is_optional_value(a):
                tags.add('optional_value')
            if is_zero_or_more(a) and not a['positional']:
                tags.add('zero_or_more_option')
            if is_zero_min_range(a):
                tags.add('zero_min_range')
            if is_flag(a) and a.get('implicit_repr') == 'false':
                tags.add('implicit_false_flag')
            if a['repeatable'] and not is_flag(a) and not arg_vector(a, '', False) and not a['accepts_optional_like']:
                tags.add('repeatable_scalar')
            if a['hidden']:
                tags.add('hidden_argument')
            if a['choices'] is not None:
                cpp, _, _ = arg_type(a)
                if cpp not in ('std::string', 'int', 'unsigned', 'long',
                               'long long', 'short', 'signed char',
                               'unsigned char', 'unsigned short',
                               'unsigned long', 'unsigned long long',
                               'char', 'bool', 'float', 'double',
                               'long double'):
                    tags.add('unsupported_choices')
            for name in a['names']:
                for tok in args[1:]:
                    if tok == name + '=true' or tok == name + '=false':
                        if is_flag(a):
                            tags.add('flag_equals')

    numeric_types = {'int', 'unsigned', 'long', 'long long', 'short',
                     'signed char', 'unsigned char', 'unsigned short',
                     'unsigned long', 'unsigned long long', 'float',
                     'double', 'long double'}
    has_numeric = any((arg_type(a)[0] in numeric_types) or
                      a.get('scan_shape') in set('diuobxXefga')
                      for parser in rec['parsers']
                      for a in parser['positionals'] + parser['optionals'])
    if has_numeric:
        for tok in args[1:]:
            if tok.startswith('+'):
                tags.add('numeric_plus_sign')
                break

    for tok in args[1:]:
        if (tok.startswith('-') and not tok.startswith('--') and len(tok) > 1):
            body = tok[1:]
            if (body and (body[0].isdigit() or body[0] == '.')
                    and tok not in all_names
                    and not is_argparse_decimal_literal(body)):
                tags.add('negative_literal_lexical')
                break

    option_names = {name for parser in rec['parsers']
                    for a in parser['optionals'] for name in a['names']}
    seq = args[1:]
    for i in range(len(seq) - 1):
        if seq[i] in option_names and seq[i + 1].startswith('-'):
            nxt = seq[i + 1]
            if nxt == '--':
                continue
            body = nxt[1:]
            negative_ok = (body and (body[0].isdigit() or body[0] == '.')
                           and is_argparse_decimal_literal(body))
            if not negative_ok:
                tags.add('option_like_value_extension')

    short_names = {name for name in option_names
                   if name.startswith('-') and not name.startswith('--')}
    short_letters = {name[1] for name in short_names if len(name) > 1}
    for tok in seq:
        if (tok.startswith('-') and not tok.startswith('--') and len(tok) > 2
                and '=' not in tok and tok not in option_names):
            body = tok[1:]
            negative_ok = (body and (body[0].isdigit() or body[0] == '.')
                           and is_argparse_decimal_literal(body))
            if not negative_ok and body[0] in short_letters:
                tags.add('compound_short_option')

    multi_positional = any(
        a['positional'] and (a['nargs_max'] is None or a['nargs_max'] > 1)
        for parser in rec['parsers']
        for a in parser['positionals'])
    if multi_positional:
        saw_positional = False
        saw_option_after_positional = False
        for tok in seq:
            if tok == '--':
                break
            if tok.startswith('-') and len(tok) > 1:
                body = tok[1:]
                is_negative = (body and (body[0].isdigit() or body[0] == '.'))
                if is_negative and is_argparse_decimal_literal(body):
                    if saw_option_after_positional:
                        tags.add('positional_interleaved')
                        break
                    continue
                if saw_positional:
                    saw_option_after_positional = True
            else:
                if saw_option_after_positional:
                    tags.add('positional_interleaved')
                    break
                saw_positional = True

    # Any invocation of a stock builtin help/version option exercises help
    # rendering / process exit semantics rather than plain parsing.
    builtin = {}
    for parser in rec['parsers']:
        for a in parser['optionals']:
            if a['builtin']:
                for name in a['names']:
                    builtin[name] = parser['program']
    if any(tok in builtin for tok in args[1:]):
        if any(tok in ('-h', '--help') and tok in builtin for tok in args[1:]):
            tags.add('builtin_help')
        if any(tok in ('-v', '--version') and tok in builtin for tok in args[1:]):
            tags.add('builtin_version')
    return tags


def normalize_record(rec):
    """Translate spellings cli.hpp cannot register (option names with '=')."""
    name_map = {}
    for parser in rec['parsers']:
        for a in parser['optionals']:
            names = []
            for name in a['names']:
                if '=' in name:
                    safe = name.replace('=', '__EQ__')
                    while safe in name_map.values():
                        safe += '_'
                    name_map[name] = safe
                    names.append(safe)
                else:
                    names.append(name)
            a['names'] = names
        parser['hidden_aliases'] = [
            [name_map.get(alias, alias), name_map.get(target, target)]
            for alias, target in parser['hidden_aliases']]
    if name_map:
        rec.setdefault('_extra_tags', set()).add('option_name_equals')
        rewritten = []
        for h in rec['args_hex']:
            raw = bytes.fromhex(h)
            text = raw.decode('latin1')
            if text in name_map:
                raw = name_map[text].encode('latin1')
            rewritten.append(raw.hex())
        rec['args_hex'] = rewritten
    return rec


def schema_positionals(parser):
    """Merge argparse's trailing varargs-plus-scalar pattern for replay.

    argparse special-cases `<varargs> <exactly-one>` by assigning the final
    token to the scalar.  cli.hpp rejects a variable-arity positional unless it
    is last, so acceptance-equivalent replay uses one min=(var_min+1) vector.
    """
    pos = parser['positionals']
    if len(pos) >= 2:
        prev = pos[-2]
        last = pos[-1]
        variable = (prev['nargs_max'] is None or
                    prev['nargs_max'] != prev['nargs_min'])
        if (variable and last['nargs_min'] == 1 and last['nargs_max'] == 1):
            merged = dict(prev)
            merged['names'] = [prev['names'][0]]
            merged['nargs_min'] = prev['nargs_min'] + 1
            merged['nargs_max'] = None
            merged['_force_vector'] = True
            merged['has_default'] = False
            merged['required'] = True
            merged['default_type'] = ''
            merged['scan_type'] = ''
            merged['choices'] = None
            return pos[:-2] + [merged]
    return pos


def validate_schema(rec):
    """Return a hard-block reason, or None when the schema is replayable."""
    for parser in rec['parsers']:
        for a in parser['positionals'] + parser['optionals']:
            if not a['names']:
                return 'unnamed-argument'
            for name in a['names']:
                if not name or name in ('-', '--') or name.startswith('---'):
                    return 'invalid-name'
                if '=' in name:
                    return 'invalid-name'
                if any(c.isspace() or ord(c) < 0x20 for c in name):
                    return 'invalid-name'
            cpp, _, tags = arg_type(a)
            if cpp is None:
                return 'unknown-type'
        # cli.hpp rejects a variable-arity positional unless it is last.  The
        # one upstream pattern that can be acceptance-translated is handled by
        # schema_positionals().
        positionals = parser['positionals']
        for i, a in enumerate(positionals[:-1]):
            if a['nargs_max'] is None or a['nargs_max'] != a['nargs_min']:
                last = positionals[-1]
                if (i == len(positionals) - 2 and last['nargs_min'] == 1 and
                        last['nargs_max'] == 1):
                    continue
                return 'variable-positional-not-last'
        names = [s['name'] for s in parser['subparsers']]
        if len(names) != len(set(names)):
            return 'duplicate-subparser-name'
        for name in names:
            if not name or name.startswith('-') or any(c.isspace() for c in name):
                return 'invalid-subparser-name'
    return None


def emit_arg(app, a, hidden_aliases=()):
    cpp, is_container, tags = arg_type(a)
    if cpp is None:
        return []
    vec = arg_vector(a, cpp, is_container)
    typ = f'std::vector<{cpp}>' if vec else cpp
    lines = []
    names = list(a['names'])
    first = names[0]
    extra_names = names[1:] + list(hidden_aliases)
    if is_flag(a) or is_implicit_nonbool(a):
        expr = f'{app}.add_flag({cpp_str(first)})'
        for n in extra_names:
            expr += f'.alias({cpp_str(n)})'
        if a['has_default']:
            expr += '.default_val(' + default_expr(a, 'bool', False) + ')'
    else:
        expr = f'{app}.add<{typ}>({cpp_str(first)})'
        for n in extra_names:
            expr += f'.alias({cpp_str(n)})'
        if a['has_default']:
            expr += '.default_val(' + default_expr(a, cpp, vec) + ')'
        if a['required']:
            expr += '.required()'
        if a['hidden']:
            expr += '.hidden()'
        choices = cpp_choices(a, cpp)
        if choices and cpp in ('std::string', 'int', 'unsigned', 'long',
                               'long long', 'short', 'signed char',
                               'unsigned char', 'unsigned short',
                               'unsigned long', 'unsigned long long',
                               'char', 'bool', 'float', 'double',
                               'long double'):
            expr += f'.choices({choices})'
        if vec:
            mn, mx = a['nargs_min'], a['nargs_max']
            if mx is None:
                if mn > 0:
                    expr += f'.nargs_at_least({mn})'
            elif mx == mn:
                if mn > 1:
                    expr += f'.nargs({mn})'
            else:
                if mn == 0:
                    expr += f'.nargs(1, {mx})'
                else:
                    expr += f'.nargs({mn}, {mx})'
    lines.append(f'    (void){expr};')
    return lines


def emit_positional(app, a):
    cpp, is_container, tags = arg_type(a)
    if cpp is None:
        return []
    vec = arg_vector(a, cpp, is_container)
    typ = f'std::vector<{cpp}>' if vec else cpp
    name = a['names'][0]
    expr = f'{app}.add_positional<{typ}>({cpp_str(name)})'
    required = bool(a['required'])
    if a['positional'] and not a['has_default'] and a['nargs_min'] >= 1:
        required = True
    if a['has_default']:
        required = False
    if not required:
        expr += '.required(false)'
    if a['has_default']:
        expr += '.default_val(' + default_expr(a, cpp, vec) + ')'
    choices = cpp_choices(a, cpp)
    if choices and cpp in ('std::string', 'int', 'unsigned', 'long',
                           'long long', 'short', 'signed char',
                           'unsigned char', 'unsigned short',
                           'unsigned long', 'unsigned long long', 'char',
                           'bool', 'float', 'double', 'long double'):
        expr += f'.choices({choices})'
    if vec:
        mn, mx = a['nargs_min'], a['nargs_max']
        if mx is None:
            if mn > 0:
                expr += f'.nargs_at_least({mn})'
        elif mx == mn:
            expr += f'.nargs({mn})'
        else:
            if mn == 0:
                expr += f'.nargs(1, {mx})'
            else:
                expr += f'.nargs({mn}, {mx})'
    return [f'    (void){expr};']


def emit_mutex(app, parser):
    lines = []
    allargs = parser['positionals'] + parser['optionals']
    for group in parser['mutex']:
        names = []
        ok = True
        for index in group['elements']:
            if index < 0 or index >= len(allargs):
                ok = False
                break
            a = allargs[index]
            if a['positional'] or not a['names'] or a['builtin']:
                ok = False
                break
            names.append(a['names'][0])
        if not ok or not names:
            continue
        arglist = ', '.join(cpp_str(n) for n in names)
        lines.append(f'    {app}.mutex_group({{{arglist}}});')
        if group['required']:
            lines.append(f'    {app}.require_one_of({{{arglist}}});')
    return lines


def emit_node(rec, pid, app_expr, emitted):
    if pid in emitted:
        return []
    emitted.add(pid)
    parser = rec['parsers'][pid]
    alias_map = {}
    for alias, target in parser['hidden_aliases']:
        alias_map.setdefault(target, []).append(alias)
    lines = []
    for a in parser['optionals']:
        if a['builtin']:
            continue
        lines.extend(emit_arg(app_expr, a, alias_map.get(a['names'][0], [])))
    for a in parser['positionals']:
        lines.extend(emit_positional(app_expr, a))
    lines.extend(emit_mutex(app_expr, parser))
    for sub in parser['subparsers']:
        child = sub['parser']
        if child in emitted:
            continue
        child_expr = f'app_{child}'
        lines.append(f'    auto& {child_expr} = {app_expr}.subcommand({cpp_str(sub["name"])}, "");')
        lines.extend(emit_node(rec, child, child_expr, emitted))
    return lines


def emit_case(rec, index, blocked_reason=None):
    args = [bytes.fromhex(h) for h in rec['args_hex']]
    tags = collect_tags(rec) if blocked_reason is None else set()
    lines = [f'static int case_{index}() {{']
    lines.append('    cli::App app_0("app");')
    emitted = set()
    emitted.add(0)
    parser = rec['parsers'][0]
    alias_map = {}
    for alias, target in parser['hidden_aliases']:
        alias_map.setdefault(target, []).append(alias)
    for a in parser['optionals']:
        if a['builtin']:
            continue
        lines.extend(emit_arg('app_0', a, alias_map.get(a['names'][0], [])))
    for a in parser['positionals']:
        lines.extend(emit_positional('app_0', a))
    lines.extend(emit_mutex('app_0', parser))
    for sub in parser['subparsers']:
        child = sub['parser']
        child_expr = f'app_{child}'
        lines.append(f'    auto& {child_expr} = app_0.subcommand({cpp_str(sub["name"])}, "");')
        lines.extend(emit_node(rec, child, child_expr, emitted))
    if rec['kind'] == 'known':
        lines.append('    app_0.allow_extras();')
    arglist = ', '.join(cpp_string_expr(x) for x in args[1:])
    lines.append(f'    auto res = app_0.parse(std::vector<std::string>{{{arglist}}});')
    expected = 'true' if rec['outcome'] == 'ok' else 'false'
    lines.append('    bool ok = res.has_value();')
    lines.append(f'    if (ok == {expected}) return 0;')
    lines.append('    return ok ? 1 : -1;')
    lines.append('}')
    return lines, tags


def main():
    recs = [json.loads(line) for line in _open_text(SRC)]
    cases = []
    blocked = Counter()
    tag_counter = Counter()
    for idx, rec in enumerate(recs):
        normalize_record(rec)
        split_tagged = False
        for parser in rec['parsers']:
            merged = schema_positionals(parser)
            if len(merged) != len(parser['positionals']):
                split_tagged = True
            parser['positionals'] = merged
        reason = validate_schema(rec)
        if reason:
            blocked[reason] += 1
            continue
        lines, tags = emit_case(rec, len(cases))
        tags |= rec.get('_extra_tags', set())
        if split_tagged:
            tags.add('split_trailing_positional')
        cases.append((len(cases), rec, lines, tags))
        for tag in tags:
            tag_counter[tag] += 1

    out = ['#include <cli.hpp>', '#include <cstdint>', '#include <string>',
           '#include <vector>', '', 'namespace {', '']
    out.append('struct Case { const char* tags; int (*fn)(); };')
    out.append('')
    for _, rec, lines, _ in cases:
        out.extend(lines)
        out.append('')
    out.append('const Case cases[] = {')
    for idx, rec, lines, tags in cases:
        tag_text = ','.join(sorted(tags)) if tags else '-'
        out.append(f'    {{{cpp_str(tag_text)}, case_{idx}}},')
    out.append('};')
    out.append('}')
    out.append('')
    out.append('#include <cstdio>')
    out.append('#include <exception>')
    out.append('int main() {')
    out.append('    int failed = 0;')
    out.append('    for (int i = 0; i < static_cast<int>(sizeof(cases)/sizeof(cases[0])); ++i) {')
    out.append('        int outcome = 0;')
    out.append('        try { outcome = cases[i].fn(); } catch (...) { outcome = -1; }')
    out.append('        if (outcome != 0) {')
    out.append('            ++failed;')
    out.append('            std::printf("FAIL\\t%d\\t%d\\t%s\\n", i, outcome, cases[i].tags);')
    out.append('        }')
    out.append('    }')
    out.append('    std::printf("SUMMARY\\t%d\\t%d\\n", failed, static_cast<int>(sizeof(cases)/sizeof(cases[0])));')
    out.append('    return failed == 0 ? 0 : 1;')
    out.append('}')
    open(OUT, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print(f'records {len(recs)}; replayable {len(cases)}; blocked {sum(blocked.values())}: {dict(blocked)}')
    print('tags:', dict(tag_counter))
    print('wrote', OUT)


if __name__ == '__main__':
    main()
