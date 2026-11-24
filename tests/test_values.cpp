#include "test_support.hpp"

#include <cmath>
#include <limits>

TEST_SUITE_BEGIN("values");

TEST_CASE("integer options accept decimal, sign and leading zeros") {
    cli::App app("t");
    auto num = app.add<int>("--num");

    REQUIRE(app.parse({"--num", "42"}));
    CHECK(*num == 42);

    REQUIRE(app.parse({"--num", "+42"}));
    CHECK(*num == 42);

    REQUIRE(app.parse({"--num", "-42"}));
    CHECK(*num == -42);

    REQUIRE(app.parse({"--num", "00042"}));
    CHECK(*num == 42);

    REQUIRE(app.parse({"--num", "-0"}));
    CHECK(*num == 0);
}

TEST_CASE("integers accept hexadecimal and binary prefixes") {
    cli::App app("t");
    auto num = app.add<int>("--num");

    REQUIRE(app.parse({"--num", "0xFF"}));
    CHECK(*num == 255);

    REQUIRE(app.parse({"--num", "0Xff"}));
    CHECK(*num == 255);

    REQUIRE(app.parse({"--num", "0b101"}));
    CHECK(*num == 5);

    REQUIRE(app.parse({"--num", "0B101"}));
    CHECK(*num == 5);

    REQUIRE(app.parse({"--num", "-0x10"}));
    CHECK(*num == -16);

    REQUIRE(app.parse({"--num", "+0b1010"}));
    CHECK(*num == 10);
}

TEST_CASE("integer boundaries parse without overflow") {
    cli::App app("t");
    auto num = app.add<int>("--num");

    REQUIRE(app.parse({"--num", "2147483647"}));
    CHECK(*num == std::numeric_limits<int>::max());

    REQUIRE(app.parse({"--num", "-2147483648"}));
    CHECK(*num == std::numeric_limits<int>::min());

    REQUIRE(app.parse({"--num", "-0x80000000"}));
    CHECK(*num == std::numeric_limits<int>::min());
}

TEST_CASE("integer overflow on either side is rejected") {
    cli::App app("t");
    (void)app.add<int>("--num");

    auto high = app.parse({"--num", "2147483648"});
    REQUIRE(!high);
    CHECK(high.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(high.error().message, "overflow"));

    auto low = app.parse({"--num", "-2147483649"});
    REQUIRE(!low);
    CHECK(low.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(low.error().message, "overflow"));
}

