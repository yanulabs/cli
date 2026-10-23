#pragma once

// ════════════════════════════════════════════════════════════════════════════
//  cli.hpp  ·  A modern C++23 command-line argument parser
//  ─────────────────────────────────────────────────────────────────────────
//  FEATURES
//  ────────
//  · Single-header, zero dependencies, cross-platform
//  · std::expected<void, ParseError> — composable, propagating errors
//  · Type-safe Option<T>, Flag, Count, Positional<T> handles
//  · Fluent builder API via C++23 deducing-this (no CRTP boilerplate)
//  · Types out-of-the-box: bool · integrals · floats · string · path · vector
//  · Custom types via ADL: cli_parse( std::string_view ) & cli_format( const T& )
//  · Constraints: .range() · .choices() · .validate(fn, desc) on Options & Positionals
//  · Multi-value delimiter splitting: .delimiter(',') for --items=a,b,c
//  · Fixed/range/at-least arity: .nargs(2) · .nargs(1,3) · .nargs_at_least(1)
//  · Mutually exclusive groups:  mutex_group({"--verbose","--quiet"})
//  · Required-together groups:   require_together({"--output","--format"})
//  · Require-at-least-one groups: require_one_of({"--json","--yaml"})
//  · Per-option & positional env-var fallback via .env("VAR")
//  · INI config files + @response files with CLI/env/config precedence
//  · Post-parse value transforms via .transform(fn)
//  · Short-flag grouping   -vxf  ≡  -v -x -f  (including -h / -V)
//  · Count options:        -vvv  ≡  -v -v -v  (increments per occurrence)
//  · Inline short value    -n42  ≡  -n 42
//  · Inline long value   --out=file  ≡  --out file  and  --flag=false
//  · Long-option abbreviation (.allow_abbrev) and option aliases (.alias)
//  · Unknown-option / unknown-command "did you mean?" suggestions
//  · Standard '-' token treated as positional (stdin/stdout)
//  · End-of-options marker  --
//  · Subcommands with independent option sets, aliases, hidden verbs, recursion
//  · Negatable flags: add_flag("--color").default_val(true).negatable()
//  · Named option sections in help: .section("Connection Options")
//  · Auto-aligned, ANSI-colored, section-structured help output
//  · Bash / Fish / Zsh shell completion generators
//  · allow_extras() + remaining_args() for wrapper/forwarding tools
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace cli {

class App;

// ────────────────────────────────────────────────────────────────────────────
// §1  ParseError — structured, descriptive error / control-flow type
// ────────────────────────────────────────────────────────────────────────────

struct ParseError {
    enum class Kind : uint8_t {
        HelpRequested,
        VersionRequested,
        UnknownOption,
        MissingValue,
        UnexpectedValue,
        InvalidValue,
        MissingRequired,
        MissingSubcommand,
        TooManyPositionals,
        ValidationFailed,
        OutOfRange,
        BadChoice,
        MutuallyExclusive,
        RequiresOption,
        UnknownCommand,
        ConfigError,
        AmbiguousOption,
        ResponseFileError,
    };

    Kind        kind;
    std::string message;
    const App*  context = nullptr; // Active App/subcommand where help/version/error occurred

    [[nodiscard]] constexpr bool is_exit_request() const noexcept {
        return kind == Kind::HelpRequested || kind == Kind::VersionRequested;
    }

    static ParseError help_requested(const App* app) {
        return { Kind::HelpRequested, {}, app };
    }
    static ParseError version_requested(const App* app) {
        return { Kind::VersionRequested, {}, app };
    }
    static ParseError unknown(std::string_view opt, std::string_view sug = {}, const App* app = nullptr) {
        if (sug.empty())
            return { Kind::UnknownOption, std::format("unknown option '{}'", opt), app };
        return { Kind::UnknownOption,
                 std::format("unknown option '{}' — did you mean '{}'?", opt, sug), app };
    }
    static ParseError missing_value(std::string_view opt, const App* app = nullptr) {
        return { Kind::MissingValue,
                 std::format("option '{}' requires a value", opt), app };
    }
    static ParseError unexpected_value(std::string_view opt, std::string_view val, const App* app = nullptr) {
        return { Kind::UnexpectedValue,
                 std::format("option '{}' does not take a value (got '{}')", opt, val), app };
    }
    static ParseError invalid(std::string_view opt, std::string_view val,
                              std::string_view why, const App* app = nullptr) {
        return { Kind::InvalidValue,
                 std::format("invalid value '{}' for '{}': {}", val, opt, why), app };
    }
    static ParseError missing_required(std::string_view name, const App* app = nullptr) {
        return { Kind::MissingRequired,
                 std::format("required option '{}' was not provided", name), app };
    }
    static ParseError missing_positional(std::string_view name, const App* app = nullptr) {
        return { Kind::MissingRequired,
                 std::format("required argument <{}> was not provided", name), app };
    }
    static ParseError missing_subcommand(std::string_view app_name, const App* app = nullptr) {
        return { Kind::MissingSubcommand,
                 std::format("a command is required for '{}'", app_name), app };
    }
    static ParseError missing_one_of(std::string_view names, const App* app = nullptr) {
        return { Kind::MissingRequired,
                 std::format("one of {} must be provided", names), app };
    }
    static ParseError out_of_range(std::string_view opt, std::string_view val,
                                   std::string_view lo, std::string_view hi, const App* app = nullptr) {
        return { Kind::OutOfRange,
                 std::format("value '{}' for '{}' is out of range [{}, {}]",
                             val, opt, lo, hi), app };
    }
    static ParseError bad_choice(std::string_view opt, std::string_view val,
                                 std::string_view choices, const App* app = nullptr) {
        return { Kind::BadChoice,
                 std::format("invalid choice '{}' for '{}' — valid: {}",
                             val, opt, choices), app };
    }
    static ParseError validation(std::string_view opt, std::string_view desc, const App* app = nullptr) {
        return { Kind::ValidationFailed,
                 std::format("validation failed for '{}': {}", opt, desc), app };
    }
    static ParseError too_many(std::string_view arg, const App* app = nullptr) {
        return { Kind::TooManyPositionals,
                 std::format("unexpected argument '{}'", arg), app };
    }
    static ParseError mutually_exclusive(std::string_view a, std::string_view b, const App* app = nullptr) {
        return { Kind::MutuallyExclusive,
                 std::format("'{}' and '{}' are mutually exclusive", a, b), app };
    }
    static ParseError requires_option(std::string_view opt, std::string_view req, const App* app = nullptr) {
        return { Kind::RequiresOption,
                 std::format("'{}' requires '{}' to also be provided", opt, req), app };
    }
    static ParseError unknown_command(std::string_view cmd, std::string_view sug = {},
                                      const App* app = nullptr) {
        if (sug.empty())
            return { Kind::UnknownCommand,
                     std::format("unknown command '{}'", cmd), app };
        return { Kind::UnknownCommand,
                 std::format("unknown command '{}' — did you mean '{}'?", cmd, sug), app };
    }
    static ParseError config(std::string_view path, std::string_view why,
                             const App* app = nullptr) {
        return { Kind::ConfigError,
                 std::format("config file '{}': {}", path, why), app };
    }
    static ParseError ambiguous(std::string_view opt, std::string_view matches,
                                const App* app = nullptr) {
        return { Kind::AmbiguousOption,
                 std::format("option '{}' is ambiguous — matches: {}", opt, matches), app };
    }
    static ParseError response_file(std::string_view path, std::string_view why,
                                    const App* app = nullptr) {
        return { Kind::ResponseFileError,
                 std::format("response file '{}': {}", path, why), app };
    }
};

using ParseResult = std::expected<void, ParseError>;

// ────────────────────────────────────────────────────────────────────────────
// §2  Internal helpers — ANSI, Levenshtein, type traits, ADL hooks, from_string
// ────────────────────────────────────────────────────────────────────────────

namespace detail {

[[nodiscard]] inline bool is_tty(int fd) noexcept {
#ifdef _WIN32
    return ::_isatty(fd) != 0;
#else
    return ::isatty(fd) != 0;
#endif
}

inline bool color_enabled() noexcept {
    static const bool v = is_tty(1) && (std::getenv("NO_COLOR") == nullptr);
    return v;
}

struct C {
    static std::string_view rst()  noexcept { return color_enabled() ? "\033[0m"    : ""; }
    static std::string_view bld()  noexcept { return color_enabled() ? "\033[1m"    : ""; }
    static std::string_view dim()  noexcept { return color_enabled() ? "\033[2m"    : ""; }
    static std::string_view cyn()  noexcept { return color_enabled() ? "\033[36m"   : ""; }
    static std::string_view yel()  noexcept { return color_enabled() ? "\033[33m"   : ""; }
    static std::string_view grn()  noexcept { return color_enabled() ? "\033[32m"   : ""; }
    static std::string_view red()  noexcept { return color_enabled() ? "\033[31m"   : ""; }
    static std::string_view bcyn() noexcept { return color_enabled() ? "\033[1;36m" : ""; }
    static std::string_view byel() noexcept { return color_enabled() ? "\033[1;33m" : ""; }
    static std::string_view bred() noexcept { return color_enabled() ? "\033[1;31m" : ""; }
    static std::string_view bgrn() noexcept { return color_enabled() ? "\033[1;32m" : ""; }
};

inline size_t edit_distance(std::string_view a, std::string_view b) noexcept {
    const size_t m = a.size(), n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<size_t> row(n + 1);
    std::iota(row.begin(), row.end(), size_t{0});
    for (size_t i = 1; i <= m; ++i) {
        size_t previous_diagonal = row[0]; // old row[i - 1]
        row[0] = i;
        for (size_t j = 1; j <= n; ++j) {
            const size_t above = row[j]; // old row[j]
            row[j] = (a[i - 1] == b[j - 1])
                ? previous_diagonal
                : 1 + std::min({row[j], row[j - 1], previous_diagonal});
            previous_diagonal = above;
        }
    }
    return row[n];
}

inline bool has_outer_whitespace(std::string_view sv) noexcept {
    return !sv.empty()
        && (std::isspace(static_cast<unsigned char>(sv.front())) != 0 ||
            std::isspace(static_cast<unsigned char>(sv.back())) != 0);
}

inline bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// ── Negative number literal detector (-42, -3.14, -.5, -1e-3) ──────────────

inline bool looks_like_negative_number(std::string_view sv) noexcept {
    if (sv.size() < 2 || sv[0] != '-') return false;
    const auto c1 = static_cast<unsigned char>(sv[1]);
    if (std::isdigit(c1)) return true;
    if (sv[1] == '.' && sv.size() >= 3 && std::isdigit(static_cast<unsigned char>(sv[2])))
        return true;
    return false;
}

// ── Type traits & ADL extensibility concepts ───────────────────────────────

template<typename T> struct is_vec                  : std::false_type {};
template<typename T> struct is_vec<std::vector<T>>  : std::true_type  {};
template<typename T> inline constexpr bool is_vec_v = is_vec<T>::value;

template<typename T> struct elem_of                 { using type = T; };
template<typename T> struct elem_of<std::vector<T>> { using type = T; };
template<typename T> using  elem_of_t = typename elem_of<T>::type;

// Stable, RTTI-free type token used by Lookup to recover the concrete option.
template<typename T>
inline constexpr char type_token = 0;

// ── ADL poison pills ───────────────────────────────────────────────────────
// These declarations exist only so unqualified lookup succeeds in the
// requires-expressions below when the user has not supplied a hook.  ADL can
// still find a `cli_parse` / `cli_format` declared in T's namespace, which is
// the intended customization point.
void cli_parse() = delete;
void cli_format() = delete;

// Makes a one-argument ADL hook dependent on T.  Without this, a call to
// `cli_parse(sv)` inside a requires-expression would be non-dependent and a
// missing hook would be a hard error instead of a false constraint.
template<typename T>
struct DependentStringView {
    std::string_view value;
    constexpr operator std::string_view() const noexcept { return value; }
};

template<typename T>
concept CustomParsable = requires(std::string_view sv) {
    { cli_parse(sv, std::type_identity<T>{}) } -> std::same_as<std::expected<T, std::string>>;
} || requires(DependentStringView<T> sv) {
    { cli_parse(sv) } -> std::same_as<std::expected<T, std::string>>;
};

template<typename T>
concept CustomFormattable = requires(const T& v) {
    { cli_format(v) } -> std::convertible_to<std::string>;
};

template<typename T>
consteval std::string_view type_meta() noexcept {
    using U = elem_of_t<T>;
    if constexpr (std::is_same_v<U, std::string>)                return "TEXT";
    else if constexpr (std::is_same_v<U, std::filesystem::path>) return "PATH";
    else if constexpr (std::is_same_v<U, bool>)                  return "BOOL";
    else if constexpr (std::is_floating_point_v<U>)              return "FLOAT";
    else if constexpr (std::is_unsigned_v<U>)                    return "UINT";
    else if constexpr (std::is_integral_v<U>)                    return "INT";
    else                                                         return "VALUE";
}

template<typename T>
[[nodiscard]] std::string to_display(const T& v) {
    if constexpr (is_vec_v<T>) {
        std::string out;
        bool first = true;
        for (const auto& e : v) {
            if (!first) out += ", ";
            first = false;
            out += to_display(e);
        }
        return out;
    } else if constexpr (CustomFormattable<T>)
        return std::string(cli_format(v));
    else if constexpr (std::is_same_v<T, std::string>)
        return std::string(v);
    else if constexpr (std::is_same_v<T, std::filesystem::path>)
        return v.string();
    else if constexpr (std::is_same_v<T, bool>)
        return v ? "true" : "false";
    else if constexpr (std::is_arithmetic_v<T>)
        return std::format("{}", v);
    else if constexpr (std::formattable<T, char>)
        return std::format("{}", v);
    else
        return "?";
}

template<typename T>
[[nodiscard]] auto from_string(std::string_view sv) -> std::expected<T, std::string> {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string(sv);

    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        return std::filesystem::path(sv);

    } else if constexpr (std::is_same_v<T, bool>) {
        if (ascii_iequals(sv, "true")  || sv == "1" ||
            ascii_iequals(sv, "yes")   || ascii_iequals(sv, "on"))  return true;
        if (ascii_iequals(sv, "false") || sv == "0" ||
            ascii_iequals(sv, "no")    || ascii_iequals(sv, "off")) return false;
        return std::unexpected(
            std::format("'{}' is not a valid boolean — use true/false/1/0/yes/no/on/off", sv));

    } else if constexpr (std::is_integral_v<T>) {
        if (sv.empty())
            return std::unexpected("empty string is not a valid integer");
        if (has_outer_whitespace(sv))
            return std::unexpected(std::format("'{}' is not a valid integer", sv));

        // Split the sign from the magnitude so that prefixed literals such as
        // -0xFF and +0b1010 are accepted as well as their decimal forms.
        std::string_view digits = sv;
        bool negative = false;
        if (digits.front() == '-' || digits.front() == '+') {
            negative = digits.front() == '-';
            digits.remove_prefix(1);
        }
        if (digits.empty())
            return std::unexpected(std::format("'{}' is not a valid integer", sv));

        int base = 10;
        if (digits.starts_with("0x") || digits.starts_with("0X")) {
            base = 16;
            digits.remove_prefix(2);
        } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
            base = 2;
            digits.remove_prefix(2);
        }
        if (digits.empty())
            return std::unexpected(std::format("'{}' is not a valid integer", sv));

        using U = std::make_unsigned_t<T>;
        U magnitude{};
        auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                       magnitude, base);
        if (ec == std::errc::result_out_of_range)
            return std::unexpected(std::format("'{}' overflows target integer type", sv));
        if (ec != std::errc{} || p != digits.data() + digits.size())
            return std::unexpected(std::format("'{}' is not a valid integer", sv));

        if constexpr (std::is_unsigned_v<T>) {
            if (negative)
                return std::unexpected(std::format("'{}' cannot be negative", sv));
            return static_cast<T>(magnitude);
        } else {
            using L = std::numeric_limits<T>;
            constexpr U max_pos = static_cast<U>(L::max());
            if (!negative) {
                if (magnitude > max_pos)
                    return std::unexpected(std::format("'{}' overflows target integer type", sv));
                return static_cast<T>(magnitude);
            }
            constexpr U min_magnitude = max_pos + U{1};
            if (magnitude > min_magnitude)
                return std::unexpected(std::format("'{}' overflows target integer type", sv));
            if (magnitude == min_magnitude)
                return L::min();
            return static_cast<T>(-static_cast<T>(magnitude));
        }

    } else if constexpr (std::is_floating_point_v<T>) {
        if (sv.empty())
            return std::unexpected("empty string is not a valid number");
        if (has_outer_whitespace(sv))
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        std::string_view num = sv;
        if (num.starts_with('+')) num.remove_prefix(1);
        if (num.empty())
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        T val{};
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        auto [p, ec] = std::from_chars(num.data(), num.data() + num.size(), val);
        if (ec != std::errc{} || p != num.data() + num.size())
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        return val;
#else
        try {
            size_t idx = 0;
            std::string s(num);
            if constexpr (std::is_same_v<T, float>)
                val = std::stof(s, &idx);
            else if constexpr (std::is_same_v<T, double>)
                val = std::stod(s, &idx);
            else
                val = std::stold(s, &idx);
            if (idx != s.size())
                return std::unexpected(std::format("'{}' is not a valid number", sv));
            return val;
        } catch (...) {
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        }
#endif
    } else if constexpr (requires(std::string_view raw) {
                             { cli_parse(raw, std::type_identity<T>{}) }
                                 -> std::same_as<std::expected<T, std::string>>;
                         }) {
        return cli_parse(sv, std::type_identity<T>{});
    } else if constexpr (CustomParsable<T>
                         && requires(DependentStringView<T> raw) {
                                { cli_parse(raw) }
                                    -> std::same_as<std::expected<T, std::string>>;
                            }) {
        return cli_parse(DependentStringView<T>{sv});
    } else {
        static_assert(sizeof(T) == 0,
            "Type T is not supported out-of-the-box by cli::from_string<T>. "
            "Define `std::expected<T, std::string> cli_parse(std::string_view, std::type_identity<T>)` "
            "or `cli_parse(std::string_view)` in T's namespace.");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// §2.5  Registration validation & O(1) option-name index
// ────────────────────────────────────────────────────────────────────────────

struct OptionBase;

// Transparent hash so the index can be queried with a string_view without
// allocating a temporary std::string on the hot parse path.
struct StringHash {
    using is_transparent = void;

    [[nodiscard]] size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
        return operator()(std::string_view(s));
    }
};

