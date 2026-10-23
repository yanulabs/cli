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
| **Delimited values** | `.delimiter(',')` — `--items=a,b,c` for `vector<T>` |
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
| **Subcommand aliases** | `.alias("ci")` — alternative names for subcommands |
| **Negatable flags** | `.negatable()` — `--[no-]flag` shown in help, `--no-flag` sets false |
| **Named sections** | `.section("Group")` — groups options under sub-headers in help |
| **Reusable App** | `parse()` resets automatically; `app.reset()` is available explicitly |
| **Initializer parse** | `app.parse({"-v", "--jobs", "8"})` — no argc/argv needed |

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

// Subcommands
App&          app.subcommand("name", "desc").alias("n");

// Constraints
App&          app.mutex_group({"--verbose", "--quiet"});
App&          app.require_together({"--output", "--format"});

// Parse (each overload resets prior state automatically)
ParseResult   app.parse(argc, argv);   // std::expected<void, ParseError>
ParseResult   app.parse({"args", "..."});
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
              .section("Group Name"); // help section header
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
`MutuallyExclusive`, `RequiresOption`

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

# Run everything (17 domain suites, aggregate run, process-level exit tests)
ctest --test-dir build --output-on-failure

# Inspect or run one domain
./build/tests/test_parse --list-test-suites
./build/tests/test_parse --test-suite=flags

# Adversarial fuzzing (Clang + libFuzzer)
cmake -S . -B build-fuzz -DCLI_BUILD_EXAMPLES=OFF -DCLI_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_parse
./build-fuzz/tests/fuzz_parse -max_len=4096 -runs=100000
```

Tests are split by domain (`flags`, `options`, `positionals`, `values`,
`tokens`, `constraints`, `groups`, `environment`, `subcommands`, `errors`,
`help`, `completion`, `lookup`, `lifecycle`, `custom_types`, `robustness`,
`api`). Each domain is a doctest suite, so failures are attributable to one
area and the whole suite can still be run at once. The fuzz target is opt-in
and requires Clang with a libFuzzer runtime (Linux CI uses Clang 19);
`CLI_FUZZER_SANITIZERS` controls the sanitizer set (default
`address,undefined`).

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
