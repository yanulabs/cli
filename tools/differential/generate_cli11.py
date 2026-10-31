#!/usr/bin/env python3
"""Generate a differential replay from the CLI11 runtime capture.

The capture records the full schema and every public parse() call from the
upstream test suite.  This script translates each schema into an equivalent
cli.hpp app, emits a replay executable, and tags schemas that use features
cli.hpp does not implement so mismatches can be classified honestly.

Hard blocks are reserved for schemas that cannot be constructed at all
(invalid names, duplicate registrations, unknown base types).  Everything
else is replayed, with unsupported semantics recorded as tags.
"""
import argparse
import gzip
import json
import os
import re
import sys
from collections import Counter, defaultdict

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


SRC, OUT = _resolve_paths('cli11')

rows = [json.loads(line) for line in _open_text(SRC)]
for r in rows:
    if 'args_hex' in r:
        r['args_b'] = [bytes.fromhex(h) for h in r['args_hex']]
        r['args'] = [b.decode('latin1') for b in r['args_b']]
    else:
        r['args_b'] = [a.encode('utf-8') for a in r['args']]
        r['args'] = list(r['args'])

BUILTIN_HELP_NAMES = ['--help', '-h']
BUILTIN_VERSION_NAMES = ['--version', '-V']
MAX_EXPECTED = 536870912

ALL_ENV_NAMES = set()


def collect_env_names(a):
    for o in a['options']:
        if o.get('env'):
            ALL_ENV_NAMES.add(o['env'])
    for s in a['subcommands']:
        collect_env_names(s)


for _row in rows:
    collect_env_names(_row['schema'])


def option_env_value(o):
    raw = o.get('env_value_hex')
    if raw is None:
        return None
    return bytes.fromhex(raw)


def env_suffix(o):
    name = o.get('env')
    if not name:
        return ''
    return f'.env({cpp_str(name)})'


def case_env_pairs(r):
    pairs = {}

    def walk(a):
        for o in a['options']:
            if o.get('env'):
                value = option_env_value(o)
                if pairs.get(o['env'], value) != value:
                    raise SystemExit(f'conflicting env value for {o["env"]}')
                pairs[o['env']] = value
        for s in a['subcommands']:
            walk(s)

    walk(r['schema'])
    return pairs


def option_display_name(o, index=0):
    names = names_for(o)
    if names:
        return names[0]
    return f'--__unnamed_{index}'


def enc(s):
    try:
        return s.encode('latin1')
    except UnicodeEncodeError:
        return s.encode('utf-8')


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
    return cpp_bytes(enc(s))


def cpp_string_expr(b):
    lit = cpp_bytes(b)
    if all(32 <= x < 127 and chr(x) not in '"\\' for x in b):
        return lit
    return f'std::string({lit}, {len(b)})'


INT_CODES = {'a': 8, 's': 16, 'i': 32, 'l': 64, 'x': 64}
UINT_CODES = {'h': 8, 't': 16, 'j': 32, 'm': 64, 'y': 64}
FLOAT_CODES = {'f': 'float', 'd': 'double', 'e': 'long double'}
ALL_SCALAR_CODES = set(INT_CODES) | set(UINT_CODES) | set(FLOAT_CODES) | {'b', 'c'}


def scalar_code(tid):
    """Best-effort scalar type code from an Itanium-mangled type name."""
    if not tid:
        return None
    if len(tid) == 1:
        return tid if tid in ALL_SCALAR_CODES else None
    for marker in ('6vectorI', '6atomicI'):
        idx = tid.find(marker)
        if idx >= 0:
            rest = tid[idx + len(marker):]
            if rest and rest[0] in ALL_SCALAR_CODES:
                return rest[0]
    return None


def base_cpp_type(o):
    """Return (cpp type or None, tags) for an option schema."""
    t = o.get('type', '')
    tid = o.get('type_id', '')
    tags = set()
    if not t:
        return None, tags
    if t.startswith('['):
        return 'std::string', {'tuple_proxy'}
    if t == 'CHAR':
        code = scalar_code(tid)
        cpp = {'a': 'signed char', 'h': 'unsigned char'}.get(code, 'char')
        return cpp, {'char_type'}
    if t == 'COMPLEX':
        return 'std::string', {'complex_proxy'}
    if t == 'KEY VALUE':
        return 'std::string', {'tuple_proxy'}
    if t == 'RETIRED':
        return 'std::string', {'retired_option'}
    base, _, desc = t.partition(':')
    if desc and 'DEPRECATED' in desc:
        tags.add('deprecated_type')
    code = scalar_code(tid)

    if base == 'INT':
        if code in INT_CODES:
            width = INT_CODES[code]
            return ('int' if width == 32 else f'std::int{width}_t'), tags
        if tid:
            tags.add('custom_type_converter')
        return 'int', tags
    if base == 'UINT':
        if code in UINT_CODES:
            width = UINT_CODES[code]
            return ('unsigned' if width == 32 else f'std::uint{width}_t'), tags
        if tid:
            tags.add('custom_type_converter')
        return 'unsigned', tags
    if base == 'FLOAT':
        if code in FLOAT_CODES:
            return FLOAT_CODES[code], tags
        return 'double', tags
    if base == 'BOOLEAN':
        return 'bool', tags
    if base == 'TEXT':
        if 'basic_string' in tid:
            return 'std::string', tags
        if any(marker in tid for marker in ('6vectorI', '5tupleI', '4pairI', '7complexI', '8optionalI')):
            if '5tupleI' in tid or '4pairI' in tid:
                tags.add('tuple_proxy')
            if '7complexI' in tid:
                tags.add('complex_proxy')
            return 'std::string', tags
        if tid:
            tags.add('custom_type_converter')
        return 'std::string', tags
    if base == 'ENUM':
        return 'std::string', tags | {'enum_type'}
    if base == '':
        return 'std::string', tags | {'untyped_type'}
    if ' ' in base:
        return 'std::string', tags | {'tuple_proxy'}
    return None, tags | {'unknown_type'}


def is_builtin(o):
    if o['type']:
        return False
    return o['lnames'] in (['help'], ['version'])


def is_vec(o):
    return o['emax'] > 1 or o['tsmax'] > 1


def names_for(o):
    return ['--' + n for n in o['lnames']] + ['-' + n for n in o['snames']]


