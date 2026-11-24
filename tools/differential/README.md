# Upstream differential replays

This directory verifies `cli.hpp` against the complete parse calls exercised by
the upstream test suites of three other C++ command-line parsers:

| Library  | Tag      | Capture records | Replayable | Tagged divergences | Untagged divergences |
|----------|----------|-----------------|------------|--------------------|----------------------|
| CLI11    | `v2.7.2` | 1,575           | 1,575      | 484                | 0                    |
| argparse | `v3.2`   | 565             | 565        | 96                 | 0                    |
| cxxopts  | `v3.3.1` | 46              | 46         | 0                  | 0                    |

The counts above are from the checked-in corpora and are reproduced by the
driver.  "Untagged divergences" must be zero; the driver fails otherwise.

## What is compared

Every captured call is replayed through a generated `cli.hpp` program with the
same parser schema and the exact same argument vector.  Two checks are made:

1. **Accept/reject parity** for all suites.  A mismatch is reported as
   direction `+1` when `cli.hpp` accepts an input the upstream library rejects,
   and `-1` when `cli.hpp` rejects an input the upstream library accepts.
2. **Parsed values** for CLI11.  CLI11 captures record each option's
   `results()` and `count()`.  Successful, untagged cases compare the value
   `cli.hpp` stored for every builtin-convertible option, positional, and flag.
   A parsed-value mismatch is reported as direction `+2`.

Value comparison derives the expected value from the upstream raw result:
strings are compared as strings, integral results through `strtoll`/`strtoull`
with base 0, floating results through `strtold`, chars through CLI11's
single-character-or-integer rule, and flags through `as<bool>()` after applying
CLI11's `to_flag_value` rules.  Floating comparisons treat NaN equal to NaN.

At the time of writing, CLI11 value comparison covers **215 of 269 untagged
successful cases and 296 parsed fields**; no field was skipped for lack of a
builtin mapping, and the remaining successful cases have no parsed option
values to compare.  The generator prints the live coverage and skip count on
every run.  If an upstream option is not builtin-convertible, or its result
count does not match the translated shape, its value is not compared and the
skip is counted; the case is still checked for accept/reject parity.

Error messages, help text, exit codes, and side effects are not compared.

## Usage

```sh
# Replay one suite
python3 tools/differential/run.py --suite cli11 --cxx clang++

# Replay all corpora (default)
python3 tools/differential/run.py --cxx clang++

# Also fail on every tagged divergence (expected red while tags remain)
python3 tools/differential/run.py --cxx clang++ --strict

# Add optimisation / sanitizer flags, or point at another checkout
python3 tools/differential/run.py --suite cli11 --cxx clang++ \
    --cxxflags '-O2 -fsanitize=address,undefined' \
    --include /path/to/include
```

Generated sources, binaries, and result TSVs go to a temporary directory unless
`--out-dir` is supplied.  The CMake test writes them under
`build/tests/differential` when enabled:

```sh
cmake -S . -B build -DCLI_BUILD_DIFFERENTIAL_TESTS=ON
cmake --build build -j
ctest --test-dir build -R differential_upstream --output-on-failure
```

The CMake registration is opt-in (`CLI_BUILD_DIFFERENTIAL_TESTS=OFF` by
default) so normal local builds stay fast and offline.  CI enables it in a
dedicated job.  It requires Python 3 and a GCC/Clang-style compiler; it is
intentionally unavailable under MSVC.

Each generator can also be run directly:

```sh
python3 tools/differential/generate_cli11.py \
    tools/differential/corpus/cli11_all.jsonl.gz /tmp/cli11_replay.cpp
```

## How mismatches are classified

The upstream suites exercise features that `cli.hpp` deliberately does not
implement (config files, help rendering, custom converters, and so on).  The
generators tag a case when its schema or arguments use any such feature.  Tags
are attached to every case that uses the feature, even when that particular
input happens to match.

The contract enforced by `run.py` is:

- no hard-blocked capture records,
- no untagged accept/reject mismatch,
- no untagged parsed-value mismatch.

