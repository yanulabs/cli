#include "test_support.hpp"

TEST_SUITE_BEGIN("tokens");

TEST_CASE("end-of-options marker passes the remainder as positionals") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file");
    auto rest = app.add_positional<std::vector<std::string>>("rest");

    REQUIRE(app.parse({"--", "--not-an-option", "-x"}));
    CHECK(*file == "--not-an-option");
    REQUIRE(rest->size() == 1);
    CHECK((*rest)[0] == "-x");
}

TEST_CASE("a lone end-of-options marker is accepted") {
    cli::App app("t");
    REQUIRE(app.parse({"--"}));
}

TEST_CASE("a second end-of-options marker becomes a positional") {
    cli::App app("t");
    auto values = app.add_positional<std::vector<std::string>>("values");
    REQUIRE(app.parse({"--", "--", "x"}));
    REQUIRE(values->size() == 2);
    CHECK((*values)[0] == "--");
    CHECK((*values)[1] == "x");
}

TEST_CASE("end-of-options marker suppresses help and version") {
    cli::App app("tool", "desc", "1.0.0");
    auto arg = app.add_positional<std::string>("arg");

    auto help = app.parse({"--", "--help"});
    REQUIRE(help);
    CHECK(*arg == "--help");

    auto version = app.parse({"--", "-V"});
    REQUIRE(version);
    CHECK(*arg == "-V");
}

TEST_CASE("a lone dash is a positional value") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file");
    REQUIRE(app.parse({"-"}));
    CHECK(*file == "-");
}

