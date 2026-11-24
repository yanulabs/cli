# cli.hpp — A modern C++23 command-line argument parser

> Single-header · zero-dependency · cross-platform · fully type-safe

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blueviolet.svg)]()

---

## Features

| Feature | Detail |
|---|---|
| **Single header** | Drop `include/cli.hpp` into your project — done |
| **`std::expected`** | Composable, propagating error handling |
| **Type-safe handles** | `Option<T>`, `Flag`, `Positional<T>`, `Count` |
| **Fluent builder** | C++23 *deducing-this* — no CRTP boilerplate |
| **Built-in types** | `bool`, integrals, floats, `string`, `path`, `vector<T>` |
| **Custom types** | ADL `cli_parse()` / `cli_format()` hooks — no registration boilerplate |
| **Constraints** | `.range()`, `.choices()`, `.validate(fn, desc)` on options *and* positionals |
| **Env-var fallback** | `.env("VAR")` on options *and* positionals |
| **Delimited values** | `.delimiter(',')` — `--items=a,b,c` for `vector<T>`; quoted elements may contain the delimiter |
| **Arity control** | `.nargs(2)`, `.nargs(1,3)`, `.nargs_at_least(1)` on vector options *and* positionals |
| **Config files** | `config_file()` + `config_option()` — INI sections, arrays, subcommands |
| **Response files** | `response_files()` — `@args.txt` with quotes, comments, nesting |
| **Option aliases** | `.alias("--colour")` adds extra spellings to any option/flag/count |
| **Global options** | `.global()` options work before or after any subcommand and appear in child help/completion |
| **Long-option abbreviation** | `allow_abbrev()` accepts unique prefixes such as `--verb` |
| **Extra arguments** | `allow_extras()` + `remaining_args()` for wrapper/forwarding tools |
| **Hidden commands** | `subcommand(...).hidden()` — dispatchable but absent from help/completion |
| **Unknown-command hints** | Levenshtein suggestions for misspelled verbs, including aliases |
| **Short-flag grouping** | `-vxf` ≡ `-v -x -f` |
| **Inline values** | `-n42` ≡ `-n 42`, `--out=file` ≡ `--out file` |
| **Negative numbers** | `-1`, `-3.14` etc. correctly handled as positional values |
| **End-of-options** | `--` |
| **Subcommands** | Recursive, each with their own option set; inherited version |
| **Rich help** | ANSI-colored, auto-aligned, section-structured; `help_text()` returns a string |
| **Suggestions** | Levenshtein "did you mean?" on unknown options (incl. `--no-X` variants) |
| **Completions** | Bash, Fish (with subcommand aliases), and Zsh generators |
| **Count options** | `add_count("--verbose")` counts `-v -v -v` occurrences; `[count]` shown in help |
| **Required flags/counts** | `.required()` works on options, flags, counts, and positionals |
| **Transforms** | `.transform(fn)` applies a post-parse value transformation |
| **Mutex groups** | `mutex_group({...})` — at most one option in the set may be used |
| **Require-together** | `require_together({...})` — all-or-nothing option sets |
| **Require-one-of** | `require_one_of({...})` — at least one option from the set |
| **Subcommand aliases** | `.alias("ci")` — alternative names for subcommands |
| **Negatable flags** | `.negatable()` — `--[no-]flag` shown in help, `--no-flag` sets false |
| **Named sections** | `.section("Group")` / `.group("Group")` — help sub-headers |
| **Reusable App** | `parse()` resets automatically; `app.reset()` is available explicitly |
| **Initializer parse** | `app.parse({"-v", "--jobs", "8"})` — no argc/argv needed |
| **Vector parse** | `app.parse(std::vector<std::string>{...})` |
| **Differential verification** | Replays the full upstream CLI11, argparse, and cxxopts test corpora, including parsed values for supported CLI11 options |

---

## Quick Start

```cpp
#include "cli.hpp"
#include <print>
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cli::App app("tool", "My blazing-fast tool");
    app.version("1.0.0").author("Yanujz")
       .example("tool build --target release -j8");

    auto verbose = app.add_flag("--verbose").short_name("-v").help("Verbose output");
    auto jobs    = app.add<unsigned>("--jobs").short_name("-j").help("Parallel jobs")
                      .default_val(4u).range(1u, 256u);
    auto output  = app.add<fs::path>("--output").short_name("-o").help("Output path")
                      .required().meta("DIR");
    auto tags    = app.add<std::vector<std::string>>("--tag").help("Tags");

    auto& build  = app.subcommand("build", "Compile sources").alias("b");
    auto  target = build.add<std::string>("--target").short_name("-t").help("Profile")
                       .choices({"debug", "release"}).default_val("debug");

    auto result = app.parse(argc, argv);
    if (!result) { app.print_error(result.error()); return 1; }

    if (*verbose) std::println("verbose mode");
    std::println("jobs={} out={}", *jobs, output->string());
}
```