A tagged mismatch means "this case uses semantics outside the supported subset",
not "this is a known bug".  A tag is never inferred from a mismatch result; it is
computed from the captured schema and arguments before playback.  Tagged cases
are still replayed, so filtering cannot hide a crash.

## Tag glossary

### CLI11

| Tag | Meaning |
|-----|---------|
| `app_ignore_case`, `app_ignore_underscore` | App-wide case/underscore-insensitive name matching. |
| `app_needs_excludes` | `needs()` / `excludes()` relations at app or option level. |
| `app_required` | App-level required option/subcommand. |
| `app_startup_state` | `disabled`, `disabled_by_default`, or `enabled_by_default`. |
| `bool_numeric_conversion` | Numeric spelling of a boolean flag value. |
| `callback_runtime` | Callback raised a runtime error during parsing. |
| `callbacks` | User callbacks on options or subcommands. |
| `choices_canonical` | Case/underscore/space-insensitive choice set that rewrites the stored value to its canonical member. |
| `choices_transform` | Choice sets with value transform chains. |
| `clamp_validator` | Clamp/bounded validators. |
| `complex_proxy` | `std::complex` represented through a string proxy. |
| `composite_validator` | `AND` / `OR` / `NOT` / chained validators. |
| `config` | Config-file loading or config keys. |
| `custom_help`, `custom_version` | Renamed help or version flags. |
| `custom_transform_validator` | Validator that transforms the value. |
| `custom_type_converter`, `unknown_type` | User-defined or unclassified bound type. |
| `custom_validator` | Opaque validator description. |
| `delimiter_edge` | Delimiter adjacent to the start/end of a token. |
| `deprecated_type`, `retired_option` | Deprecated/retired option types. |
| `dot_notation` | Dot-separated option paths. |
| `duplicate_subcommand` | The same subcommand is entered more than once. |
| `empty_argument` | An empty argument token. |
| `enum_type` | Enum-typed option. |
| `env_only_unnamed` | Synthetic unnamed option whose value comes from the environment. |
| `extras_mode` | `allow_extras` mode other than `Error` or `Capture`. |
| `fallthrough`, `subcommand_fallthrough_off` | Parent/child fallthrough parsing. |
| `flag_like_option` | A value option translated as a flag because it takes zero tokens. |
| `flag_negation` | Negated `--no-foo` flag names. |
| `flag_override` | `--flag=true/false` override spelling. |
| `help_all` | `--help-all` handling. |
| `immediate_callback` | Parse-time immediate callbacks. |
| `multi_policy`, `multi_policy_throw` | Non-default `MultiOptionPolicy`, including repeated scalars that throw. |
| `multi_subcommand` | More than one subcommand parsed in one invocation. |
| `named_positional` | Positional that also has option names. |
| `nameless_subcommand` | Subcommand without a usable name. |
| `needs_excludes` | Option `needs()` / `excludes()` relations. |
| `nested_vector_container` | Nested vector/array container type. |
| `nested_vector_escape`, `vector_escape_syntax` | `[[...]]` / `[...]` vector escape spellings. |
| `no_help` | Help flag disabled. |
| `numeric_lexical_extension` | Hex, binary, or leading-zero octal integer literals. |
| `numeric_validator` | Numeric predicate/range applied to a non-arithmetic type. |
| `option_group`, `option_group_alias` | `Option_group` construction and aliases. |
| `option_ignore_case`, `option_ignore_underscore` | Per-option name matching. |
| `optional_values` | Option may take no value. |
| `path_validator` | Existing file/directory/path validator. |
| `plusplus` | `++` token. |
| `positional_priority` | Hungry option competing with a required positional. |
| `positional_subcommand_mix` | Positionals and subcommands on the same app. |
| `positionals_at_end` | `positionals_at_end()` behaviour. |
| `prefix_command` | Prefix-command parsing. |
| `require_option`, `require_subcommand_range` | Required option counts / subcommand ranges. |
| `subcommand_prefix` | Subcommand prefix matching. |
| `terminator` | `--` end-of-options terminator. |
| `tuple_proxy` | Tuple/pair represented through a string proxy. |
| `unit_suffix` | Byte-size / duration unit suffixes. |
| `untyped_option`, `untyped_positional`, `untyped_type` | Untyped value bound as text. |
| `user_callback` | User-supplied option callback. |
| `variable_positional_merged` | Generator merged positionals after a variadic one for `positionals_at_end` schemas. |
| `variable_positional_order` | More than one variadic positional. |
| `vector_default` | Vector positional with a default. |
| `version_short_alias`, `version_used` | Version flag aliases and use. |
| `windows_style` | Windows-style `/option` parsing. |