def valid_option_name(n):
    if len(n) < 2 or not n.startswith('-'):
        return False
    if n in ('-', '--') or n.startswith('---'):
        return False
    if '=' in n:
        return False
    return not any(c.isspace() or ord(c) < 0x20 for c in n)


def valid_word(s):
    return bool(s) and not any(c.isspace() or ord(c) < 0x20 for c in s)


def valid_command_name(n):
    return valid_word(n) and not n.startswith('-')

def sanitize_fragment(name):
    out = []
    for c in name:
        if c.isspace() or ord(c) < 0x20 or c == '=':
            out.append('_')
        else:
            out.append(c)
    text = ''.join(out)
    return text


def sanitize_schema(node, cmd_events, opt_events, prefix_ok=False):
    """Rename names cli.hpp cannot construct, recording argument rewrites."""
    child_prefix = bool(node.get('subcommand_prefix')) or prefix_ok
    used_commands = set()
    duplicate_lists = {}
    for sub in node.get('subcommands', []):
        old = sub.get('name', '')
        if old:
            new = sanitize_fragment(old)
            if not new or new.startswith('-'):
                new = 'command_' + new.lstrip('-')
            if new in used_commands:
                base = new
                serial = 2
                while f'{base}__{serial}' in used_commands:
                    serial += 1
                new = f'{base}__{serial}'
                names = duplicate_lists.setdefault(old, [old])
                names.append(new)
                cmd_events.append({'old': old, 'names': names,
                                   'prefix': child_prefix})
            else:
                if new != old:
                    cmd_events.append({'old': old, 'names': [new],
                                       'prefix': child_prefix})
            used_commands.add(new)
            aliases = []
            for alias in sub.get('aliases', []):
                new_alias = sanitize_fragment(alias)
                if not new_alias or new_alias.startswith('-'):
                    new_alias = 'alias_' + new_alias.lstrip('-')
                candidate = new_alias
                serial = 2
                while candidate in used_commands:
                    candidate = f'{new_alias}__{serial}'
                    serial += 1
                used_commands.add(candidate)
                aliases.append(candidate)
                if candidate != alias:
                    cmd_events.append({'old': alias, 'names': [candidate],
                                       'prefix': child_prefix})
            sub['name'] = new
            sub['aliases'] = aliases
        sanitize_schema(sub, cmd_events, opt_events, child_prefix)

    used_options = set()
    for opt in node.get('options', []):
        lnames = []
        for name in opt.get('lnames', []):
            new = sanitize_fragment(name)
            candidate = new
            serial = 2
            while candidate in used_options:
                candidate = f'{new}_{serial}'
                serial += 1
            used_options.add(candidate)
            lnames.append(candidate)
            if candidate != name:
                opt_events.append(('--' + name, '--' + candidate))
        snames = []
        for name in opt.get('snames', []):
            new = sanitize_fragment(name)
            candidate = new
            serial = 2
            while candidate in used_options:
                candidate = f'{new}_{serial}'
                serial += 1
            used_options.add(candidate)
            snames.append(candidate)
            if candidate != name:
                opt_events.append(('-' + name, '-' + candidate))
        opt['lnames'] = lnames
        opt['snames'] = snames
        if opt.get('pname'):
            pname = sanitize_fragment(opt['pname'])
            opt['pname'] = pname or 'value'
    return child_prefix


def rewrite_args(args, cmd_events, opt_events):
    counts = {}
    out = []
    for tok in args:
        replaced = tok
        for event in cmd_events:
            old = event['old']
            names = event['names']
            if tok == old:
                index = min(counts.get(id(event), 0), len(names) - 1)
                counts[id(event)] = counts.get(id(event), 0) + 1
                replaced = names[index]
                break
        if replaced == tok:
            for event in cmd_events:
                old = event['old']
                if event['prefix'] and tok and len(tok) < len(old) and old.startswith(tok):
                    replaced = event['names'][0]
                    break
        if replaced == tok:
            for old, new in opt_events:
                if tok == old:
                    replaced = new
                    break
                if tok.startswith(old + '='):
                    replaced = new + tok[len(old):]
                    break
                if old.startswith('-') and not old.startswith('--') \
                        and tok.startswith(old) and len(tok) > len(old):
                    replaced = new + tok[len(old):]
                    break
        out.append(replaced)
    return out


def dedupe_subcommands(a, cmd_events):
    """Rename duplicate subcommand names that only collide after flattening."""
    used = set()
    event_lists = {}
    for sub in a.get('subcommands', []):
        old = sub.get('name', '')
        new = old
        if old and old in used:
            base = old
            serial = 2
            while f'{base}__{serial}' in used:
                serial += 1
            new = f'{base}__{serial}'
            names = event_lists.setdefault(old, [old])
            names.append(new)
            cmd_events.append({'old': old, 'names': names,
                               'prefix': bool(a.get('subcommand_prefix'))})
            sub['name'] = new
        used.add(new)
        aliases = []
        for alias in sub.get('aliases', []):
            candidate = alias
            serial = 2
            while candidate in used:
                candidate = f'{alias}__{serial}'
                serial += 1
            used.add(candidate)
            aliases.append(candidate)
            if candidate != alias:
                cmd_events.append({'old': alias, 'names': [candidate],
                                   'prefix': bool(a.get('subcommand_prefix'))})
        sub['aliases'] = aliases
        dedupe_subcommands(sub, cmd_events)


def merge_trailing_positionals(a):
    """Drop fixed positionals after a variadic one when CLI11 treats it as trailing."""
    if a.get('positionals_at_end'):
        positionals = [o for o in a['options'] if o['positional'] and not is_builtin(o)]
        variadic = []
        for index, o in enumerate(positionals):
            shape = option_shape(o, set())
            if shape and shape.get('vector') and shape.get('nargs') \
                    and shape['nargs'][0] in ('range', 'atleast'):
                variadic.append(index)
        if len(variadic) == 1 and variadic[0] != len(positionals) - 1:
            dropped = {id(o) for o in positionals[variadic[0] + 1:]}
            a['options'] = [o for o in a['options'] if id(o) not in dropped]
            a['_norm_tags'].add('variable_positional_merged')
    for s in a.get('subcommands', []):
        merge_trailing_positionals(s)



