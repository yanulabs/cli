#pragma once

// ════════════════════════════════════════════════════════════════════════════
//  cli.hpp  ·  A modern C++23 command-line argument parser
//  ─────────────────────────────────────────────────────────────────────────
//  FEATURES
//  ────────
//  · Single-header, zero dependencies, cross-platform
//  · std::expected<void, ParseError> — composable, propagating errors
//  · Type-safe Option<T>, Flag, Positional<T> handles
//  · Fluent builder API via C++23 deducing-this (no CRTP boilerplate)
//  · Types out-of-the-box: bool · integrals · floats · string · path · vector
//  · Constraints: .range() · .choices() · .validate(fn, desc)
//  · Per-option env-var fallback via .env("VAR")
//  · Short-flag grouping   -vxf  ≡  -v -x -f
//  · Inline short value    -n42  ≡  -n 42
//  · Inline long value   --out=file  ≡  --out file
//  · End-of-options marker  --
//  · Subcommands with independent option sets and recursive help
//  · Auto-aligned, ANSI-colored, section-structured help output
//  · Levenshtein "did you mean?" suggestions on unknown options
//  · Bash / Fish shell completion generators
//
//  QUICK START
//  ───────────
//  #include "cli.hpp"
//  namespace fs = std::filesystem;
//
//  int main(int argc, char** argv) {
//      cli::App app("tool", "My blazing-fast tool");
//      app.version("1.0.0").author("Yanujz Labs")
//         .example("tool build --target release -j8");
//
//      auto verbose = app.flag("-v", "--verbose", "Verbose output");
//      auto jobs    = app.option<unsigned>("-j", "--jobs",  "Parallel jobs")
//                        .default_val(4u).range(1u, 256u);
//      auto output  = app.option<fs::path>("-o", "--output", "Output path")
//                        .required().meta("DIR");
//      auto tags    = app.option<std::vector<std::string>>("--tag", "Tags");
//
//      auto& build  = app.subcommand("build", "Compile sources");
//      auto  target = build.option<std::string>("-t","--target","Profile")
//                         .choices({"debug","release"}).default_val("debug");
//
//      auto result = app.parse(argc, argv);
//      if (!result) { app.print_error(result.error()); return 1; }
//
//      if (*verbose) { /* ... */ }
//      std::println("jobs={} out={}", *jobs, output->string());
//  }
//
//  REQUIREMENTS
//  ────────────
//  · C++23 (GCC ≥ 13, Clang ≥ 17, MSVC 19.35+)
//  · std::from_chars for float: GCC ≥ 11, Clang ≥ 14, MSVC 2019 16.4+
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  define CLI_ISATTY(fd) ::_isatty(fd)
#else
#  include <unistd.h>
#  define CLI_ISATTY(fd) ::isatty(fd)
#endif

namespace cli {

// ────────────────────────────────────────────────────────────────────────────
// §1  ParseError — structured, descriptive error type
// ────────────────────────────────────────────────────────────────────────────

struct ParseError {
    enum class Kind : uint8_t {
        UnknownOption,
        MissingValue,
        InvalidValue,
        MissingRequired,
        TooManyPositionals,
        ValidationFailed,
        OutOfRange,
        BadChoice,
    };

    Kind        kind;
    std::string message;

