#include "test_support.hpp"

TEST_SUITE_BEGIN("flags");

TEST_CASE("a flag is false and not set when absent") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    REQUIRE(app.parse({}));
    CHECK(!*verbose);
    CHECK(!verbose.is_set());
}

TEST_CASE("a flag is true when given by long or short name") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v");

    REQUIRE(app.parse({"--verbose"}));
    CHECK(*verbose);
    CHECK(static_cast<bool>(verbose));
    CHECK(verbose.is_set());

    REQUIRE(app.parse({"-v"}));
    CHECK(*verbose);
    CHECK(static_cast<bool>(verbose));
    CHECK(verbose.is_set());
}

TEST_CASE("repeating a flag leaves it true") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    REQUIRE(app.parse({"--verbose", "-v", "--verbose"}));
    CHECK(*verbose);
}

TEST_CASE("flags can be explicitly assigned true or false") {
    cli::App app("t");
    auto color = app.add_flag("--color").default_val(true);

    REQUIRE(app.parse({"--color=false"}));
    CHECK(!*color);
    CHECK(color.is_set());

    REQUIRE(app.parse({"--color=true"}));
    CHECK(*color);
    CHECK(color.is_set());

    REQUIRE(app.parse({"--color=0"}));
    CHECK(!*color);

    REQUIRE(app.parse({"--color=1"}));
    CHECK(*color);
}

TEST_CASE("explicit boolean flag values are case-insensitive") {
    cli::App app("t");
    auto color = app.add_flag("--color");

    REQUIRE(app.parse({"--color=TRUE"}));
    CHECK(*color);

    REQUIRE(app.parse({"--color=No"}));
    CHECK(!*color);
}

TEST_CASE("an invalid explicit flag value is rejected") {
    cli::App app("t");
    (void)app.add_flag("--color");

    auto r = app.parse({"--color=maybe"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "maybe"));
}

TEST_CASE("a negatable flag defaults to false and accepts --no-") {
    cli::App app("t");
    auto color = app.add_flag("--color").negatable();

    REQUIRE(app.parse({}));
    CHECK(!*color);
    CHECK(!color.is_set());

    REQUIRE(app.parse({"--no-color"}));
    CHECK(!*color);
    CHECK(color.is_set());
}

TEST_CASE("negatable flags use last-one-wins ordering") {
    cli::App app("t");
    auto color = app.add_flag("--color").negatable();

    REQUIRE(app.parse({"--color", "--no-color"}));
    CHECK(!*color);

    REQUIRE(app.parse({"--no-color", "--color"}));
    CHECK(*color);
}

TEST_CASE("a default-true negatable flag stays true when absent") {
    cli::App app("t");
    auto color = app.add_flag("--color").default_val(true).negatable();

    REQUIRE(app.parse({}));
    CHECK(*color);
    CHECK(!color.is_set());

    REQUIRE(app.parse({"--no-color"}));
    CHECK(!*color);
}