TEST_CASE("64-bit integer boundaries are handled") {
    cli::App app("t");
    auto num = app.add<long long>("--num");

    REQUIRE(app.parse({"--num", "9223372036854775807"}));
    CHECK(*num == std::numeric_limits<long long>::max());

    REQUIRE(app.parse({"--num", "-9223372036854775808"}));
    CHECK(*num == std::numeric_limits<long long>::min());

    auto overflow = app.parse({"--num", "9223372036854775808"});
    REQUIRE(!overflow);
    CHECK(overflow.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("unsigned integers accept their maximum and reject overflow") {
    cli::App app("t");
    auto num = app.add<unsigned>("--num");

    REQUIRE(app.parse({"--num", "4294967295"}));
    CHECK(*num == std::numeric_limits<unsigned>::max());

    auto overflow = app.parse({"--num", "4294967296"});
    REQUIRE(!overflow);
    CHECK(overflow.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(overflow.error().message, "overflow"));
}

TEST_CASE("unsigned integers reject decimal and prefixed negatives") {
    cli::App app("t");
    (void)app.add<unsigned>("--num");

    auto decimal = app.parse({"--num", "-1"});
    REQUIRE(!decimal);
    CHECK(decimal.error().kind == cli::ParseError::Kind::InvalidValue);

    auto hex = app.parse({"--num", "-0x1"});
    REQUIRE(!hex);
    CHECK(hex.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("64-bit unsigned maximum parses") {
    cli::App app("t");
    auto num = app.add<unsigned long long>("--num");
    REQUIRE(app.parse({"--num", "18446744073709551615"}));
    CHECK(*num == std::numeric_limits<unsigned long long>::max());

    auto overflow = app.parse({"--num", "18446744073709551616"});
    REQUIRE(!overflow);
    CHECK(overflow.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("malformed integer spellings are rejected") {
    const std::string_view invalid[] = {
        "", "+", "-", "0x", "0X", "0b", "0B", "1x", "42 ", " 42",
        "1_000", "0xGG", "0b102", "--1", "++1",
    };

    for (auto text : invalid) {
        cli::App app("t");
        (void)app.add<int>("--num");
        auto r = app.parse({"--num", text});
        CAPTURE(text);
        REQUIRE(!r);
        CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    }
}

TEST_CASE("floating-point values accept common literal forms") {
    cli::App app("t");
    auto value = app.add<double>("--value");

    REQUIRE(app.parse({"--value", "3.5"}));
    CHECK(*value == doctest::Approx(3.5));

    REQUIRE(app.parse({"--value", "+3.5"}));
    CHECK(*value == doctest::Approx(3.5));

    REQUIRE(app.parse({"--value", "-.5"}));
    CHECK(*value == doctest::Approx(-0.5));

    REQUIRE(app.parse({"--value", ".5"}));
    CHECK(*value == doctest::Approx(0.5));

    REQUIRE(app.parse({"--value", "5."}));
    CHECK(*value == doctest::Approx(5.0));

    REQUIRE(app.parse({"--value", "1e3"}));
    CHECK(*value == doctest::Approx(1000.0));

    REQUIRE(app.parse({"--value", "-1E-3"}));
    CHECK(*value == doctest::Approx(-0.001));
}

TEST_CASE("floating-point types preserve hexadecimal and special values") {
    cli::App app("t");
    auto f = app.add<float>("--float");
    auto d = app.add<double>("--double");
    auto ld = app.add<long double>("--long-double");

    REQUIRE(app.parse({
        "--float", "0x1.8p1",
        "--double", "Infinity",
        "--long-double", "-NaN",
    }));
    CHECK(*f == 3.0f);
    CHECK(std::isinf(*d));
    CHECK(*d > 0);
    CHECK(std::isnan(*ld));
    CHECK(std::signbit(*ld));
}

TEST_CASE("malformed floating-point spellings are rejected") {
    const std::string_view invalid[] = {
        "", "+", "-", "1.2.3", "1e", "e3", " 1.0", "1.0 ", "nanx",
    };

    for (auto text : invalid) {
        cli::App app("t");
        (void)app.add<double>("--value");
        auto r = app.parse({"--value", text});
        CAPTURE(text);
        REQUIRE(!r);
        CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    }
}

TEST_CASE("boolean options accept all documented spellings") {
    const std::pair<std::string_view, bool> cases[] = {
        {"true", true}, {"false", false}, {"TRUE", true}, {"False", false},
        {"yes", true},  {"no", false},    {"on", true},   {"off", false},
        {"1", true},    {"0", false},
    };

    for (const auto& [text, expected] : cases) {
        cli::App app("t");
        auto value = app.add<bool>("--value");
        REQUIRE(app.parse({"--value", text}));
        CAPTURE(text);
        CHECK(*value == expected);
    }
}

TEST_CASE("malformed boolean spellings are rejected") {
    const std::string_view invalid[] = {
        "", "maybe", "2", "true ", " true", "tru", "oui",
    };

    for (auto text : invalid) {
        cli::App app("t");
        (void)app.add<bool>("--value");
        auto r = app.parse({"--value", text});
        CAPTURE(text);
        REQUIRE(!r);
        CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    }
}

TEST_CASE("negative integer positionals accept decimal, hex and binary") {
    cli::App app("t");
    auto value = app.add_positional<int>("value");

    REQUIRE(app.parse({"-1"}));
    CHECK(*value == -1);

    REQUIRE(app.parse({"-0x10"}));
    CHECK(*value == -16);

    REQUIRE(app.parse({"-0b101"}));
    CHECK(*value == -5);
}

TEST_CASE("negative floating positionals accept dot and exponent forms") {
    cli::App app("t");
    auto value = app.add_positional<double>("value");

    REQUIRE(app.parse({"-3.14"}));
    CHECK(*value == doctest::Approx(-3.14));

    REQUIRE(app.parse({"-3"}));
    CHECK(*value == doctest::Approx(-3.0));

    REQUIRE(app.parse({"-.5"}));
    CHECK(*value == doctest::Approx(-0.5));

    REQUIRE(app.parse({"-1e-3"}));
    CHECK(*value == doctest::Approx(-0.001));
}

TEST_CASE("negative option values do not require positional handling") {
    cli::App app("t");
    auto num = app.add<int>("--num");
    REQUIRE(app.parse({"--num", "-5"}));
    CHECK(*num == -5);
}

TEST_CASE("a negative-looking token is validated as a positional") {
    cli::App app("t");
    (void)app.add_positional<int>("num");
    auto r = app.parse({"-3.5"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("vector<bool> values parse and split like other element types") {
    cli::App app("t");
    auto flags = app.add<std::vector<bool>>("--flag").delimiter(',');

    REQUIRE(app.parse({"--flag=true,false,on,0"}));
    REQUIRE(flags->size() == 4);
    CHECK((*flags)[0] == true);
    CHECK((*flags)[1] == false);
    CHECK((*flags)[2] == true);
    CHECK((*flags)[3] == false);
}

TEST_CASE("boolean options accept explicit numeric forms") {
    cli::App app("t");
    auto off  = app.add<bool>("--bool");
    auto on   = app.add<bool>("--debug");
    auto one  = app.add<bool>("--verbose");
    auto zero = app.add<bool>("--dry-run");

    REQUIRE(app.parse({"--bool=false", "--debug=true", "--verbose=1", "--dry-run=0"}));
    CHECK(!*off);
    CHECK(*on);
    CHECK(*one);
    CHECK(!*zero);
}

TEST_CASE("single-letter boolean spellings are accepted case-insensitively") {
    const std::pair<std::string_view, bool> cases[] = {
        {"t", true},  {"T", true},  {"y", true},  {"Y", true},
        {"f", false}, {"F", false}, {"n", false}, {"N", false},
    };
    for (const auto& [text, expected] : cases) {
        cli::App app("t");
        auto value = app.add<bool>("--value");
        REQUIRE(app.parse({"--value", text}));
        CAPTURE(text);
        CHECK(*value == expected);
    }
}

TEST_CASE("longer near-miss boolean spellings are still rejected") {
    for (const char* text : {"tr", "ye", "nope", "ff", ""}) {
        cli::App app("t");
        (void)app.add<bool>("--value");
        CAPTURE(text);
        CHECK(!app.parse({"--value", text}));
    }
}

TEST_SUITE_END();