inline void validate_option_name(std::string_view name) {
    if (name.size() < 2 || name.front() != '-')
        throw std::invalid_argument(
            std::format("cli: option name '{}' must start with '-'", name));
    if (name == "--" || name == "-")
        throw std::invalid_argument(
            std::format("cli: '{}' is reserved and cannot be used as an option name", name));
    if (name.starts_with("---"))
        throw std::invalid_argument(
            std::format("cli: option name '{}' has too many leading dashes", name));
    if (name.find('=') != std::string_view::npos)
        throw std::invalid_argument(
            std::format("cli: option name '{}' must not contain '='", name));
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isspace(u) != 0 || std::iscntrl(u) != 0)
            throw std::invalid_argument(
                std::format("cli: option name '{}' must not contain whitespace or control characters", name));
    }
}

inline void validate_command_name(std::string_view name) {
    if (name.empty())
        throw std::invalid_argument("cli: command name must not be empty");
    if (name.front() == '-')
        throw std::invalid_argument(
            std::format("cli: command name '{}' must not start with '-'", name));
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isspace(u) != 0 || std::iscntrl(u) != 0)
            throw std::invalid_argument(
                std::format("cli: command name '{}' must not contain whitespace or control characters", name));
    }
}

inline void validate_positional_name(std::string_view name) {
    if (name.empty())
        throw std::invalid_argument("cli: positional name must not be empty");
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isspace(u) != 0 || std::iscntrl(u) != 0)
            throw std::invalid_argument(
                std::format("cli: positional name '{}' must not contain whitespace or control characters", name));
    }
}

inline void validate_env_name(std::string_view name) {
    if (name.empty())
        throw std::invalid_argument("cli: environment variable name must not be empty");
    if (name.find('=') != std::string_view::npos)
        throw std::invalid_argument(
            std::format("cli: environment variable name '{}' must not contain '='", name));
    if (name.find('\0') != std::string_view::npos)
        throw std::invalid_argument(
            "cli: environment variable name must not contain a NUL byte");
}

// Owned-key hash map: registration is cold, lookup is hot.  Copying the name
// keeps keys valid even if an option's `names` vector reallocates later.
struct OptionRegistry {
    std::unordered_map<std::string, OptionBase*, StringHash, std::equal_to<>> by_name;
    // One-character short options are by far the most common token shape.
    // A direct ASCII index avoids hashing for every `-v` / `-j` lookup.
    std::array<OptionBase*, 128> one_char{};

    [[nodiscard]] OptionBase* find(std::string_view name) const noexcept {
        if (name.size() == 2 && name[0] == '-') {
            const auto c = static_cast<unsigned char>(name[1]);
            if (c < one_char.size() && one_char[c] != nullptr)
                return one_char[c];
        }
        const auto it = by_name.find(name);
        return it == by_name.end() ? nullptr : it->second;
    }

    void ensure_available(std::string_view name) const {
        if (find(name) != nullptr)
            throw std::invalid_argument(
                std::format("cli: duplicate option name '{}'", name));
    }

    void add(OptionBase* option, std::string_view name) {
        const auto [it, inserted] = by_name.try_emplace(std::string(name), option);
        (void)it;
        if (!inserted)
            throw std::invalid_argument(
                std::format("cli: duplicate option name '{}'", name));
        if (name.size() == 2 && name[0] == '-') {
            const auto c = static_cast<unsigned char>(name[1]);
            if (c < one_char.size()) one_char[c] = option;
        }
    }