    // Named constructors for every error scenario
    static ParseError unknown(std::string_view opt, std::string_view sug = {}) {
        if (sug.empty())
            return { Kind::UnknownOption, std::format("unknown option '{}'", opt) };
        return { Kind::UnknownOption,
                 std::format("unknown option '{}' — did you mean '{}'?", opt, sug) };
    }
    static ParseError missing_value(std::string_view opt) {
        return { Kind::MissingValue,
                 std::format("option '{}' requires a value", opt) };
    }
    static ParseError invalid(std::string_view opt, std::string_view val,
                              std::string_view why) {
        return { Kind::InvalidValue,
                 std::format("invalid value '{}' for '{}': {}", val, opt, why) };
    }
    static ParseError missing_required(std::string_view name) {
        return { Kind::MissingRequired,
                 std::format("required option '{}' was not provided", name) };
    }
    static ParseError missing_positional(std::string_view name) {
        return { Kind::MissingRequired,
                 std::format("required argument <{}> was not provided", name) };
    }
    static ParseError out_of_range(std::string_view opt, std::string_view val,
                                   std::string_view lo, std::string_view hi) {
        return { Kind::OutOfRange,
                 std::format("value '{}' for '{}' is out of range [{}, {}]",
                             val, opt, lo, hi) };
    }
    static ParseError bad_choice(std::string_view opt, std::string_view val,
                                 std::string_view choices) {
        return { Kind::BadChoice,
                 std::format("invalid choice '{}' for '{}' — valid: {}",
                             val, opt, choices) };
    }
    static ParseError validation(std::string_view opt, std::string_view desc) {
        return { Kind::ValidationFailed,
                 std::format("validation failed for '{}': {}", opt, desc) };
    }
    static ParseError too_many(std::string_view arg) {
        return { Kind::TooManyPositionals,
                 std::format("unexpected argument '{}'", arg) };
    }
};

using ParseResult = std::expected<void, ParseError>;

// ────────────────────────────────────────────────────────────────────────────
// §2  Internal helpers — ANSI, Levenshtein, type traits, from_string
// ────────────────────────────────────────────────────────────────────────────

namespace detail {

// ── Terminal color ─────────────────────────────────────────────────────────

inline bool color_enabled() noexcept {
    static const bool v = CLI_ISATTY(1) != 0;
    return v;
}

// Thin helpers so call sites stay readable
struct C {
    static std::string_view rst()  { return color_enabled() ? "\033[0m"    : ""; }
    static std::string_view bld()  { return color_enabled() ? "\033[1m"    : ""; }
    static std::string_view dim()  { return color_enabled() ? "\033[2m"    : ""; }
    static std::string_view cyn()  { return color_enabled() ? "\033[36m"   : ""; }
    static std::string_view yel()  { return color_enabled() ? "\033[33m"   : ""; }
    static std::string_view grn()  { return color_enabled() ? "\033[32m"   : ""; }
    static std::string_view red()  { return color_enabled() ? "\033[31m"   : ""; }
    static std::string_view bcyn() { return color_enabled() ? "\033[1;36m" : ""; }
    static std::string_view byel() { return color_enabled() ? "\033[1;33m" : ""; }
    static std::string_view bred() { return color_enabled() ? "\033[1;31m" : ""; }
    static std::string_view bgrn() { return color_enabled() ? "\033[1;32m" : ""; }
};

// ── Levenshtein distance (for "did you mean?" suggestions) ────────────────

inline size_t edit_distance(std::string_view a, std::string_view b) noexcept {
    const size_t m = a.size(), n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<size_t> row(n + 1);
    std::iota(row.begin(), row.end(), size_t{0});
    for (size_t i = 1; i <= m; ++i) {
        size_t prev = i;
        for (size_t j = 1; j <= n; ++j) {
            const size_t tmp = row[j];
            row[j] = (a[i-1] == b[j-1])
                ? row[j-1]
                : 1 + std::min({row[j], row[j-1], prev});
            prev = tmp;
        }
    }
    return row[n];
}

// ── Type traits ────────────────────────────────────────────────────────────

template<typename T> struct is_vec                   : std::false_type {};
template<typename T> struct is_vec<std::vector<T>>   : std::true_type  {};
template<typename T> inline constexpr bool is_vec_v  = is_vec<T>::value;

template<typename T> struct elem_of              { using type = T; };
template<typename T> struct elem_of<std::vector<T>> { using type = T; };
template<typename T> using  elem_of_t = typename elem_of<T>::type;

// ── Human-readable type name used in <META> placeholders ──────────────────

template<typename T>
consteval std::string_view type_meta() noexcept {
    using U = elem_of_t<T>;
    if constexpr (std::is_same_v<U, std::string>)                return "TEXT";
    else if constexpr (std::is_same_v<U, std::filesystem::path>) return "PATH";
    else if constexpr (std::is_same_v<U, bool>)                  return "BOOL";
    else if constexpr (std::is_floating_point_v<U>)              return "FLOAT";
    else if constexpr (std::is_unsigned_v<U>)                    return "UINT";
    else if constexpr (std::is_integral_v<U>)                    return "INT";
    else                                                          return "VALUE";
}

// ── to_display: T → human-readable string ─────────────────────────────────

template<typename T>
[[nodiscard]] std::string to_display(const T& v) {
    if constexpr (std::is_same_v<T, std::string>)
        return std::format("\"{}\"", v);
    else if constexpr (std::is_same_v<T, std::filesystem::path>)
        return v.string();
    else if constexpr (std::is_same_v<T, bool>)
        return v ? "true" : "false";
    else if constexpr (std::is_arithmetic_v<T>)
        return std::format("{}", v);
    else
        return "?";
}

// ── from_string<T>: std::string_view → std::expected<T, std::string> ──────
// Single-function, if-constexpr dispatch. No specializations needed.

template<typename T>
[[nodiscard]] auto from_string(std::string_view sv) -> std::expected<T, std::string> {

    if constexpr (std::is_same_v<T, std::string>) {
        return std::string(sv);

    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        return std::filesystem::path(sv);

    } else if constexpr (std::is_same_v<T, bool>) {
        std::string lo(sv);
        std::ranges::transform(lo, lo.begin(),
            [](unsigned char c){ return (char)std::tolower(c); });
        if (lo == "true"  || lo == "1" || lo == "yes" || lo == "on")  return true;
        if (lo == "false" || lo == "0" || lo == "no"  || lo == "off") return false;
        return std::unexpected(
            std::format("'{}' is not a valid boolean — use true/false/1/0/yes/no", sv));

    } else if constexpr (std::is_integral_v<T>) {
        T val{};
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        if (ec != std::errc{} || p != sv.data() + sv.size())
            return std::unexpected(std::format("'{}' is not a valid integer", sv));
        return val;

    } else if constexpr (std::is_floating_point_v<T>) {
        T val{};
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        if (ec != std::errc{} || p != sv.data() + sv.size())
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        return val;
#else
        // Fallback for older stdlibs (from_chars for float not universally supported)
        try {
            size_t idx = 0;
            if constexpr (std::is_same_v<T, float>)
                val = std::stof(std::string(sv), &idx);
            else if constexpr (std::is_same_v<T, double>)
                val = std::stod(std::string(sv), &idx);
            else
                val = std::stold(std::string(sv), &idx);
            if (idx != sv.size())
                return std::unexpected(std::format("'{}' is not a valid number", sv));
            return val;
        } catch (...) {
            return std::unexpected(std::format("'{}' is not a valid number", sv));
        }
#endif
    } else {
        // Extensibility hook: if your type defines
        //   std::expected<T, std::string> cli_parse(std::string_view)
        // in its namespace, ADL will find it here.
        return std::unexpected(
            std::format("no parser registered for this type; value was '{}'", sv));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// §3  OptionBase — internal polymorphic option node
// ────────────────────────────────────────────────────────────────────────────

struct OptionBase {
    std::vector<std::string> names;      // e.g. ["-v", "--verbose"]
    std::string              help_text;
    std::string              meta;       // placeholder shown as <META>
    std::string              env_var;
    bool                     required_  = false;
    bool                     is_flag_   = false;
    bool                     is_set_    = false;
    bool                     hidden_    = false;

    virtual ~OptionBase()              = default;
    virtual ParseResult parse(std::string_view val, std::string_view opt_name) = 0;
    virtual bool        has_default()  const = 0;
    virtual void        apply_default()      = 0;
    virtual std::string default_str()  const = 0;
    virtual std::string choices_str()  const = 0;
    virtual bool        is_multi()     const = 0;

    [[nodiscard]] bool matches(std::string_view t) const noexcept {
        return std::ranges::any_of(names, [t](const auto& n){ return n == t; });
    }
    [[nodiscard]] std::string_view short_name() const noexcept {
        for (auto& n : names)
            if (n.size() >= 2 && n[0] == '-' && n[1] != '-') return n;
        return {};
    }
    [[nodiscard]] std::string_view long_name() const noexcept {
        for (auto& n : names)
            if (n.size() >= 3 && n[0] == '-' && n[1] == '-') return n;
        return {};
    }
    [[nodiscard]] std::string_view display_name() const noexcept {
        auto ln = long_name();
        return ln.empty() ? short_name() : ln;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §4  OptionImpl<T> — typed implementation of OptionBase
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
struct OptionImpl final : OptionBase {
    using E = elem_of_t<T>;             // element type (= T when T is not vector)

    std::optional<T>              value;
    std::optional<T>              default_;
    std::optional<std::pair<E,E>> range_;
    std::optional<std::vector<E>> choices_;
    std::function<bool(const T&)> validator_;
    std::string                   validator_desc;

    ParseResult parse(std::string_view sv, std::string_view opt) override {
        if constexpr (is_vec_v<T>) {
            // Multi-value: accumulate each token into the vector
            auto e = from_string<E>(sv);
            if (!e) return std::unexpected(ParseError::invalid(opt, sv, e.error()));
            if (range_ && (*e < range_->first || *e > range_->second))
                return std::unexpected(ParseError::out_of_range(
                    opt, sv, to_display(range_->first), to_display(range_->second)));
            if (choices_ && !std::ranges::contains(*choices_, *e))
                return std::unexpected(ParseError::bad_choice(opt, sv, fmt_choices()));
            if (!value) value = T{};
            value->push_back(std::move(*e));
        } else {
            auto r = from_string<T>(sv);
            if (!r) return std::unexpected(ParseError::invalid(opt, sv, r.error()));
            if (range_ && (*r < range_->first || *r > range_->second))
                return std::unexpected(ParseError::out_of_range(
                    opt, sv, to_display(range_->first), to_display(range_->second)));
            if (choices_ && !std::ranges::contains(*choices_, *r))
                return std::unexpected(ParseError::bad_choice(opt, sv, fmt_choices()));
            value = std::move(*r);
        }
        is_set_ = true;
        if (validator_ && !validator_(*value))
            return std::unexpected(ParseError::validation(opt, validator_desc));
        return {};
    }

    bool        has_default()  const override { return default_.has_value(); }
    void        apply_default()       override { if (default_) value = *default_; }
    bool        is_multi()     const override { return is_vec_v<T>; }

    std::string default_str() const override {
        if (!default_) return {};
        if constexpr (is_vec_v<T>) {
            std::string s;
            for (auto& e : *default_) { if (!s.empty()) s += ", "; s += to_display(e); }
            return "[" + s + "]";
        } else {
            return to_display(*default_);
        }
    }

    std::string choices_str() const override { return fmt_choices(); }

private:
    [[nodiscard]] std::string fmt_choices() const {
        if (!choices_) return {};
        std::string s;
        for (const auto c : *choices_) { if (!s.empty()) s += "|"; s += to_display(c); }
        return s;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// §5  PositionalBase / PositionalImpl<T>
// ────────────────────────────────────────────────────────────────────────────

struct PositionalBase {
    std::string name;
    std::string help_text;
    bool        required_ = true;
    bool        is_set_   = false;
    bool        variadic_ = false;   // true when T is vector or user sets it

    virtual ~PositionalBase()                  = default;
    virtual ParseResult parse(std::string_view v) = 0;
    virtual bool        has_default()  const      = 0;
    virtual void        apply_default()            = 0;
    virtual std::string default_str()  const      = 0;
};

template<typename T>
struct PositionalImpl final : PositionalBase {
    using E = elem_of_t<T>;

    std::optional<T> value;
    std::optional<T> default_;

    ParseResult parse(std::string_view sv) override {
        if constexpr (is_vec_v<T>) {
            auto e = from_string<E>(sv);
            if (!e) return std::unexpected(ParseError{
                ParseError::Kind::InvalidValue,
                std::format("invalid value '{}' for <{}>: {}", sv, name, e.error()) });
            if (!value) value = T{};
            value->push_back(std::move(*e));
        } else {
            auto r = from_string<T>(sv);
            if (!r) return std::unexpected(ParseError{
                ParseError::Kind::InvalidValue,
                std::format("invalid value '{}' for <{}>: {}", sv, name, r.error()) });
            value = std::move(*r);
        }
        is_set_ = true;
        return {};
    }

    bool        has_default()  const override { return default_.has_value(); }
    void        apply_default()       override { if (default_) value = *default_; }
    std::string default_str()  const override {
        if (!default_) return {};
        if constexpr (is_vec_v<T>) {
            std::string s;
            for (auto& e : *default_) { if (!s.empty()) s += ", "; s += to_display(e); }
            return s;
        } else {
            return to_display(*default_);
        }
    }
};

} // namespace detail

// ────────────────────────────────────────────────────────────────────────────
// §6  Option<T>  — the user-facing typed option handle
//
//  Provides a rich fluent builder and ergonomic value access.
//  Internally it is a pointer wrapper around detail::OptionImpl<T>.
//  The App owns the OptionImpl; Option<T> is cheap to copy.
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
class [[nodiscard]] Option {
    detail::OptionImpl<T>* p_;
    friend class App;
public:
    explicit Option(detail::OptionImpl<T>* p) noexcept : p_(p) {}

    // ── Fluent builder  ───────────────────────────────────────────────────
    //  Each method returns *this by reference (deducing-this so subclasses
    //  are handled transparently — no CRTP needed).

    /// Set a default value (implies the option is optional)
    auto&& default_val(this auto&& self, T v)
        { self.p_->default_ = std::move(v); return std::forward<decltype(self)>(self); }

    /// Mark the option as mandatory
    auto&& required(this auto&& self, bool r = true)
        { self.p_->required_ = r; return std::forward<decltype(self)>(self); }

    /// Hide from help output (useful for internal/debug options)
    auto&& hidden(this auto&& self, bool h = true)
        { self.p_->hidden_ = h; return std::forward<decltype(self)>(self); }

    /// Fall back to an environment variable when the flag is absent
    auto&& env(this auto&& self, std::string_view var)
        { self.p_->env_var = std::string(var); return std::forward<decltype(self)>(self); }

    /// Override the <META> placeholder shown in help (e.g. "DIR", "URL")
    auto&& meta(this auto&& self, std::string_view m)
        { self.p_->meta = std::string(m); return std::forward<decltype(self)>(self); }

    /// Reject values outside [lo, hi] (inclusive)
    auto&& range(this auto&& self, detail::elem_of_t<T> lo, detail::elem_of_t<T> hi)
        { self.p_->range_ = { std::move(lo), std::move(hi) }; return std::forward<decltype(self)>(self); }

    /// Restrict to an explicit set of allowed values
    auto&& choices(this auto&& self, std::initializer_list<detail::elem_of_t<T>> c)
        { self.p_->choices_ = std::vector<detail::elem_of_t<T>>(c); return std::forward<decltype(self)>(self); }

    /// Attach an arbitrary predicate validator
    auto&& validate(this auto&& self,
                    std::function<bool(const T&)> fn,
                    std::string desc = "custom validation failed") {
        self.p_->validator_      = std::move(fn);
        self.p_->validator_desc  = std::move(desc);
        return std::forward<decltype(self)>(self);
    }

    // ── Value access ──────────────────────────────────────────────────────

    /// Direct access to the underlying optional — inspect or modify freely
    [[nodiscard]] std::optional<T>&       get()       noexcept { return p_->value; }
    [[nodiscard]] const std::optional<T>& get() const noexcept { return p_->value; }

    /// Dereference — UB if the option was not set and has no default; check bool() first
    [[nodiscard]] const T&  operator*()  const { return *p_->value; }
    [[nodiscard]] const T*  operator->() const { return &*p_->value; }

    /// True if the option has a value (was set or has a default that was applied)
    [[nodiscard]] explicit operator bool() const noexcept { return p_->value.has_value(); }

    /// True only if the option was explicitly passed on the command line
    [[nodiscard]] bool is_set()   const noexcept { return p_->is_set_; }

    /// Safely return the value or a fallback
    [[nodiscard]] T    value_or(T fallback) const
        { return p_->value.value_or(std::move(fallback)); }
};

// ────────────────────────────────────────────────────────────────────────────
// §7  Flag — boolean switch with minimal footprint
// ────────────────────────────────────────────────────────────────────────────

class [[nodiscard]] Flag {
    detail::OptionImpl<bool>* p_;
    friend class App;
public:
    explicit Flag(detail::OptionImpl<bool>* p) noexcept : p_(p) {}

    auto&& hidden(this auto&& self, bool h = true) { self.p_->hidden_ = h; return std::forward<decltype(self)>(self); }
    auto&& env   (this auto&& self, std::string_view v)
        { self.p_->env_var = std::string(v); return std::forward<decltype(self)>(self); }

    /// True when the flag was passed on the command line
    [[nodiscard]] bool operator*()              const noexcept { return p_->value.value_or(false); }
    [[nodiscard]] explicit operator bool()      const noexcept { return p_->value.value_or(false); }
    [[nodiscard]] bool is_set()                 const noexcept { return p_->is_set_; }
};

// ────────────────────────────────────────────────────────────────────────────
// §8  Positional<T> — positional (non-option) argument handle
// ────────────────────────────────────────────────────────────────────────────

template<typename T>
class [[nodiscard]] Positional {
    detail::PositionalImpl<T>* p_;
    friend class App;
public:
    explicit Positional(detail::PositionalImpl<T>* p) noexcept : p_(p) {}

    auto&& default_val(this auto&& self, T v) {
        self.p_->default_  = std::move(v);
        self.p_->required_ = false;
        return std::forward<decltype(self)>(self);
    }
    auto&& required(this auto&& self, bool r = true)
        { self.p_->required_ = r; return std::forward<decltype(self)>(self); }

    [[nodiscard]] std::optional<T>&       get()       noexcept { return p_->value; }
    [[nodiscard]] const std::optional<T>& get() const noexcept { return p_->value; }
    [[nodiscard]] const T&  operator*()  const { return *p_->value; }
    [[nodiscard]] const T*  operator->() const { return &*p_->value; }
    [[nodiscard]] explicit operator bool() const noexcept { return p_->value.has_value(); }
    [[nodiscard]] bool is_set()    const noexcept { return p_->is_set_; }
    [[nodiscard]] T    value_or(T fallback) const
        { return p_->value.value_or(std::move(fallback)); }
};

// ────────────────────────────────────────────────────────────────────────────
// §9  App — the main parser, subcommand node, and help renderer
// ────────────────────────────────────────────────────────────────────────────

class App {
    std::string name_, desc_, version_, author_;
    std::vector<std::string>                                examples_;
    std::vector<std::unique_ptr<detail::OptionBase>>        options_;
    std::vector<std::unique_ptr<detail::PositionalBase>>    positionals_;
    std::vector<std::unique_ptr<App>>                       subcommands_;
    App*  parent_     = nullptr;
    App*  active_sub_ = nullptr;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

public:
    explicit App(std::string name, std::string desc = {})
        : name_(std::move(name)), desc_(std::move(desc)) {}
    App(App&&) = default;

    // ── App metadata builder ──────────────────────────────────────────────

    auto&& version(this auto&& self, std::string v)  { self.version_ = std::move(v); return std::forward<decltype(self)>(self); }
    auto&& author (this auto&& self, std::string a)  { self.author_  = std::move(a); return std::forward<decltype(self)>(self); }

    /// Add an example to the help EXAMPLES section
    auto&& example(this auto&& self, std::string ex)
        { self.examples_.push_back(std::move(ex)); return std::forward<decltype(self)>(self); }

    // ── Option registration ───────────────────────────────────────────────

    /// Register a typed option with optional short and long names
    template<typename T>
    [[nodiscard]] Option<T> option(std::string_view sn, std::string_view ln,
                                    std::string_view help) {
        auto impl = std::make_unique<detail::OptionImpl<T>>();
        if (!sn.empty()) impl->names.emplace_back(sn);
        if (!ln.empty()) impl->names.emplace_back(ln);
        impl->help_text  = std::string(help);
        impl->meta       = std::string(detail::type_meta<T>());
        auto* p          = impl.get();
        options_.push_back(std::move(impl));
        return Option<T>(p);
    }

    /// Convenience overload with long name only
    template<typename T>
    [[nodiscard]] Option<T> option(std::string_view ln, std::string_view help)
        { return option<T>({}, ln, help); }

    /// Register a boolean flag (presence = true, absence = false)
    [[nodiscard]] Flag flag(std::string_view sn, std::string_view ln, std::string_view help) {
        auto impl        = std::make_unique<detail::OptionImpl<bool>>();
        if (!sn.empty()) impl->names.emplace_back(sn);
        if (!ln.empty()) impl->names.emplace_back(ln);
        impl->help_text  = std::string(help);
        impl->is_flag_   = true;
        auto* p          = impl.get();
        options_.push_back(std::move(impl));
        return Flag(p);
    }

    /// Convenience overload with long name only
    [[nodiscard]] Flag flag(std::string_view ln, std::string_view help)
        { return flag({}, ln, help); }

    /// Register a positional argument.  Use T = std::vector<U> for variadic.
    template<typename T>
    [[nodiscard]] Positional<T> positional(std::string_view name, std::string_view help) {
        auto impl       = std::make_unique<detail::PositionalImpl<T>>();
        impl->name      = std::string(name);
        impl->help_text = std::string(help);
        impl->variadic_ = detail::is_vec_v<T>;
        auto* p         = impl.get();
        positionals_.push_back(std::move(impl));
        return Positional<T>(p);
    }

    // ── Subcommand registration ───────────────────────────────────────────

    /// Create and return a reference to a nested subcommand App
    [[nodiscard]] App& subcommand(std::string name, std::string desc = {}) {
        auto sub     = std::make_unique<App>(std::move(name), std::move(desc));
        sub->parent_ = this;
        auto* p      = sub.get();
        subcommands_.push_back(std::move(sub));
        return *p;
    }

    /// Returns the subcommand that was selected, or nullptr
    [[nodiscard]] App* active_subcommand() const noexcept { return active_sub_; }

    /// Check if a particular subcommand was selected by name
    [[nodiscard]] bool is_subcommand(std::string_view n) const noexcept
        { return active_sub_ && active_sub_->name_ == n; }

    [[nodiscard]] const std::string& name()        const noexcept { return name_;    }
    [[nodiscard]] const std::string& description() const noexcept { return desc_;    }
    [[nodiscard]] const std::string& ver()         const noexcept { return version_; }

    // ── Parse ─────────────────────────────────────────────────────────────

    /// Parse argc/argv.  argv[0] (program name) is automatically skipped.
    [[nodiscard]] ParseResult parse(int argc, char** argv) {
        std::span<char* const> args(argv + 1, static_cast<size_t>(argc > 0 ? argc - 1 : 0));
        return do_parse(args);
    }

    // ── Help ──────────────────────────────────────────────────────────────

    void print_help() const {
        using X = detail::C;
        auto full = full_name();

        // ── Header ──────────────────────────────────────────────────────
        std::print("{}{}{}", X::bld(), full, X::rst());
        if (!version_.empty())
            std::print("  {}v{}{}", X::dim(), version_, X::rst());
        if (!desc_.empty())
            std::print("  —  {}", desc_);
        std::println();
        if (!author_.empty())
            std::println("{}  Author: {}{}", X::dim(), author_, X::rst());
        std::println();

        // ── Usage ────────────────────────────────────────────────────────
        std::println("{}USAGE{}", X::byel(), X::rst());
        std::print("  {}", full);
        if (!options_.empty() || true) std::print(" [OPTIONS]");
        if (!subcommands_.empty())     std::print(" <COMMAND>");
        for (auto& pos : positionals_) {
                if (pos->variadic_)
                    std::print(" <{}...>", pos->name);
                else if (pos->required_)
                    std::print(" <{}>", pos->name);
                else
                    std::print(" [{}]", pos->name);
        }
        std::println();
        std::println();

        // ── Options ──────────────────────────────────────────────────────
        std::println("{}OPTIONS{}", X::byel(), X::rst());
        render_option_list();
        std::println();

        // ── Positional arguments ─────────────────────────────────────────
        if (!positionals_.empty()) {
            std::println("{}ARGUMENTS{}", X::byel(), X::rst());
            for (auto& pos : positionals_) {
                std::string lhs = std::format("  <{}{}>",
                    pos->name, pos->variadic_ ? "..." : "");
                std::print("{}{:<32}{}", X::bcyn(), lhs, X::rst());
                std::print("{}", pos->help_text);
                if (!pos->default_str().empty())
                    std::print("  {}[default: {}]{}", X::dim(), pos->default_str(), X::rst());
                if (pos->required_)
                    std::print("  {}[required]{}", X::bred(), X::rst());
                std::println();
            }
            std::println();
        }

        // ── Subcommands ──────────────────────────────────────────────────
        if (!subcommands_.empty()) {
            std::println("{}COMMANDS{}", X::byel(), X::rst());
            size_t col = 4;
            for (auto& s : subcommands_) col = std::max(col, s->name_.size() + 4);
            for (auto& s : subcommands_) {
                std::string lhs = std::format("  {}", s->name_);
                std::print("{}{:<{}}{}", X::bcyn(), lhs, col, X::rst());
                std::println("{}", s->desc_);
            }
            std::println();
            std::println("{}  Run '{} <COMMAND> --help' for details on a command.{}",
                X::dim(), full, X::rst());
            std::println();
        }

        // ── Examples ─────────────────────────────────────────────────────
        if (!examples_.empty()) {
            std::println("{}EXAMPLES{}", X::byel(), X::rst());
            for (auto& ex : examples_)
                std::println("  {}{}{}", X::grn(), ex, X::rst());
            std::println();
        }
    }

    // ── Error display ─────────────────────────────────────────────────────

    void print_error(const ParseError& e) const {
        using X = detail::C;
        std::println(stderr, "{}error:{} {}", X::bred(), X::rst(), e.message);
        std::println(stderr, "{}Try '{} --help' for usage information.{}",
            X::dim(), full_name(), X::rst());
    }

    // ── Shell completion generators ───────────────────────────────────────

    void bash_completion(std::ostream& out = std::cout) const {
        auto fn = name_;
        std::ranges::replace(fn, '-', '_');
        out << "# Bash completion for " << name_ << "\n";
        out << "_" << fn << "() {\n"
               "    local cur prev opts subs\n"
               "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
               "    opts=\"";
        for (auto& opt : options_)
            for (auto& n : opt->names) out << n << " ";
        out << "--help --version\"\n"
               "    subs=\"";
        for (auto& s : subcommands_) out << s->name_ << " ";
        out << "\"\n"
               "    COMPREPLY=($(compgen -W \"${opts} ${subs}\" -- \"${cur}\"))\n"
               "}\n"
               "complete -F _" << fn << " " << name_ << "\n";
    }

    void fish_completion(std::ostream& out = std::cout) const {
        for (auto& opt : options_) {
            if (opt->hidden_) continue;
            out << "complete -c " << name_;
            auto sn = opt->short_name();
            auto ln = opt->long_name();
            if (!sn.empty()) out << " -s " << sn.substr(1);
            if (!ln.empty()) out << " -l " << ln.substr(2);
            if (opt->is_flag_) out << " -f";
            if (!opt->choices_str().empty()) {
                out << " -a '";
                // Replace | with space for fish
                for (char c : opt->choices_str())
                    out << (c == '|' ? ' ' : c);
                out << "'";
            }
            out << " -d '" << opt->help_text << "'\n";
        }
        out << "complete -c " << name_ << " -s h -l help    -f -d 'Print help'\n";
        if (!version_.empty())
            out << "complete -c " << name_ << " -s V -l version -f -d 'Print version'\n";
    }

private:

    // ── Core parsing logic ────────────────────────────────────────────────

    [[nodiscard]] ParseResult do_parse(std::span<char* const> args) {
        // 1. Pull in env-var defaults before touching argv
        apply_env_vars();

        bool   end_opts = false;
        size_t pos_idx  = 0;

        for (size_t i = 0; i < args.size(); ++i) {
            std::string_view arg(args[i]);

            // ── End-of-options sentinel
            if (!end_opts && arg == "--") { end_opts = true; continue; }

            // ── Built-in --help / -h (always wins, no lookup needed)
            if (!end_opts && (arg == "--help" || arg == "-h"))
                { print_help(); std::exit(0); }

            // ── Built-in --version / -V
            if (!end_opts && (arg == "--version" || arg == "-V") && !version_.empty())
                { std::println("{} {}", name_, version_); std::exit(0); }

            // ── Subcommand dispatch (non-option tokens before any '--')
            if (!end_opts && !arg.starts_with('-') && !subcommands_.empty()) {
                for (auto& sub : subcommands_) {
                    if (sub->name_ == arg) {
                        active_sub_ = sub.get();
                        // Validate parent's own requirements before descending
                        if (auto r = finalize(); !r) return r;
                        return sub->do_parse(args.subspan(i + 1));
                    }
                }
                // Falls through to positional handling
            }

            // ── Positional argument
            if (end_opts || !arg.starts_with('-')) {
                if (pos_idx < positionals_.size()) {
                    auto& pos = positionals_[pos_idx];
                    if (auto r = pos->parse(arg); !r) return r;
                    if (!pos->variadic_) ++pos_idx;
                } else {
                    return std::unexpected(ParseError::too_many(arg));
                }
                continue;
            }

            // ── Long option:  --name  or  --name=value
            if (arg.starts_with("--")) {
                const auto eq      = arg.find('=');
                const auto name    = (eq != std::string_view::npos) ? arg.substr(0, eq) : arg;
                const auto inlval  = (eq != std::string_view::npos) ? arg.substr(eq + 1)
                                                                     : std::string_view{};
                const bool has_eq  = (eq != std::string_view::npos);

                auto* opt = find_opt(name);
                if (!opt) return std::unexpected(ParseError::unknown(name, suggest(name)));

                if (opt->is_flag_) {
                    if (auto r = opt->parse("true", name); !r) return r;
                } else if (has_eq) {
                    if (auto r = opt->parse(inlval, name); !r) return r;
                } else {
                    if (++i >= args.size())
                        return std::unexpected(ParseError::missing_value(name));
                    if (auto r = opt->parse(std::string_view(args[i]), name); !r) return r;
                }
                continue;
            }

            // ── Short option(s):  -v  ·  -vxf  ·  -n5  ·  -n 5
            // (at this point arg starts with '-' and len >= 2)
            for (size_t ch = 1; ch < arg.size(); ++ch) {
                const auto sopt = std::string("-") + arg[ch];
                auto* opt = find_opt(sopt);
                if (!opt) return std::unexpected(ParseError::unknown(sopt, suggest(sopt)));

                if (opt->is_flag_) {
                    if (auto r = opt->parse("true", sopt); !r) return r;
                    // Continue to next char (grouped flags: -vxf)
                } else {
                    // Value = rest of this arg (inline: -n5) or next arg (-n 5)
                    std::string_view val;
                    if (ch + 1 < arg.size()) {
                        val = arg.substr(ch + 1);
                    } else {
                        if (++i >= args.size())
                            return std::unexpected(ParseError::missing_value(sopt));
                        val = std::string_view(args[i]);
                    }
                    if (auto r = opt->parse(val, sopt); !r) return r;
                    break;  // consumed everything in this arg token
                }
            }
        } // for each arg

        return finalize();
    }

    // Apply defaults and enforce required constraints
    [[nodiscard]] ParseResult finalize() {
        for (auto& opt : options_)
            if (!opt->is_set_ && opt->has_default()) opt->apply_default();
        for (auto& pos : positionals_)
            if (!pos->is_set_ && pos->has_default()) pos->apply_default();

        for (auto& opt : options_) {
            if (opt->required_ && !opt->is_set_ && !opt->has_default())
                return std::unexpected(ParseError::missing_required(opt->display_name()));
        }
        for (auto& pos : positionals_) {
            if (pos->required_ && !pos->is_set_)
                return std::unexpected(ParseError::missing_positional(pos->name));
        }
        return {};
    }

    // ── Lookup helpers ────────────────────────────────────────────────────

    [[nodiscard]] detail::OptionBase* find_opt(std::string_view n) const noexcept {
        for (auto& o : options_) if (o->matches(n)) return o.get();
        return nullptr;
    }

    [[nodiscard]] std::string suggest(std::string_view token) const {
        // Strip leading '-' so "--verbos" vs "--verbose" = distance 1 (not 7)
        auto strip = [](std::string_view s) {
            while (!s.empty() && s[0] == '-') s.remove_prefix(1);
            return s;
        };
        const auto tok = strip(token);
        std::string best;
        size_t best_d = SIZE_MAX;
        for (auto& o : options_) {
            for (auto& n : o->names) {
                const auto d = detail::edit_distance(tok, strip(n));
                if (d < best_d && d <= 3) { best_d = d; best = n; }
            }
        }
        return best;
    }

    void apply_env_vars() {
        for (auto& opt : options_) {
            if (opt->env_var.empty() || opt->is_set_) continue;
            if (const char* v = std::getenv(opt->env_var.c_str()))
                static_cast<void>(opt->parse(v, opt->env_var)); // best-effort, non-fatal
        }
    }

    // ── Help rendering ────────────────────────────────────────────────────

    [[nodiscard]] std::string full_name() const {
        return parent_ ? (parent_->full_name() + " " + name_) : name_;
    }

    void render_option_list() const {
        using X = detail::C;

        // Measure the widest left-hand column to align all descriptions
        size_t col = 0;
        auto measure = [&](std::string_view sn, std::string_view ln,
                           std::string_view meta, bool is_flag) {
            size_t w = 2;
            if (!sn.empty()) w += sn.size();
            if (!sn.empty() && !ln.empty()) w += 2;
            if (!ln.empty()) w += ln.size();
            if (!is_flag && !meta.empty()) w += 2 + meta.size() + 1; // " <META>"
            col = std::max(col, w);
        };
        for (auto& o : options_)
            if (!o->hidden_) measure(o->short_name(), o->long_name(), o->meta, o->is_flag_);
        measure("-h", "--help",    "",      true);
        measure("-V", "--version", "",      true);
        col = std::max(col + 2, size_t(30));

        // Print one row
        auto row = [&](std::string_view sn, std::string_view ln, std::string_view meta,
                       bool is_flag, std::string_view help, bool req,
                       std::string_view def, std::string_view choices_s,
                       std::string_view env_v) {
            std::string lhs = "  ";
            if (!sn.empty()) lhs += sn;
            if (!sn.empty() && !ln.empty()) lhs += ", ";
            if (!ln.empty()) lhs += ln;
            if (!is_flag && !meta.empty())
                lhs += std::format(" <{}>", meta);
            std::print("{}{:<{}}{}", X::bcyn(), lhs, col, X::rst());
            std::print("{}", help);
            if (req)              std::print("  {}[required]{}", X::bred(), X::rst());
            if (!def.empty())     std::print("  {}[default: {}]{}", X::dim(), def, X::rst());
            if (!choices_s.empty()) std::print("  {}[{}]{}", X::dim(), choices_s, X::rst());
            if (!env_v.empty())   std::print("  {}[env: {}]{}", X::dim(), env_v, X::rst());
            std::println();
        };

        for (auto& o : options_) {
            if (o->hidden_) continue;
            row(o->short_name(), o->long_name(), o->meta,
                o->is_flag_, o->help_text, o->required_,
                o->default_str(), o->choices_str(), o->env_var);
        }
        // Built-ins — always shown at the bottom
        row("-h", "--help",    "", true, "Print help and exit",    false, {}, {}, {});
        if (!version_.empty())
            row("-V", "--version", "", true, "Print version and exit", false, {}, {}, {});
    }
};

} // namespace cli