---

## Installation

### Option A — copy the header

```bash
cp include/cli.hpp /path/to/your/project/
```

### Option B — CMake `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
    cli
    GIT_REPOSITORY https://github.com/yanujz/cli.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(cli)

target_link_libraries(your_target PRIVATE cli::cli)
```

### Option C — CMake install + `find_package`

```bash
cmake -B build -DCLI_BUILD_EXAMPLES=OFF -DCLI_BUILD_TESTS=OFF
cmake --install build --prefix /usr/local
```

```cmake
find_package(cli REQUIRED)
target_link_libraries(your_target PRIVATE cli::cli)
```

---

## API Reference

### `cli::App`

```cpp
cli::App app("name", "description");
app.version("1.0.0").author("Name").example("name --flag");

// Options
Option<T>    app.add<T>("--long").short_name("-s").help("...").default_val(v);
Flag         app.add_flag("--verbose").short_name("-v").help("...");
Count        app.add_count("--verbose").short_name("-v").help("...");
Positional<T> app.add_positional<T>("name").help("...");
// .global() on an option makes it visible to every descendant subcommand

// Subcommands
App&          app.subcommand("name", "desc").alias("n");

// Constraints
App&          app.mutex_group({"--verbose", "--quiet"});
App&          app.require_together({"--output", "--format"});
App&          app.require_one_of({"--json", "--yaml"});

// Input sources & permissive parsing
App&          app.config_file("tool.ini", /*required=*/false);
App&          app.config_option("--config");
App&          app.response_files();
App&          app.allow_abbrev();
App&          app.allow_extras();
const auto&   app.remaining_args();

// Parse (each overload resets prior state automatically)
ParseResult   app.parse(argc, argv);   // std::expected<void, ParseError>
ParseResult   app.parse({"args", "..."});
ParseResult   app.parse(std::vector<std::string>{...});
void          app.parse_or_exit(argc, argv);

// Introspection
App*          app.active_subcommand();
bool          app.is_subcommand("name");
bool          app.has_flag("--flag");
bool          app.has_option("--opt");

// Output
std::string   app.help_text();          // render help without printing
void          app.print_help();
void          app.print_help(ostream&); // e.g. into a stringstream
void          app.print_error(const ParseError&);
void          app.bash_completion(ostream&);
void          app.fish_completion(ostream&);
void          app.zsh_completion(ostream&);
```

### `Option<T>` builder chain

```cpp
auto opt = app.add<T>("--flag")
              .short_name("-x")
              .alias("--alias")       // extra spelling
              .help("description")
              .default_val(value)     // implies optional
              .required()             // must be provided
              .hidden()               // exclude from help
              .env("ENV_VAR")         // fallback env variable
              .meta("PLACEHOLDER")    // override <META> in help
              .range(lo, hi)          // inclusive bounds
              .choices({a, b, c})     // allowed values
              .validate(fn, "desc")   // custom predicate
              .transform(fn)          // post-parse value transform
              .delimiter(',')         // split vector values (e.g. --tag=a,b,c)
              .nargs(2)               // vector: consume exactly 2 tokens
              .section("Group Name"); // help section header (alias: .group)
```

Constraints and custom validators are also applied to values that come from
defaults or environment variables, not only from the command line.

### Value access

```cpp
if (opt)          { /* has value */ }
*opt              // T& (UB if not set and no default)
opt->member       // shortcut
opt.is_set()      // true only if explicitly passed on CLI
opt.value_or(x)   // safe fallback
opt.get()         // std::optional<T>&
```

### `ParseError`

```cpp
result.error().kind;     // ParseError::Kind enum
result.error().message;  // human-readable string
```

**Kinds:** `HelpRequested`, `VersionRequested`, `UnknownOption`, `MissingValue`,
`UnexpectedValue`, `InvalidValue`, `MissingRequired`, `MissingSubcommand`,
`TooManyPositionals`, `ValidationFailed`, `OutOfRange`, `BadChoice`,
`MutuallyExclusive`, `RequiresOption`, `UnknownCommand`, `ConfigError`,
`AmbiguousOption`, `ResponseFileError`

---

## Advanced Features

### Count options

Count how many times a flag appears (e.g. `-v`, `-vv`, `-vvv`):

```cpp
auto verbosity = app.add_count("--verbose").short_name("-v").help("Verbosity level");
app.parse_or_exit(argc, argv);
std::println("Verbosity: {}", *verbosity);  // 0, 1, 2, 3, ...
```

### Post-parse transforms

Apply a function to a parsed value after all constraints pass:

```cpp
auto name = app.add<std::string>("--name").required()
               .transform([](std::string s) {
                   std::ranges::transform(s, s.begin(), ::toupper);
                   return s;
               });
