# Upstream capture provenance

The differential corpora in `../corpus/` are runtime captures of the full
upstream test suites, not hand-picked examples.  Each instrumented upstream
parse writes one JSON line containing:

- the exact argument vector (byte-preserving `args_hex` for argparse),
- the constructed parser schema,
- the accept/reject outcome and error,
- for CLI11 and cxxopts, parsed option/positional values used for value checks.

## Versions

| Library | Tag | Commit | Corpus |
|---------|-----|--------|--------|
| CLI11 | `v2.7.2` | `cbd58a3696887b34c70949aef21a71735a0c2ad5` | `cli11_all.jsonl.gz` (1,575 records) |
| argparse | `v3.2` | `3eda91b2e1ce7d569f84ba295507c4cd8fd96910` | `argparse_all.jsonl.gz` (565 records) |
| cxxopts | `v3.3.1` | `44380e5a44706ab7347f400698c703eb2a196202` | `cxxopts_all.jsonl.gz` (46 records) |

The upstream commits were shallow clones of the tags above.  The full upstream
test suites were used; no source files were skipped from capture.

## Instrumentation

The patches add capture-only code behind a macro and do not change parsing
behaviour when the macro is undefined:

- `argparse.patch`: `ARGPARSE_DIFF_CAPTURE`
- `cxxopts.patch`: `CXXOPTS_DIFF_CAPTURE`
- `cli11.patch`: `CLI_DIFF_CAPTURE`, plus `tests/diff_capture.cpp` and
  `include/CLI/diff_hook.hpp` (both created by the patch).

`cli11_diff_capture.cpp` and `cli11_diff_hook.hpp` are checked in separately as
a readable copy; the patch creates the same files.  Only CLI11 needs a capture
translation unit because the hook is called from `App::parse(std::vector<...>)`.

## Rebuilding a corpus

Use a clean checkout of the exact tag.  The commands below mirror the builds
used for the checked-in corpora.

### CLI11

```sh
git clone --depth 1 --branch v2.7.2 https://github.com/CLIUtils/CLI11.git
patch -p1 -d CLI11 < capture/cli11.patch
cmake -S CLI11 -B CLI11-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCLI11_BUILD_TESTS=ON \
  -DCLI11_BUILD_EXAMPLES=OFF
cmake --build CLI11-build -j
rm -f cli11_all.jsonl
CLI_DIFF_OUT="$PWD/cli11_all.jsonl" \
  ctest --test-dir CLI11-build --output-on-failure
```

### argparse

```sh
git clone --depth 1 --branch v3.2 https://github.com/p-ranav/argparse.git
patch -p1 -d argparse < capture/argparse.patch
cmake -S argparse -B argparse-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DARGPARSE_BUILD_TESTS=ON \
  -DARGPARSE_BUILD_SAMPLES=OFF \
  -DCMAKE_CXX_FLAGS=-DARGPARSE_DIFF_CAPTURE
cmake --build argparse-build -j
rm -f argparse_all.jsonl
ARGPARSE_DIFF_OUT="$PWD/argparse_all.jsonl" \
  ctest --test-dir argparse-build --output-on-failure
```

### cxxopts

```sh
git clone --depth 1 --branch v3.3.1 https://github.com/jarro2783/cxxopts.git
patch -p1 -d cxxopts < capture/cxxopts.patch
cmake -S cxxopts -B cxxopts-build \
  -DCXXOPTS_CXX_STANDARD=17 \
  -DCXXOPTS_BUILD_TESTS=ON \
  -DCXXOPTS_BUILD_EXAMPLES=OFF \
  -DCMAKE_CXX_FLAGS=-DCXXOPTS_DIFF_CAPTURE
cmake --build cxxopts-build -j
rm -f cxxopts_all.jsonl
CXXOPTS_DIFF_OUT="$PWD/cxxopts_all.jsonl" \
  ctest --test-dir cxxopts-build --output-on-failure
```

Finally compress each capture:

```sh
gzip -9 -c cli11_all.jsonl    > corpus/cli11_all.jsonl.gz
gzip -9 -c argparse_all.jsonl > corpus/argparse_all.jsonl.gz
gzip -9 -c cxxopts_all.jsonl  > corpus/cxxopts_all.jsonl.gz
```

## Notes and limits

- The capture runs the upstream tests, so a test that aborts early can hide
  later parse calls in that binary.  The recorded counts are what the suites
  emitted on the reference run.
- CLI11's capture hook is on `App::parse(std::vector<std::string>&)` and the
  rvalue overload.  Parses performed inside spawned helper applications are not
  captured; their effects are still visible through the owning test.
- Environment values read by CLI11 options are recorded (`env_set`,
  `env_value_hex`) because the replay has to recreate them; unrelated ambient
  environment state is not part of the corpus.
- argparse captures schemas and outcomes, not parsed values.
- cxxopts must be built with C++17 (`CXXOPTS_CXX_STANDARD=17`, as in the
  command above) so the `std::optional` and `std::filesystem::path` test cases guarded by
  `CXXOPTS_HAS_OPTIONAL` and `CXXOPTS_HAS_FILESYSTEM` are compiled in.  A
  C++11/14 build silently omits six parse calls from the capture.  The
  checked-in corpus contains all 46 `Options::parse` calls in
  `test/options.cpp`, which is the only test source that performs parses.

## License

The patches are derivative works of the upstream projects and retain their
licenses:

- CLI11: BSD-3-Clause, Copyright (c) 2017-2026 University of Cincinnati,
  developed by Henry Schreiner and contributors.
- argparse: MIT, Copyright (c) 2019-2022 Pranav Srinivas Kumar and
  contributors.
- cxxopts: MIT, Copyright (c) 2014-2022 Jarryd Beck.

The corpora contain parser schemas and test arguments derived from the
upstream test suites under those licenses.