TEST_CASE("a lone dash without a positional is an unexpected argument") {
    cli::App app("t");
    auto r = app.parse({"-"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
    CHECK(contains(r.error().message, "'-'"));
}

TEST_CASE("an empty token is a valid positional value") {
    cli::App app("t");
    auto arg = app.add_positional<std::string>("arg");
    REQUIRE(app.parse({""}));
    CHECK(arg.is_set());
    CHECK(arg->empty());
}

TEST_CASE("short flags can be grouped") {
    cli::App app("t");
    auto v = app.add_flag("--verbose").short_name("-v");
    auto q = app.add_flag("--quiet").short_name("-q");
    auto d = app.add_flag("--debug").short_name("-d");

    REQUIRE(app.parse({"-vqd"}));
    CHECK(*v);
    CHECK(*q);
    CHECK(*d);
}

TEST_CASE("a value option consumes the rest of a short group") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto jobs    = app.add<int>("--jobs").short_name("-j");

    REQUIRE(app.parse({"-vj8"}));
    CHECK(*verbose);
    CHECK(*jobs == 8);
}

TEST_CASE("a value option at the end of a short group requires a following value") {
    cli::App app("t");
    (void)app.add_flag("--verbose").short_name("-v");
    (void)app.add<int>("--jobs").short_name("-j");

    auto r = app.parse({"-vj"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingValue);
    CHECK(contains(r.error().message, "-j"));
}

TEST_CASE("a short value may be attached or separated") {
    cli::App app("t");
    auto a = app.add<int>("--a").short_name("-a");
    auto b = app.add<int>("--b").short_name("-b");

    REQUIRE(app.parse({"-a5", "-b", "6"}));
    CHECK(*a == 5);
    CHECK(*b == 6);
}

TEST_CASE("short and long inline values accept an equals sign") {
    cli::App app("t");
    auto n = app.add<int>("--num").short_name("-n");
    REQUIRE(app.parse({"-n=5"}));
    CHECK(*n == 5);

    REQUIRE(app.parse({"--num=6"}));
    CHECK(*n == 6);
}

TEST_CASE("long inline values preserve additional equals signs") {
    cli::App app("t");
    auto define = app.add<std::string>("--define");
    REQUIRE(app.parse({"--define=A=B=C"}));
    CHECK(*define == "A=B=C");
}

TEST_CASE("an empty inline value is valid for strings") {
    cli::App app("t");
    auto name = app.add<std::string>("--name");
    REQUIRE(app.parse({"--name="}));
    CHECK(name.is_set());
    CHECK(name->empty());
}

TEST_CASE("empty inline values are rejected for numbers") {
    cli::App app("t");
    (void)app.add<int>("--num").short_name("-n");

    auto long_form = app.parse({"--num="});
    REQUIRE(!long_form);
    CHECK(long_form.error().kind == cli::ParseError::Kind::InvalidValue);

    auto short_form = app.parse({"-n="});
    REQUIRE(!short_form);
    CHECK(short_form.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("missing values at end of line are reported") {
    cli::App app("t");
    (void)app.add<std::string>("--name").short_name("-n");

    auto long_form = app.parse({"--name"});
    REQUIRE(!long_form);
    CHECK(long_form.error().kind == cli::ParseError::Kind::MissingValue);

    auto short_form = app.parse({"-n"});
    REQUIRE(!short_form);
    CHECK(short_form.error().kind == cli::ParseError::Kind::MissingValue);
}

TEST_CASE("the next token is consumed as a value even when it looks like an option") {
    cli::App app("t");
    auto name = app.add<std::string>("--name");
    auto num  = app.add<int>("--num");

    REQUIRE(app.parse({"--name", "--something", "--num", "-5"}));
    CHECK(*name == "--something");
    CHECK(*num == -5);
}

TEST_CASE("an invalid option-looking value is still a value error") {
    cli::App app("t");
    (void)app.add<int>("--num");
    auto r = app.parse({"--num", "--verbose"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "--verbose"));
}

TEST_CASE("a repeated scalar option uses the last value") {
    cli::App app("t");
    auto num = app.add<int>("--num");
    REQUIRE(app.parse({"--num", "1", "--num", "2", "--num=3"}));
    CHECK(*num == 3);
}

TEST_CASE("short repeated scalar options use the last value") {
    cli::App app("t");
    auto num = app.add<int>("--num").short_name("-n");
    REQUIRE(app.parse({"-n1", "-n", "2"}));
    CHECK(*num == 2);
}

TEST_CASE("options and positionals may be interleaved") {
    cli::App app("t");
    auto src  = app.add_positional<std::string>("src");
    auto dst  = app.add_positional<std::string>("dst");
    auto flag = app.add_flag("--verbose").short_name("-v");

    REQUIRE(app.parse({"a.txt", "-v", "b.txt"}));
    CHECK(*src == "a.txt");
    CHECK(*dst == "b.txt");
    CHECK(*flag);
}

TEST_CASE("option and subcommand names are case-sensitive") {
    cli::App app("t");
    (void)app.add<int>("--num").short_name("-n");
    (void)app.subcommand("Build", "");

    auto long_form = app.parse({"--Num", "1"});
    REQUIRE(!long_form);
    CHECK(long_form.error().kind == cli::ParseError::Kind::UnknownOption);

    auto short_form = app.parse({"-N"});
    REQUIRE(!short_form);
    CHECK(short_form.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("an unknown short in a group reports the failing spelling") {
    cli::App app("t");
    (void)app.add_flag("--verbose").short_name("-v");
    auto r = app.parse({"-vx"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "-x"));
}

TEST_CASE("a triple-dash token is an unknown long option") {
    cli::App app("t");
    auto r = app.parse({"---verbose"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "---verbose"));
}

TEST_CASE("an equals-only long token is an unknown option") {
    cli::App app("t");
    auto r = app.parse({"--=x"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("a registered negative-number short option beats positional interpretation") {
    cli::App app("t");
    auto one = app.add<int>("--one").short_name("-1");

    REQUIRE(app.parse({"-1", "5"}));
    CHECK(*one == 5);

    auto missing = app.parse({"-1"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingValue);
}

TEST_CASE("a registered single-dash name that looks numeric is matched exactly") {
    cli::App app("t");
    auto value = app.add<int>("-1abc");

    REQUIRE(app.parse({"-1abc", "7"}));
    CHECK(*value == 7);
}

TEST_CASE("a registered single-dash name accepts inline values") {
    cli::App app("t");
    auto value = app.add<int>("-verbose");
    auto flag  = app.add_flag("-color");

    REQUIRE(app.parse({"-verbose=7", "-color=false"}));
    CHECK(*value == 7);
    CHECK(!*flag);
    CHECK(flag.is_set());

    auto bad = app.parse({"-verbose=oops"});
    REQUIRE(!bad);
    CHECK(bad.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("a registered numeric single-dash name accepts inline values") {
    cli::App app("t");
    auto value = app.add<int>("-1abc");

    REQUIRE(app.parse({"-1abc=9"}));
    CHECK(*value == 9);
}

TEST_CASE("negative numeric tokens with no positional are unexpected arguments") {
    cli::App app("t");
    auto integer = app.parse({"-42"});
    REQUIRE(!integer);
    CHECK(integer.error().kind == cli::ParseError::Kind::TooManyPositionals);

    auto real = app.parse({"-3.14"});
    REQUIRE(!real);
    CHECK(real.error().kind == cli::ParseError::Kind::TooManyPositionals);
}

TEST_SUITE_END();