// *name is now uppercase regardless of input
```

### Mutually exclusive options

```cpp
app.add_flag("--verbose").short_name("-v");
app.add_flag("--quiet").short_name("-q");
app.mutex_group({"--verbose", "--quiet"});
// Error if both -v and -q are passed
```

### Required-together options

```cpp
app.add<std::string>("--output").required();
app.add<std::string>("--format").required();
app.require_together({"--output", "--format"});
// If either is set, both must be set
```

### Subcommand aliases

```cpp
auto& commit = app.subcommand("commit", "Record changes").alias("ci");
// Both "commit" and "ci" dispatch to the same subcommand
```

### Global options

```cpp
auto verbose = app.add_flag("--verbose").short_name("-v").global();
auto& build  = app.subcommand("build", "");
// tool build --verbose and tool --verbose build both work
```

Globals are inherited by every descendant, participate in abbreviation and
typo suggestions, and appear in child help/completion. Shadowing is
name-by-name: a child option named `-v` hides only the inherited `-v`, while
the global `--verbose` spelling remains functional and documented. Parsing a
subcommand directly (`build.parse(...)`) applies ancestor environment, config,
and default fallbacks, but only inherited globals participate in required and
group checks; non-global ancestor options and positionals cannot be supplied
from that entry point and do not make the direct parse fail. Parent positionals
are likewise not finalized when a subcommand is selected through the root.

### Negatable flags

```cpp
auto color = app.add_flag("--color").negatable().default_val(true);
// Accepts --color (true) and --no-color (false)
// Help shows: --[no-]color
```

### Named option sections

```cpp
app.add<std::string>("--host").section("Connection Options");
app.add<unsigned>("--port").section("Connection Options");
app.add_flag("--verbose").section("Output Options");
// Help groups options under section headers
```

### Variable arity options

A `vector<T>` option normally consumes one token per occurrence. Use
`.nargs()` when one occurrence should consume several tokens:

```cpp
auto files = app.add<std::vector<std::string>>("--file").nargs(2);
// tool --file a.txt b.txt --file c.txt d.txt → four values

auto tags = app.add<std::vector<std::string>>("--tag").nargs(1, 3);
// stops before the next option once the minimum is met

auto paths = app.add<std::vector<std::string>>("--path").nargs_at_least(1);
// consumes until the next option-looking token
```

Exact arities consume dash-prefixed values as values; ranged and at-least
arities stop at the next option so flags are not swallowed.

### Variable arity positionals

Vector positionals accept the same arity controls:

```cpp
auto pair = app.add_positional<std::vector<int>>("pair").nargs(2);
auto rest = app.add_positional<std::string>("rest");
// tool 1 2 label → pair = {1, 2}, rest = "label"

auto all = app.add_positional<std::vector<std::string>>("all").nargs_at_least(1);
// must be the last positional: consumes every remaining non-option token
```

Fixed arities may be followed by other positionals. Ranged and at-least
arities are variable, so they must be registered last; doing otherwise throws
`std::invalid_argument` at registration time. Arity is revalidated before each
parse, so changing a handle from fixed to variable arity after registration is
also rejected. Positional defaults must satisfy the configured arity.

### Config files

```cpp
app.config_file("tool.ini");       // optional default source
app.config_option();               // adds --config <FILE>
auto jobs = app.add<unsigned>("--jobs").default_val(4u);
```

```ini
# tool.ini
jobs = 8