### argparse

| Tag | Meaning |
|-----|---------|
| `accepts_optional_like` | Optional-like positional arguments. |
| `builtin_help`, `builtin_version` | Stock help/version handling. |
| `compound_short_option` | Clustered short options. |
| `container_deque`, `container_list`, `container_set` | Non-vector containers. |
| `custom_assign`, `custom_prefix` | Non-default assignment or prefix characters. |
| `flag_equals`, `option_name_equals` | `--flag=true` / `name=value` name spellings. |
| `hidden_argument` | Hidden argument. |
| `implicit_false_flag` | Flag whose implicit value is `false`. |
| `implicit_non_bool` | Non-boolean implicit value. |
| `negative_literal_lexical`, `option_like_value_extension` | Negative or option-looking values. |
| `numeric_plus_sign` | Explicit `+` numeric spelling. |
| `optional_type`, `optional_value` | `std::optional` binding or optional value. |
| `path_type` | `std::filesystem::path` binding. |
| `positional_interleaved` | Options interleaved with variadic positionals. |
| `repeatable_scalar` | Repeatable scalar argument. |
| `scan_format_<c>` | `scanf`-style conversion beyond `i`/`d`/`u` (for example `x`, `o`, `e`, `f`, `g`, `a`). |
| `split_trailing_positional` | Generator split a merged trailing positional. |
| `suppressed_help` | Help suppressed. |
| `tuple_proxy` | Tuple/pair binding. |
| `unknown_container_element`, `unknown_type` | Unsupported bound type. |
| `unsupported_choices` | Choices on a type the replay cannot constrain. |
| `user_callback` | User action callback. |
| `zero_min_range`, `zero_or_more_option` | Zero-minimum ranges / zero-or-more option consumption. |

### cxxopts

| Tag | Meaning |
|-----|---------|
| `allow_unrecognised` | Unrecognised arguments allowed. |
| `implicit_value` | Non-boolean implicit value. |
| `invalid_positional`, `named_positional` | Positional binding problems. |
| `optional_type`, `path_type`, `tuple_proxy` | Proxy or special bound types. |
| `unknown_type`, `unknown_vector_element` | Unsupported bound type. |
| `untyped_option`, `vector_proxy` | Untyped or vector-proxy binding. |
| `unmatched_positionals` | Positional arguments without a declared positional. |

## Corpora and provenance

The corpora are gzip-compressed JSONL runtime captures of the upstream test
suites.  They were produced by instrumenting the exact upstream tags, running
each complete test binary, and recording every public parse invocation with its
schema, arguments, and result.  Capture patches, commit hashes, rebuild
commands, environment variables, and license notices are in
[`capture/README.md`](capture/README.md).

The corpus files are checked in so the differential test is offline and
reproducible.  Generated replay sources are not checked in.

## Known limitations

- Only CLI11 captures parsed values.  argparse and cxxopts are checked for
  accept/reject parity only.
- Values are compared only for cases with no tags, a successful upstream
  outcome, builtin-convertible element types, and a translated result shape.
- Tags are feature-based.  They identify semantics outside the supported
  subset; a tagged case is not proof of a bug, and a matching tagged case is
  not proof of support.
- Capture is runtime-based: a test binary that aborts early can hide later
  parse calls in that binary.
- The corpus reflects one platform/toolchain (macOS, Apple libc++/clang at
  capture time).  Standard-library-dependent conversions such as `strtold`
  spelling can differ on other platforms.
- MSVC is not covered by the differential CTest registration.
- Help text, error messages, exit codes, and filesystem side effects are out of
  scope.

## License

The capture patches and corpora are derivative works of CLI11 (BSD-3-Clause),
argparse (MIT), and cxxopts (MIT).  See `capture/README.md` for the full
notices.
