# Changelog

All notable changes to this project will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] — 2026-03-31

### Added
- **Count options** (`add_count(name)`) — counts flag occurrences; `-vvv` or `-v -v -v` → 3
- **`.transform(fn)`** — post-parse value transformation on `Option<T>` and `Positional<T>`
- **`mutex_group({...})`** — declares mutually exclusive options; error if more than one is set
- **`require_together({...})`** — declares co-required options; error if only some are set
- **Subcommand aliases** (`.alias(name)`) — e.g. `subcommand("commit").alias("ci")`
- **Negatable flags** (`.negatable()`) — auto-accepts `--no-FLAG` to set flag to false; shown as `--[no-]flag` in help
- **Named option sections** (`.section(name)`) — groups options under sub-headers in help output
- **Zsh completion** (`zsh_completion(ostream)`) — generates `#compdef`-style completion function
- **`has_callback()` / `fire_callback()`** — public API to inspect and invoke subcommand callbacks
- **`ParseError::Kind::MutuallyExclusive`** and **`ParseError::Kind::RequiresOption`** error kinds
- Bash completion now includes `--no-*` entries for negatable flags and subcommand aliases

### Fixed
- `has_flag()` now correctly returns false for negatable flags set to false via `--no-FLAG`
- `has_option()` now correctly returns true when an option has a default value applied

## [1.1.0] — 2026-03-31

### Added
- **`Count` option type** (`add_count(name)`) — counts flag occurrences; `-vvv` or `-v -v -v` → 3
- **`.transform(fn)`** — post-parse value transformation on `Option<T>` and `Positional<T>`
- **`mutex_group({…})`** — declares mutually exclusive options; error if more than one is set
- **`require_together({…})`** — declares co-required options; error if only some are set
- **Subcommand aliases** (`.alias(name)`) — e.g. `subcommand("commit").alias("ci")`
- **Negatable flags** (`.negatable()`) — auto-accepts `--no-FLAG` to set the flag to false; displayed as `--[no-]flag` in help
- **Named option sections** (`.section(name)`) — groups options under sub-headers in the help OPTIONS block
- **Zsh completion** (`zsh_completion(ostream)`) — generates `#compdef`-style completion function
- **`has_callback()` / `fire_callback()`** — public API to inspect and invoke subcommand callbacks without accessing private members
- **`ParseError::Kind::MutuallyExclusive`** and **`ParseError::Kind::RequiresOption`** error kinds with named constructors
- `OptionBase::has_value()` virtual method; `Lookup::operator bool()` now reflects presence of a value (from CLI or default) rather than explicit-set state
- Bash completion now includes `--no-*` entries for negatable flags and subcommand aliases
- `completions` example updated with Zsh support

### Fixed
- `has_option(name)` now correctly returns `true` when the option has a default value, matching its documented behavior

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
