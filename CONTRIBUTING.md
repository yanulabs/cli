# Contributing to cli.hpp

Thanks for your interest in improving `cli.hpp`. Bug reports, documentation
fixes, tests, and focused feature proposals are all welcome.

Please read the [Code of Conduct](CODE_OF_CONDUCT.md) before participating.

## Ways to contribute

- Report reproducible bugs with a minimal parser schema and argument vector.
- Propose features that fit the single-header, C++23, zero-dependency design.
- Improve documentation, examples, or shell completion generation.
- Add regression tests for parser, lifecycle, config, or completion behavior.
- Improve differential coverage against CLI11, argparse, or cxxopts.
- Optimize hot paths when a benchmark demonstrates the change.

Please search existing issues and pull requests before opening a new one.

## Development setup

Requirements:

- A C++23 compiler supported by the project.
- CMake 3.25 or newer.
- Python 3 for optional differential replay tests.
- Clang with libFuzzer support for optional fuzz builds.

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run one test domain directly:

```sh
./build/tests/test_parse --test-suite=completion
./build/tests/test_parse --test-suite=subcommands
./build/tests/test_parse --test-suite=lifecycle
```

The full test binary contains 582 cases across 19 domain suites.

## Optional verification

### Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitize --output-on-failure
```

### Strict warnings and no RTTI

```sh
c++ -std=c++23 -Iinclude -Itests \
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
  -Werror -fno-rtti -fsyntax-only \
  tests/*.cpp examples/*.cpp bench/bench_parse.cpp
```

### Fuzzing

```sh
cmake -S . -B build-fuzz -DCLI_BUILD_EXAMPLES=OFF -DCLI_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_parse --parallel
./build-fuzz/tests/fuzz_parse tests/fuzz_corpus \
  -max_len=4096 -runs=200000
```

The fuzz oracle checks values, explicit-set flags, extras, active command chains,
repeated parses, direct-child parsing, and full failure-state reset.

### Upstream differential replay

```sh
cmake -S . -B build-diff -DCLI_BUILD_DIFFERENTIAL_TESTS=ON
cmake --build build-diff --parallel
ctest --test-dir build-diff -R differential_upstream --output-on-failure
```

See [`tools/differential/README.md`](tools/differential/README.md) for coverage,
classification rules, limitations, and corpus regeneration instructions.

## Code and test conventions

- Keep the public library in `include/cli.hpp` unless a change genuinely requires
  a new file.
- Preserve the single-header, zero-runtime-dependency design.
- Do not introduce an RTTI dependency.
- Follow the naming, formatting, and ownership patterns of surrounding code.
- Add focused regression tests for every behavior change.
- Keep generated differential replay sources out of the repository.
- Update `CHANGELOG.md` for user-visible changes.
- Preserve third-party copyright and license notices.

For parser changes, consider failure-state rollback, repeated parses, inherited
globals, direct subcommand parsing, config/environment precedence, and shell
completion behavior as part of the regression surface.

## Pull requests

1. Create a focused branch from `main`.
2. Keep unrelated cleanup out of the change.
3. Add or update tests and documentation.
4. Run the full CTest suite and any relevant optional gates.
5. Use a Conventional Commits-style subject, such as:
   - `fix: preserve empty environment values on Windows`
   - `feat: add context-aware shell completions`
   - `test: compare argparse parsed values`
6. Explain non-obvious compatibility or performance decisions in the PR body.

The pull-request template includes the expected checklist.

## Third-party contributions

If you add third-party code, data, or derived tests:

- Record the upstream project, version, copyright holder, and license.
- Add the applicable license text under `LICENSES/`.
- Update `NOTICE`.
- Preserve attribution in source comments where appropriate.

## License

By contributing, you agree that your contributions are provided under the
project's [MIT License](LICENSE). Copyright for the project is held by
**Yanu Labs**.
