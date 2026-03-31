# Changelog

All notable changes to this project will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0]

### Added
- **`parse({...})` initializer-list overload** — parse without `argc`/`argv`; ideal for testing
- **Negative number positionals** — tokens like `-1`, `-3.14`, `-42` are now correctly treated as positional values when no matching short option is registered
- **`Lookup::as_count()`** — dedicated accessor for Count option values; `as<unsigned>()` also works transparently for Count options
- **`App::reset()`** — clears all parsed state so an App can be safely reused across multiple `parse()` calls
- **`[count]` annotation in help** — Count options show `[count]` in the choices column to signal they are repeatable
- **Blank lines between named help sections** — visually separates option groups in the OPTIONS block
- **Fish completion: subcommand completions** — `fish_completion()` now emits completions for all subcommands and their aliases
- **Fish completion: single-quote escaping** — help text with apostrophes no longer breaks generated fish scripts
- **Negatable suggestion** — `suggest()` now includes `--no-X` candidates so typos like `--no-colr` suggest `--no-color`
- **`parse_or_exit()` nested subcommand dispatch** — fires the callback of the deepest active subcommand rather than stopping at the first level
- **`OptionBase::reset()` / `PositionalBase::reset()` virtual methods** — enables clean reuse via `App::reset()`

### Fixed
- `Lookup::as<unsigned>()` for Count options now correctly returns the accumulated count instead of `0`
- `Lookup::as_or<T>()` for Count options now returns the fallback when the option is absent
- Fish completion no longer emits `[count]` as a choice list for Count options

### Changed
- `[OPTIONS]` is always shown in the USAGE line (previously behind a dead `|| true` guard)
- README Quick Start updated to v2 API (`add_flag`, `add<T>`, etc.)
- All nodiscard warnings in test suite resolved with explicit `(void)` casts

## [1.1.0]

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
