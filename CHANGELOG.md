# Changelog

All notable changes to this project will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Variable-arity options** — `.nargs(N)`, `.nargs(lo, hi)`, and
  `.nargs_at_least(N)` let one vector-option occurrence consume several
  tokens; exact arities keep dash-prefixed values, ranged arities stop at the
  next option
- **Variable-arity positionals** — the same `.nargs(N)`, `.nargs(lo, hi)`,
  and `.nargs_at_least(N)` controls on vector positionals; fixed arities keep
  following positional slots reachable, while variable-arity positionals must
  still be registered last
- **Inherited globals in help and completion** — `.global()` options are now
  listed in descendant help and Bash/Fish/Zsh completion, and a global custom
  `-h`/`--help`/`-V`/`--version` correctly suppresses the corresponding
  built-in rows; shadowing is name-by-name, so a local `-v` hides only the
  inherited `-v` while `--verbose` remains available
- **INI config files** — `config_file(path, required)` and `config_option()`
  load root and `[command.path]` sections, quoted values, arrays, and
  negatable flags; section headers may be quoted or use TOML `[[section]]`
  syntax, `[default]` addresses the root command, quoted keys and UTF-8 BOMs
  are accepted, and bare flags may carry inline comments; precedence is
  CLI > environment > config > default
- **Response files** — `response_files()` expands `@file` with shell-like
  quoting, comments, nested includes (8 levels), `@@` escaping, UTF-8 BOM
  tolerance, a 4 MiB size cap, and dedicated `ResponseFileError` diagnostics
- **Option aliases** — `.alias("--colour")` on options, flags, and counts
- **Long-option abbreviation** — opt-in `allow_abbrev()` with unique-prefix
  resolution and `AmbiguousOption` errors; the root setting is inherited by
  subcommands, and inherited global options participate in resolution
- **Global options** — `.global()` makes an option usable before or after
  any descendant subcommand, including required and negatable options
- **Extra-argument collection** — `allow_extras()` plus `remaining_args()`
  for wrapper/forwarding tools; unknown options and surplus positionals are
  preserved instead of failing, extras before a verb do not block subcommand
  dispatch, unknown short-group suffixes are forwarded intact, and extras
  accepted inside a subcommand are inherited through the root policy and
  aggregated onto the root in command-line order
- **Quoted delimiter elements** — `.delimiter(...)` values such as
  `a,"b,c",d` keep delimiters inside quoted elements
- **Single-letter booleans** — `t`/`f` and `y`/`n` are accepted alongside
  the existing boolean spellings
- **Hidden subcommands** — `.hidden()` verbs remain dispatchable but are
  excluded from help and Bash/Fish/Zsh completion output
- **Unknown-command diagnostics** — misspelled commands return
  `UnknownCommand` with Levenshtein suggestions, including aliases
- **`require_one_of({...})`** — at-least-one-of option groups
- **`.group()`** as a vocabulary alias for `.section()`
- **`parse(std::vector<std::string>)`** convenience overload
- **Throughput benchmark target** (`CLI_BUILD_BENCHMARKS=ON`) covering flags,
  typed options, mixed schemas, and subcommands
- **Comparative benchmark target** (`CLI_BUILD_COMPARATIVE_BENCHMARKS=ON`)
  measuring the same workloads head-to-head against CLI11 and cxxopts
- **`config` example** demonstrating config files, response files, nargs, and
  extras
- **Custom value types via ADL** — `cli_parse(std::string_view)` and
  `cli_format(const T&)` in the type's namespace; a
  `cli_parse(std::string_view, std::type_identity<T>)` overload is also
  supported for templates and overload sets
- **`help_text()`** — render help into a `std::string` for tests, logging, or
  custom output; **`print_help(std::ostream&)`** writes to any stream
- **Positional parity with options** — `.range()`, `.choices()`,
  `.validate()`, `.transform()`, `.env()`, and `.delimiter()` now work on
  positionals; vector positionals split delimited values
- **`.delimiter(char)` for vector options** — `--tag=a,b,c`; also used when
  splitting vector environment variables
- **`.required()` on `Flag` and `Count`**
- **Short inline values with `=`** — `-n=5` is accepted
- **Explicit boolean values** — `--flag=false` and `--no-flag` both work
- **Inherited versions** — subcommands use the root app's version, so
  `tool sub --version` works
- **Default validation** — defaults are checked against `range()`,
  `choices()`, and custom validators
- **Domain-scoped test suite** — 555 test cases across 19 doctest suites
  (`api`, `completion`, `constraints`, `custom_types`, `environment`,
  `errors`, `extensions`, `flags`, `groups`, `help`, `lifecycle`, `lookup`,
  `options`, `positionals`, `robustness`, `subcommands`, `tokens`,
  `upstream_edge_cases`, `values`), each registered as its own CTest entry
  and runnable via `--test-suite=NAME`
- **Upstream-derived regression cases** — attributed tests adapted from
  CLI11 (BSD-3-Clause), cxxopts (MIT), and argparse (MIT) cover `--`
  handling, negative-number grammar, arbitrary option names, values that
  look like options, empty tokens, subcommand extras, and config sections
- **Process-level `parse_or_exit()` tests** — separate driver verifies help,
  version, error exit codes, nested help/version scoping, and callback
  dispatch without terminating the unit-test process
- **Adversarial robustness suite** — deterministic random token streams,
  repeated-parse reset checks, very long unknown options, malformed `argv`,
  48-level nested commands, and a 256-option flat schema
- **libFuzzer harness** (`CLI_BUILD_FUZZERS=ON`) — NUL-separated argument
  streams exercise options, positionals, counts, constraints, groups,
  inherited globals, config files, response files, fixed-arity positionals,
  subcommands, help/version control flow, duplicate-parse determinism, and
  direct child parsing; a checked-in seed corpus is used by CI