[build]
target = release
```

Precedence is **command line > environment > config > default**. Sections map
to command paths (`[remote.add]` for nested commands), values may be quoted,
and array syntax `[a, b, "c,d"]` populates vector options and positionals
without re-splitting quoted delimiters. `[]` supplies an explicit empty vector,
while `[""]` supplies one empty element. Section headers may be quoted
(`["build"]`) or use TOML's double-bracket spelling (`[[build]]`); `[default]`
configures the root command, and a UTF-8 BOM at the start of the file is
ignored. Missing optional files are ignored; `config_file(path, true)` makes
them mandatory. Malformed headers, quotes, arrays, and trailing text return
`ParseError::Kind::ConfigError`.

### Response files

```cpp
app.response_files();
// tool @args.txt
```

Files are tokenised with shell-like quoting, support `#` comments and nested
`@file` references (up to 8 levels), and produce
`ParseError::Kind::ResponseFileError` on failure. Nested paths are resolved
relative to the including file. `@@name` escapes a literal `@name` token.

### Extra arguments for wrapper tools

```cpp
app.allow_extras();
app.parse_or_exit(argc, argv);
for (const auto& token : app.remaining_args()) { /* forward it */ }
```

Unknown options and surplus positionals are collected instead of rejected.
Forwarded tokens before a subcommand do not prevent that subcommand from being
dispatched, so wrapper tools can mix their own arguments with a verb. Because
`allow_extras()` is a parser policy, it applies inside subcommands too, and
the root's `remaining_args()` contains the child's extras appended in
command-line order. The `--` marker itself is not included.

### Abbreviations and aliases

```cpp
app.allow_abbrev();                                // --verb → --verbose
auto color = app.add<std::string>("--color")
                 .alias("--colour")
                 .alias("-c");
```

Abbreviation is opt-in and only resolves unique long-option prefixes;
ambiguous prefixes return `ParseError::Kind::AmbiguousOption`. The root's
setting is inherited by subcommands, and inherited `.global()` options take
part in prefix resolution.

### Require-one-of groups and hidden commands

```cpp
app.require_one_of({"--json", "--yaml"});          // at least one
auto& debug = app.subcommand("debug", "Internal").hidden();
```

Hidden commands stay fully dispatchable but are omitted from help and all
completion generators.

### Custom value types (ADL)

Any type with `cli_parse()` and `cli_format()` in its own namespace works
directly with `add<T>()` and `add_positional<T>()`:

```cpp
namespace app {
struct Level { int value; };

inline std::expected<Level, std::string> cli_parse(std::string_view sv) {
    if (sv == "low")  return Level{1};
    if (sv == "high") return Level{3};
    return std::unexpected("expected low or high");
}
inline std::string cli_format(const Level& l) {
    return std::format("Level({})", l.value);
}
}

auto level = app.add<app::Level>("--level").default_val(app::Level{1});
```

For templates or multiple overloads, use the type-identity form:

```cpp
inline std::expected<Point, std::string>
cli_parse(std::string_view sv, std::type_identity<Point>);
```

`cli_format` is used in help output, defaults, and error messages.

### Positional arguments

Positionals support the same constraints and fallbacks as options:

```cpp
auto input = app.add_positional<std::string>("input")
                .help("Input file")
                .env("APP_INPUT")
                .validate([](const std::string& s) { return !s.empty(); },
                          "input must not be empty");

auto values = app.add_positional<std::vector<int>>("values")
                 .delimiter(',')     // one token: 1,2,3 → {1, 2, 3}
                 .range(0, 100);
```

Positionals are required by default; `.default_val(v)` makes them optional.

### Help and version behavior

`--help` / `-h` and `--version` / `-V` are built in whenever no user option
occupies the same spelling. Version strings are inherited by subcommands, so
`tool sub --version` works even when only the root app defines a version.

```cpp
std::string text = app.help_text();  // capture instead of printing
app.print_help(std::cerr);           // write to any ostream
```

### Zsh completion

```cpp
app.zsh_completion(std::cout);  // writes #compdef-style function
// Usage: eval "$(mytool --shell zsh)"
```

---

## Building Examples & Tests

