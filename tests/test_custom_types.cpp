#include "test_support.hpp"

#include <format>
#include <type_traits>

namespace test_types {

struct Level {
    int value = 0;
    friend bool operator==(const Level&, const Level&) = default;
};

inline std::expected<Level, std::string> cli_parse(std::string_view text) {
    if (text == "low")  return Level{1};
    if (text == "high") return Level{3};
    return std::unexpected("expected low or high");
}

inline std::string cli_format(const Level& level) {
    return std::format("Level({})", level.value);
}

struct Point {
    int x = 0;
    int y = 0;
    friend bool operator==(const Point&, const Point&) = default;
};

inline std::expected<Point, std::string>
cli_parse(std::string_view text, std::type_identity<Point>) {
    const auto comma = text.find(',');
    if (comma == std::string_view::npos)
        return std::unexpected("expected x,y");

    Point point{};
    auto x = std::from_chars(text.data(), text.data() + comma, point.x);
    auto y = std::from_chars(text.data() + comma + 1, text.data() + text.size(), point.y);
    if (x.ec != std::errc{} || x.ptr != text.data() + comma ||
        y.ec != std::errc{} || y.ptr != text.data() + text.size())
        return std::unexpected("expected x,y");
    return point;
}

inline std::string cli_format(const Point& point) {
    return std::format("{},{}", point.x, point.y);
}

// Has a standard formatter but no cli_format, exercising the std::formattable
// fallback in detail::to_display().
struct Formatted {
    int value = 0;
};

inline std::expected<Formatted, std::string>
cli_parse(std::string_view text, std::type_identity<Formatted>) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::unexpected("expected an integer");
    return Formatted{value};
}

// Deliberately defines only operator<; range checking must not require
// operator> or operator<=>.
struct Rank {
    int value = 0;
    friend bool operator<(const Rank& a, const Rank& b) { return a.value < b.value; }
};

inline std::expected<Rank, std::string>
cli_parse(std::string_view text, std::type_identity<Rank>) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::unexpected("expected an integer");
    return Rank{value};
}

inline std::string cli_format(const Rank& rank) {
    return std::to_string(rank.value);
}

} // namespace test_types

template<>
struct std::formatter<test_types::Formatted, char> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const test_types::Formatted& value, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "Formatted({})", value.value);
    }
};

TEST_SUITE_BEGIN("custom_types");

TEST_CASE("a one-argument ADL hook parses option values") {
    cli::App app("t");
    auto level = app.add<test_types::Level>("--level").required();

    REQUIRE(app.parse({"--level", "high"}));
    REQUIRE(level);
    CHECK(level->value == 3);
    CHECK(app["--level"].as<test_types::Level>().value == 3);
}

TEST_CASE("a type_identity ADL hook parses option values") {
    cli::App app("t");
    auto point = app.add<test_types::Point>("--point");

    REQUIRE(app.parse({"--point", "3,4"}));
    REQUIRE(point);
    CHECK(point->x == 3);
    CHECK(point->y == 4);
}

TEST_CASE("custom types work as positionals") {
    cli::App app("t");
    auto level = app.add_positional<test_types::Level>("level");

    REQUIRE(app.parse({"low"}));
    CHECK(level->value == 1);
}

TEST_CASE("custom types work in vector options") {
    cli::App app("t");
    auto levels = app.add<std::vector<test_types::Level>>("--level").delimiter(',');

    REQUIRE(app.parse({"--level=low,high"}));
    REQUIRE(levels->size() == 2);
    CHECK((*levels)[0].value == 1);
    CHECK((*levels)[1].value == 3);
}

TEST_CASE("type_identity custom types work in vectors") {
    cli::App app("t");
    auto points = app.add<std::vector<test_types::Point>>("--point").delimiter(';');

    REQUIRE(app.parse({"--point=1,2;3,4"}));
    REQUIRE(points->size() == 2);
    CHECK((*points)[0].x == 1);
    CHECK((*points)[1].y == 4);
}

TEST_CASE("custom parse errors keep the hook's explanation") {
    cli::App app("t");
    (void)app.add<test_types::Level>("--level");

    auto r = app.parse({"--level", "sideways"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "sideways"));
    CHECK(contains(r.error().message, "expected low or high"));
}