def normalize_node(a):
    """Flatten option groups and nameless subcommands into their parent."""
    out = {k: v for k, v in a.items() if k not in ('options', 'subcommands')}
    out['options'] = list(a['options'])
    out['subcommands'] = []
    out['_norm_tags'] = set()
    for s in a['subcommands']:
        ns = normalize_node(s)
        if not s.get('name'):
            out['options'].extend(ns['options'])
            out['subcommands'].extend(ns['subcommands'])
            out['_norm_tags'] |= ns['_norm_tags']
            if s.get('option_group'):
                out['_norm_tags'].add('option_group')
                if s.get('aliases'):
                    out['_norm_tags'].add('option_group_alias')
            else:
                out['_norm_tags'].add('nameless_subcommand')
        else:
            out['subcommands'].append(ns)
    used_names = set()
    for o in out['options']:
        used_names.update(names_for(o))
    counter = 0
    for index, o in enumerate(out['options']):
        if o.get('positional') or names_for(o):
            continue
        if not o.get('env'):
            continue
        while True:
            synthetic = f'__unnamed_{counter}'
            counter += 1
            candidate = '--' + synthetic
            if candidate not in used_names:
                break
        copy = dict(o)
        copy['lnames'] = [synthetic]
        copy['_synthetic_name'] = True
        out['options'][index] = copy
        used_names.add(candidate)
    return out


def choice_canonicalized(o, args):
    """True when an active filtered choice set may rewrite the stored value."""
    results = o.get('results') or []
    if not results:
        return False
    if not o['positional'] and option_occurrences(o, args) == 0:
        return False
    for validator in o.get('validators', []):
        if not validator.get('active', True):
            continue
        if not (validator.get('ignore_case_probe')
                or validator.get('ignore_underscore_probe')
                or validator.get('ignore_space_probe')):
            continue
        if plain_choice_items(validator.get('desc', '')):
            return True
    return False


def analysis_tags(o, args):
    tags = set()
    tags.update(validator_tags(o))
    shape = option_shape(o, set())
    cpp = shape.get('cpp') if shape else None
    if cpp is not None:
        for validator in o.get('validators', []):
            if not validator.get('active', True):
                continue
            if plain_choice_items(validator.get('desc', '').strip()) is None:
                continue
            if unmodeled_transform(validator, cpp):
                tags.add('custom_transform_validator')
            if choice_canonicalized(o, args):
                tags.add('choices_canonical')
    base = o.get('type', '').partition(':')[0]
    if base in ('INT', 'UINT'):
        for value in o.get('results') or []:
            if (re.fullmatch(r'[+-]?0[xX][0-9a-fA-F]+', value)
                    or re.fullmatch(r'[+-]?0[bB][01]+', value)
                    or re.fullmatch(r'[+-]?0[0-7]+', value)):
                tags.add('numeric_lexical_extension')
                break
    tid = o.get('type_id', '')
    if ('6vectorINS0_I' in tid or '6vectorI6vectorI' in tid
            or '6vectorINS_5arrayI' in tid or '5arrayI5arrayI' in tid):
        tags.add('nested_vector_container')
    if shape and cpp and not is_arithmetic(cpp):
        for validator in o.get('validators', []):
            desc = validator.get('desc', '').strip()
            if numeric_predicate(desc, 'int') is not None or parse_range_desc(desc) is not None:
                tags.add('numeric_validator')
    if o.get('_synthetic_name'):
        tags.add('env_only_unnamed')
    if o.get('user_callback'):
        tags.add('user_callback')
    if o.get('fnames'):
        tags.add('flag_negation')
    flag_like = o.get('emin') == 0 and o.get('emax') == 0
    if o.get('multi') in (2, 3, 4, 5, 6) or (o.get('multi') == 1 and not flag_like):
        tags.add('multi_policy')
    if o.get('ignore_case'):
        tags.add('option_ignore_case')
    if o.get('ignore_underscore'):
        tags.add('option_ignore_underscore')
    if o.get('type') and o.get('emin') == 0 and o.get('emax', 0) >= 1:
        tags.add('optional_values')
    if o.get('disable_flag_override'):
        tags.add('flag_override')
    if o.get('positional') and (o.get('lnames') or o.get('snames')):
        tags.add('named_positional')
    if o.get('type') and o.get('default') and is_vec(o):
        tags.add('vector_default')
    return tags


def option_shape(o, tags):
    """Classify an option and return a translation description."""
    t = o['type']
    if t == '':
        if o['emin'] == 0:
            return {'kind': 'flag'}
        if o['tsmin'] == 0 and o['tsmax'] == 0:
            tags.add('flag_like_option')
            return {'kind': 'flag'}
        if o['tsmin'] == 0 and o['tsmax'] > 0:
            tags.add('optional_values')
            return {'kind': 'flag'}
        tags.add('untyped_option' if not o['positional'] else 'untyped_positional')
        cpp = 'std::string'
    else:
        cpp, ttags = base_cpp_type(o)
        tags |= ttags
        if cpp is None:
            return None
        if o.get('emin') == 0 and o['emax'] >= 1:
            tags.add('optional_values')
        if o['tsmin'] == 0 and o['tsmax'] > 0:
            tags.add('optional_values')
            return {'kind': 'flag'}
        if o['tsmin'] == 0 and o['tsmax'] == 0:
            tags.add('flag_like_option')
            return {'kind': 'flag'}

    tokens_min = o['tsmin']
    tokens_max = o['tsmax']
    if tokens_max < tokens_min:
        tokens_max = tokens_min

    shape = {'kind': 'positional' if o['positional'] else 'option', 'cpp': cpp}
    if o['positional']:
        lo = max(1, o['emin'] * max(tokens_min, 1))
        hi = o['emax'] * max(tokens_max, 1)
        fixed = (o['emin'] == o['emax'] and tokens_min == tokens_max)
        if fixed and lo == 1:
            shape['vector'] = False
            shape['nargs'] = None
        else:
            shape['vector'] = True
            if o['emax'] >= MAX_EXPECTED or tokens_max >= MAX_EXPECTED:
                shape['nargs'] = ('atleast', lo)
            elif lo == hi:
                shape['nargs'] = ('exact', lo)
            else:
                shape['nargs'] = ('range', lo, hi)
        return shape

    max_items = o['emax']
    min_items = o['emin']
    width = tokens_max if tokens_max > 1 else 1
    lo = max(1, min_items) * width
    hi = max_items * width if max_items < MAX_EXPECTED else MAX_EXPECTED
    vector = max_items > 1 or tokens_max > 1 or o.get('allow_extra_args', False)
    shape['vector'] = vector
    if o.get('allow_extra_args') or hi >= MAX_EXPECTED:
        if vector:
            shape['nargs'] = ('atleast', lo)
        else:
            shape['nargs'] = None
    elif hi > lo:
        shape['nargs'] = ('range', lo, hi)
    elif hi == lo and hi > 1:
        shape['nargs'] = ('exact', lo)
    else:
        shape['nargs'] = None
    return shape