- **CI matrix** — GCC/Clang/macOS/Windows builds, ASan+UBSan, strict
  warnings with `-fno-rtti`, a 200k-run fuzz smoke test, and an installed
  `find_package` consumer build

### Fixed
- `App::reset()` now clears the loaded config path and data, so a later parse
  cannot reuse a previous parse's configuration
- `config_option()` registers `--config` as a global option, so
  `tool build --config file.ini` works for subcommands
- Response-file expansion honors the root `response_files()` policy even when
  a subcommand is parsed directly, preserves backslashes before ordinary
  characters (Windows paths and regex literals), and caps expansion at one
  million tokens
- Parsing a subcommand directly now resets the whole invocation tree, applies
  root config and response-file policy, finalizes every ancestor's inherited
  globals, records the active command chain, and aggregates extras on
  ancestors — matching `root.parse({"sub", ...})`
- Custom-type support no longer fails to compile due to unqualified ADL
  `cli_parse` / `cli_format` requires-expressions
- Integer parsing now accepts signs and `0x` / `0b` prefixes, handles
  `INT_MIN`, rejects negative values for unsigned types, and reports overflow
- Float parsing accepts a leading `+`
- Negative-number detection now matches a complete decimal, hexadecimal, or
  binary literal, so near-misses such as `-1abc`, `-0xZZ`, `-1e`, and
  `-1.2.3` are reported as unknown options instead of being passed as
  positionals
- The `--` end-of-options marker is no longer accepted as a would-be option
  value (`--file --` reports a missing value); an inline spelling such as
  `--file=--` still supplies the literal marker
- A dash followed by whitespace (`- text`) is treated as a positional value
  rather than an option-looking token
- Subcommand parse errors (including `--help` / `--version`) are returned
  directly instead of being masked by parent finalization
- Subcommands are only dispatched before the first positional token, removing
  positional/subcommand ambiguity
- An exact `--no-X` option now wins over interpreting `--no-X` as a negated
  `--X` flag
- Built-in `-h`/`--help` and `-V`/`--version` step aside per spelling when a
  user option occupies that name
- Help rendering: named and unnamed sections are separated correctly, string
  defaults/choices are no longer quoted, and positional rows align
- Completions: no duplicate built-ins, inherited versions, `--no-` variants,
  valid zsh `:META:ACTION` specs and choice lists, and `-r` for fish options
  that take values
- `Lookup` no longer depends on RTTI (`-fno-rtti` builds work)
- Option typo suggestions now compute Levenshtein distance correctly; the
  previous one-row implementation under-counted edits and could pick the wrong
  candidate or miss a valid one
- Numeric parsing rejects leading/trailing whitespace consistently instead of
  depending on the standard library's floating-point `from_chars`
  implementation
- Optional variadic positionals render as `[name...]` in both `USAGE` and
  `ARGUMENTS`, vector defaults no longer render as `[default: [a, b]]`, and
  vector constraint errors identify the offending element
- Mutex groups now treat environment-provided values as explicit choices,
  matching `require_together()` semantics; defaults still do not participate
- `print_error()` writes through `std::cerr`, so callers can redirect or
  capture diagnostics with `rdbuf()`
- `parse_or_exit()` now falls back to the nearest ancestor command with a
  callback, rather than skipping intermediate commands and jumping straight
  to the root
- `parse(argc, argv)` tolerates a null `argv` and null argument entries
  instead of constructing an invalid span or `std::string_view`
- Unknown-option suggestions skip candidates whose length differs by more
  than the edit threshold, avoiding pathological work for huge tokens
- Shell completion generation now produces valid function identifiers and
  quotes unusual command/option names in Bash, Fish, and Zsh output
- Unknown-option suggestions include inherited `.global()` options, so a typo
  after a subcommand can point at a parent-defined flag
- Subcommand lookup (`operator[]`, `has_option()`, `has_flag()`) resolves
  inherited global options while still hiding ordinary parent options
- Defaults and fallbacks accept explicitly convertible arguments (for example
  `std::string_view` for `std::string`/`std::filesystem::path`), vector
  defaults accept initializer lists, and `nullptr_t` is rejected at compile
  time instead of selecting a string overload
- Range checks reject NaN and compare bounds with `<` only, so custom ordered
  types need no `operator>` or `operator<=>`
- Option and positional handles expose mutable `T&` / `T*` access after parsing
- Vector defaults and choice lists join empty elements with their separators
  (`[default: , two]`, `[|auto]`) instead of silently dropping them

### Changed
- Whole-value validators and transforms run before mutex/require group checks,
  so an invalid value is reported ahead of a structural group conflict
- `parse(argc, argv)` no longer copies the token list into a temporary vector;
  subcommand recursion parses the original pointer span
- One-character short options are resolved through a direct 128-slot ASCII
  index, bypassing the hash map on the token hot path
- An unknown bare word now reports `UnknownCommand` (with suggestions) instead
  of `TooManyPositionals` when no positional slot consumed it; genuine surplus
  positionals still report `TooManyPositionals`
- `parse()` and `parse_or_exit()` accept `const char* const*`, so mutable,
  const, and mixed `argv` arrays all work without overload ambiguity
- Per-parse version lookup and subcommand lookup are cached/skipped when
  unused, reducing hot-loop work without changing semantics
- All `parse()` overloads reset prior state automatically
- `App` is intentionally non-movable so parent links and
  `ParseError::context` pointers stay stable
- A lone `-` is treated as a normal positional token (stdin/stdout), while
  `--` remains the end-of-options marker
- The header no longer depends on `<print>`; `print_help()` and
  `parse_or_exit()` use `std::cout`/`std::format`, broadening standard-library
  compatibility

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