```bash
cmake -B build
cmake --build build

# Run everything (19 domain suites, aggregate run, process-level exit tests)
ctest --test-dir build --output-on-failure

# Inspect or run one domain
./build/tests/test_parse --list-test-suites
./build/tests/test_parse --test-suite=flags

# Throughput microbenchmarks
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
      -DCLI_BUILD_TESTS=OFF -DCLI_BUILD_BENCHMARKS=ON
cmake --build build-bench --target bench_parse
./build-bench/bench/bench_parse 500000

# Head-to-head against CLI11 and cxxopts (fetched at configure time)
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release \
      -DCLI_BUILD_TESTS=OFF -DCLI_BUILD_BENCHMARKS=ON \
      -DCLI_BUILD_COMPARATIVE_BENCHMARKS=ON
cmake --build build-compare --target bench_compare
./build-compare/bench/bench_compare 200000

# Adversarial fuzzing (Clang + libFuzzer, checked-in seed corpus)
cmake -S . -B build-fuzz -DCLI_BUILD_EXAMPLES=OFF -DCLI_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_parse
./build-fuzz/tests/fuzz_parse tests/fuzz_corpus -max_len=4096 -runs=200000

# Upstream differential replays (opt-in locally; required in CI)
cmake -S . -B build-diff -DCLI_BUILD_DIFFERENTIAL_TESTS=ON
cmake --build build-diff -j
ctest --test-dir build-diff -R differential_upstream --output-on-failure
```

### Upstream differential testing

`tools/differential/` replays every parse call captured from the complete
CLI11 v2.7.2, argparse v3.2, and cxxopts v3.3.1 test suites — 1,575, 565, and
46 records respectively. Each replay reconstructs the upstream schema and feeds
the identical argument vector to `cli.hpp`. Accept/reject parity is checked for
all three suites; all three also compare parsed option, positional, and flag
values for successful untagged cases. Coverage is 215/269 cases and 296 fields
for CLI11, 118/136 cases and 129 fields for argparse, and 11/14 cases and 20
fields for cxxopts, with no captured field skipped for lack of a builtin mapping.

The driver fails on any untagged divergence, parsed-value mismatch, or schema
that cannot be replayed. Tagged divergences name semantics outside the supported
subset (config files, custom converters, help rendering, exotic containers, and
so on); they are expected and are not counted as parser mismatches. The checked
corpora currently produce zero untagged divergences and zero hard blocks.  A
dedicated CI job replays all three suites on every push and pull request.

```bash
python3 tools/differential/run.py --cxx clang++
python3 tools/differential/run.py --suite cli11 --cxx clang++ --strict
```

The scope, tag glossary, capture provenance, rebuild commands, and limitations
are documented in [`tools/differential/README.md`](tools/differential/README.md).
This is differential testing against captured upstream behaviour, not a claim
of full upstream feature parity.

Tests are split by domain (`flags`, `options`, `positionals`, `values`,
`tokens`, `upstream_edge_cases`, `constraints`, `groups`, `environment`,
`extensions`, `subcommands`, `errors`, `help`, `completion`, `lookup`,
`lifecycle`, `custom_types`, `robustness`, `api`). Each domain is a doctest
suite, so failures are attributable to one area and the whole suite can still
be run at once. The fuzz target is opt-in and requires Clang with a libFuzzer runtime
(Linux CI uses Clang 19); `CLI_FUZZER_SANITIZERS` controls the sanitizer set
(default `address,undefined`).  Its oracle compares parsed values, explicit-set
flags, extras, active command chains, repeated parses, and direct-child parsing
against equivalent root dispatch.

### Performance

`bench_parse` measures end-to-end `parse()` throughput including automatic
reset. On an Apple-silicon laptop (Release, Clang) the suite currently runs at:

| Scenario | ns/parse | parses/sec |
|---|---:|---:|
| 8 short flags | ~130 | ~7.7 M |
| 6 typed options (12 tokens) | ~228 | ~4.4 M |
| mixed flags/options/vector/positional | ~227 | ~4.4 M |
| subcommand + options | ~130 | ~7.7 M |
| nested subcommand | ~116 | ~8.6 M |

`bench_compare` runs equivalent schema shapes against CLI11 2.7.2 and cxxopts
3.3.1. Across repeated runs on the same machine, cli.hpp is roughly 2.8–6.8×
faster per parse than CLI11 and 26–56× faster than cxxopts on the shared
scenarios. Subcommand workloads are reported separately because cxxopts does
not support them.

Numbers vary by machine; run the benchmark targets on your own hardware.
Token handling is allocation-free for `parse(argc, argv)`; string and vector
values may allocate. One-character short options and collision-free long
options use direct ASCII indexes, floating-point conversion uses
`std::from_chars` where available, and formatting is confined to help/error
paths.

---

## Requirements

| Compiler | Minimum version |
|---|---|
| GCC | 13 |
| Clang | 17 |
| MSVC | 19.35 (VS 2022 17.5) |

Requires **C++23** (`std::expected`, `std::format`, *deducing-this*). The
example programs additionally use `std::print`.

---

## License

[MIT](LICENSE) © 2024 Yanujz Labs
