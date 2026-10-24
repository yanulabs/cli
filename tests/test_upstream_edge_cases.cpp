// ─────────────────────────────────────────────────────────────────────────────
// tests/test_upstream_edge_cases.cpp
//
// Edge cases ported and adapted from major permissive open-source CLI libraries:
// - CLI11 (BSD-3-Clause, Copyright (c) 2017-2026 University of Cincinnati)
// - cxxopts (MIT, Copyright (c) 2014 Jarryd Beck)
// - argparse (MIT, Copyright (c) 2018 Pranav Srinivas Kumar)
// ─────────────────────────────────────────────────────────────────────────────
#include <doctest.h>
#include "test_support.hpp"
#include <cli.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

TEST_SUITE("upstream_edge_cases") {

// ── 1. Stdin / Stdout single dash '-' (CLI11 & POSIX convention) ─────────────

TEST_CASE("CLI11/cxxopts: single dash '-' is treated as positional argument") {
    cli::App app("t");
    auto input = app.add_positional<std::string>("input");
    // Pure single '-' stdin/stdout convention (Issue #3 tracks tokens starting with '- ')
    REQUIRE(app.parse({"-"})) ;
    CHECK(*input == "-");
}

TEST_CASE("CLI11: single dash '-' as option value") {
    cli::App app("t");
    auto output = app.add<std::string>("-o");
    REQUIRE(app.parse({"-o", "-"}));
    CHECK(*output == "-");
}

TEST_CASE("CLI11: two single dashes as distinct positionals") {
    cli::App app("t");
    auto p1 = app.add_positional<std::string>("in");
    auto p2 = app.add_positional<std::string>("out");
    REQUIRE(app.parse({"-", "-"}));
    CHECK(*p1 == "-");
    CHECK(*p2 == "-");
}

// ── 2. End of options '--' (CLI11, cxxopts & argparse) ───────────────────────

TEST_CASE("CLI11/argparse: double dash '--' with no following arguments") {
    cli::App app("t");
    auto flag = app.add_flag("-v");
    REQUIRE(app.parse({"-v", "--"}));
    CHECK(*flag == true);
}

TEST_CASE("CLI11: double dash '--' followed by flag-like token '-h'") {
    cli::App app("t");
    auto pos = app.add_positional<std::string>("file");
    auto res = app.parse({"--", "-h"});
    REQUIRE(res.has_value());
    CHECK(*pos == "-h");
}

TEST_CASE("CLI11: double dash '--' followed by '--'") {
    cli::App app("t");
    auto p1 = app.add_positional<std::string>("p1");
    auto p2 = app.add_positional<std::string>("p2");
    REQUIRE(app.parse({"--", "--", "after"}));
    CHECK(*p1 == "--");
    CHECK(*p2 == "after");
}

TEST_CASE("cxxopts: double dash '--' followed by option-like tokens") {
    cli::App app("t");
    auto pos = app.add_positional<std::vector<std::string>>("args");
    REQUIRE(app.parse({"--", "--fake-option", "-x", "-v"}));
    REQUIRE(pos->size() == 3);
    CHECK((*pos)[0] == "--fake-option");
    CHECK((*pos)[1] == "-x");
    CHECK((*pos)[2] == "-v");
}

TEST_CASE("CLI11/cxxopts: option-like extras after '--' are retained verbatim") {
    cli::App app("t");
    app.allow_extras();
    auto verbose = app.add_flag("--verbose");
    REQUIRE(app.parse({"--verbose", "--", "--foo", "-bar"}));
    CHECK(*verbose);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--foo");
    CHECK(rest[1] == "-bar");
}

// ── 3. Zero / Null arguments (CLI11) ─────────────────────────────────────────

TEST_CASE("CLI11: parse with argc = 0 and null argv") {
    cli::App app("t");
    auto res = app.parse(0, nullptr);
    CHECK(res.has_value());
}

TEST_CASE("CLI11: parse with argc = 1 and empty argv array") {
    cli::App app("t");
    const char* argv[] = {"my_program"};
    auto res = app.parse(1, argv);
    CHECK(res.has_value());
}

// ── 4. Equal sign handling (CLI11 & argparse) ────────────────────────────────

TEST_CASE("CLI11/argparse: multiple equal signs preserved in value") {
    cli::App app("t");
    auto def = app.add<std::string>("--define");
    REQUIRE(app.parse({"--define=KEY=VAL=EXTRA"}));
    CHECK(*def == "KEY=VAL=EXTRA");
}

TEST_CASE("CLI11: empty inline value with equals") {
    cli::App app("t");
    auto s = app.add<std::string>("--str");
    REQUIRE(app.parse({"--str="}));
    CHECK(*s == "");
}

TEST_CASE("cxxopts: short option with attached equals and empty value") {
    cli::App app("t");
    auto s = app.add<std::string>("-s");
    REQUIRE(app.parse({"-s="}));
    CHECK(*s == "");
}

TEST_CASE("argparse: short option with equals and value") {
    cli::App app("t");
    auto s = app.add<std::string>("-s");
    REQUIRE(app.parse({"-s=hello"}));
    CHECK(*s == "hello");
}

TEST_CASE("cxxopts: short attached values keep every '=' in the value") {
    {
        cli::App app("t");
        auto s = app.add<std::string>("-o");
        REQUIRE(app.parse({"-o==*hi_$&"}));
        CHECK(*s == "=*hi_$&");
    }
    {
        cli::App app("t");
        auto s = app.add<std::string>("-o");
        REQUIRE(app.parse({"-oDEBUG=1"}));
        CHECK(*s == "DEBUG=1");
    }
    {
        cli::App app("t");
        auto b = app.add_flag("-b");
        auto c = app.add<std::string>("-c");
        REQUIRE(app.parse({"-bcfoo=something"}));
        CHECK(*b);
        CHECK(*c == "foo=something");
    }
}

TEST_CASE("cxxopts: long inline value may itself start with '='") {
    cli::App app("t");
    auto x = app.add<std::string>("--x");
    REQUIRE(app.parse({"--x==a=b"}));
    CHECK(*x == "=a=b");
}

TEST_CASE("cxxopts: short boolean flags accept explicit true/false/1/0") {
    cli::App app("t");
    auto b = app.add_flag("-b");
    REQUIRE(app.parse({"-b=true"}));
    CHECK(*b);
    REQUIRE(app.parse({"-b=false"}));
    CHECK(!*b);
    REQUIRE(app.parse({"-b=1"}));
    CHECK(*b);
    REQUIRE(app.parse({"-b=0"}));
    CHECK(!*b);
    auto bad = app.parse({"-b=bogus"});
    REQUIRE(!bad);
    CHECK(bad.error().kind == cli::ParseError::Kind::InvalidValue);
}

// ── 5. Empty and whitespace string values (cxxopts & CLI11) ──────────────────

TEST_CASE("CLI11/argparse: empty string passed as option value") {
    cli::App app("t");
    auto s = app.add<std::string>("--name").default_val("default");
    REQUIRE(app.parse({"--name", ""}));
    CHECK(*s == "");
}

TEST_CASE("cxxopts: empty string passed as positional argument") {
    cli::App app("t");
    auto p = app.add_positional<std::string>("pos");
    REQUIRE(app.parse({""}));
    CHECK(*p == "");
}

TEST_CASE("argparse: whitespace-only string preserved") {
    cli::App app("t");
    auto s = app.add<std::string>("--text");
    REQUIRE(app.parse({"--text", "   "}));
    CHECK(*s == "   ");
}

// ── 6. Value looking like an option (CLI11) ──────────────────────────────────

TEST_CASE("CLI11: value starting with dash is consumed by option expecting value") {
    cli::App app("t");
    auto msg = app.add<std::string>("--msg");
    REQUIRE(app.parse({"--msg", "-hello"}));
    CHECK(*msg == "-hello");
}

TEST_CASE("CLI11: value is literally '--help'") {
    cli::App app("t");
    auto msg = app.add<std::string>("--msg");
    auto res = app.parse({"--msg", "--help"});
    REQUIRE(res.has_value());
    CHECK(*msg == "--help");
}

TEST_CASE("CLI11/argparse/cxxopts: bare '--' cannot satisfy an option value") {
    // The sentinel ends option parsing; it is not a value.  argparse reports
    // "expected one argument", CLI11 and cxxopts likewise reject the token.
    {
        cli::App app("t");
        (void)app.add<std::string>("--msg");
        auto res = app.parse({"--msg", "--"});
        REQUIRE(!res);
        CHECK(res.error().kind == cli::ParseError::Kind::MissingValue);
    }
    {
        cli::App app("t");
        (void)app.add<std::string>("-msg");
        auto res = app.parse({"-msg", "--"});
        REQUIRE(!res);
        CHECK(res.error().kind == cli::ParseError::Kind::MissingValue);
    }
    {
        cli::App app("t");
        (void)app.add<std::string>("-m");
        auto res = app.parse({"-m", "--"});
        REQUIRE(!res);
        CHECK(res.error().kind == cli::ParseError::Kind::MissingValue);
    }
}

TEST_CASE("CLI11/argparse: inline '=' still allows a literal '--' value") {
    {
        cli::App app("t");
        auto msg = app.add<std::string>("--msg");
        REQUIRE(app.parse({"--msg=--"}));
        CHECK(*msg == "--");
    }
    {
        cli::App app("t");
        auto msg = app.add<std::string>("-m");
        REQUIRE(app.parse({"-m=--"}));
        CHECK(*msg == "--");
    }
}

TEST_CASE("argparse: bare '--' is not a value even with allow_extras") {
    cli::App app("t");
    app.allow_extras();
    (void)app.add<std::string>("--msg");
    auto res = app.parse({"--msg", "--"});
    REQUIRE(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::MissingValue);
}

// ── 7. Negative numbers (argparse & CLI11) ───────────────────────────────────

TEST_CASE("argparse: negative integer as positional") {
    cli::App app("t");
    auto pos = app.add_positional<int>("num");
    REQUIRE(app.parse({"-100"}));
    CHECK(*pos == -100);
}

TEST_CASE("argparse: negative float as positional") {
    cli::App app("t");
    auto pos = app.add_positional<double>("val");
    REQUIRE(app.parse({"-3.14159"}));
    CHECK(*pos == doctest::Approx(-3.14159));
}

TEST_CASE("argparse: negative integers and floats into vector positionals") {
    {
        cli::App app("t");
        auto nums = app.add_positional<std::vector<int>>("number");
        REQUIRE(app.parse({"-1", "-2", "3"}));
        REQUIRE(nums->size() == 3);
        CHECK((*nums)[0] == -1);
        CHECK((*nums)[1] == -2);
        CHECK((*nums)[2] == 3);
    }
    {
        cli::App app("t");
        auto nums = app.add_positional<std::vector<double>>("number");
        REQUIRE(app.parse({"-1.001", "-2.002", "3.003"}));
        REQUIRE(nums->size() == 3);
        CHECK((*nums)[0] == doctest::Approx(-1.001));
        CHECK((*nums)[1] == doctest::Approx(-2.002));
        CHECK((*nums)[2] == doctest::Approx(3.003));
    }
}

TEST_CASE("argparse: registered short option '-1' beats negative positional") {
    cli::App app("t");
    auto f = app.add_flag("-1");
    auto pos = app.add_positional<int>("p").default_val(0);
    REQUIRE(app.parse({"-1"}));
    CHECK(*f == true);
    CHECK(*pos == 0);
}

TEST_CASE("CLI11: short option with attached negative value") {
    cli::App app("t");
    auto n = app.add<int>("-n");
    REQUIRE(app.parse({"-n-42"}));
    CHECK(*n == -42);
}

TEST_CASE("argparse: negative-number grammar accepts decimal and exponent forms") {
    // Adapted from argparse's test_negative_numbers.cpp.  Leading-zero forms
    // such as -00 are accepted too, matching C++ integer parsing (cxxopts).
    const char* valid[] = {
        "-0", "-00", "-003", "-9", "-92180",
        "-0.", "-8.", "-200.", "-003.", "-.0927",
        "-1e32", "-1e+32", "-00e-0",
        "-18.e0", "-18.e+92", "-0.e-92",
        "-.023e0", "-.2e+92", "-.71564e-92",
        "-02.023e4000", "-3.239e+76", "-238237.0e-2",
        "-0x10", "-0b101",
    };
    for (const char* text : valid) {
        cli::App app("t");
        auto value = app.add_positional<std::string>("value");
        CAPTURE(text);
        REQUIRE(app.parse({text}));
        CHECK(*value == text);
    }
}

TEST_CASE("argparse: malformed negative-number forms stay option-like") {
    // Adapted from argparse's test_negative_numbers.cpp.  A token that merely
    // resembles a number must not be silently treated as a positional.
    const char* invalid[] = {
        "-.", "-e", "-.e", "-e32", "-e+7", "-e-1", "-0e", "-13e",
        "-13.e", "-.283e", "-.e3", "-.e+3", "-.e-3",
        "-3.14e", "-2.17e+", "-13.6e-", "-13.6e+-23", "-3.6e23+",
        "-3.6e2e9", "-3.6.3", "-+42", "-0x27.8p1",
        "-3.8p1", "-0x1e+2", "-inf", "-INFINITY", "-nan", "-NAN",
        "-1abc", "-0xZZ", "-0b102", "-1.2.3",
    };
    for (const char* text : invalid) {
        cli::App app("t");
        (void)app.add_positional<std::string>("value");
        CAPTURE(text);
        auto r = app.parse({text});
        REQUIRE(!r);
        CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    }
}

// ── 8. Number formats & base (cxxopts & CLI11) ───────────────────────────────

TEST_CASE("cxxopts: hexadecimal numbers with positive and negative signs") {
    cli::App app("t");
    auto h1 = app.add<int>("--h1");
    auto h2 = app.add<int>("--h2");
    auto h3 = app.add<int>("--h3");
    REQUIRE(app.parse({"--h1", "0xFF", "--h2", "+0x10", "--h3", "-0x10"}));
    CHECK(*h1 == 255);
    CHECK(*h2 == 16);
    CHECK(*h3 == -16);
}

TEST_CASE("cxxopts: binary numbers with positive and negative signs") {
    cli::App app("t");
    auto b1 = app.add<int>("--b1");
    auto b2 = app.add<int>("--b2");
    REQUIRE(app.parse({"--b1", "0b1011", "--b2", "-0b101"}));
    CHECK(*b1 == 11);
    CHECK(*b2 == -5);
}

TEST_CASE("cxxopts: leading zero integers are decimal") {
    cli::App app("t");
    auto num = app.add<int>("--num");
    REQUIRE(app.parse({"--num", "010"}));
    CHECK(*num == 10);
}

TEST_CASE("CLI11/cxxopts: unsigned integers reject negative values") {
    cli::App app("t");
    auto u = app.add<unsigned>("--u");
    auto res = app.parse({"--u", "-5"});
    CHECK(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("cxxopts: 64-bit integer limits and overflow detection") {
    cli::App app("t");
    auto i64 = app.add<int64_t>("--i64");
    REQUIRE(app.parse({"--i64", "9223372036854775807"}));
    CHECK(*i64 == std::numeric_limits<int64_t>::max());

    auto r_overflow = app.parse({"--i64", "9223372036854775808"});
    CHECK(!r_overflow);
    CHECK(r_overflow.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("cxxopts: trailing non-digits rejected for integers") {
    cli::App app("t");
    auto n = app.add<int>("--num");
    auto res = app.parse({"--num", "123abc"});
    CHECK(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::InvalidValue);
}

// ── 9. Floating point forms (CLI11 & cxxopts) ────────────────────────────────

TEST_CASE("CLI11: scientific notation and leading dots") {
    cli::App app("t");
    auto f1 = app.add<double>("--f1");
    auto f2 = app.add<double>("--f2");
    auto f3 = app.add<double>("--f3");
    auto f4 = app.add<double>("--f4");
    REQUIRE(app.parse({"--f1", "1e-4", "--f2", ".5", "--f3", "-.25", "--f4", "+3.5"}));
    CHECK(*f1 == doctest::Approx(0.0001));
    CHECK(*f2 == doctest::Approx(0.5));
    CHECK(*f3 == doctest::Approx(-0.25));
    CHECK(*f4 == doctest::Approx(3.5));
}

TEST_CASE("cxxopts: special float values inf, -inf, nan") {
    cli::App app("t");
    auto inf = app.add<double>("--inf");
    auto ninf = app.add<double>("--ninf");
    auto nan = app.add<double>("--nan");
    REQUIRE(app.parse({"--inf", "inf", "--ninf", "-inf", "--nan", "nan"}));
    CHECK(std::isinf(*inf));
    CHECK(*inf > 0);
    CHECK(std::isinf(*ninf));
    CHECK(*ninf < 0);
    CHECK(std::isnan(*nan));
}

// ── 10. Short flag grouping (CLI11 & argparse) ───────────────────────────────

TEST_CASE("CLI11: short flag grouping with value at end without space") {
    cli::App app("t");
    auto v = app.add_flag("-v");
    auto x = app.add_flag("-x");
    auto f = app.add<std::string>("-f");
    REQUIRE(app.parse({"-vxfoutput.bin"}));
    CHECK(*v == true);
    CHECK(*x == true);
    CHECK(*f == "output.bin");
}

TEST_CASE("CLI11: short flag grouping with value at end with equals") {
    cli::App app("t");
    auto v = app.add_flag("-v");
    auto f = app.add<std::string>("-f");
    REQUIRE(app.parse({"-vf=output.bin"}));
    CHECK(*v == true);
    CHECK(*f == "output.bin");
}

TEST_CASE("CLI11: several short flags may be grouped in one token") {
    cli::App app("t");
    auto a = app.add_flag("-a");
    auto b = app.add_flag("-b");
    auto c = app.add_flag("-c");
    REQUIRE(app.parse({"-abc"}));
    CHECK(*a == true);
    CHECK(*b == true);
    CHECK(*c == true);
}

TEST_CASE("cxxopts: unknown suffix of a short group is forwarded as one token") {
    cli::App app("t");
    app.allow_extras();
    auto verbose = app.add_count("-v");
    REQUIRE(app.parse({"-vvq"}));
    CHECK(*verbose == 2u);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 1);
    CHECK(rest[0] == "-q");
}

TEST_CASE("CLI11: registered single-dash long name beats short grouping") {
    cli::App app("t");
    auto v       = app.add_flag("-v");
    auto verbose = app.add_flag("-verbose");
    REQUIRE(app.parse({"-verbose"}));
    CHECK(*verbose == true);
    CHECK(*v == false);
}

// ── 10b. Compound short arguments and nargs (argparse) ──────────────────────

TEST_CASE("argparse: compound flags followed by a multi-value option") {
    cli::App app("t");
    auto a = app.add_flag("-a");
    auto b = app.add_flag("-b");
    auto c = app.add<std::vector<double>>("-c").nargs(2);
    auto files = app.add<std::vector<std::string>>("--input_files").nargs(3);

    REQUIRE(app.parse({"-abc", "3.14", "2.718",
                       "--input_files", "a.txt", "b.txt", "c.txt"}));
    CHECK(*a);
    CHECK(*b);
    REQUIRE(c->size() == 2);
    CHECK((*c)[0] == doctest::Approx(3.14));
    CHECK((*c)[1] == doctest::Approx(2.718));
    REQUIRE(files->size() == 3);
    CHECK((*files)[0] == "a.txt");
    CHECK((*files)[1] == "b.txt");
    CHECK((*files)[2] == "c.txt");
}

TEST_CASE("argparse: inline equals counts as the first nargs value") {
    cli::App app("t");
    auto values = app.add<std::vector<std::string>>("--long").nargs(2);
    REQUIRE(app.parse({"--long=value1", "value2"}));
    REQUIRE(values->size() == 2);
    CHECK((*values)[0] == "value1");
    CHECK((*values)[1] == "value2");
}

TEST_CASE("argparse: a remaining-style option consumes negative numbers") {
    cli::App app("t");
    auto values = app.add<std::vector<int>>("-i").nargs_at_least(1);
    REQUIRE(app.parse({"-i", "-42", "8", "100", "300"}));
    REQUIRE(values->size() == 4);
    CHECK((*values)[0] == -42);
    CHECK((*values)[1] == 8);
    CHECK((*values)[2] == 100);
    CHECK((*values)[3] == 300);
}

TEST_CASE("argparse: two remaining-style options split at the next option") {
    cli::App app("t");
    auto numbers = app.add<std::vector<int>>("-i").nargs_at_least(1);
    auto words   = app.add<std::vector<std::string>>("-s").nargs_at_least(1);
    REQUIRE(app.parse({"-i", "-42", "8", "100", "300",
                       "-s", "ok", "this", "works"}));
    REQUIRE(numbers->size() == 4);
    CHECK((*numbers)[0] == -42);
    CHECK((*numbers)[3] == 300);
    REQUIRE(words->size() == 3);
    CHECK((*words)[0] == "ok");
    CHECK((*words)[1] == "this");
    CHECK((*words)[2] == "works");
}

TEST_CASE("cxxopts: arbitrary option names and short attached values") {
    // Adapted from cxxopts' "Option names as arbitrary characters" tests.
    cli::App app("t");
    auto hash     = app.add<std::string>("-#");
    auto dollars  = app.add<std::string>("--$$$");
    auto dot      = app.add<std::string>("-.");
    auto at       = app.add_flag("-@");
    auto colon    = app.add_flag("-:");
    auto question = app.add_flag("-?");
    auto bracket  = app.add_flag("-[");
    auto brace    = app.add_flag("-}");
    auto star     = app.add_flag("-*");
    auto tilde    = app.add_flag("-~");
    auto bang     = app.add_flag("-!");

    REQUIRE(app.parse({"-#", "abc", "--$$$=@#$$", "-.=..", "-@:?[}*~!"}));
    CHECK(*hash == "abc");
    CHECK(*dollars == "@#$$");
    CHECK(*dot == "..");
    CHECK(*at);
    CHECK(*colon);
    CHECK(*question);
    CHECK(*bracket);
    CHECK(*brace);
    CHECK(*star);
    CHECK(*tilde);
    CHECK(*bang);
}

TEST_CASE("cxxopts: long names may contain hyphens and underscores") {
    // Adapted from cxxopts' "Arbitrary chars and spaces" case: the trimmed
    // "ab-cd" and "ab_cd" spellings are addressable from argv.
    cli::App app("t");
    auto dashed = app.add<std::string>("--ab-cd");
    auto scored = app.add<std::string>("--ab_cd");

    REQUIRE(app.parse({"--ab-cd", "xyz", "--ab_cd={}()"}));
    CHECK(*dashed == "xyz");
    CHECK(*scored == "{}()");
}

// ── 11. Interleaved positionals and flags (CLI11 & argparse) ─────────────────

TEST_CASE("CLI11: options and positionals interleaved") {
    cli::App app("t");
    auto v = app.add_flag("-v");
    auto o = app.add<std::string>("-o");
    auto p1 = app.add_positional<std::string>("p1");
    auto p2 = app.add_positional<std::string>("p2");
    REQUIRE(app.parse({"file1.txt", "-v", "file2.txt", "-o", "out.bin"}));
    CHECK(*v == true);
    CHECK(*o == "out.bin");
    CHECK(*p1 == "file1.txt");
    CHECK(*p2 == "file2.txt");
}

TEST_CASE("CLI11: a lone '-' is positional regardless of option order") {
    {
        cli::App app("t");
        auto l     = app.add_flag("-l");
        auto input = app.add_positional<std::string>("input");
        REQUIRE(app.parse({"-", "-l"}));
        CHECK(*input == "-");
        CHECK(*l == true);
    }
    {
        cli::App app("t");
        auto l     = app.add_flag("-l");
        auto input = app.add_positional<std::string>("input");
        REQUIRE(app.parse({"-l", "-"}));
        CHECK(*input == "-");
        CHECK(*l == true);
    }
}

TEST_CASE("cxxopts: known options interleaved with extras preserve extras order") {
    cli::App app("t");
    app.allow_extras();
    auto output = app.add<std::string>("--output");
    REQUIRE(app.parse({"--extra", "before", "--output", "out.txt", "tail"}));
    CHECK(*output == "out.txt");
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 3);
    CHECK(rest[0] == "--extra");
    CHECK(rest[1] == "before");
    CHECK(rest[2] == "tail");
}

// ── 12. Repeated scalar options take last (CLI11 TakeLastOpt) ────────────────

TEST_CASE("CLI11: repeated scalar option keeps the last value") {
    cli::App app("t");
    auto port = app.add<int>("--port");
    REQUIRE(app.parse({"--port", "8000", "--port", "8080"}));
    CHECK(*port == 8080);
}

// ── 13. Abbreviation resolution (CLI11) ─────────────────────────────────────

TEST_CASE("CLI11: long option abbreviations accept inline values") {
    cli::App app("t");
    app.allow_abbrev();
    auto verbose = app.add<std::string>("--verbose");
    REQUIRE(app.parse({"--verb=yes"}));
    CHECK(*verbose == "yes");
}

TEST_CASE("CLI11: an exact option name wins over a shorter abbreviation") {
    cli::App app("t");
    app.allow_abbrev();
    auto exact  = app.add_flag("--ver");
    auto longer = app.add_flag("--verbose");
    REQUIRE(app.parse({"--ver"}));
    CHECK(*exact == true);
    CHECK(*longer == false);
}

// ── 14. Subcommand scoping & help bypass (CLI11 SubcommandTest) ──────────────

TEST_CASE("CLI11: child --help bypasses parent required options") {
    cli::App app("t");
    (void)app.add<std::string>("--parent-req").required();
    auto& sub = app.subcommand("sub");
    (void)sub.add<int>("--child-opt");
    auto res = app.parse({"sub", "--help"});
    CHECK(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::HelpRequested);
    CHECK(res.error().context == &sub);
}

TEST_CASE("CLI11: child required option enforced when child executed") {
    cli::App app("t");
    auto& sub = app.subcommand("build");
    (void)sub.add<std::string>("--target").required();
    auto res = app.parse({"build"});
    CHECK(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(res.error().context == &sub);
}

TEST_CASE("CLI11: child option shadows parent option of same name") {
    cli::App app("t");
    auto p_opt = app.add<std::string>("--name").default_val("parent");
    auto& sub = app.subcommand("run");
    auto c_opt = sub.add<std::string>("--name").default_val("child");
    REQUIRE(app.parse({"run", "--name", "custom"}));
    CHECK(*c_opt == "custom");
    CHECK(*p_opt == "parent");
}

} // TEST_SUITE