def hard_block(a):
    seen_options = set()
    for o in a['options']:
        if is_builtin(o):
            continue
        tags = set()
        shape = option_shape(o, tags)
        if o['positional']:
            if not valid_word(o.get('pname', '')):
                return 'invalid-positional-name'
        else:
            ns = names_for(o)
            if not ns:
                return 'unnamed-option'
            for n in ns:
                if not valid_option_name(n):
                    return 'invalid-option-name'
                if n in seen_options:
                    return 'duplicate-option-name'
                seen_options.add(n)
        if shape is None:
            return 'type:' + o['type']
    seen_subcommands = set()
    for s in a['subcommands']:
        if not valid_command_name(s.get('name', '')):
            return 'invalid-subcommand-name'
        if s['name'] in seen_subcommands:
            return 'duplicate-subcommand-name'
        seen_subcommands.add(s['name'])
        for alias in s.get('aliases', []):
            if not valid_command_name(alias):
                return 'invalid-subcommand-alias'
            if alias in seen_subcommands:
                return 'duplicate-subcommand-name'
            seen_subcommands.add(alias)
        r = hard_block(s)
        if r:
            return r
    return None


def default_literal(o, cpp):
    d = o.get('default', '')
    if not d:
        return None
    if cpp == 'std::string':
        return cpp_str(d)
    if cpp == 'bool':
        low = d.lower()
        if low in ('true', '1'):
            return 'true'
        if low in ('false', '0'):
            return 'false'
        return None
    if cpp in ('int', 'unsigned'):
        try:
            int(d, 0)
        except ValueError:
            return None
        return d
    if cpp == 'double':
        try:
            float(d)
        except ValueError:
            return None
        return d
    if cpp == 'char':
        return None
    return None


def is_arithmetic(cpp):
    return (cpp == 'bool' or cpp == 'char' or cpp == 'unsigned' or
            cpp.startswith('std::int') or cpp.startswith('std::uint') or
            cpp in ('int', 'long', 'long long', 'float', 'double', 'long double'))


def cpp_choice_literal(text, cpp):
    text = text.strip()
    if cpp == 'std::string':
        return cpp_str(text)
    if not is_arithmetic(cpp) or cpp == 'bool':
        return None
    try:
        if cpp in ('float', 'double', 'long double'):
            float(text)
        else:
            int(text, 0)
    except ValueError:
        return None
    return text


def plain_choice_items(desc):
    """Return the item strings of a plain `{a,b}` set, or None."""
    desc = desc.strip()
    if not (desc.startswith('{') and desc.endswith('}')):
        return None
    if desc.startswith('{}'):
        return []
    body = desc[1:-1]
    if '->' in body or ':' in body or '{' in body or '}' in body:
        return None
    return [item for item in body.split(',')]


def parse_range_desc(desc):
    match = re.fullmatch(
        r'(?:INT|UINT|FLOAT) in \[([^-\]]+) - ([^\]]+)\]',
        desc.strip())
    if not match:
        return None
    return match.group(1).strip(), match.group(2).strip()


def numeric_predicate(desc, cpp):
    if not is_arithmetic(cpp) or cpp == 'bool':
        return None
    desc = desc.strip()
    if desc == 'POSITIVE':
        return '(e > 0)'
    if desc == 'NONNEGATIVE':
        return '(e >= 0)'
    if desc == 'NOT POSITIVE':
        return '(!(e > 0))'
    return None


def validator_tags(o):
    tags = set()
    for validator in o.get('validators', []):
        if not validator.get('active', True):
            continue
        desc = validator.get('desc', '').strip()
        if desc == '':
            tags.add('custom_validator')
            continue
        if desc == 'DEPRECATED':
            continue
        if 'bounded to' in desc:
            tags.add('clamp_validator')
            continue
        if plain_choice_items(desc) is not None:
            continue
        if parse_range_desc(desc) is not None:
            continue
        if numeric_predicate(desc, 'int') is not None:
            continue
        if 'UNIT' in desc or desc.startswith('SIZE '):
            tags.add('unit_suffix')
        elif 'FILE' in desc or 'PATH' in desc:
            tags.add('path_validator')
        elif '->' in desc or ':' in desc:
            tags.add('composite_validator')
        elif ' AND ' in desc or ' OR ' in desc or desc.startswith('NOT '):
            tags.add('composite_validator')
            if '->' in desc or '{' in desc:
                tags.add('choices_transform')
        elif desc == 'NUMBER':
            tags.add('numeric_validator')
        else:
            tags.add('custom_validator')
    return tags


def set_filter_flags(validator, cpp):
    """Probe flags that are meaningful for the option's element type."""
    if cpp != 'std::string':
        return False, False, False
    return (validator.get('ignore_case_probe', False),
            validator.get('ignore_underscore_probe', False),
            validator.get('ignore_space_probe', False))


def membership_expr(items, cpp, ignore_case, ignore_underscore, ignore_space, negate):
    if not items:
        return 'true' if negate else 'false'
    literals = [cpp_choice_literal(item, cpp) for item in items]
    if any(lit is None for lit in literals):
        return None
    checks = []
    for lit in literals:
        if cpp == 'std::string' and (ignore_case or ignore_underscore or ignore_space):
            transform = 'e'
            value = lit
            if ignore_underscore:
                transform = f'diff_squeeze({transform})'
                value = f'diff_squeeze({value})'
            if ignore_space:
                transform = f'diff_strip_space({transform})'
                value = f'diff_strip_space({value})'
            if ignore_case:
                transform = f'diff_lower({transform})'
                value = f'diff_lower({value})'
            checks.append(f'({transform} == {value})')
        else:
            checks.append(f'(e == {lit})')
    expr = '(' + ' || '.join(checks) + ')'
    return f'(!{expr})' if negate else expr


def unmodeled_transform(validator, cpp):
    """True when a set validator modifies its input in a way probes cannot explain."""
    if not validator.get('modifying'):
        return False
    case, underscore, space = set_filter_flags(validator, cpp)
    return not (case or underscore or space)


