# Changelog

All notable changes to this project will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2024-01-01

### Added
- Single-header `include/cli.hpp` — zero dependencies, cross-platform
- `std::expected<void, ParseError>` error model (C++23)
- Type-safe `Option<T>`, `Flag`, `Positional<T>` handles with `[[nodiscard]]`
- Fluent builder API using C++23 *deducing-this* (no CRTP)
- Built-in type support: `bool`, all integrals, `float`/`double`/`long double`, `std::string`, `std::filesystem::path`, `std::vector<T>`
- Constraints: `.range(lo, hi)`, `.choices({…})`, `.validate(fn, desc)`
- Per-option environment-variable fallback via `.env("VAR")`
- Short-flag grouping: `-vxf` ≡ `-v -x -f`
- Inline value parsing: `-n42` ≡ `-n 42`, `--out=file` ≡ `--out file`
- End-of-options marker `--`
- Recursive subcommands with independent option sets
- Auto-aligned, ANSI-colored, section-structured help output
- Built-in `-h`/`--help` and `-V`/`--version` flags
- Levenshtein "did you mean?" suggestions for unknown options (threshold ≤ 3)
- Bash and Fish shell completion generators
- CMake interface target `cli::cli` with install / `find_package` support
- Three example programs: `basic`, `subcommands`, `completions`
- Full doctest test suite with 30+ test cases
