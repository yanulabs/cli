#!/usr/bin/env python3
"""Run the upstream differential replays against cli.hpp.

Each generator translates a runtime capture of an upstream library's test
suite into a standalone C++ program.  The program reproduces every captured
parse call and reports a mismatch when cli.hpp accepts an input the upstream
library rejects (or vice versa).  Mismatches carry a tag naming the upstream
semantics cli.hpp does not model; an untagged mismatch is a parser divergence
and fails this driver.

The checked-in corpora were captured from:
  CLI11    v2.7.2
  cxxopts  v3.3.1
  argparse v3.2

Accept/reject parity is checked for every replayable capture.  CLI11 captures
also record option results, so successful untagged cases are checked for
parsed-value equality as well (value mismatches use direction 2).
"""
from __future__ import annotations

import argparse
import collections
import os
import re
import shlex
import subprocess
import sys
import tempfile

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOL_DIR, '..', '..'))

SUITES = {
    'cli11': {
        'generator': 'generate_cli11.py',
        'tag_field': 3,
        'direction_field': 2,
    },
    'argparse': {
        'generator': 'generate_argparse.py',
        'tag_field': 3,
        'direction_field': 2,
    },
    'cxxopts': {
        'generator': 'generate_cxxopts.py',
        'tag_field': 2,
        'direction_field': None,
    },
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('all',) + tuple(SUITES), default='all')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'),
                        help='C++ compiler used for the replays')
    parser.add_argument('--cxxflags', default='-O0',
                        help='extra compiler flags (default: -O0)')
    parser.add_argument('--include',
                        default=os.path.join(REPO_ROOT, 'include'),
                        help='directory containing cli.hpp')
    parser.add_argument('--out-dir', default=None,
                        help='directory for generated sources and binaries')
    parser.add_argument('--timeout', type=int, default=900,
                        help='per-replay timeout in seconds')
    parser.add_argument('--strict', action='store_true',
                        help='fail on tagged mismatches too')
    return parser.parse_args()


def parse_generator_output(text):
    blocked = re.search(r'blocked (\d+)', text)
    return int(blocked.group(1)) if blocked else 0


def parse_failures(text, spec):
    mismatches = []
    for line in text.splitlines():
        if not line.startswith('FAIL\t'):
            continue
        fields = line.split('\t')
        tag_index = spec['tag_field']
        tags = fields[tag_index] if tag_index < len(fields) else ''
        if not tags:
            tags = '-'
        direction = 0
        if spec['direction_field'] is not None and spec['direction_field'] < len(fields):
            try:
                direction = int(fields[spec['direction_field']])
            except ValueError:
                direction = 0
        detail = '\t'.join(fields[1:])
        mismatches.append({'tags': tags, 'direction': direction, 'detail': detail})
    return mismatches


def parse_summary(text):
    match = re.search(r'^SUMMARY\t(\d+)\t(\d+)$', text, re.MULTILINE)
    if not match:
        return None
    return int(match.group(1)), int(match.group(2))


def run_suite(name, spec, args, out_dir):
    source = os.path.join(out_dir, f'{name}_replay.cpp')
    binary = os.path.join(out_dir, f'{name}_replay')
    generator = os.path.join(TOOL_DIR, spec['generator'])

    print(f'== {name} ==', flush=True)
    corpus = os.path.join(TOOL_DIR, 'corpus', f'{name}_all.jsonl')
    if not os.path.exists(corpus):
        corpus += '.gz'
    generated = subprocess.run(
        [sys.executable, generator, corpus, source],
        text=True, capture_output=True)
    if generated.returncode != 0:
        sys.stdout.write(generated.stdout)
        sys.stderr.write(generated.stderr)
        print(f'{name}: generator failed', file=sys.stderr)
        return False

    summary_line = next(
        (line for line in generated.stdout.splitlines() if 'replayable ' in line),
        generated.stdout.strip().splitlines()[-1] if generated.stdout.strip() else '')
    print(f'  {summary_line}')
    for line in generated.stdout.splitlines():
        if line.startswith('value comparisons '):
            print(f'  {line}')
    if name != 'cli11':
        print('  parsed values: not captured by this corpus; accept/reject only')
    blocked = parse_generator_output(generated.stdout)
    if blocked:
        print(f'{name}: {blocked} capture record(s) could not be replayed', file=sys.stderr)
        return False

    command = [args.cxx, '-std=c++23', f'-I{args.include}']
    command += shlex.split(args.cxxflags)
    command += [source, '-o', binary]
    compiled = subprocess.run(command, text=True, capture_output=True)
    if compiled.returncode != 0:
        sys.stderr.write(compiled.stderr)
        print(f'{name}: replay compilation failed', file=sys.stderr)
        return False

    try:
        executed = subprocess.run([binary], text=True, capture_output=True,
                                  timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f'{name}: replay timed out after {args.timeout}s', file=sys.stderr)
        return False
    if executed.returncode not in (0, 1):
        sys.stderr.write(executed.stderr)
        print(f'{name}: replay exited with {executed.returncode}', file=sys.stderr)
        return False

    with open(os.path.join(out_dir, f'{name}_results.tsv'), 'w',
              encoding='utf-8') as handle:
        handle.write(executed.stdout)

    summary = parse_summary(executed.stdout)
    if summary is None:
        sys.stderr.write(executed.stdout)
        sys.stderr.write(executed.stderr)
        print(f'{name}: replay produced no summary', file=sys.stderr)
        return False
    failed, total = summary
    mismatches = parse_failures(executed.stdout, spec)
    untagged = [m for m in mismatches if m['tags'] == '-']
    directions = collections.Counter(m['direction'] for m in mismatches)
    tag_counts = collections.Counter(
        tag for m in mismatches if m['tags'] != '-'
        for tag in m['tags'].split(','))

    print(f'  replayed {total}; mismatches {failed} '
          f'({failed - len(untagged)} tagged, {len(untagged)} untagged)')
    if directions:
        print('  directions: ' + ', '.join(
            f'{key:+d}={value}' for key, value in sorted(directions.items())))
    if tag_counts:
        top = ', '.join(f'{tag}={count}' for tag, count
                        in tag_counts.most_common(8))
        print(f'  tags: {top}')
    print(f'  results: {os.path.join(out_dir, name + "_results.tsv")}')

    if untagged:
        for mismatch in untagged:
            print(f'  UNTAGGED {mismatch["detail"]}', file=sys.stderr)
        print(f'{name}: {len(untagged)} untagged mismatch(es)', file=sys.stderr)
        return False
    if args.strict and failed:
        print(f'{name}: {failed} tagged mismatch(es) under --strict', file=sys.stderr)
        return False
    return True


def main():
    args = parse_args()
    out_dir = args.out_dir or tempfile.mkdtemp(prefix='cli-differential-')
    os.makedirs(out_dir, exist_ok=True)
    print(f'compiler: {args.cxx}')
    print(f'include:  {args.include}')
    print(f'out:      {out_dir}')

    selected = list(SUITES) if args.suite == 'all' else [args.suite]
    results = {name: run_suite(name, SUITES[name], args, out_dir)
               for name in selected}
    print()
    for name in selected:
        print(f'{name}: {"ok" if results[name] else "FAILED"}')
    return 0 if all(results.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