    void remove(OptionBase* option) {
        for (auto& slot : one_char)
            if (slot == option) slot = nullptr;
        for (auto it = by_name.begin(); it != by_name.end();) {
            if (it->second == option) it = by_name.erase(it);
            else ++it;
        }
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §3  OptionBase — internal polymorphic option node
// ────────────────────────────────────────────────────────────────────────────

struct OptionBase {
    std::vector<std::string> names;
    std::string              help_text;
    std::string              meta;
    std::string              env_var;
    bool                     required_  = false;
    bool                     is_flag_   = false;
    bool                     is_set_    = false;
    bool                     from_env_  = false;
    bool                     from_config_ = false;
    bool                     hidden_    = false;
    std::string              section_;
    bool                     negatable_ = false;
    char                     delimiter_ = '\0';
    // 0/0 means the classic one-value-per-occurrence behaviour.  When set,
    // nargs_min_/nargs_max_ count *tokens* consumed by one occurrence
    // (SIZE_MAX means unbounded).
    size_t                   nargs_min_ = 0;
    size_t                   nargs_max_ = 0;
    bool                     has_default_        = false;
    bool                     needs_finalize_     = false;
    OptionRegistry*          registry_   = nullptr;

    virtual ~OptionBase() = default;
    virtual ParseResult parse(std::string_view val, std::string_view opt_name, const App* app) = 0;
    virtual ParseResult parse_env(std::string_view val, const App* app) = 0;
    // Fast path for an occurrence with no explicit value (`-v`, `--flag`).
    // Count options override this to increment; bool flags avoid formatting
    // and re-parsing the literal "true" on every token.
    virtual ParseResult set_implicit(const App* app) {
        return parse("true", display_name(), app);
    }
    virtual ParseResult set_negated(const App* app) {
        return parse("false", display_name(), app);
    }
    virtual ParseResult validate_and_finalize(const App* app) = 0;
    virtual bool        has_value()        const noexcept = 0;
    virtual bool        has_default()      const noexcept = 0;
    virtual void        apply_default()                   = 0;
    virtual std::string default_str()      const          = 0;
    virtual std::string choices_str()      const          = 0;
    virtual bool        is_multi()         const noexcept = 0;
    virtual bool        is_count_option()  const noexcept { return false; }
    virtual const void* type_id()          const noexcept = 0;
    virtual void        reset() noexcept { is_set_ = false; from_env_ = false; from_config_ = false; }

    [[nodiscard]] bool matches(std::string_view t) const noexcept {
        return std::ranges::any_of(names, [t](const auto& n) { return n == t; });
    }
    [[nodiscard]] std::string_view short_name() const noexcept {
        for (const auto& n : names)
            if (n.size() >= 2 && n[0] == '-' && n[1] != '-') return n;
        return {};
    }
    [[nodiscard]] std::string_view long_name() const noexcept {
        for (const auto& n : names)
            if (n.size() >= 3 && n[0] == '-' && n[1] == '-') return n;
        return {};
    }
    [[nodiscard]] std::string_view display_name() const noexcept {
        auto ln = long_name();
        return ln.empty() ? short_name() : ln;
    }
};

inline void append_option_name(OptionBase* option, std::string_view name) {
    validate_option_name(name);
    option->registry_->ensure_available(name);
    option->names.emplace_back(name);
    option->registry_->add(option, name);
}

inline void prepend_option_name(OptionBase* option, std::string_view name) {
    validate_option_name(name);
    option->registry_->ensure_available(name);
    option->names.insert(option->names.begin(), std::string(name));
    option->registry_->add(option, name);
}

// ────────────────────────────────────────────────────────────────────────────
// §4  OptionImpl<T> — typed implementation of OptionBase
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
struct OptionImpl final : OptionBase {
    using E = elem_of_t<T>;

    std::optional<T>              value;
    std::optional<T>              default_;
    std::optional<std::pair<E,E>> range_;
    std::optional<std::vector<E>> choices_;
    std::function<bool(const T&)> validator_;
    std::string                   validator_desc;
    std::function<T(T)>           transform_;
    bool                          needs_check_ = false;

    void reset() noexcept override {
        OptionBase::reset();
        value.reset();
        needs_check_ = false;
    }

    ParseResult parse_one(std::string_view sv, std::string_view opt, const App* app) {
        if constexpr (is_vec_v<T>) {
            auto e = from_string<E>(sv);
            if (!e) return std::unexpected(ParseError::invalid(opt, sv, e.error(), app));
            if (auto r = check_one(*e, opt, sv, app); !r) return r;
            if (!value) value = T{};
            value->push_back(std::move(*e));
        } else {
            auto r = from_string<T>(sv);
            if (!r) return std::unexpected(ParseError::invalid(opt, sv, r.error(), app));
            if (auto c = check_one(*r, opt, sv, app); !c) return c;
            value = std::move(*r);
        }
        return {};
    }

    ParseResult parse(std::string_view sv, std::string_view opt, const App* app) override {
        if constexpr (is_vec_v<T>) {
            if (delimiter_ != '\0') {
                size_t start = 0;
                while (start <= sv.size()) {
                    const auto pos = sv.find(delimiter_, start);
                    const auto tok = (pos == std::string_view::npos)
                        ? sv.substr(start)
                        : sv.substr(start, pos - start);
                    if (auto r = parse_one(tok, opt, app); !r) return r;
                    if (pos == std::string_view::npos) break;
                    start = pos + 1;
                }
                is_set_ = true;
                return {};
            }
        }
        if (auto r = parse_one(sv, opt, app); !r) return r;
        is_set_ = true;
        return {};
    }

    ParseResult set_implicit(const App* app) override {
        if constexpr (std::is_same_v<T, bool>) {
            // Fast path only when there is no constraint to check.  A boolean
            // can legitimately carry .range()/.choices(), and those must keep
            // producing the same diagnostics as an explicit `--flag=true`.
            if (!range_ && !choices_) {
                value     = true;
                is_set_   = true;
                return {};
            }
        }
        return OptionBase::set_implicit(app);
    }

    ParseResult set_negated(const App* app) override {
        if constexpr (std::is_same_v<T, bool>) {
            if (!range_ && !choices_) {
                value     = false;
                is_set_   = true;
                return {};
            }
        }
        return OptionBase::set_negated(app);
    }

    ParseResult parse_env(std::string_view sv, const App* app) override {
        const char delim = (delimiter_ != '\0') ? delimiter_ : ',';
        if constexpr (is_vec_v<T>) {
            // An empty environment variable means "no values" for a vector.
            // This keeps `TAGS=` from inventing a single empty tag, while a
            // scalar string still treats the empty value as a real value.
            if (sv.empty()) {
                value     = T{};
                from_env_ = true;
                return {};
            }
            size_t start = 0;
            while (start <= sv.size()) {
                const auto pos = sv.find(delim, start);
                const auto tok = (pos == std::string_view::npos)
                    ? sv.substr(start)
                    : sv.substr(start, pos - start);
                if (auto r = parse_one(tok, env_var, app); !r) return r;
                if (pos == std::string_view::npos) break;
                start = pos + 1;
            }
        } else {
            if (auto r = parse_one(sv, env_var, app); !r) return r;
        }
        from_env_ = true;
        return {};
    }

    ParseResult validate_and_finalize(const App* app) override {
        // Values parsed from the command line or environment were already
        // checked element-by-element by parse_one().  Only defaults bypass that
        // path, so avoid re-formatting and re-checking every hot-path value.
        if (needs_check_) {
            needs_check_ = false;
            if (value && (range_ || choices_)) {
                if constexpr (is_vec_v<T>) {
                    for (const auto& e : *value)
                        if (auto r = check_one(e, display_name(), to_display(e), app); !r) return r;
                } else {
                    if (auto r = check_one(*value, display_name(), to_display(*value), app); !r) return r;
                }
            }
        }
        if (validator_ && value && !validator_(*value))
            return std::unexpected(ParseError::validation(display_name(), validator_desc, app));
        if (transform_ && value)
            value = transform_(std::move(*value));
        return {};
    }

    bool has_default() const noexcept override { return default_.has_value(); }
    bool has_value()   const noexcept override { return value.has_value(); }
    void apply_default() override {
        if (default_) {
            value = *default_;
            needs_check_ = true;
        }
    }
    bool is_multi()    const noexcept override { return is_vec_v<T>; }
    const void* type_id() const noexcept override { return &type_token<T>; }

    std::string default_str() const override {
        if (!default_) return {};
        if constexpr (std::is_same_v<T, bool>) {
            // Only show [default: true] on flags when default is true
            if (is_flag_)
                return *default_ ? "true" : std::string{};
        }
        return to_display(*default_);
    }

    std::string choices_str() const override { return fmt_choices(); }

private:
    ParseResult check_one(const E& e, std::string_view opt,
                          std::string_view shown, const App* app) const {
        if (range_) {
            if constexpr (requires(const E& a, const E& b) {
                              { a < b } -> std::convertible_to<bool>;
                          }) {
                // NaN compares false against every bound, so check it first.
                // `hi < e` (rather than `e > hi`) supports ordered types that
                // only define operator<.
                if constexpr (std::is_floating_point_v<E>) {
                    if (e != e)
                        return std::unexpected(ParseError::out_of_range(
                            opt, shown, to_display(range_->first), to_display(range_->second), app));
                }
                if (e < range_->first || range_->second < e)
                    return std::unexpected(ParseError::out_of_range(
                        opt, shown, to_display(range_->first), to_display(range_->second), app));
            }
        }
        if (choices_) {
            if constexpr (std::equality_comparable<E>) {
                if (!std::ranges::contains(*choices_, e))
                    return std::unexpected(ParseError::bad_choice(opt, shown, fmt_choices(), app));
            }
        }
        return {};
    }

    [[nodiscard]] std::string fmt_choices() const {
        if (!choices_) return {};
        std::string s;
        bool first = true;
        for (const auto& c : *choices_) {
            if (!first) s += "|";
            first = false;
            s += to_display(c);
        }
        return s;
    }
};

// ── CountImpl — counts occurrences of a flag (-v -v -v = 3, or -vvv = 3) ───

struct CountImpl final : OptionBase {
    unsigned count_ = 0;

    ParseResult parse(std::string_view, std::string_view, const App*) override {
        ++count_;
        is_set_ = true;
        return {};
    }
    ParseResult set_implicit(const App*) override {
        ++count_;
        is_set_ = true;
        return {};
    }
    ParseResult parse_env(std::string_view sv, const App* app) override {
        auto r = from_string<unsigned>(sv);
        if (!r) return std::unexpected(ParseError::invalid(env_var, sv, r.error(), app));
        count_    = *r;
        from_env_ = true;
        return {};
    }
    ParseResult validate_and_finalize(const App*) override { return {}; }
    bool        has_default()      const noexcept override { return false; }
    bool        has_value()        const noexcept override { return count_ > 0 || is_set_ || from_env_; }
    void        apply_default()                   override {}
    std::string default_str()      const          override { return {}; }
    std::string choices_str()      const          override { return "count"; }
    bool        is_multi()         const noexcept override { return false; }
    bool        is_count_option()  const noexcept override { return true; }
    const void* type_id()          const noexcept override { return &type_token<CountImpl>; }
    void        reset()            noexcept       override { OptionBase::reset(); count_ = 0; }
};

// ────────────────────────────────────────────────────────────────────────────
// §5  PositionalBase / PositionalImpl<T>
// ────────────────────────────────────────────────────────────────────────────

struct PositionalBase {
    std::string name;
    std::string label; // pre-rendered "<name>" for diagnostics
    std::string help_text;
    std::string env_var;
    bool        required_ = true;
    bool        is_set_   = false;
    bool        from_env_ = false;
    bool        from_config_ = false;
    bool        variadic_ = false;
    char        delimiter_ = '\0';
    bool        has_default_     = false;
    bool        needs_finalize_  = false;

    virtual ~PositionalBase() = default;
    virtual ParseResult parse(std::string_view v, const App* app)     = 0;
    virtual ParseResult parse_env(std::string_view v, const App* app) = 0;
    virtual ParseResult validate_and_finalize(const App* app)         = 0;
    virtual bool        has_default() const noexcept                  = 0;
    virtual void        apply_default()                               = 0;
    virtual std::string default_str() const                           = 0;
    virtual std::string choices_str() const                           = 0;
    virtual void        reset() noexcept { is_set_ = false; from_env_ = false; from_config_ = false; }
};

template<typename T>
struct PositionalImpl final : PositionalBase {
    using E = elem_of_t<T>;

    std::optional<T>              value;
    std::optional<T>              default_;
    std::optional<std::pair<E,E>> range_;
    std::optional<std::vector<E>> choices_;
    std::function<bool(const T&)> validator_;
    std::string                   validator_desc;
    std::function<T(T)>           transform_;
    bool                          needs_check_ = false;

    ParseResult parse_one(std::string_view sv, std::string_view error_label,
                          const App* app) {
        if constexpr (is_vec_v<T>) {
            auto e = from_string<E>(sv);
            if (!e) return std::unexpected(ParseError::invalid(error_label, sv, e.error(), app));
            if (auto r = check_one(*e, error_label, sv, app); !r) return r;
            if (!value) value = T{};
            value->push_back(std::move(*e));
        } else {
            auto r = from_string<T>(sv);
            if (!r) return std::unexpected(ParseError::invalid(error_label, sv, r.error(), app));
            if (auto c = check_one(*r, error_label, sv, app); !c) return c;
            value = std::move(*r);
        }
        return {};
    }

    ParseResult parse(std::string_view sv, const App* app) override {
        if constexpr (is_vec_v<T>) {
            if (delimiter_ != '\0') {
                size_t start = 0;
                while (start <= sv.size()) {
                    const auto pos = sv.find(delimiter_, start);
                    const auto tok = (pos == std::string_view::npos)
                        ? sv.substr(start)
                        : sv.substr(start, pos - start);
                    if (auto r = parse_one(tok, label, app); !r) return r;
                    if (pos == std::string_view::npos) break;
                    start = pos + 1;
                }
                is_set_ = true;
                return {};
            }
        }
        if (auto r = parse_one(sv, label, app); !r) return r;
        is_set_ = true;
        return {};
    }

    ParseResult parse_env(std::string_view sv, const App* app) override {
        const auto& env_label = env_var;
        if constexpr (is_vec_v<T>) {
            if (sv.empty()) {
                value     = T{};
                from_env_ = true;
                return {};
            }
            const char delim = (delimiter_ != '\0') ? delimiter_ : ',';
            size_t start = 0;
            while (start <= sv.size()) {
                const auto pos = sv.find(delim, start);
                const auto tok = (pos == std::string_view::npos)
                    ? sv.substr(start)
                    : sv.substr(start, pos - start);
                if (auto r = parse_one(tok, env_label, app); !r) return r;
                if (pos == std::string_view::npos) break;
                start = pos + 1;
            }
        } else {
            if (auto r = parse_one(sv, env_label, app); !r) return r;
        }
        from_env_ = true;
        return {};
    }

    ParseResult validate_and_finalize(const App* app) override {
        // CLI and environment values were already checked in parse_one();
        // only defaults need a second pass.
        if (needs_check_) {
            needs_check_ = false;
            if (value && (range_ || choices_)) {
                if constexpr (is_vec_v<T>) {
                    for (const auto& e : *value)
                        if (auto r = check_one(e, label, to_display(e), app); !r) return r;
                } else {
                    if (auto r = check_one(*value, label, to_display(*value), app); !r) return r;
                }
            }
        }
        if (validator_ && value && !validator_(*value))
            return std::unexpected(ParseError::validation(label, validator_desc, app));
        if (transform_ && value)
            value = transform_(std::move(*value));
        return {};
    }

    bool has_default() const noexcept override { return default_.has_value(); }
    void apply_default() override {
        if (default_) {
            value = *default_;
            needs_check_ = true;
        }
    }
    void reset() noexcept override {
        PositionalBase::reset();
        value.reset();
        needs_check_ = false;
    }

    std::string default_str() const override {
        if (!default_) return {};
        return to_display(*default_);
    }

    std::string choices_str() const override { return fmt_choices(); }

private:
    ParseResult check_one(const E& e, std::string_view error_label,
                          std::string_view shown, const App* app) const {
        if (range_) {
            if constexpr (requires(const E& a, const E& b) {
                              { a < b } -> std::convertible_to<bool>;
                          }) {
                // See OptionImpl::check_one: reject NaN and compare with `<`
                // only so ordered user types need no operator>.
                if constexpr (std::is_floating_point_v<E>) {
                    if (e != e)
                        return std::unexpected(ParseError::out_of_range(
                            error_label, shown, to_display(range_->first), to_display(range_->second), app));
                }
                if (e < range_->first || range_->second < e)
                    return std::unexpected(ParseError::out_of_range(
                        error_label, shown, to_display(range_->first), to_display(range_->second), app));
            }
        }
        if (choices_) {
            if constexpr (std::equality_comparable<E>) {
                if (!std::ranges::contains(*choices_, e))
                    return std::unexpected(ParseError::bad_choice(error_label, shown, fmt_choices(), app));
            }
        }
        return {};
    }

    [[nodiscard]] std::string fmt_choices() const {
        if (!choices_) return {};
        std::string s;
        bool first = true;
        for (const auto& c : *choices_) {
            if (!first) s += "|";
            first = false;
            s += to_display(c);
        }
        return s;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §5.5  Config data — small INI/TOML-style reader
// ────────────────────────────────────────────────────────────────────────────

struct ConfigData {
    using Section = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;
    std::unordered_map<std::string, Section, StringHash, std::equal_to<>> sections;
};

[[nodiscard]] inline std::string_view trim_view(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

inline void strip_matching_quotes(std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"')
                          || (s.front() == '\'' && s.back() == '\'')))
        s = s.substr(1, s.size() - 2);
}

/// Remove an unquoted `#`/`;` comment that starts a new field (preceded by
/// whitespace), preserving hashes inside paths such as `a#b`.
[[nodiscard]] inline std::string_view strip_inline_comment(std::string_view s) noexcept {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (escaped) { escaped = false; continue; }
        if (quote != '\0') {
            if (c == '\\' && quote == '"') escaped = true;
            else if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if ((c == '#' || c == ';')
                   && (i == 0 || std::isspace(static_cast<unsigned char>(s[i - 1])))) {
            return s.substr(0, i);
        }
    }
    return s;
}

[[nodiscard]] inline std::expected<ConfigData, std::string>
parse_config(std::string_view text) {
    ConfigData data;
    std::string section;
    size_t line_no = 0;
    while (!text.empty()) {
        ++line_no;
        const auto nl = text.find('\n');
        auto line = trim_view(text.substr(0, nl));
        text = (nl == std::string_view::npos) ? std::string_view{}
                                              : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == std::string_view::npos)
                return std::unexpected(
                    std::format("line {}: unterminated section header", line_no));
            section = std::string(trim_view(line.substr(1, close - 1)));
            if (section.empty())
                return std::unexpected(
                    std::format("line {}: empty section name", line_no));
            continue;
        }
        const auto eq = line.find('=');
        std::string key;
        std::string value;
        if (eq == std::string_view::npos) {
            key   = std::string(trim_view(line));
            value = "true";
        } else {
            key   = std::string(trim_view(line.substr(0, eq)));
            auto raw_value = trim_view(line.substr(eq + 1));
            value = std::string(trim_view(strip_inline_comment(raw_value)));
        }
        if (key.empty())
            return std::unexpected(
                std::format("line {}: missing key", line_no));
        strip_matching_quotes(value);
        data.sections[section][std::move(key)] = std::move(value);
    }
    return data;
}

/// Return the config value for the first matching option spelling.
[[nodiscard]] inline const std::string*
config_value_for(const ConfigData::Section& section,
                 const std::vector<std::string>& names) {
    for (const auto& name : names) {
        std::string_view key = name;
        if (key.starts_with("--"))      key.remove_prefix(2);
        else if (key.starts_with('-'))  key.remove_prefix(1);
        if (const auto it = section.find(key); it != section.end())
            return &it->second;
        if (const auto it = section.find(name); it != section.end())
            return &it->second;
    }
    return nullptr;
}

/// Split `[a, "b,c"]` (or a plain scalar) into individual config values.
[[nodiscard]] inline std::vector<std::string> config_values(std::string_view raw) {
    const bool list = raw.size() >= 2 && raw.front() == '[' && raw.back() == ']';
    if (!list) return { std::string(raw) };
    raw = raw.substr(1, raw.size() - 2);
    std::vector<std::string> values;
    std::string current;
    char quote = '\0';
    for (char c : raw) {
        if (quote != '\0') {
            if (c == quote) quote = '\0';
            else current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ',') {
            auto v = trim_view(current);
            values.emplace_back(v);
            current.clear();
        } else {
            current += c;
        }
    }
    auto last = trim_view(current);
    if (!last.empty() || !values.empty())
        values.emplace_back(last);
    return values;
}

// ── Response-file tokenisation ──────────────────────────────────────────────

[[nodiscard]] inline std::expected<std::vector<std::string>, std::string>
tokenize_response_file(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    bool started = false;
    char quote   = '\0';
    bool escaped = false;
    bool comment = false;
    for (char c : text) {
        if (comment) {
            if (c == '\n') comment = false;
            continue;
        }
        if (escaped) {
            current += c;
            started = true;
            escaped = false;
            continue;
        }
        if (quote != '\0') {
            if (c == quote) quote = '\0';
            else if (c == '\\' && quote == '"') escaped = true;
            else current += c;
            started = true;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            started = true;
        } else if (c == '"' || c == '\'') {
            quote   = c;
            started = true;
        } else if (c == '#') {
            if (!started) {
                comment = true;
            } else {
                current += c;
            }
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (started) {
                tokens.push_back(std::move(current));
                current.clear();
                started = false;
            }
        } else {
            current += c;
            started = true;
        }
    }
    if (escaped || quote != '\0')
        return std::unexpected("unterminated quote or escape");
    if (started) tokens.push_back(std::move(current));
    return tokens;
}

inline std::expected<std::vector<std::string>, ParseError>
expand_response_tokens(std::span<const std::string_view> args,
                       const App* app, unsigned depth = 0) {
    std::vector<std::string> expanded;
    expanded.reserve(args.size());
    for (const auto token : args) {
        if (token.size() < 2 || token.front() != '@') {
            expanded.emplace_back(token);
            continue;
        }
        if (token[1] == '@') { // escape: @@name expands to @name
            expanded.emplace_back(token.substr(1));
            continue;
        }
        if (depth >= 8)
            return std::unexpected(ParseError::response_file(
                token.substr(1), "nesting exceeds 8 levels", app));
        const std::string path(token.substr(1));
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::unexpected(ParseError::response_file(
                path, "cannot open file", app));
        in.seekg(0, std::ios::end);
        const auto end = in.tellg();
        if (end < 0)
            return std::unexpected(ParseError::response_file(
                path, "cannot determine size", app));
        constexpr std::streamoff max_size = 4 * 1024 * 1024;
        if (end > max_size)
            return std::unexpected(ParseError::response_file(
                path, "file is larger than 4 MiB", app));
        in.seekg(0, std::ios::beg);
        std::string text(static_cast<size_t>(end), '\0');
        if (end > 0 && !in.read(text.data(), end))
            return std::unexpected(ParseError::response_file(
                path, "read failed", app));
        auto tokens = tokenize_response_file(text);
        if (!tokens)
            return std::unexpected(ParseError::response_file(path, tokens.error(), app));
        std::vector<std::string_view> views;
        views.reserve(tokens->size());
        for (const auto& t : *tokens) views.emplace_back(t);
        auto nested = expand_response_tokens(views, app, depth + 1);
        if (!nested) return nested;
        for (auto& t : *nested) expanded.push_back(std::move(t));
    }
    return expanded;
}

} // namespace detail