TEST_CASE("--no-X is unknown for a non-negatable flag") {
    cli::App app("t");
    (void)app.add_flag("--color");
    auto r = app.parse({"--no-color"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("an exact --no-X option wins over negation") {
    cli::App app("t");
    auto no_cache = app.add_flag("--no-cache");
    auto cache    = app.add_flag("--cache").negatable();

    REQUIRE(app.parse({"--no-cache"}));
    CHECK(*no_cache);
    CHECK(!cache.is_set());
}

TEST_CASE("a required flag must be present") {
    cli::App app("t");
    (void)app.add_flag("--force").required();

    auto missing = app.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);

    REQUIRE(app.parse({"--force"}));
}

TEST_CASE("required(false) makes a flag optional") {
    cli::App app("t");
    (void)app.add_flag("--force").required(false);
    REQUIRE(app.parse({}));
}

TEST_CASE("count options start at zero") {
    cli::App app("t");
    auto verbosity = app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse({}));
    CHECK(*verbosity == 0u);
    CHECK(!verbosity.is_set());
    CHECK(!*verbosity);
}

TEST_CASE("count options count long, short and grouped occurrences") {
    cli::App app("t");
    auto verbosity = app.add_count("--verbose").short_name("-v");

    REQUIRE(app.parse({"--verbose"}));
    CHECK(*verbosity == 1u);

    REQUIRE(app.parse({"-v", "-v"}));
    CHECK(*verbosity == 2u);

    REQUIRE(app.parse({"-vvv"}));
    CHECK(*verbosity == 3u);

    REQUIRE(app.parse({"--verbose", "-vv"}));
    CHECK(*verbosity == 3u);
}

TEST_CASE("count options can be grouped with ordinary flags") {
    cli::App app("t");
    auto verbose = app.add_count("--verbose").short_name("-v");
    auto quiet   = app.add_flag("--quiet").short_name("-q");
    auto debug   = app.add_flag("--debug").short_name("-d");

    REQUIRE(app.parse({"-vvqd"}));
    CHECK(*verbose == 2u);
    CHECK(*quiet);
    CHECK(*debug);
}

TEST_CASE("count options reject inline values") {
    cli::App app("t");
    (void)app.add_count("--verbose").short_name("-v");

    auto r = app.parse({"--verbose=3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnexpectedValue);
    CHECK(contains(r.error().message, "3"));

    auto short_r = app.parse({"-v=3"});
    REQUIRE(!short_r);
    CHECK(short_r.error().kind == cli::ParseError::Kind::UnexpectedValue);
    CHECK(contains(short_r.error().message, "3"));
}

TEST_CASE("short flags accept explicit values with equals") {
    cli::App app("t");
    auto flag = app.add_flag("--color").short_name("-c");

    REQUIRE(app.parse({"-c=false"}));
    CHECK(!*flag);
    CHECK(flag.is_set());

    REQUIRE(app.parse({"-c=TRUE"}));
    CHECK(*flag);

    auto bad = app.parse({"-c=maybe"});
    REQUIRE(!bad);
    CHECK(bad.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("a required count option must appear at least once") {
    cli::App app("t");
    (void)app.add_count("--verbose").short_name("-v").required();

    auto missing = app.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);

    REQUIRE(app.parse({"-v"}));
}

TEST_CASE("built-in help and version work inside a short group") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("--verbose").short_name("-v");

    auto help = app.parse({"-vh"});
    REQUIRE(!help);
    CHECK(help.error().kind == cli::ParseError::Kind::HelpRequested);

    auto version = app.parse({"-vV"});
    REQUIRE(!version);
    CHECK(version.error().kind == cli::ParseError::Kind::VersionRequested);
}

TEST_CASE("help and version are unknown when no version is configured") {
    cli::App app("tool");

    auto long_version = app.parse({"--version"});
    REQUIRE(!long_version);
    CHECK(long_version.error().kind == cli::ParseError::Kind::UnknownOption);

    auto short_version = app.parse({"-V"});
    REQUIRE(!short_version);
    CHECK(short_version.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("custom help and version flags replace the built-ins") {
    cli::App app("tool", "desc", "1.0.0");
    auto long_help    = app.add_flag("--help");
    auto short_help   = app.add_flag("-h");
    auto long_version = app.add_flag("--version");
    auto short_ver    = app.add_flag("-V");

    REQUIRE(app.parse({"--help", "-h", "--version", "-V"}));
    CHECK(*long_help);
    CHECK(*short_help);
    CHECK(*long_version);
    CHECK(*short_ver);
}

TEST_CASE("count value_or is fallback-sensitive before first use") {
    cli::App app("t");
    auto verbosity = app.add_count("--verbose").short_name("-v");

    REQUIRE(app.parse({}));
    CHECK(verbosity.value_or(7u) == 7u);

    REQUIRE(app.parse({"-vv"}));
    CHECK(verbosity.value_or(7u) == 2u);
}

TEST_SUITE_END();