def membership_validator(o, cpp):
    """Return (expressions, tags) for membership-style checks."""
    expressions = []
    tags = set()
    for validator in o.get('validators', []):
        if not validator.get('active', True):
            continue
        desc = validator.get('desc', '').strip()
        items = plain_choice_items(desc)
        if items is None:
            continue
        if unmodeled_transform(validator, cpp):
            tags.add('custom_transform_validator')
            continue
        case, underscore, space = set_filter_flags(validator, cpp)
        expr = membership_expr(items, cpp, case, underscore, space, False)
        if expr is None:
            tags.add('choices_transform')
            continue
        expressions.append((expr, validator.get('index', -1)))
    return expressions, tags


def compile_validate(o, shape):
    """Return (suffix, tags) for the option's validators translated to cli.hpp."""
    cpp = shape.get('cpp')
    if cpp is None or not shape.get('kind') in ('option', 'positional'):
        return '', set()
    vector = shape.get('vector', False)
    tags = validator_tags(o)
    validators = [v for v in o.get('validators', []) if v.get('active', True)]
    if not validators:
        return '', tags

    # Fast path: exactly one plain membership set, no filters, non-modifying.
    if len(validators) == 1:
        desc = validators[0].get('desc', '').strip()
        items = plain_choice_items(desc)
        case, underscore, space = set_filter_flags(validators[0], cpp)
        if items is not None and not (case or underscore or space) \
                and not validators[0].get('modifying'):
            literals = [cpp_choice_literal(item, cpp) for item in items]
            if all(lit is not None for lit in literals):
                if not literals:
                    return '.validate([](const auto&) { return false; })', tags
                return '.choices({' + ', '.join(literals) + '})', tags

    # Fast path: exactly one numeric range.
    if len(validators) == 1:
        bounds = parse_range_desc(validators[0].get('desc', ''))
        if bounds is not None and is_arithmetic(cpp) and cpp != 'bool':
            lo, hi = bounds
            try:
                if cpp in ('float', 'double', 'long double'):
                    float(lo); float(hi)
                else:
                    int(lo, 0); int(hi, 0)
            except ValueError:
                bounds = None
            if bounds is not None:
                return f'.range({lo}, {hi})', tags

    # General path: build one predicate over each element.
    conditions = []
    member_exprs, member_tags = membership_validator(o, cpp)
    tags |= member_tags
    for expr, index in member_exprs:
        if vector:
            if index < 0:
                conditions.append(expr)
            else:
                conditions.append(f'(idx != {index} || {expr})')
        elif index in (-1, 0):
            conditions.append(expr)

    for validator in validators:
        desc = validator.get('desc', '').strip()
        predicate = numeric_predicate(desc, cpp)
        if predicate is None:
            continue
        index = validator.get('index', -1)
        if vector:
            if index < 0:
                conditions.append(predicate)
            else:
                conditions.append(f'(idx != {index} || {predicate})')
        elif index in (-1, 0):
            conditions.append(predicate)

    if not conditions:
        return '', tags
    body = ' && '.join(f'({c})' for c in conditions)
    if vector:
        suffix = (f'.validate([](const std::vector<{cpp}>& v) {{ '
                  f'for (std::size_t i = 0; i < v.size(); ++i) {{ '
                  f'const auto& e = v[i]; const auto idx = static_cast<int>(i); '
                  f'if (!({body})) return false; }} return true; }})')
    else:
        suffix = f'.validate([](const {cpp}& e) {{ return {body}; }})'
    return suffix, tags


def nargs_suffix(shape):
    n = shape.get('nargs')
    if not n:
        return ''
    kind = n[0]
    if kind == 'exact':
        return f'.nargs({n[1]})'
    if kind == 'range':
        return f'.nargs({n[1]}, {n[2]})'
    return f'.nargs_at_least({n[1]})'


CMP_STRING_TYPES = {'std::string'}
CMP_CHAR_TYPES = {'char', 'signed char', 'unsigned char'}
CMP_FLOAT_TYPES = {'float', 'double', 'long double'}
CMP_SIGNED_TYPES = {'int', 'short', 'long', 'long long',
                    'std::int8_t', 'std::int16_t', 'std::int32_t', 'std::int64_t'}
CMP_UNSIGNED_TYPES = {'unsigned', 'unsigned short', 'unsigned long',
                      'unsigned long long', 'std::uint8_t', 'std::uint16_t',
                      'std::uint32_t', 'std::uint64_t'}


def bool_expected(value):
    """Translate an upstream flag result the way CLI11's to_flag_value does."""
    low = value.lower()
    if low in ('true', 't', 'yes', 'y', 'on'):
        return True
    if low in ('false', 'f', 'no', 'n', 'off', '0'):
        return False
    try:
        return int(value, 0) > 0
    except ValueError:
        return None


def expected_scalar(cpp, value):
    """Return a C++ expression producing the value upstream parsed, or None."""
    if cpp in CMP_STRING_TYPES:
        return cpp_str(value)
    if cpp == 'bool':
        parsed = bool_expected(value)
        return None if parsed is None else ('true' if parsed else 'false')
    if cpp in CMP_CHAR_TYPES:
        if len(value) == 1:
            return f'static_cast<{cpp}>({ord(value)})'
        try:
            number = int(value, 0)
        except ValueError:
            return None
        return f'static_cast<{cpp}>({number})'
    if cpp in CMP_SIGNED_TYPES:
        return f'static_cast<{cpp}>(std::strtoll({cpp_str(value)}, nullptr, 0))'
    if cpp in CMP_UNSIGNED_TYPES:
        return f'static_cast<{cpp}>(std::strtoull({cpp_str(value)}, nullptr, 0))'
    if cpp in CMP_FLOAT_TYPES:
        return f'static_cast<{cpp}>(std::strtold({cpp_str(value)}, nullptr))'
    return None