// ────────────────────────────────────────────────────────────────────────────
// §6  Option<T> — user-facing typed option handle
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
class [[nodiscard]] Option {
    detail::OptionImpl<T>* p_;
    friend class App;
public:
    explicit Option(detail::OptionImpl<T>* p) noexcept : p_(p) {}

    template<typename V>
    auto&& default_val(this auto&& self, V&& v)
        requires std::constructible_from<T, V&&>
              && (!std::same_as<std::remove_cvref_t<V>, std::nullptr_t>) {
        self.p_->default_ = T(std::forward<V>(v));
        self.p_->has_default_ = true;
        if (self.p_->range_ || self.p_->choices_)
            self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    auto&& default_val(this auto&& self,
                       std::initializer_list<detail::elem_of_t<T>> values)
        requires detail::is_vec_v<T> {
        self.p_->default_ = T(values.begin(), values.end());
        self.p_->has_default_ = true;
        if (self.p_->range_ || self.p_->choices_)
            self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    auto&& short_name(this auto&& self, std::string_view s) {
        detail::prepend_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }

    auto&& help(this auto&& self, std::string_view h) {
        self.p_->help_text = std::string(h);
        return std::forward<decltype(self)>(self);
    }

    auto&& required(this auto&& self, bool r = true) {
        self.p_->required_ = r;
        return std::forward<decltype(self)>(self);
    }

    auto&& hidden(this auto&& self, bool h = true) {
        self.p_->hidden_ = h;
        return std::forward<decltype(self)>(self);
    }

    auto&& env(this auto&& self, std::string_view var) {
        detail::validate_env_name(var);
        self.p_->env_var = std::string(var);
        return std::forward<decltype(self)>(self);
    }

    auto&& meta(this auto&& self, std::string_view m) {
        self.p_->meta = std::string(m);
        return std::forward<decltype(self)>(self);
    }

    auto&& delimiter(this auto&& self, char d) requires detail::is_vec_v<T> {
        self.p_->delimiter_ = d;
        return std::forward<decltype(self)>(self);
    }

    auto&& range(this auto&& self, detail::elem_of_t<T> lo, detail::elem_of_t<T> hi) {
        static_assert(requires(const detail::elem_of_t<T>& a, const detail::elem_of_t<T>& b) {
                          { a < b } -> std::convertible_to<bool>;
                          { b < a } -> std::convertible_to<bool>;
                      },
                      "range() requires an ordered element type");
        if (hi < lo)
            throw std::invalid_argument("cli: range lower bound exceeds upper bound");
        self.p_->range_ = { std::move(lo), std::move(hi) };
        if (self.p_->has_default_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    auto&& choices(this auto&& self, std::initializer_list<detail::elem_of_t<T>> c) {
        static_assert(std::equality_comparable<detail::elem_of_t<T>>,
                      "choices() requires an equality-comparable element type");
        if (c.size() == 0)
            throw std::invalid_argument("cli: choices() requires at least one value");
        self.p_->choices_ = std::vector<detail::elem_of_t<T>>(c);
        if (self.p_->has_default_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    auto&& validate(this auto&& self,
                    std::function<bool(const T&)> fn,
                    std::string desc = "custom validation failed") {
        self.p_->validator_     = std::move(fn);
        self.p_->validator_desc = std::move(desc);
        self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    auto&& transform(this auto&& self, std::function<T(T)> fn) {
        self.p_->transform_ = std::move(fn);
        self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    /// Register an additional spelling for this option.
    auto&& alias(this auto&& self, std::string_view s) {
        detail::append_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }

    /// Consume exactly `n` following tokens for one occurrence.
    auto&& nargs(this auto&& self, size_t n) requires detail::is_vec_v<T> {
        if (n == 0)
            throw std::invalid_argument("cli: nargs() requires at least one value");
        self.p_->nargs_min_ = n;
        self.p_->nargs_max_ = n;
        return std::forward<decltype(self)>(self);
    }

    /// Consume between `lo` and `hi` following tokens for one occurrence.
    auto&& nargs(this auto&& self, size_t lo, size_t hi) requires detail::is_vec_v<T> {
        if (lo == 0 || hi < lo)
            throw std::invalid_argument(
                "cli: nargs(lo, hi) requires 1 <= lo <= hi");
        self.p_->nargs_min_ = lo;
        self.p_->nargs_max_ = hi;
        return std::forward<decltype(self)>(self);
    }

    /// Consume at least `lo` tokens, stopping at the next option-looking token.
    auto&& nargs_at_least(this auto&& self, size_t lo) requires detail::is_vec_v<T> {
        if (lo == 0)
            throw std::invalid_argument(
                "cli: nargs_at_least() requires at least one value");
        self.p_->nargs_min_ = lo;
        self.p_->nargs_max_ = static_cast<size_t>(-1);
        return std::forward<decltype(self)>(self);
    }

    auto&& section(this auto&& self, std::string_view s) {
        self.p_->section_ = std::string(s);
        return std::forward<decltype(self)>(self);
    }

    /// Alias for section(), matching the vocabulary of other CLI libraries.
    auto&& group(this auto&& self, std::string_view s) {
        return std::forward<decltype(self)>(self).section(s);
    }

    [[nodiscard]] std::optional<T>&       get()       noexcept { return p_->value; }
    [[nodiscard]] const std::optional<T>& get() const noexcept { return p_->value; }
    [[nodiscard]] T&        operator*()       { return *p_->value; }
    [[nodiscard]] const T&  operator*() const { return *p_->value; }
    [[nodiscard]] T*        operator->()       { return &*p_->value; }
    [[nodiscard]] const T*  operator->() const { return &*p_->value; }
    [[nodiscard]] explicit operator bool() const noexcept { return p_->value.has_value(); }
    [[nodiscard]] bool is_set()   const noexcept { return p_->is_set_; }
    [[nodiscard]] T&   value()       { return *p_->value; }
    [[nodiscard]] T    value() const { return *p_->value; }
    template<typename V>
    [[nodiscard]] T value_or(V&& fallback) const
        requires std::constructible_from<T, V&&>
              && (!std::same_as<std::remove_cvref_t<V>, std::nullptr_t>) {
        if (p_->value) return *p_->value;
        return T(std::forward<V>(fallback));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §7  Flag — boolean switch
// ────────────────────────────────────────────────────────────────────────────

class [[nodiscard]] Flag {
    detail::OptionImpl<bool>* p_;
    friend class App;
public:
    explicit Flag(detail::OptionImpl<bool>* p) noexcept : p_(p) {}

    auto&& short_name(this auto&& self, std::string_view s) {
        detail::prepend_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }
    auto&& help(this auto&& self, std::string_view h) {
        self.p_->help_text = std::string(h);
        return std::forward<decltype(self)>(self);
    }
    auto&& hidden(this auto&& self, bool h = true) {
        self.p_->hidden_ = h;
        return std::forward<decltype(self)>(self);
    }
    auto&& env(this auto&& self, std::string_view v) {
        detail::validate_env_name(v);
        self.p_->env_var = std::string(v);
        return std::forward<decltype(self)>(self);
    }
    auto&& section(this auto&& self, std::string_view s) {
        self.p_->section_ = std::string(s);
        return std::forward<decltype(self)>(self);
    }
    /// Alias for section(), matching the vocabulary of other CLI libraries.
    auto&& group(this auto&& self, std::string_view s) {
        return std::forward<decltype(self)>(self).section(s);
    }
    /// Register an additional spelling for this option.
    auto&& alias(this auto&& self, std::string_view s) {
        detail::append_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }
    auto&& required(this auto&& self, bool r = true) {
        self.p_->required_ = r;
        return std::forward<decltype(self)>(self);
    }
    auto&& default_val(this auto&& self, bool d) {
        self.p_->default_ = d;
        self.p_->has_default_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& negatable(this auto&& self, bool n = true) {
        self.p_->negatable_ = n;
        return std::forward<decltype(self)>(self);
    }

    [[nodiscard]] bool operator*()         const noexcept { return p_->value.value_or(p_->default_.value_or(false)); }
    [[nodiscard]] explicit operator bool() const noexcept { return **this; }
    [[nodiscard]] bool is_set()            const noexcept { return p_->is_set_; }
};

// ────────────────────────────────────────────────────────────────────────────
// §7.5  Count — counts flag occurrences (-v -v -v = 3, or -vvv = 3)
// ────────────────────────────────────────────────────────────────────────────

class [[nodiscard]] Count {
    detail::CountImpl* p_;
    friend class App;
public:
    explicit Count(detail::CountImpl* p) noexcept : p_(p) {}

    auto&& short_name(this auto&& self, std::string_view s) {
        detail::prepend_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }
    auto&& help(this auto&& self, std::string_view h) {
        self.p_->help_text = std::string(h);
        return std::forward<decltype(self)>(self);
    }
    auto&& hidden(this auto&& self, bool h = true) {
        self.p_->hidden_ = h;
        return std::forward<decltype(self)>(self);
    }
    auto&& env(this auto&& self, std::string_view v) {
        detail::validate_env_name(v);
        self.p_->env_var = std::string(v);
        return std::forward<decltype(self)>(self);
    }
    auto&& section(this auto&& self, std::string_view s) {
        self.p_->section_ = std::string(s);
        return std::forward<decltype(self)>(self);
    }
    /// Alias for section(), matching the vocabulary of other CLI libraries.
    auto&& group(this auto&& self, std::string_view s) {
        return std::forward<decltype(self)>(self).section(s);
    }
    /// Register an additional spelling for this option.
    auto&& alias(this auto&& self, std::string_view s) {
        detail::append_option_name(self.p_, s);
        return std::forward<decltype(self)>(self);
    }
    auto&& required(this auto&& self, bool r = true) {
        self.p_->required_ = r;
        return std::forward<decltype(self)>(self);
    }

    [[nodiscard]] unsigned operator*()            const noexcept { return p_->count_; }
    [[nodiscard]] explicit operator bool()        const noexcept { return p_->count_ > 0; }
    [[nodiscard]] bool     is_set()               const noexcept { return p_->is_set_; }
    [[nodiscard]] unsigned value_or(unsigned fb)  const noexcept { return (p_->is_set_ || p_->from_env_ || p_->from_config_) ? p_->count_ : fb; }
};

// ────────────────────────────────────────────────────────────────────────────
// §8  Positional<T> — positional argument handle
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
class [[nodiscard]] Positional {
    detail::PositionalImpl<T>* p_;
    friend class App;
public:
    explicit Positional(detail::PositionalImpl<T>* p) noexcept : p_(p) {}

    template<typename V>
    auto&& default_val(this auto&& self, V&& v)
        requires std::constructible_from<T, V&&>
              && (!std::same_as<std::remove_cvref_t<V>, std::nullptr_t>) {
        self.p_->default_  = T(std::forward<V>(v));
        self.p_->required_ = false;
        self.p_->has_default_ = true;
        if (self.p_->range_ || self.p_->choices_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& default_val(this auto&& self,
                       std::initializer_list<detail::elem_of_t<T>> values)
        requires detail::is_vec_v<T> {
        self.p_->default_  = T(values.begin(), values.end());
        self.p_->required_ = false;
        self.p_->has_default_ = true;
        if (self.p_->range_ || self.p_->choices_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& help(this auto&& self, std::string_view h) {
        self.p_->help_text = std::string(h);
        return std::forward<decltype(self)>(self);
    }
    auto&& required(this auto&& self, bool r = true) {
        self.p_->required_ = r;
        return std::forward<decltype(self)>(self);
    }
    auto&& env(this auto&& self, std::string_view var) {
        detail::validate_env_name(var);
        self.p_->env_var = std::string(var);
        return std::forward<decltype(self)>(self);
    }
    auto&& delimiter(this auto&& self, char d) requires detail::is_vec_v<T> {
        self.p_->delimiter_ = d;
        return std::forward<decltype(self)>(self);
    }
    auto&& range(this auto&& self, detail::elem_of_t<T> lo, detail::elem_of_t<T> hi) {
        static_assert(requires(const detail::elem_of_t<T>& a, const detail::elem_of_t<T>& b) {
                          { a < b } -> std::convertible_to<bool>;
                          { b < a } -> std::convertible_to<bool>;
                      },
                      "range() requires an ordered element type");
        if (hi < lo)
            throw std::invalid_argument("cli: range lower bound exceeds upper bound");
        self.p_->range_ = { std::move(lo), std::move(hi) };
        if (self.p_->has_default_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& choices(this auto&& self, std::initializer_list<detail::elem_of_t<T>> c) {
        static_assert(std::equality_comparable<detail::elem_of_t<T>>,
                      "choices() requires an equality-comparable element type");
        if (c.size() == 0)
            throw std::invalid_argument("cli: choices() requires at least one value");
        self.p_->choices_ = std::vector<detail::elem_of_t<T>>(c);
        if (self.p_->has_default_) self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& validate(this auto&& self,
                    std::function<bool(const T&)> fn,
                    std::string desc = "custom validation failed") {
        self.p_->validator_     = std::move(fn);
        self.p_->validator_desc = std::move(desc);
        self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }
    auto&& transform(this auto&& self, std::function<T(T)> fn) {
        self.p_->transform_ = std::move(fn);
        self.p_->needs_finalize_ = true;
        return std::forward<decltype(self)>(self);
    }

    [[nodiscard]] std::optional<T>&       get()       noexcept { return p_->value; }
    [[nodiscard]] const std::optional<T>& get() const noexcept { return p_->value; }
    [[nodiscard]] T&        operator*()       { return *p_->value; }
    [[nodiscard]] const T&  operator*() const { return *p_->value; }
    [[nodiscard]] T*        operator->()       { return &*p_->value; }
    [[nodiscard]] const T*  operator->() const { return &*p_->value; }
    [[nodiscard]] explicit operator bool() const noexcept { return p_->value.has_value(); }
    [[nodiscard]] bool is_set()    const noexcept { return p_->is_set_; }
    [[nodiscard]] T&   value()       { return *p_->value; }
    [[nodiscard]] T    value() const { return *p_->value; }
    template<typename V>
    [[nodiscard]] T value_or(V&& fallback) const
        requires std::constructible_from<T, V&&>
              && (!std::same_as<std::remove_cvref_t<V>, std::nullptr_t>) {
        if (p_->value) return *p_->value;
        return T(std::forward<V>(fallback));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §9  App — main parser, subcommand node, and help renderer
// ────────────────────────────────────────────────────────────────────────────

class App {
    std::string name_, desc_, version_, author_;
    std::vector<std::string>                             examples_;
    detail::OptionRegistry                               registry_;
    std::vector<std::unique_ptr<detail::OptionBase>>     options_;
    std::vector<std::unique_ptr<detail::PositionalBase>> positionals_;
    std::vector<std::unique_ptr<App>>                    subcommands_;
    std::unordered_map<std::string, App*, detail::StringHash, std::equal_to<>>
                                                         command_index_;
    App*  parent_     = nullptr;
    App*  active_sub_ = nullptr;
    std::function<void(App&)>                            callback_;
    std::vector<std::string>                             aliases_;
    bool                                                 require_subcommand_ = false;
    bool                                                 hidden_ = false;
    bool                                                 allow_extras_ = false;
    bool                                                 allow_abbrev_ = false;
    bool                                                 response_files_ = false;
    std::vector<std::string>                             remaining_args_;
    std::string                                          config_file_;
    bool                                                 config_required_ = false;
    detail::OptionImpl<std::string>*                     config_option_ = nullptr;
    detail::ConfigData                                   config_data_;
    std::string                                          config_loaded_path_;
    bool                                                 config_loaded_ = false;
    std::vector<std::vector<std::string>>                mutex_groups_;
    std::vector<std::vector<std::string>>                require_groups_;
    std::vector<std::vector<std::string>>                require_one_groups_;
    // Group references are resolved once after registration and reused by
    // every parse.  Pointers stay valid because options are heap-owned.
    std::vector<std::vector<detail::OptionBase*>>        mutex_resolved_;
    std::vector<std::vector<detail::OptionBase*>>        require_resolved_;
    std::vector<std::vector<detail::OptionBase*>>        require_one_resolved_;
    bool                                                 groups_dirty_ = true;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    // Moving an App would invalidate parent links for owned subcommands and
    // any Option<T>/Positional<T> handles held by the caller.  Keep it stable.
    App(App&&)                 = delete;
    App& operator=(App&&)      = delete;

public:
    explicit App(std::string name, std::string desc = {})
        : name_(std::move(name)), desc_(std::move(desc)) {}

    App(std::string name, std::string desc, std::string ver)
        : name_(std::move(name)), desc_(std::move(desc)), version_(std::move(ver)) {}

    // ── Dynamic lookup proxy ──────────────────────────────────────────────

    struct Lookup {
        detail::OptionBase* p_ = nullptr;
        [[nodiscard]] explicit operator bool() const noexcept { return p_ && p_->has_value(); }
        [[nodiscard]] bool is_set()            const noexcept { return p_ && p_->is_set_; }
        [[nodiscard]] bool found()             const noexcept { return p_ != nullptr; }

        template<typename T>
        [[nodiscard]] std::optional<T> try_as() const {
            if (!p_) return std::nullopt;
            if constexpr (std::is_integral_v<T>) {
                if (p_->type_id() == &detail::type_token<detail::CountImpl>) {
                    if (!p_->has_value()) return std::nullopt;
                    return static_cast<T>(static_cast<const detail::CountImpl*>(p_)->count_);
                }
            }
            if (p_->type_id() == &detail::type_token<T>) {
                const auto* impl = static_cast<const detail::OptionImpl<T>*>(p_);
                if (impl->value) return *impl->value;
            }
            return std::nullopt;
        }

        template<typename T>
        [[nodiscard]] T as() const {
            return try_as<T>().value_or(T{});
        }

        template<typename T>
        [[nodiscard]] T as_or(T fallback) const {
            return try_as<T>().value_or(std::move(fallback));
        }

        [[nodiscard]] unsigned as_count() const noexcept {
            if (p_ && p_->type_id() == &detail::type_token<detail::CountImpl>)
                return static_cast<const detail::CountImpl*>(p_)->count_;
            return 0u;
        }
    };

    [[nodiscard]] Lookup operator[](std::string_view name) const noexcept {
        return Lookup{find_opt(name)};
    }

    // ── App metadata builder ──────────────────────────────────────────────

    auto&& version(this auto&& self, std::string v) {
        self.version_ = std::move(v);
        return std::forward<decltype(self)>(self);
    }
    auto&& author(this auto&& self, std::string a) {
        self.author_ = std::move(a);
        return std::forward<decltype(self)>(self);
    }
    auto&& example(this auto&& self, std::string ex) {
        self.examples_.push_back(std::move(ex));
        return std::forward<decltype(self)>(self);
    }

    // ── Option registration (v1 compat) ───────────────────────────────────

    template<typename T>
    [[nodiscard]] Option<T> option(std::string_view sn, std::string_view ln,
                                   std::string_view help) {
        if (sn.empty() && ln.empty())
            throw std::invalid_argument("cli: option requires at least one name");
        auto impl = std::make_unique<detail::OptionImpl<T>>();
        impl->help_text = std::string(help);
        impl->meta      = std::string(detail::type_meta<T>());
        auto* p         = impl.get();
        adopt_option(std::move(impl), {sn, ln});
        return Option<T>(p);
    }

    template<typename T>
    [[nodiscard]] Option<T> option(std::string_view ln, std::string_view help) {
        return option<T>({}, ln, help);
    }

    [[nodiscard]] Flag flag(std::string_view sn, std::string_view ln, std::string_view help) {
        if (sn.empty() && ln.empty())
            throw std::invalid_argument("cli: option requires at least one name");
        auto impl = std::make_unique<detail::OptionImpl<bool>>();
        impl->help_text = std::string(help);
        impl->is_flag_  = true;
        auto* p         = impl.get();
        adopt_option(std::move(impl), {sn, ln});
        return Flag(p);
    }

    [[nodiscard]] Flag flag(std::string_view ln, std::string_view help) {
        return flag({}, ln, help);
    }

    template<typename T>
    [[nodiscard]] Positional<T> positional(std::string_view name, std::string_view help) {
        detail::validate_positional_name(name);
        if (std::ranges::any_of(positionals_, [](const auto& p) {
                return p->variadic_;
            })) {
            throw std::invalid_argument(
                "cli: a variadic positional must be the last positional");
        }
        auto impl       = std::make_unique<detail::PositionalImpl<T>>();
        impl->name      = std::string(name);
        impl->label     = std::format("<{}>", name);
        impl->help_text = std::string(help);
        impl->variadic_ = detail::is_vec_v<T>;
        auto* p         = impl.get();
        positionals_.push_back(std::move(impl));
        return Positional<T>(p);
    }

    // ── Option registration (v2 fluent) ───────────────────────────────────

    template<typename T>
    [[nodiscard]] Option<T> add(std::string_view long_name) {
        detail::validate_option_name(long_name);
        auto impl = std::make_unique<detail::OptionImpl<T>>();
        impl->meta = std::string(detail::type_meta<T>());
        auto* p    = impl.get();
        adopt_option(std::move(impl), {long_name});
        return Option<T>(p);
    }

    [[nodiscard]] Flag add_flag(std::string_view long_name) {
        detail::validate_option_name(long_name);
        auto impl      = std::make_unique<detail::OptionImpl<bool>>();
        impl->is_flag_ = true;
        auto* p        = impl.get();
        adopt_option(std::move(impl), {long_name});
        return Flag(p);
    }

    template<typename T>
    [[nodiscard]] Positional<T> add_positional(std::string_view name) {
        detail::validate_positional_name(name);
        // A variadic positional consumes every remaining token, so nothing
        // registered after it could ever be reached.  Reject the configuration
        // at registration time instead of silently leaving the later
        // positional unsatisfiable.
        if (std::ranges::any_of(positionals_, [](const auto& p) {
                return p->variadic_;
            })) {
            throw std::invalid_argument(
                "cli: a variadic positional must be the last positional");
        }

        auto impl       = std::make_unique<detail::PositionalImpl<T>>();
        impl->name      = std::string(name);
        impl->label     = std::format("<{}>", name);
        impl->variadic_ = detail::is_vec_v<T>;
        auto* p         = impl.get();
        positionals_.push_back(std::move(impl));
        return Positional<T>(p);
    }

    [[nodiscard]] Count add_count(std::string_view long_name) {
        detail::validate_option_name(long_name);
        auto impl      = std::make_unique<detail::CountImpl>();
        impl->is_flag_ = true;
        auto* p        = impl.get();
        adopt_option(std::move(impl), {long_name});
        return Count(p);
    }

    App& mutex_group(std::initializer_list<std::string_view> names) {
        std::vector<std::string> g;
        g.reserve(names.size());
        for (auto n : names) g.emplace_back(n);
        mutex_groups_.push_back(std::move(g));
        groups_dirty_ = true;
        return *this;
    }

    App& require_together(std::initializer_list<std::string_view> names) {
        std::vector<std::string> g;
        g.reserve(names.size());
        for (auto n : names) g.emplace_back(n);
        require_groups_.push_back(std::move(g));
        groups_dirty_ = true;
        return *this;
    }

    /// At least one of the listed options must be provided.
    App& require_one_of(std::initializer_list<std::string_view> names) {
        if (names.size() == 0)
            throw std::invalid_argument("cli: require_one_of() requires at least one option");
        std::vector<std::string> g;
        g.reserve(names.size());
        for (auto n : names) g.emplace_back(n);
        require_one_groups_.push_back(std::move(g));
        groups_dirty_ = true;
        return *this;
    }
    // ── Subcommand registration ───────────────────────────────────────────

    App& require_subcommand(bool required = true) {
        require_subcommand_ = required;
        return *this;
    }

    /// Hide this command from help and shell-completion output.  It remains
    /// fully dispatchable, which is useful for internal or experimental verbs.
    App& hidden(bool h = true) {
        hidden_ = h;
        return *this;
    }

    /// Collect unknown options and surplus positional tokens instead of
    /// failing, and expose them through remaining_args().
    App& allow_extras(bool enabled = true) {
        allow_extras_ = enabled;
        return *this;
    }

    /// Accept unambiguous prefixes of long options, e.g. --verb → --verbose.
    App& allow_abbrev(bool enabled = true) {
        allow_abbrev_ = enabled;
        return *this;
    }

    /// Expand `@file` arguments using shell-like quoting.  Must be enabled on
    /// the root command; nested response files are supported up to 8 levels.
    App& response_files(bool enabled = true) {
        if (parent_ != nullptr)
            throw std::invalid_argument(
                "cli: response_files() must be configured on the root command");
        response_files_ = enabled;
        return *this;
    }

    /// Tokens that were not consumed by an option or positional.  Populated
    /// only when allow_extras() is enabled.
    [[nodiscard]] const std::vector<std::string>& remaining_args() const noexcept {
        return remaining_args_;
    }

    /// Load defaults from an INI/TOML-style file.  Command-line values win
    /// over environment variables, which win over config values, which win
    /// over built-in defaults.  A missing file is ignored unless `required`.
    App& config_file(std::string path, bool required = false) {
        if (parent_ != nullptr)
            throw std::invalid_argument(
                "cli: config_file() must be configured on the root command");
        config_file_     = std::move(path);
        config_required_ = required;
        config_loaded_   = false;
        config_loaded_path_.clear();
        config_data_ = {};
        return *this;
    }

    /// Register a `--config <FILE>` option whose value overrides config_file()
    /// for this invocation.  Must be called on the root command.
    App& config_option(std::string_view name = "--config") {
        if (parent_ != nullptr)
            throw std::invalid_argument(
                "cli: config_option() must be configured on the root command");
        if (config_option_ != nullptr)
            throw std::invalid_argument("cli: config_option() already registered");
        auto opt = add<std::string>(name);
        opt.help("Read defaults from an INI-style config file").meta("FILE");
        config_option_ = opt.p_;
        return *this;
    }

    [[nodiscard]] App& subcommand(std::string name, std::string desc = {}) {
        detail::validate_command_name(name);
        if (command_index_.contains(name))
            throw std::invalid_argument(
                std::format("cli: duplicate command name '{}'", name));
        std::string key = name;
        auto sub     = std::make_unique<App>(std::move(name), std::move(desc));
        sub->parent_ = this;
        auto* p      = sub.get();
        subcommands_.push_back(std::move(sub));
        try {
            const auto [it, inserted] = command_index_.emplace(std::move(key), p);
            (void)it;
            if (!inserted)
                throw std::invalid_argument(
                    std::format("cli: duplicate command name '{}'", p->name_));
        } catch (...) {
            if (!subcommands_.empty() && subcommands_.back().get() == p)
                subcommands_.pop_back();
            throw;
        }
        return *p;
    }

    App& alias(std::string name) {
        detail::validate_command_name(name);
        if (name_ == name)
            throw std::invalid_argument(
                std::format("cli: alias '{}' duplicates the command name", name));
        if (std::ranges::contains(aliases_, name))
            throw std::invalid_argument(
                std::format("cli: duplicate alias '{}'", name));
        if (parent_ != nullptr && parent_->command_index_.contains(name))
            throw std::invalid_argument(
                std::format("cli: alias '{}' conflicts with another command", name));
        aliases_.push_back(name);
        if (parent_ != nullptr) {
            try {
                const auto [it, inserted] = parent_->command_index_.emplace(std::move(name), this);
                (void)it;
                if (!inserted)
                    throw std::invalid_argument(
                        std::format("cli: alias '{}' conflicts with another command",
                                    aliases_.back()));
            } catch (...) {
                aliases_.pop_back();
                throw;
            }
        }
        return *this;
    }

    App& on_execute(std::function<void(App&)> fn) {
        callback_ = std::move(fn);
        return *this;
    }

    [[nodiscard]] bool has_callback() const noexcept { return callback_ != nullptr; }
    void fire_callback() { if (callback_) callback_(*this); }

    void reset() noexcept {
        for (auto& opt : options_)     opt->reset();
        for (auto& pos : positionals_) pos->reset();
        remaining_args_.clear();
        config_loaded_ = false;
        active_sub_ = nullptr;
        for (auto& sub : subcommands_) sub->reset();
    }

    [[nodiscard]] App* active_subcommand() const noexcept { return active_sub_; }

    [[nodiscard]] bool is_subcommand(std::string_view n) const noexcept {
        return active_sub_
            && (active_sub_->name_ == n
                || std::ranges::contains(active_sub_->aliases_, n));
    }

    [[nodiscard]] bool has_flag(std::string_view name) const noexcept {
        auto l = (*this)[name];
        return l.found() && l.as_or<bool>(false);
    }

    [[nodiscard]] bool has_option(std::string_view name) const noexcept {
        auto l = (*this)[name];
        if (!l.found()) return false;
        return l.is_set() || l.p_->from_env_ || l.p_->from_config_
            || l.p_->has_default_;
    }

    [[nodiscard]] const std::string& name()        const noexcept { return name_;    }
    [[nodiscard]] const std::string& description() const noexcept { return desc_;    }
    [[nodiscard]] const std::string& ver()         const noexcept { return version_; }

    // ── Parse ─────────────────────────────────────────────────────────────

    [[nodiscard]] ParseResult parse(int argc, const char* const* argv) {
        reset();
        // Treat a null argv as an empty command line rather than constructing
        // a non-empty span over a null pointer.
        std::span<const char* const> raw;
        if (argv != nullptr && argc > 0)
            raw = std::span<const char* const>(argv, static_cast<size_t>(argc));
        // Skip argv[0] without copying the remaining pointers into a vector.
        return run_parse(raw.size() > 1 ? raw.subspan(1)
                                        : std::span<const char* const>{});
    }

    [[nodiscard]] ParseResult parse(std::span<const std::string_view> args) {
        reset();
        return run_parse(args);
    }

    [[nodiscard]] ParseResult parse(std::initializer_list<std::string_view> args) {
        reset();
        return run_parse(std::span<const std::string_view>(args.begin(), args.size()));
    }

    /// Convenience overload for callers that already own a vector of strings.
    template<typename Allocator>
    [[nodiscard]] ParseResult parse(const std::vector<std::string, Allocator>& args) {
        reset();
        std::vector<std::string_view> tokens;
        tokens.reserve(args.size());
        for (const auto& a : args) tokens.emplace_back(a);
        return run_parse(std::span<const std::string_view>(tokens));
    }

    void parse_or_exit(int argc, const char* const* argv) {
        auto r = parse(argc, argv);
        if (!r) {
            const auto& err = r.error();
            const App* target_app = err.context ? err.context : this;
            if (err.kind == ParseError::Kind::HelpRequested) {
                target_app->print_help();
                std::exit(0);
            }
            if (err.kind == ParseError::Kind::VersionRequested) {
                std::cout << std::format("{} {}\n", target_app->full_name(),
                                         target_app->effective_version());
                std::exit(0);
            }
            target_app->print_error(err);
            std::exit(1);
        }
        App* target = active_sub_;
        while (target && target->active_sub_) target = target->active_sub_;
        if (!target) target = this;
        // Use the deepest command that actually has a callback, walking up
        // through intermediate commands before falling back to the root.
        for (App* command = target; command != nullptr; command = command->parent_) {
            if (command->callback_) {
                command->callback_(*command);
                return;
            }
        }
    }

    // ── Help ──────────────────────────────────────────────────────────────

    /// Render the complete help screen as a string (useful for tests/logging).
    [[nodiscard]] std::string help_text() const {
        using X = detail::C;
        const auto full = full_name();
        const auto ver  = effective_version();

        std::string out;
        out += std::format("{}{}{}", X::bld(), full, X::rst());
        if (!ver.empty())
            out += std::format("  {}v{}{}", X::dim(), ver, X::rst());
        if (!desc_.empty())
            out += std::format("  —  {}", desc_);
        out += '\n';
        if (!author_.empty())
            out += std::format("{}  Author: {}{}\n", X::dim(), author_, X::rst());
        out += '\n';

        out += std::format("{}USAGE{}\n", X::byel(), X::rst());
        out += std::format("  {} [OPTIONS]", full);
        if (!subcommands_.empty())
            out += require_subcommand_ ? " <COMMAND>" : " [COMMAND]";
        for (const auto& pos : positionals_) {
            if (pos->variadic_)
                out += pos->required_
                    ? std::format(" <{}...>", pos->name)
                    : std::format(" [{}...]", pos->name);
            else if (pos->required_)
                out += std::format(" <{}>", pos->name);
            else
                out += std::format(" [{}]", pos->name);
        }
        out += "\n\n";

        out += std::format("{}OPTIONS{}\n", X::byel(), X::rst());
        render_option_list(out);
        out += '\n';

        if (!positionals_.empty()) {
            out += std::format("{}ARGUMENTS{}\n", X::byel(), X::rst());
            size_t pos_col = 0;
            for (const auto& pos : positionals_) {
                const size_t w = 3 + pos->name.size() + (pos->variadic_ ? 3 : 0);
                pos_col = std::max(pos_col, w);
            }
            pos_col = std::max(pos_col + 2, size_t(30));
            for (const auto& pos : positionals_) {
                const auto body = std::format("{}{}", pos->name, pos->variadic_ ? "..." : "");
                const std::string lhs = pos->required_
                    ? std::format("  <{}>", body)
                    : std::format("  [{}]", body);
                out += std::format("{}{:<{}}{}", X::bcyn(), lhs, pos_col, X::rst());
                out += pos->help_text;
                if (!pos->default_str().empty())
                    out += std::format("  {}[default: {}]{}", X::dim(), pos->default_str(), X::rst());
                if (!pos->choices_str().empty())
                    out += std::format("  {}[{}]{}", X::dim(), pos->choices_str(), X::rst());
                if (!pos->env_var.empty())
                    out += std::format("  {}[env: {}]{}", X::dim(), pos->env_var, X::rst());
                if (pos->required_)
                    out += std::format("  {}[required]{}", X::bred(), X::rst());
                out += '\n';
            }
            out += '\n';
        }

        if (!subcommands_.empty()) {
            out += std::format("{}COMMANDS{}\n", X::byel(), X::rst());
            size_t col = 4;
            for (const auto& s : subcommands_) {
                if (s->hidden_) continue;
                size_t w = s->name_.size();
                for (const auto& a : s->aliases_) w += 2 + a.size();
                col = std::max(col, w + 4);
            }
            for (const auto& s : subcommands_) {
                if (s->hidden_) continue;
                std::string lhs = "  " + s->name_;
                for (const auto& a : s->aliases_) lhs += ", " + a;
                out += std::format("{}{:<{}}{}", X::bcyn(), lhs, col, X::rst());
                out += std::format("{}\n", s->desc_);
            }
            out += '\n';
            out += std::format("{}  Run '{} <COMMAND> --help' for details on a command.{}\n\n",
                X::dim(), full, X::rst());
        }

        if (!examples_.empty()) {
            out += std::format("{}EXAMPLES{}\n", X::byel(), X::rst());
            for (const auto& ex : examples_)
                out += std::format("  {}{}{}\n", X::grn(), ex, X::rst());
            out += '\n';
        }

        return out;
    }

    void print_help() const {
        std::cout << help_text();
    }

    void print_help(std::ostream& out) const {
        out << help_text();
    }

    void print_error(const ParseError& e) const {
        using X = detail::C;
        const App* ctx = e.context ? e.context : this;
        std::cerr << std::format("{}error:{} {}\n", X::bred(), X::rst(), e.message);
        std::cerr << std::format("{}Try '{} --help' for usage information.{}\n",
            X::dim(), ctx->full_name(), X::rst());
    }

    // ── Shell completion generators ───────────────────────────────────────

    void bash_completion(std::ostream& out = std::cout) const {
        // Keep the generated function identifier valid even when the app name
        // is a path or otherwise not a shell identifier.
        auto fn = name_;
        for (auto& c : fn)
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';

        // Quote only when needed so the common `tool` case stays unquoted.
        auto shell_word = [](std::string_view s) {
            const bool safe = !s.empty() && std::ranges::all_of(s, [](char c) {
                const auto u = static_cast<unsigned char>(c);
                return std::isalnum(u) || c == '_' || c == '-' || c == '.'
                    || c == '/' || c == '+';
            });
            if (safe) return std::string(s);
            std::string quoted = "'";
            for (char c : s) {
                if (c == '\'') quoted += "'\\''";
                else           quoted += c;
            }
            quoted += '\'';
            return quoted;
        };
        auto dq_escape = [](std::string_view s) {
            std::string escaped;
            escaped.reserve(s.size());
            for (char c : s) {
                if (c == '\\' || c == '"' || c == '$' || c == '`') escaped += '\\';
                escaped += c;
            }
            return escaped;
        };

        std::string comment = name_;
        for (auto& c : comment)
            if (std::iscntrl(static_cast<unsigned char>(c))) c = ' ';

        out << "# Bash completion for " << comment << "\n";
        out << "_" << fn << "() {\n"
               "    local cur prev opts subs\n"
               "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
               "    opts=\"";
        for (const auto& opt : options_) {
            if (opt->hidden_) continue;
            for (const auto& n : opt->names) out << dq_escape(n) << " ";
            if (opt->negatable_) {
                auto ln = opt->long_name();
                if (ln.starts_with("--"))
                    out << dq_escape("--no-" + std::string(ln.substr(2))) << " ";
            }
        }
        if (!find_opt("-h"))      out << "-h ";
        if (!find_opt("--help"))  out << "--help ";
        if (!effective_version().empty()) {
            if (!find_opt("-V"))        out << "-V ";
            if (!find_opt("--version")) out << "--version ";
        }
        out << "\"\n    subs=\"";
        for (const auto& s : subcommands_) {
            if (s->hidden_) continue;
            out << dq_escape(s->name_) << " ";
            for (const auto& a : s->aliases_) out << dq_escape(a) << " ";
        }
        out << "\"\n"
               "    COMPREPLY=($(compgen -W \"${opts} ${subs}\" -- \"${cur}\"))\n"
               "}\n"
               "complete -F _" << fn << " " << shell_word(name_) << "\n";
    }

    void fish_completion(std::ostream& out = std::cout) const {
        auto fish_esc = [](std::string_view s) {
            std::string r;
            r.reserve(s.size());
            for (char c : s)
                r += (c == '\'') ? "'\"'\"'" : std::string(1, c);
            return r;
        };
        auto fish_word = [&](std::string_view s) {
            const bool safe = !s.empty() && std::ranges::all_of(s, [](char c) {
                const auto u = static_cast<unsigned char>(c);
                return std::isalnum(u) || c == '_' || c == '-' || c == '.'
                    || c == '/' || c == '+';
            });
            if (safe) return std::string(s);
            return std::string("'") + fish_esc(s) + "'";
        };

        for (const auto& opt : options_) {
            if (opt->hidden_) continue;
            out << "complete -c " << fish_word(name_);
            auto sn = opt->short_name();
            auto ln = opt->long_name();
            if (!sn.empty()) {
                // Fish distinguishes classic one-letter shorts (-v) from
                // old-style single-dash long options (-verbose).
                if (sn.size() == 2) out << " -s " << fish_word(sn.substr(1));
                else                out << " -o " << fish_word(sn.substr(1));
            }
            if (!ln.empty()) out << " -l " << fish_word(ln.substr(2));
            if (opt->is_flag_) out << " -f";
            else               out << " -r";
            if (!opt->choices_str().empty() && !opt->is_count_option()) {
                std::string choices;
                for (char c : opt->choices_str()) {
                    if (c == '|') choices += ' ';
                    else if (c != '"') choices += c;
                }
                out << " -a '" << fish_esc(choices) << "'";
            }
            if (!opt->help_text.empty())
                out << " -d '" << fish_esc(opt->help_text) << "'";
            out << "\n";

            if (opt->negatable_ && !ln.empty() && ln.starts_with("--")) {
                out << "complete -c " << fish_word(name_) << " -l "
                    << fish_word("no-" + std::string(ln.substr(2))) << " -f";
                out << " -d 'Disable --" << fish_esc(ln.substr(2)) << "'\n";
            }
        }
        if (!find_opt("-h"))
            out << "complete -c " << fish_word(name_) << " -s h -f -d 'Print help'\n";
        if (!find_opt("--help"))
            out << "complete -c " << fish_word(name_) << " -l help -f -d 'Print help'\n";
        if (!effective_version().empty()) {
            if (!find_opt("-V"))
                out << "complete -c " << fish_word(name_) << " -s V -f -d 'Print version'\n";
            if (!find_opt("--version"))
                out << "complete -c " << fish_word(name_) << " -l version -f -d 'Print version'\n";
        }

        for (const auto& s : subcommands_) {
            if (s->hidden_) continue;
            out << "complete -c " << fish_word(name_) << " -f -a '"
                << fish_esc(s->name_) << "'";
            if (!s->desc_.empty()) out << " -d '" << fish_esc(s->desc_) << "'";
            out << "\n";
            for (const auto& a : s->aliases_) {
                out << "complete -c " << fish_word(name_) << " -f -a '"
                    << fish_esc(a) << "'";
                if (!s->desc_.empty())
                    out << " -d '" << fish_esc(s->desc_) << " (→ "
                        << fish_esc(s->name_) << ")'";
                out << "\n";
            }
        }
    }

    void zsh_completion(std::ostream& out = std::cout) const {
        auto fn = name_;
        for (auto& c : fn) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';

        auto zsh_esc = [](std::string_view s) {
            std::string r;
            r.reserve(s.size());
            for (char c : s) {
                if (c == '\'') r += "'\\''";
                else if (c == '[' || c == ']' || c == ':') { r += '\\'; r += c; }
                else r += c;
            }
            return r;
        };

        std::string compdef_name = name_;
        for (auto& c : compdef_name)
            if (std::iscntrl(static_cast<unsigned char>(c))) c = '_';

        out << "#compdef " << compdef_name << "\n\n";
        out << "_" << fn << "() {\n";

        if (!subcommands_.empty()) {
            out << "    local context state state_descr line\n";
            out << "    typeset -A opt_args\n\n";
        }

        out << "    local -a args\n";
        out << "    args=(\n";

        auto choice_action = [&zsh_esc](const detail::OptionBase& opt) {
            if (opt.is_flag_ || opt.is_count_option()) return std::string{};
            const auto choices = opt.choices_str();
            if (choices.empty()) return std::string{};
            std::string action = "(";
            for (char c : choices) {
                if (c == '|') action += ' ';
                else if (c != '"') action += c;
            }
            action += ')';
            return zsh_esc(action);
        };

        auto emit_spec = [&](const detail::OptionBase& opt) {
            const auto sn = opt.short_name();
            const auto ln = opt.long_name();
            if (sn.empty() && ln.empty()) return;
            const auto sn_esc = zsh_esc(sn);
            const auto ln_esc = zsh_esc(ln);

            std::string spec;
            if (opt.is_multi()) spec += '*';
            if (!sn.empty() && !ln.empty()) {
                spec += "'(" + sn_esc + " " + ln_esc + ")'{"
                        + sn_esc + "," + ln_esc + "}";
                spec += "'";
            } else {
                spec += "'" + (ln.empty() ? sn_esc : ln_esc);
            }
            if (!opt.help_text.empty())
                spec += "[" + zsh_esc(opt.help_text) + "]";
            spec += "'";
            if (!opt.is_flag_) {
                const auto meta = opt.meta.empty() ? std::string("value") : zsh_esc(opt.meta);
                auto action = choice_action(opt);
                if (action.empty()) action = "_default";
                spec += ":" + meta + ":" + action;
            }
            out << "        " << spec << "\n";

            if (opt.negatable_ && !ln.empty() && ln.starts_with("--")) {
                out << "        '--no-" << zsh_esc(ln.substr(2)) << "[Disable --"
                    << zsh_esc(ln.substr(2)) << "]'\n";
            }
        };

        for (const auto& opt : options_) {
            if (opt->hidden_) continue;
            emit_spec(*opt);
        }
        if (!find_opt("-h") && !find_opt("--help")) {
            out << "        '(-h --help)'{-h,--help}'[Print help]'\n";
        } else {
            if (!find_opt("-h"))     out << "        '-h[Print help]'\n";
            if (!find_opt("--help")) out << "        '--help[Print help]'\n";
        }
        if (!effective_version().empty()) {
            if (!find_opt("-V") && !find_opt("--version")) {
                out << "        '(-V --version)'{-V,--version}'[Print version]'\n";
            } else {
                if (!find_opt("-V"))        out << "        '-V[Print version]'\n";
                if (!find_opt("--version")) out << "        '--version[Print version]'\n";
            }
        }
        out << "    )\n\n";

        if (!subcommands_.empty()) {
            out << "    local -a subcmds\n";
            out << "    subcmds=(\n";
            for (const auto& s : subcommands_) {
                if (s->hidden_) continue;
                out << "        '" << zsh_esc(s->name_) << ":" << zsh_esc(s->desc_) << "'\n";
                for (const auto& a : s->aliases_)
                    out << "        '" << zsh_esc(a) << ":" << zsh_esc(s->desc_) << " (alias)'\n";
            }
            out << "    )\n\n";
            out << "    _arguments -C $args[@] ':command:->command'\n";
            out << "    case $state in\n";
            out << "        command) _describe 'commands' subcmds ;;\n";
            out << "    esac\n";
        } else {
            out << "    _arguments $args[@]\n";
        }

        out << "}\n\n";
        out << "_" << fn << "\n";
    }

private:

    void adopt_option(std::unique_ptr<detail::OptionBase> impl,
                      std::initializer_list<std::string_view> names) {
        auto* p = impl.get();
        options_.push_back(std::move(impl));
        try {
            p->registry_ = &registry_;
            for (auto name : names)
                if (!name.empty()) detail::append_option_name(p, name);
        } catch (...) {
            // Registration is transactional: a bad or duplicate name must not
            // leave a half-registered option behind.
            registry_.remove(p);
            options_.pop_back();
            throw;
        }
    }

    void validate_group_references() {
        if (!groups_dirty_) return;
        auto resolve = [this](const std::vector<std::vector<std::string>>& groups,
                              std::vector<std::vector<detail::OptionBase*>>& resolved,
                              std::string_view kind) {
            resolved.clear();
            resolved.reserve(groups.size());
            for (const auto& group : groups) {
                std::vector<detail::OptionBase*> members;
                members.reserve(group.size());
                for (const auto& name : group) {
                    auto* opt = find_opt(name);
                    if (opt == nullptr)
                        throw std::invalid_argument(std::format(
                            "cli: {} references unknown option '{}'", kind, name));
                    if (std::ranges::contains(members, opt))
                        throw std::invalid_argument(std::format(
                            "cli: {} references the same option more than once ('{}')",
                            kind, name));
                    members.push_back(opt);
                }
                resolved.push_back(std::move(members));
            }
        };
        resolve(mutex_groups_, mutex_resolved_, "mutex_group");
        resolve(require_groups_, require_resolved_, "require_together");
        resolve(require_one_groups_, require_one_resolved_, "require_one_of");
        groups_dirty_ = false;
    }

    [[nodiscard]] App* root_app() noexcept {
        App* app = this;
        while (app->parent_ != nullptr) app = app->parent_;
        return app;
    }

    /// Load the root config file once per parse, honoring a CLI-provided path.
    [[nodiscard]] ParseResult ensure_config_loaded() {
        App* root = root_app();
        std::string path = root->config_file_;
        bool required    = root->config_required_;
        if (root->config_option_ != nullptr && root->config_option_->value) {
            path     = *root->config_option_->value;
            required = true;
        }
        if (path.empty()) {
            root->config_loaded_ = true;
            root->config_loaded_path_.clear();
            root->config_data_ = {};
            return {};
        }
        if (root->config_loaded_ && root->config_loaded_path_ == path)
            return {};

        root->config_loaded_      = true;
        root->config_loaded_path_ = path;
        root->config_data_        = {};

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return required
                ? std::unexpected(ParseError::config(path, "cannot open file", root))
                : ParseResult{};
        in.seekg(0, std::ios::end);
        const auto end = in.tellg();
        if (end < 0)
            return std::unexpected(ParseError::config(path, "cannot determine size", root));
        constexpr std::streamoff max_size = 4 * 1024 * 1024;
        if (end > max_size)
            return std::unexpected(ParseError::config(path, "file is larger than 4 MiB", root));
        in.seekg(0, std::ios::beg);
        std::string text(static_cast<size_t>(end), '\0');
        if (end > 0 && !in.read(text.data(), end))
            return std::unexpected(ParseError::config(path, "read failed", root));
        auto parsed = detail::parse_config(text);
        if (!parsed)
            return std::unexpected(ParseError::config(path, parsed.error(), root));
        root->config_data_ = std::move(*parsed);
        return {};
    }

    /// Full command path used as the config section name (`remote.add`).
    [[nodiscard]] std::string config_section_key() const {
        std::string path;
        for (const App* app = this; app->parent_ != nullptr; app = app->parent_) {
            if (!path.empty()) path.insert(0, 1, '.');
            path.insert(0, app->name_);
        }
        return path;
    }

    [[nodiscard]] ParseResult apply_config_option(
        detail::OptionBase* opt, const detail::ConfigData::Section& section) {
        if (opt->is_set_ || opt->from_env_ || opt->from_config_) return {};
        if (const auto* raw = detail::config_value_for(section, opt->names)) {
            auto values = opt->is_multi() ? detail::config_values(*raw)
                                          : std::vector<std::string>{ *raw };
            if (values.empty()) return {};
            for (const auto& v : values) {
                if (auto r = opt->parse(v, opt->display_name(), this); !r) return r;
            }
            opt->is_set_     = false;
            opt->from_config_ = true;
            return {};
        }
        // `no-flags = true` is accepted as a config-native way to negate a
        // negatable flag, even if --no-flags was not registered explicitly.
        if (opt->is_flag_ && opt->negatable_) {
            const auto ln = opt->long_name();
            if (ln.starts_with("--")) {
                std::string key = "no-";
                key.append(ln.substr(2));
                if (const auto it = section.find(key); it != section.end()) {
                    if (auto r = opt->set_negated(this); !r) return r;
                    opt->is_set_      = false;
                    opt->from_config_ = true;
                }
            }
        }
        return {};
    }

    [[nodiscard]] ParseResult apply_config_positional(
        detail::PositionalBase* pos, const detail::ConfigData::Section& section) {
        if (pos->is_set_ || pos->from_env_ || pos->from_config_) return {};
        const auto it = section.find(pos->name);
        if (it == section.end()) return {};
        auto values = pos->variadic_ ? detail::config_values(it->second)
                                     : std::vector<std::string>{ it->second };
        if (values.empty()) return {};
        for (const auto& v : values) {
            if (auto r = pos->parse(v, this); !r) return r;
        }
        pos->is_set_      = false;
        pos->from_config_ = true;
        return {};
    }

    template<typename Token>
    [[nodiscard]] static std::string_view token_text(const Token& token) noexcept {
        if constexpr (std::is_pointer_v<Token>)
            return token != nullptr ? std::string_view(token) : std::string_view{};
        else
            return std::string_view(token);
    }

    /// True when a token should stop a variable-length value run.  Negative
    /// numbers and a lone `-` remain values; `--` is always a boundary.
    [[nodiscard]] bool is_option_boundary(std::string_view t) const noexcept {
        if (t.size() < 2 || t.front() != '-') return false;
        if (t == "--") return true;
        if (detail::looks_like_negative_number(t)) return false;
        return true;
    }

    /// Consume the remaining tokens of a `.nargs()` occurrence.  `already`
    /// counts values supplied inline (for example `--files=a`).
    template<typename Token>
    [[nodiscard]] ParseResult consume_nargs(detail::OptionBase* opt, size_t& i,
                                            std::span<const Token> args,
                                            std::string_view opt_name,
                                            size_t already) {
        size_t count = already;
        while (count < opt->nargs_max_ && i + 1 < args.size()) {
            const auto next = token_text(args[i + 1]);
            if (next == "--") break;
            // With an exact arity, dash-prefixed tokens are values.  With a
            // range, stop as soon as the minimum is met and an option-looking
            // token appears, so `--files a b --verbose` does not swallow the
            // flag.
            if (opt->nargs_max_ != opt->nargs_min_
                && count >= opt->nargs_min_ && is_option_boundary(next))
                break;
            if (auto r = opt->parse(next, opt_name, this); !r) return r;
            ++i;
            ++count;
        }
        if (count < opt->nargs_min_)
            return std::unexpected(ParseError::missing_value(opt_name, this));
        return {};
    }

    /// Expand response files (when enabled) and dispatch to the parser.
    template<typename Token>
    [[nodiscard]] ParseResult run_parse(std::span<const Token> tokens) {
        if (!response_files_ || tokens.empty())
            return do_parse(tokens);
        std::vector<std::string_view> views;
        views.reserve(tokens.size());
        for (const auto& token : tokens)
            views.emplace_back(token_text(token));
        auto expanded = detail::expand_response_tokens(views, this);
        if (!expanded) return std::unexpected(std::move(expanded.error()));
        std::vector<std::string_view> expanded_views;
        expanded_views.reserve(expanded->size());
        for (const auto& token : *expanded)
            expanded_views.emplace_back(token);
        return do_parse(std::span<const std::string_view>(expanded_views));
    }

    template<typename Token>
    [[nodiscard]] ParseResult do_parse(std::span<const Token> args) {
        validate_group_references();
        bool   end_opts       = false;
        bool   saw_positional = false;
        size_t pos_idx        = 0;
        // Version lookup walks the parent chain; cache it for the token loop.
        const bool has_version = !effective_version().empty();
        const bool has_subcommands = !subcommands_.empty();

        for (size_t i = 0; i < args.size(); ++i) {
            const std::string_view arg = token_text(args[i]);

            // ── End-of-options sentinel
            if (!end_opts && arg == "--") {
                end_opts = true;
                continue;
            }

            // ── Built-in --help / -h (unless user registered their own -h/--help)
            if (!end_opts && (arg == "--help" || arg == "-h") && !find_opt(arg))
                return std::unexpected(ParseError::help_requested(this));

            // ── Built-in --version / -V (unless user registered their own -V/--version)
            if (!end_opts && has_version
                && (arg == "--version" || arg == "-V") && !find_opt(arg))
                return std::unexpected(ParseError::version_requested(this));

            // ── Subcommand dispatch (only before any positional argument)
            if (!end_opts && !saw_positional && has_subcommands
                && !arg.starts_with('-')) {
                if (auto* sub = find_command(arg)) {
                    active_sub_ = sub;
                    // Parse child first so `tool sub --help` succeeds even if parent has required options.
                    // Any child failure (including --help / --version) is returned as-is;
                    // finalizing the parent here would mask the real error.
                    auto sub_res = sub->do_parse(args.subspan(i + 1));
                    if (!sub_res) return sub_res;
                    if (auto r = finalize(/*subcommand_active=*/true); !r)
                        return r;
                    return {};
                }
            }

            // ── Positional argument (including single '-' stdin/stdout token)
            if (end_opts || !arg.starts_with('-') || arg == "-") {
                const bool first_positional = !saw_positional;
                saw_positional = true;
                if (pos_idx < positionals_.size()) {
                    auto& pos = positionals_[pos_idx];
                    if (auto r = pos->parse(arg, this); !r) return r;
                    if (!pos->variadic_) ++pos_idx;
                } else if (allow_extras_) {
                    remaining_args_.emplace_back(arg);
                } else if (has_subcommands && first_positional && !end_opts) {
                    return std::unexpected(ParseError::unknown_command(
                        arg, suggest_command(arg), this));
                } else {
                    return std::unexpected(ParseError::too_many(arg, this));
                }
                continue;
            }

            // ── Negative numeric literals: -1, -3.14, -.5
            if (detail::looks_like_negative_number(arg)) {
                const char short_name[2] = {'-', arg[1]};
                // A registered name that happens to look numeric (for example
                // a single-dash long option `-1abc`) still wins.  The same is
                // true for an inline spelling such as `-1abc=5`.
                const auto eq = arg.find('=');
                const bool has_registered_prefix =
                    eq != std::string_view::npos && find_opt(arg.substr(0, eq)) != nullptr;
                if (!find_opt(std::string_view(short_name, 2)) &&
                    !find_opt(arg) && !has_registered_prefix) {
                    const bool first_positional = !saw_positional;
                    saw_positional = true;
                    if (pos_idx < positionals_.size()) {
                        auto& pos = positionals_[pos_idx];
                        if (auto r = pos->parse(arg, this); !r) return r;
                        if (!pos->variadic_) ++pos_idx;
                    } else if (allow_extras_) {
                        remaining_args_.emplace_back(arg);
                    } else if (has_subcommands && first_positional) {
                        return std::unexpected(ParseError::unknown_command(
                            arg, suggest_command(arg), this));
                    } else {
                        return std::unexpected(ParseError::too_many(arg, this));
                    }
                    continue;
                }
            }

            // ── Long option: --name or --name=value
            if (arg.starts_with("--")) {
                const auto eq     = arg.find('=');
                const bool has_eq = (eq != std::string_view::npos);
                const auto name   = has_eq ? arg.substr(0, eq) : arg;
                const auto inlval = has_eq ? arg.substr(eq + 1) : std::string_view{};

                // An exact option name wins over the --no-FLAG interpretation.
                auto* opt = find_opt(name);
                if (!opt && !has_eq && name.starts_with("--no-")) {
                    // Build "--" + name.substr(5) on the stack for the common
                    // case; long names fall back to an owned string.
                    const auto positive = name.substr(5);
                    detail::OptionBase* nopt = nullptr;
                    if (positive.size() <= 126) {
                        char buf[128];
                        buf[0] = '-';
                        buf[1] = '-';
                        std::ranges::copy(positive, buf + 2);
                        nopt = find_opt(std::string_view(buf, positive.size() + 2));
                    } else {
                        std::string full;
                        full.reserve(positive.size() + 2);
                        full.append("--").append(positive);
                        nopt = find_opt(full);
                    }
                    if (nopt && nopt->is_flag_ && nopt->negatable_) {
                        if (auto r = nopt->set_negated(this); !r) return r;
                        continue;
                    }
                }
                if (!opt && allow_abbrev_) {
                    auto match = resolve_prefix(name);
                    if (match.option != nullptr) {
                        opt = match.option;
                    } else if (match.ambiguous) {
                        return std::unexpected(
                            ParseError::ambiguous(name, match.names, this));
                    }
                }
                if (!opt) {
                    if (allow_extras_) {
                        remaining_args_.emplace_back(arg);
                        continue;
                    }
                    return std::unexpected(ParseError::unknown(name, suggest(name), this));
                }

                if (opt->is_flag_) {
                    if (has_eq) {
                        if (opt->is_count_option())
                            return std::unexpected(ParseError::unexpected_value(name, inlval, this));
                        if (auto r = opt->parse(inlval, name, this); !r) return r;
                    } else {
                        if (auto r = opt->set_implicit(this); !r) return r;
                    }
                } else if (has_eq) {
                    if (auto r = opt->parse(inlval, name, this); !r) return r;
                    if (opt->nargs_max_ != 0) {
                        if (auto r = consume_nargs(opt, i, args, name, 1); !r) return r;
                    }
                } else {
                    if (++i >= args.size())
                        return std::unexpected(ParseError::missing_value(name, this));
                    if (auto r = opt->parse(token_text(args[i]), name, this); !r) return r;
                    if (opt->nargs_max_ != 0) {
                        if (auto r = consume_nargs(opt, i, args, name, 1); !r) return r;
                    }
                }
                continue;
            }

            // A registered single-dash name may carry an inline value, e.g.
            // `-verbose=5` or `-color=false`, just like its `--long` form.
            if (arg.size() > 2 && arg[1] != '-') {
                const auto eq = arg.find('=');
                if (eq != std::string_view::npos) {
                    const auto name = arg.substr(0, eq);
                    if (auto* exact = find_opt(name)) {
                        const auto inlval = arg.substr(eq + 1);
                        if (exact->is_count_option())
                            return std::unexpected(ParseError::unexpected_value(name, inlval, this));
                        if (auto r = exact->parse(inlval, name, this); !r) return r;
                        if (exact->nargs_max_ != 0) {
                            if (auto r = consume_nargs(exact, i, args, name, 1); !r) return r;
                        }
                        continue;
                    }
                }
            }

            // A registered single-dash name longer than one character (for
            // example `-verbose`) is matched as a whole token before falling
            // back to short-flag grouping.
            if (auto* exact = find_opt(arg)) {
                if (exact->is_flag_) {
                    if (auto r = exact->set_implicit(this); !r) return r;
                } else {
                    if (++i >= args.size())
                        return std::unexpected(ParseError::missing_value(arg, this));
                    if (auto r = exact->parse(token_text(args[i]), arg, this); !r) return r;
                    if (exact->nargs_max_ != 0) {
                        if (auto r = consume_nargs(exact, i, args, arg, 1); !r) return r;
                    }
                }
                continue;
            }

            // ── Short option(s): -v · -vxf · -n5 · -n 5
            for (size_t ch = 1; ch < arg.size(); ++ch) {
                const char short_name[2] = {'-', arg[ch]};
                const std::string_view sopt(short_name, 2);
                auto* opt = find_opt(sopt);
                if (!opt) {
                    if (sopt == "-h") return std::unexpected(ParseError::help_requested(this));
                    if (sopt == "-V" && has_version)
                        return std::unexpected(ParseError::version_requested(this));
                    if (allow_extras_) {
                        remaining_args_.emplace_back(arg);
                        break;
                    }
                    return std::unexpected(ParseError::unknown(sopt, suggest(sopt), this));
                }

                if (opt->is_flag_) {
                    // `-v=false` mirrors the long `--flag=false` spelling.
                    if (ch + 1 < arg.size() && arg[ch + 1] == '=') {
                        const auto inlval = arg.substr(ch + 2);
                        if (opt->is_count_option())
                            return std::unexpected(ParseError::unexpected_value(sopt, inlval, this));
                        if (auto r = opt->parse(inlval, sopt, this); !r) return r;
                        break;
                    }
                    if (auto r = opt->set_implicit(this); !r) return r;
                } else {
                    std::string_view val;
                    if (ch + 1 < arg.size()) {
                        val = arg.substr(ch + 1);
                        if (!val.empty() && val.front() == '=')
                            val.remove_prefix(1);
                    } else {
                        if (++i >= args.size())
                            return std::unexpected(ParseError::missing_value(sopt, this));
                        val = token_text(args[i]);
                    }
                    if (auto r = opt->parse(val, sopt, this); !r) return r;
                    if (opt->nargs_max_ != 0) {
                        if (auto r = consume_nargs(opt, i, args, sopt, 1); !r) return r;
                    }
                    break;
                }
            }
        }

        return finalize(/*subcommand_active=*/false);
    }

    [[nodiscard]] ParseResult finalize(bool subcommand_active) {
        // Load config lazily: by this point any --config CLI value has been
        // parsed, and child finalization can reach the root data.
        App* root = root_app();
        if (!root->config_file_.empty()
            || (root->config_option_ != nullptr && root->config_option_->value)) {
            if (auto r = root->ensure_config_loaded(); !r) return r;
        }
        const detail::ConfigData::Section* config_section = nullptr;
        if (!root->config_data_.sections.empty()) {
            const auto key = config_section_key();
            if (const auto it = root->config_data_.sections.find(key);
                it != root->config_data_.sections.end())
                config_section = &it->second;
        }

        // 1. Apply environment fallbacks, then config values, then defaults,
        //    then enforce required options.  These are independent per
        //    option, so a single pass replaces four traversals.
        for (auto& opt : options_) {
            if (opt->is_set_) continue;
            if (!opt->env_var.empty() && !opt->from_env_ && !opt->from_config_) {
                if (const char* v = std::getenv(opt->env_var.c_str())) {
                    if (auto r = opt->parse_env(v, this); !r) return r;
                }
            }
            if (!opt->from_env_ && config_section) {
                if (auto r = apply_config_option(opt.get(), *config_section); !r)
                    return r;
            }
            if (!opt->from_env_ && !opt->from_config_ && opt->has_default_)
                opt->apply_default();
            if (opt->required_ && !opt->from_env_ && !opt->from_config_
                && !opt->has_default_)
                return std::unexpected(ParseError::missing_required(opt->display_name(), this));
        }

        for (auto& pos : positionals_) {
            if (pos->is_set_) continue;
            if (!pos->env_var.empty() && !pos->from_env_ && !pos->from_config_) {
                if (const char* v = std::getenv(pos->env_var.c_str())) {
                    if (auto r = pos->parse_env(v, this); !r) return r;
                }
            }
            if (!pos->from_env_ && !pos->from_config_ && config_section) {
                if (auto r = apply_config_positional(pos.get(), *config_section); !r)
                    return r;
            }
            if (!pos->from_env_ && !pos->from_config_ && pos->has_default_)
                pos->apply_default();
        }
        if (!subcommand_active) {
            for (const auto& pos : positionals_) {
                // Match options: a configured default satisfies required() even
                // when required() is applied after default_val().
                if (pos->required_ && !pos->is_set_ && !pos->from_env_
                    && !pos->from_config_ && !pos->has_default_)
                    return std::unexpected(ParseError::missing_positional(pos->name, this));
            }
            if (require_subcommand_ && !subcommands_.empty())
                return std::unexpected(ParseError::missing_subcommand(full_name(), this));
        }

        // 2. Mutex group constraints.  Values supplied through the environment
        // or a config file count as explicit choices; defaults do not.
        for (const auto& group : mutex_resolved_) {
            std::string first_set;
            for (auto* opt : group) {
                if (opt->is_set_ || opt->from_env_ || opt->from_config_) {
                    if (!first_set.empty())
                        return std::unexpected(ParseError::mutually_exclusive(
                            first_set, opt->display_name(), this));
                    first_set = opt->display_name();
                }
            }
        }

        // 3. Require-together constraints
        for (const auto& group : require_resolved_) {
            std::string_view any_set;
            for (auto* opt : group) {
                if (opt->is_set_ || opt->from_env_ || opt->from_config_) {
                    any_set = opt->display_name();
                    break;
                }
            }
            if (!any_set.empty()) {
                for (auto* opt : group) {
                    if (!opt->is_set_ && !opt->from_env_ && !opt->from_config_)
                        return std::unexpected(ParseError::requires_option(
                            any_set, opt->display_name(), this));
                }
            }
        }

        // 3b. Require-at-least-one groups
        for (const auto& group : require_one_resolved_) {
            bool any_set = false;
            std::string names;
            for (auto* opt : group) {
                if (opt->is_set_ || opt->from_env_ || opt->from_config_) {
                    any_set = true;
                    break;
                }
            }
            if (any_set) continue;
            for (auto* opt : group) {
                if (!names.empty()) names += ", ";
                names += opt->display_name();
            }
            return std::unexpected(ParseError::missing_one_of(names, this));
        }

        // 4. Run whole-value validators and post-parse transforms
        for (auto& opt : options_)
            if (opt->needs_finalize_)
                if (auto r = opt->validate_and_finalize(this); !r) return r;
        for (auto& pos : positionals_)
            if (pos->needs_finalize_)
                if (auto r = pos->validate_and_finalize(this); !r) return r;

        return {};
    }

    [[nodiscard]] detail::OptionBase* find_opt(std::string_view n) const noexcept {
        return registry_.find(n);
    }

    struct PrefixMatch {
        detail::OptionBase* option = nullptr;
        std::string         names;
        bool                ambiguous = false;
    };

    /// Resolve an unambiguous `--long` prefix.  Only called on the error-ish
    /// path when an exact lookup failed and allow_abbrev() is enabled.
    [[nodiscard]] PrefixMatch resolve_prefix(std::string_view name) const {
        PrefixMatch match;
        if (name.size() <= 2 || !name.starts_with("--")) return match;
        std::vector<detail::OptionBase*> found;
        for (const auto& opt : options_) {
            for (const auto& n : opt->names) {
                if (n.size() > 2 && n.starts_with("--") && n.starts_with(name)) {
                    if (!std::ranges::contains(found, opt.get())) {
                        found.push_back(opt.get());
                        if (!match.names.empty()) match.names += ", ";
                        match.names += n;
                    }
                    break;
                }
            }
        }
        if (found.size() == 1)
            match.option = found.front();
        else if (found.size() > 1)
            match.ambiguous = true;
        return match;
    }

    [[nodiscard]] App* find_command(std::string_view n) const noexcept {
        const auto it = command_index_.find(n);
        return it == command_index_.end() ? nullptr : it->second;
    }

    [[nodiscard]] std::string suggest(std::string_view token) const {
        auto strip = [](std::string_view s) {
            while (!s.empty() && s[0] == '-') s.remove_prefix(1);
            return s;
        };
        const auto tok = strip(token);
        // Suggestions are a diagnostic aid, not a hot path.  Cap the input so
        // an adversarial token cannot force quadratic edit-distance work even
        // if the application registered an equally huge option name.
        if (tok.size() > 128) return {};
        // Avoid quadratic edit-distance work for absurdly long unknown tokens.
        // A candidate can only be within distance 3 when its length is close.
        auto distance = [tok](std::string_view candidate) -> size_t {
            const size_t diff = tok.size() > candidate.size()
                ? tok.size() - candidate.size()
                : candidate.size() - tok.size();
            if (diff > 3) return SIZE_MAX;
            return detail::edit_distance(tok, candidate);
        };
        std::string best;
        size_t best_d = SIZE_MAX;
        for (const auto& o : options_) {
            if (o->hidden_) continue;
            for (const auto& n : o->names) {
                const auto d = distance(strip(n));
                if (d < best_d && d <= 3) { best_d = d; best = n; }
            }
            if (o->negatable_) {
                auto ln = o->long_name();
                if (ln.starts_with("--")) {
                    auto neg = std::string("--no-") + std::string(ln.substr(2));
                    const auto d = distance(strip(neg));
                    if (d < best_d && d <= 3) { best_d = d; best = neg; }
                }
            }
        }
        return best;
    }

    [[nodiscard]] std::string suggest_command(std::string_view token) const {
        if (token.size() > 128) return {};
        auto distance = [token](std::string_view candidate) -> size_t {
            const size_t diff = token.size() > candidate.size()
                ? token.size() - candidate.size()
                : candidate.size() - token.size();
            if (diff > 3) return SIZE_MAX;
            return detail::edit_distance(token, candidate);
        };
        std::string best;
        size_t best_d = SIZE_MAX;
        for (const auto& sub : subcommands_) {
            if (sub->hidden_) continue;
            auto consider = [&](std::string_view name) {
                const auto d = distance(name);
                if (d < best_d && d <= 3) {
                    best_d = d;
                    best.assign(name);
                }
            };
            consider(sub->name_);
            for (const auto& alias : sub->aliases_) consider(alias);
        }
        return best;
    }

    [[nodiscard]] std::string_view effective_version() const noexcept {
        for (const App* p = this; p; p = p->parent_)
            if (!p->version_.empty()) return p->version_;
        return {};
    }

    [[nodiscard]] std::string full_name() const {
        return parent_ ? (parent_->full_name() + " " + name_) : name_;
    }

    void render_option_list(std::string& out) const {
        using X = detail::C;

        // Built-ins step aside name-by-name when the user registered a custom
        // option with the same spelling.
        const bool show_help_short = find_opt("-h") == nullptr;
        const bool show_help_long  = find_opt("--help") == nullptr;
        const bool has_version     = !effective_version().empty();
        const bool show_ver_short  = has_version && find_opt("-V") == nullptr;
        const bool show_ver_long   = has_version && find_opt("--version") == nullptr;

        std::vector<std::string> section_order;
        for (const auto& o : options_) {
            if (o->hidden_) continue;
            if (!std::ranges::contains(section_order, o->section_))
                section_order.push_back(o->section_);
        }
        const bool has_unnamed = std::ranges::any_of(options_, [](const auto& o) {
            return !o->hidden_ && o->section_.empty();
        });
        const bool has_builtin = show_help_short || show_help_long
                              || show_ver_short || show_ver_long;
        if ((has_unnamed || has_builtin)
            && !std::ranges::contains(section_order, std::string{}))
            section_order.push_back({});

        const bool has_named = std::ranges::any_of(section_order,
            [](const auto& s) { return !s.empty(); });

        size_t col = 0;
        auto measure = [&](std::string_view sn, std::string_view ln,
                           std::string_view meta, bool is_flag, bool neg,
                           bool spread) {
            size_t w = 2;
            if (!sn.empty()) w += sn.size();
            if (!sn.empty() && !ln.empty()) w += 2;
            if (!ln.empty()) {
                if (neg && ln.starts_with("--"))
                    w += 7 + (ln.size() - 2);
                else
                    w += ln.size();
            }
            if (!is_flag && !meta.empty()) w += 3 + meta.size() + (spread ? 3 : 0);
            col = std::max(col, w);
        };
        for (const auto& o : options_)
            if (!o->hidden_)
                measure(o->short_name(), o->long_name(), o->meta, o->is_flag_,
                        o->negatable_, o->nargs_max_ != 0);
        if (show_help_short || show_help_long)
            measure(show_help_short ? "-h" : "", show_help_long ? "--help" : "",
                    {}, true, false, false);
        if (show_ver_short || show_ver_long)
            measure(show_ver_short ? "-V" : "", show_ver_long ? "--version" : "",
                    {}, true, false, false);
        col = std::max(col + 2, size_t(30));

        auto row = [&](std::string_view sn, std::string_view ln, std::string_view meta,
                       bool is_flag, bool neg, bool spread, std::string_view help, bool req,
                       std::string_view def, std::string_view choices_s,
                       std::string_view env_v) {
            std::string lhs = "  ";
            if (!sn.empty()) lhs += sn;
            if (!sn.empty() && !ln.empty()) lhs += ", ";
            if (!ln.empty()) {
                if (neg && ln.starts_with("--"))
                    lhs += "--[no-]" + std::string(ln.substr(2));
                else
                    lhs += ln;
            }
            if (!is_flag && !meta.empty()) {
                lhs += std::format(" <{}>", meta);
                if (spread) lhs += "...";
            }
            out += std::format("{}{:<{}}{}", X::bcyn(), lhs, col, X::rst());
            out += help;
            if (req)                 out += std::format("  {}[required]{}", X::bred(), X::rst());
            if (!def.empty())        out += std::format("  {}[default: {}]{}", X::dim(), def, X::rst());
            if (!choices_s.empty())  out += std::format("  {}[{}]{}", X::dim(), choices_s, X::rst());
            if (!env_v.empty())      out += std::format("  {}[env: {}]{}", X::dim(), env_v, X::rst());
            out += '\n';
        };

        for (size_t si = 0; si < section_order.size(); ++si) {
            const auto& sec = section_order[si];
            if (si > 0) out += '\n';
            if (has_named) {
                if (!sec.empty())
                    out += std::format("{}  {}{}:\n", X::bld(), sec, X::rst());
                else
                    out += std::format("{}  Other Options{}:\n", X::bld(), X::rst());
            }
            for (const auto& o : options_) {
                if (o->hidden_ || o->section_ != sec) continue;
                row(o->short_name(), o->long_name(), o->meta,
                    o->is_flag_, o->negatable_, o->nargs_max_ != 0, o->help_text,
                    o->required_, o->default_str(), o->choices_str(), o->env_var);
            }
            if (sec.empty()) {
                if (show_help_short || show_help_long)
                    row(show_help_short ? "-h" : "",
                        show_help_long ? "--help" : "",
                        {}, true, false, false, "Print help and exit", false, {}, {}, {});
                if (show_ver_short || show_ver_long)
                    row(show_ver_short ? "-V" : "",
                        show_ver_long ? "--version" : "",
                        {}, true, false, false, "Print version and exit", false, {}, {}, {});
            }
        }
    }
};

} // namespace cli