TEST_CASE("custom formatting appears in help defaults") {
    cli::App app("tool");
    (void)app.add<test_types::Level>("--level").default_val(test_types::Level{1});

    const auto help = app.help_text();
    CHECK(contains(help, "[default: Level(1)]"));
}

TEST_CASE("std::formattable types use the standard formatter for display") {
    cli::App app("tool");
    auto value = app.add<test_types::Formatted>("--value")
                     .default_val(test_types::Formatted{42});

    CHECK(contains(app.help_text(), "[default: Formatted(42)]"));

    REQUIRE(app.parse({"--value", "9"}));
    CHECK(value->value == 9);
}

TEST_CASE("custom formatting appears in choice diagnostics") {
    cli::App app("t");
    (void)app.add<test_types::Level>("--level")
        .choices({test_types::Level{1}});

    REQUIRE(app.parse({"--level", "low"}));

    auto r = app.parse({"--level", "high"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "high"));
    CHECK(contains(r.error().message, "Level(1)"));
}

TEST_CASE("custom types participate in default validation") {
    cli::App app("t");
    (void)app.add<test_types::Level>("--level")
        .default_val(test_types::Level{3})
        .choices({test_types::Level{1}});

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "Level(3)"));
    CHECK(contains(r.error().message, "Level(1)"));
}

TEST_CASE("custom types work with environment fallback") {
    cli::App app("t");
    auto level = app.add<test_types::Level>("--level").env("CLI_TEST_CUSTOM_LEVEL");
    ScopedEnv env("CLI_TEST_CUSTOM_LEVEL", "high");

    REQUIRE(app.parse({}));
    CHECK(level->value == 3);
    CHECK(!level.is_set());
}

TEST_CASE("custom types participate in transforms") {
    cli::App app("t");
    auto level = app.add<test_types::Level>("--level")
                     .transform([](test_types::Level value) {
                         value.value *= 10;
                         return value;
                     });

    REQUIRE(app.parse({"--level", "high"}));
    CHECK(level->value == 30);
}

TEST_CASE("custom types can be used as positional defaults") {
    cli::App app("t");
    auto point = app.add_positional<test_types::Point>("point")
                     .default_val(test_types::Point{1, 2});

    REQUIRE(app.parse({}));
    CHECK(point->x == 1);
    CHECK(point->y == 2);
    CHECK(!point.is_set());
}

TEST_CASE("custom formatting appears in positional help") {
    cli::App app("tool");
    (void)app.add_positional<test_types::Level>("level")
        .default_val(test_types::Level{1});

    CHECK(contains(app.help_text(), "[default: Level(1)]"));
}

TEST_CASE("custom types only need operator< for range constraints") {
    cli::App app("t");
    auto rank = app.add<test_types::Rank>("--rank")
                    .range(test_types::Rank{1}, test_types::Rank{5});

    REQUIRE(app.parse({"--rank", "3"}));
    CHECK(rank->value == 3);

    auto low = app.parse({"--rank", "0"});
    REQUIRE(!low);
    CHECK(low.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(low.error().message, "[1, 5]"));

    auto high = app.parse({"--rank", "6"});
    REQUIRE(!high);
    CHECK(high.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("custom types only need operator< for positional ranges") {
    cli::App app("t");
    auto rank = app.add_positional<test_types::Rank>("rank")
                    .range(test_types::Rank{1}, test_types::Rank{5});

    REQUIRE(app.parse({"4"}));
    CHECK(rank->value == 4);

    auto r = app.parse({"9"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("custom parsing concepts are correctly constrained") {
    static_assert(cli::detail::CustomParsable<test_types::Level>);
    static_assert(cli::detail::CustomParsable<test_types::Point>);
    static_assert(cli::detail::CustomFormattable<test_types::Level>);
    static_assert(cli::detail::CustomFormattable<test_types::Point>);
    static_assert(!cli::detail::CustomParsable<int>);
    static_assert(!cli::detail::CustomFormattable<int>);
    CHECK(true);
}

TEST_SUITE_END();