def comparison_for(o, shape, var):
    """Describe a parsed-value check, or None when upstream semantics differ."""
    results = o.get('results') or []
    if not results or is_builtin(o):
        return None
    kind = shape['kind']
    if kind == 'flag':
        if o.get('type') not in ('', 'BOOLEAN'):
            return None
        parsed = bool_expected(results[-1])
        if parsed is None:
            return None
        return {'kind': 'flag', 'lookup': var, 'name': names_for(o)[0],
                'label': o.get('pname') or names_for(o)[0],
                'expected': 'true' if parsed else 'false'}
    if kind not in ('option', 'positional'):
        return None
    cpp = shape['cpp']
    if len(results) > 1 and not shape.get('vector'):
        return None
    items = [expected_scalar(cpp, value) for value in results]
    if any(item is None for item in items):
        return None
    if shape.get('vector'):
        expected = f'std::vector<{cpp}>{{{", ".join(items)}}}'
    else:
        expected = items[0]
    return {'kind': 'value', 'handle': None, 'cpp': cpp,
            'vector': bool(shape.get('vector')),
            'label': o.get('pname') or names_for(o)[0],
            'expected': expected}


def emit_option(o, var, lines, checks=None, counter=None, path=''):
    shape = option_shape(o, set())
    if shape is None:
        return
    ns = names_for(o)
    check = None
    if checks is not None:
        check = comparison_for(o, shape, var)
    if o['positional']:
        cpp = shape['cpp']
        typ = f'std::vector<{cpp}>' if shape['vector'] else cpp
        expr = f'{var}.add_positional<{typ}>({cpp_str(o["pname"])})'
        if o.get('default') and not shape['vector']:
            lit = default_literal(o, cpp)
            if lit is not None:
                expr += f'.default_val({lit})'
        expr += '.required(' + ('true' if o['required'] else 'false') + ')'
        expr += nargs_suffix(shape)
        expr += compile_validate(o, shape)[0]
        expr += env_suffix(o)
        if check is not None:
            handle = f'opt_{counter[0]}'
            counter[0] += 1
            check['handle'] = handle
            check['label'] = path + check['label']
            checks.append(check)
            lines.append(f'    auto {handle} = {expr};')
        else:
            lines.append(f'    (void){expr};')
        return

    first = ns[0]
    kind = shape['kind']
    if kind == 'flag':
        expr = f'{var}.add_flag({cpp_str(first)})'
        for n in ns[1:]:
            expr += f'.alias({cpp_str(n)})'
        if o['required']:
            expr += '.required()'
        if o.get('default') and o['default'].lower() in ('true', 'false'):
            expr += f'.default_val({o["default"].lower()})'
        expr += env_suffix(o)
        lines.append(f'    (void){expr};')
        if check is not None:
            check['label'] = path + check['label']
            checks.append(check)
        return

    cpp = shape['cpp']
    typ = f'std::vector<{cpp}>' if shape['vector'] else cpp
    expr = f'{var}.add<{typ}>({cpp_str(first)})'
    if o.get('default') and not shape['vector']:
        lit = default_literal(o, cpp)
        if lit is not None:
            expr += f'.default_val({lit})'
    if o['required']:
        expr += '.required()'
    if shape['vector'] and o.get('delimiter'):
        expr += f'.delimiter(static_cast<char>({o["delimiter"]}))'
    expr += nargs_suffix(shape)
    expr += compile_validate(o, shape)[0]
    expr += env_suffix(o)
    for n in ns[1:]:
        expr += f'.alias({cpp_str(n)})'
    if check is not None:
        handle = f'opt_{counter[0]}'
        counter[0] += 1
        check['handle'] = handle
        check['label'] = path + check['label']
        checks.append(check)
        lines.append(f'    auto {handle} = {expr};')
    else:
        lines.append(f'    (void){expr};')


def emit_app(a, var, lines, checks=None, counter=None, path=''):
    if a.get('version'):
        lines.append(f'    {var}.version({cpp_str(a["version"])});')
    if a.get('allow_extras') or a.get('allow_extras_mode', 0) >= 2:
        lines.append(f'    {var}.allow_extras();')
    if a.get('require_subcommand_min') == 1 and a.get('require_subcommand_max') == 0:
        lines.append(f'    {var}.require_subcommand();')

    for o in a['options']:
        if is_builtin(o):
            continue
        emit_option(o, var, lines, checks, counter, path)

    for s in a['subcommands']:
        expr = f'{var}.subcommand({cpp_str(s["name"])})'
        for alias in s.get('aliases', []):
            expr += f'.alias({cpp_str(alias)})'
        lines.append(f'    auto& {subvar(s)} = {expr};')
        emit_app(s, subvar(s), lines, checks, counter, path + s['name'] + '/')


def count_value_coverage(valid):
    """Count the value checks the emitter will produce and explicit skips."""
    stats = Counter()
    for r in valid:
        if r['tags'] or r['outcome'] != 'ok':
            continue
        stats['cases'] += 1
        has_check = False

        def walk(a):
            nonlocal has_check
            for o in a['options']:
                if is_builtin(o):
                    continue
                if not (o.get('results') or []):
                    continue
                shape = option_shape(o, set())
                if shape is None:
                    stats['skipped'] += 1
                    continue
                if comparison_for(o, shape, 'app') is None:
                    stats['skipped'] += 1
                else:
                    has_check = True
                    stats['fields'] += 1
            for s in a['subcommands']:
                walk(s)

        walk(r['schema'])
        if has_check:
            stats['compared'] += 1
    return stats


def emit_value_checks(checks, lines):
    for check in checks:
        label = cpp_str(check['label'])
        expected = check['expected']
        if check['kind'] == 'flag':
            actual = f'{check["lookup"]}[{cpp_str(check["name"])}].as<bool>()'
            lines.append(f'    if (ok && {actual} != {expected}) {{')
            lines.append('        value_ok = false;')
            lines.append(f'        std::printf("VALUE\\t%s\\t%s\\t%s\\n", {label}, '
                         f'{expected} ? "true" : "false", {actual} ? "true" : "false");')
            lines.append('    }')
            continue
        handle = check['handle']
        lines.append(f'    if (ok && !{handle}) {{')
        lines.append('        value_ok = false;')
        lines.append(f'        std::printf("VALUE\\t%s\\t%s\\t<unset>\\n", {label}, '
                     f'diff_repr({expected}).c_str());')
        lines.append('    }')
        lines.append(f'    else if (ok && !diff_eq({handle}.value(), {expected})) {{')
        lines.append('        value_ok = false;')
        lines.append(f'        std::printf("VALUE\\t%s\\t%s\\t%s\\n", {label}, '
                     f'diff_repr({expected}).c_str(), diff_repr({handle}.value()).c_str());')
        lines.append('    }')


def subvar(s):
    return 'sub_' + str(s.get('_id'))


