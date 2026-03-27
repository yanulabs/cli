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
| **Type-safe handles** | `Option<T>`, `Flag`, `Positional<T>` |
| **Fluent builder** | C++23 *deducing-this* — no CRTP boilerplate |
| **Built-in types** | `bool`, integrals, floats, `string`, `path`, `vector<T>` |
| **Constraints** | `.range()`, `.choices()`, `.validate(fn, desc)` |
| **Env-var fallback** | `.env("VAR")` |
| **Short-flag grouping** | `-vxf` ≡ `-v -x -f` |
| **Inline values** | `-n42` ≡ `-n 42`, `--out=file` ≡ `--out file` |
| **End-of-options** | `--` |
| **Subcommands** | Recursive, each with their own option set |
| **Rich help** | ANSI-colored, auto-aligned, section-structured |
| **Suggestions** | Levenshtein "did you mean?" on unknown options |
| **Completions** | Bash and Fish generators |

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

    auto verbose = app.flag("-v", "--verbose", "Verbose output");
    auto jobs    = app.option<unsigned>("-j", "--jobs", "Parallel jobs")
                      .default_val(4u).range(1u, 256u);
    auto output  = app.option<fs::path>("-o", "--output", "Output path")
                      .required().meta("DIR");
    auto tags    = app.option<std::vector<std::string>>("--tag", "Tags");

    auto& build  = app.subcommand("build", "Compile sources");
    auto  target = build.option<std::string>("-t", "--target", "Profile")
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
Option<T>    app.option<T>("-s", "--long", "help");   // short + long
Option<T>    app.option<T>("--long", "help");          // long only

// Flags
Flag         app.flag("-v", "--verbose", "help");

// Positionals
Positional<T> app.positional<T>("name", "help");       // T=vector for variadic

// Subcommands
App&          app.subcommand("name", "description");

// Parse
ParseResult   app.parse(argc, argv);   // std::expected<void, ParseError>

// Introspection
App*          app.active_subcommand();
bool          app.is_subcommand("name");

// Output
void          app.print_help();
void          app.print_error(const ParseError&);
void          app.bash_completion(ostream&);
void          app.fish_completion(ostream&);
```

### `Option<T>` builder chain

```cpp
auto opt = app.option<T>("-x", "--flag", "help")
              .default_val(value)     // implies optional
              .required()             // must be provided
              .hidden()               // exclude from help
              .env("ENV_VAR")         // fallback env variable
              .meta("PLACEHOLDER")    // override <META> in help
              .range(lo, hi)          // inclusive bounds
              .choices({a, b, c})     // allowed values
              .validate(fn, "desc");  // custom predicate
```

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

**Kinds:** `UnknownOption`, `MissingValue`, `InvalidValue`, `MissingRequired`,
`TooManyPositionals`, `ValidationFailed`, `OutOfRange`, `BadChoice`

---

## Building Examples & Tests

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Requirements

| Compiler | Minimum version |
|---|---|
| GCC | 13 |
| Clang | 17 |
| MSVC | 19.35 (VS 2022 17.5) |

Requires **C++23** (`std::expected`, `std::print`, *deducing-this*).

---

## License

[MIT](LICENSE) © 2024 Yanujz Labs