def assign_ids(a, counter):
    for s in a['subcommands']:
        counter[0] += 1
        s['_id'] = counter[0]
        assign_ids(s, counter)


def find_child(a, name):
    for s in a['subcommands']:
        if s['name'] == name or name in s.get('aliases', []):
            return s
    return None


def matches_dot_path(a, token):
    if '.' not in token:
        return False
    cur = a
    for part in token.split('.'):
        cur = find_child(cur, part)
        if cur is None:
            return False
    return True


def option_occurrences(o, args):
    names = names_for(o)
    count = 0
    for tok in args:
        if tok in names or any(tok.startswith(n + '=') for n in names):
            count += 1
        elif any(len(n) == 2 and n.startswith('-') and not n.startswith('--')
                 and tok.startswith(n) and len(tok) > 2 for n in names):
            count += 1
    return count


def looks_like_numeric(token):
    body = token.strip()
    if body[:1] in ('+', '-'):
        body = body[1:]
    return bool(body) and (body[0].isdigit() or body.startswith('.'))


def tags_for_case(r):
    tags = set()
    args = r['args']
    schema = r['schema']
    error = r.get('error', '')
    opts_seen = set()
    delims = set()
    hungry_option = False
    required_positional = False
    variadic_positionals = 0

    def walk(a):
        nonlocal hungry_option, required_positional, variadic_positionals
        tags.update(a.get('_norm_tags', set()))
        if a.get('positionals_at_end'):
            tags.add('positionals_at_end')
        if a.get('prefix_command'):
            tags.add('prefix_command')
        if a.get('fallthrough'):
            tags.add('fallthrough')
        if a.get('subcommand_prefix'):
            tags.add('subcommand_prefix')
        if a.get('config'):
            tags.add('config')
        if a.get('allow_extras_mode', 0) not in (0, 5):
            tags.add('extras_mode')
        if a.get('windows_style'):
            tags.add('windows_style')
        if a.get('immediate_callback'):
            tags.add('immediate_callback')
        if a.get('require_subcommand_max') != 0 or a.get('require_subcommand_min') not in (0, 1):
            tags.add('require_subcommand_range')
        if a.get('require_option_min') or a.get('require_option_max'):
            tags.add('require_option')
        if not a.get('subcommand_fallthrough', True):
            tags.add('subcommand_fallthrough_off')
        if a.get('ignore_case'):
            tags.add('app_ignore_case')
        if a.get('ignore_underscore'):
            tags.add('app_ignore_underscore')
        if a.get('disabled') or a.get('disabled_by_default') or a.get('enabled_by_default'):
            tags.add('app_startup_state')
        if a.get('need_subcommands') or a.get('exclude_subcommands'):
            tags.add('app_needs_excludes')
        if a.get('need_options') or a.get('exclude_options'):
            tags.add('app_needs_excludes')
        if a.get('callbacks'):
            tags.add('callbacks')
        if a.get('required'):
            tags.add('app_required')
        if len(a.get('parsed_subcommands', [])) > 1:
            tags.add('multi_subcommand')
        if a.get('count', 0) > 1:
            tags.add('duplicate_subcommand')
        if not a.get('has_help'):
            tags.add('no_help')
        if a.get('has_help') and a.get('help_names') != BUILTIN_HELP_NAMES:
            tags.add('custom_help')
        if a.get('has_help_all'):
            tags.add('help_all')
        if a.get('has_version'):
            version_names = a.get('version_names', [])
            if any(tok in version_names for tok in args):
                tags.add('version_used')
            elif version_names not in ([], BUILTIN_VERSION_NAMES):
                tags.add('custom_version')
            if '-V' in args and '-V' not in version_names:
                tags.add('version_short_alias')
        for o in a['options']:
            tags.update(analysis_tags(o, args))
            shape_tags = set()
            shape = option_shape(o, shape_tags)
            tags.update(shape_tags)
            if o.get('delimiter'):
                delims.add(chr(o['delimiter']))
            if not o['positional'] and (o.get('allow_extra_args') or o.get('emax', 0) >= MAX_EXPECTED):
                hungry_option = True
            if o['positional'] and o.get('required'):
                required_positional = True
            if o['positional'] and shape and shape.get('vector') and shape.get('nargs') \
                    and shape['nargs'][0] in ('range', 'atleast'):
                variadic_positionals += 1
            if shape and shape.get('kind') == 'flag' and o['type']:
                tags.add('flag_like_option')
            if shape and o['positional'] and o.get('default') and shape.get('vector'):
                tags.add('vector_default')
            if o.get('multi') == 0 and not o['positional'] and option_occurrences(o, args) > 1:
                tags.add('multi_policy_throw')
            base = o.get('type', '').partition(':')[0]
            if base in ('INT', 'UINT', 'FLOAT', 'BOOLEAN'):
                opts_seen.add(base)
        if any(o['positional'] for o in a['options']) and a['subcommands']:
            tags.add('positional_subcommand_mix')
        for s in a['subcommands']:
            walk(s)

    walk(schema)
    if 'BOOLEAN' in opts_seen and any(
            looks_like_numeric(tok) and tok.lower() not in
            {'true', 'false', 'yes', 'no', 'on', 'off', 't', 'f', 'y', 'n', '1', '0'}
            for tok in args):
        tags.add('bool_numeric_conversion')
    if opts_seen & {'INT', 'UINT', 'FLOAT'}:
        for tok in args:
            body = tok.strip()
            if body != tok or "'" in body or '_' in body or body.lower().startswith('0o'):
                tags.add('numeric_lexical_extension')
                break
    if '++' in args:
        tags.add('plusplus')
    if '--' in args:
        tags.add('terminator')
    if any(tok.startswith('[[') and tok.endswith(']]') for tok in args):
        tags.add('nested_vector_escape')
    if any(tok.startswith('[') and tok.endswith(']') for tok in args):
        tags.add('vector_escape_syntax')
    if '' in args:
        tags.add('empty_argument')
    if delims:
        for tok in args:
            if any(tok.startswith(d) or tok.endswith(d) or (d + ' ') in tok
                   or (d + '\t') in tok or (' ' + d) in tok or ('\t' + d) in tok
                   for d in delims):
                tags.add('delimiter_edge')
                break
    if hungry_option and required_positional:
        tags.add('positional_priority')
    if variadic_positionals > 1:
        tags.add('variable_positional_order')
    if any(matches_dot_path(schema, tok) for tok in args):
        tags.add('dot_notation')
    if error == 'Runtime error':
        tags.add('callback_runtime')
    if 'requires ' in error or 'excludes ' in error:
        tags.add('needs_excludes')
    return tags


blocked = Counter()
valid = []
for r in rows:
    cmd_events = []
    opt_events = []
    sanitize_schema(r['schema'], cmd_events, opt_events)
    r['args'] = rewrite_args(r['args'], cmd_events, opt_events)
    r['schema'] = normalize_node(r['schema'])
    dedupe_events = []
    dedupe_subcommands(r['schema'], dedupe_events)
    if dedupe_events:
        r['args'] = rewrite_args(r['args'], dedupe_events, [])
    merge_trailing_positionals(r['schema'])
    r['args_b'] = [a.encode('utf-8') for a in r['args']]
    reason = hard_block(r['schema'])
    if reason:
        blocked[reason] += 1
        continue
    r['tags'] = tags_for_case(r)
    valid.append(r)

print(f'replayable {len(valid)} / {len(rows)}; blocked {sum(blocked.values())}: {dict(blocked)}')
print('tagged cases:', sum(1 for r in valid if r['tags']))

lines = ['#include <cli.hpp>', '#include <algorithm>', '#include <cctype>',
         '#include <cmath>', '#include <cstdio>', '#include <cstdlib>',
         '#include <sstream>', '#include <string>', '#include <vector>', '']
lines.append('[[maybe_unused]] static std::string diff_lower(std::string value) {')
lines.append('    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));')
lines.append('    return value;')
lines.append('}')
lines.append('[[maybe_unused]] static std::string diff_squeeze(std::string value) {')
lines.append("    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());")
lines.append('    return value;')
lines.append('}')
lines.append('[[maybe_unused]] static std::string diff_strip_space(std::string value) {')
lines.append("    value.erase(std::remove_if(value.begin(), value.end(), [](char c) {")
lines.append("        return c == ' ' || c == '\\t'; }), value.end());")
lines.append('    return value;')
lines.append('}')
lines.append('')
lines.append('static bool diff_eq(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }')
lines.append('static bool diff_eq(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }')
lines.append('static bool diff_eq(long double a, long double b) { return a == b || (std::isnan(a) && std::isnan(b)); }')
lines.append('template <typename T>')
lines.append('static bool diff_eq(const T& a, const T& b) { return a == b; }')
lines.append('static bool diff_eq(const std::string& a, const char* b) { return a == b; }')
lines.append('static bool diff_eq(const char* a, const std::string& b) { return a == b; }')
lines.append('template <typename T>')
lines.append('static bool diff_eq(const std::vector<T>& a, const std::vector<T>& b) {')
lines.append('    if (a.size() != b.size()) return false;')
lines.append('    for (std::size_t i = 0; i < a.size(); ++i) {')
lines.append('        if (!diff_eq(a[i], b[i])) return false;')
lines.append('    }')
lines.append('    return true;')
lines.append('}')
lines.append('static std::string diff_repr(bool value) { return value ? "true" : "false"; }')
lines.append('static std::string diff_repr(char value) { return std::string(1, value); }')
lines.append('template <typename T>')
lines.append('static std::string diff_repr(const T& value) {')
lines.append('    std::ostringstream out;')
lines.append('    out << value;')
lines.append('    return out.str();')
lines.append('}')
lines.append('template <typename T>')
lines.append('static std::string diff_repr(const std::vector<T>& values) {')
lines.append('    std::string out = "[";')
lines.append('    for (std::size_t i = 0; i < values.size(); ++i) {')
lines.append('        if (i) out += ", ";')
lines.append('        out += diff_repr(values[i]);')
lines.append('    }')
lines.append('    return out + "]";')
lines.append('}')
lines.append('')
lines.append('static void clear_diff_env() {')
for name in sorted(ALL_ENV_NAMES):
    lines.append(f'    ::unsetenv({cpp_str(name)});')
lines.append('}')
lines.append('')
coverage = count_value_coverage(valid)
for i, r in enumerate(valid):
    assign_ids(r['schema'], [0])
    checks = []
    compare = not r['tags'] and r['outcome'] == 'ok'
    lines.append('static int case_' + str(i) + '() {')
    lines.append('    clear_diff_env();')
    for name, value in sorted(case_env_pairs(r).items()):
        if value is None:
            continue
        if b'\0' in value:
            raise SystemExit(f'NUL in environment value for {name}')
        lines.append(f'    ::setenv({cpp_str(name)}, {cpp_bytes(value)}, 1);')
    lines.append('    bool ok = false;')
    lines.append('    bool value_ok = true;')
    lines.append('    try {')
    lines.append('    cli::App app("app");')
    emit_app(r['schema'], 'app', lines,
             checks if compare else None, [0])
    arglist = ', '.join(cpp_string_expr(x) for x in r['args_b'])
    lines.append(f'    auto res = app.parse(std::vector<std::string>{{{arglist}}});')
    lines.append('    ok = res.has_value();')
    if checks:
        emit_value_checks(checks, lines)
    lines.append('    } catch (const std::exception&) { ok = false; }')
    expected = 'true' if r['outcome'] == 'ok' else 'false'
    lines.append(f'    if (ok == {expected}) return value_ok ? 0 : 2;')
    lines.append('    return ok ? 1 : -1;')
    lines.append('}')
    lines.append('')

lines.append('int main() {')
lines.append('    int failures = 0;')
for i, r in enumerate(valid):
    test = r.get('test', '')
    args = ' '.join(r['args'])
    tags = ','.join(sorted(r['tags'])) or '-'
    lines.append(
        f'    {{ int d = case_{i}(); if (d != 0) {{ std::printf("FAIL\\t{i}\\t%d\\t%s\\t%s\\t%s\\n", d, '
        f'{cpp_str(tags)}, {cpp_str(test)}, {cpp_str(args)}); ++failures; }} }}')
lines.append(f'    std::printf("SUMMARY\\t%d\\t{len(valid)}\\n", failures);')
lines.append('    return failures == 0 ? 0 : 1;')
lines.append('}')
open(OUT, 'w').write('\n'.join(lines))
print(f"value comparisons {coverage['compared']} / {coverage['cases']} "
      f"untagged successful cases, {coverage['fields']} fields; "
      f"skipped {coverage['skipped']} field(s) without a builtin mapping")
print('wrote', OUT)
