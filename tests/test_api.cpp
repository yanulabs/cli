#include "test_support.hpp"

#include <type_traits>

TEST_SUITE_BEGIN("api");

TEST_CASE("metadata constructors and accessors") {
    cli::App app("tool", "description", "2.1.0");
    CHECK(app.name() == "tool");
    CHECK(app.description() == "description");
    CHECK(app.ver() == "2.1.0");

    cli::App minimal("plain");
    CHECK(minimal.name() == "plain");
    CHECK(minimal.description().empty());
    CHECK(minimal.ver().empty());
}

TEST_CASE("metadata builders are chainable and reachable through help") {
    cli::App app("tool", "description");
    app.version("3.0.0").author("Ada").example("tool --verbose").example("tool -j 4");

    CHECK(app.ver() == "3.0.0");
    const auto help = app.help_text();
    CHECK(contains(help, "Author: Ada"));
    CHECK(contains(help, "tool --verbose"));
    CHECK(contains(help, "tool -j 4"));
    CHECK(contains(help, "v3.0.0"));
}

TEST_CASE("ParseResult and ParseError aliases match the documented types") {
    static_assert(std::is_same_v<cli::ParseResult,
                                 std::expected<void, cli::ParseError>>);
    static_assert(std::is_same_v<decltype(cli::ParseError::help_requested(nullptr)),
                                 cli::ParseError>);
}

TEST_CASE("ParseError exposes control-flow and diagnostics kinds") {
    auto h = cli::ParseError::help_requested(nullptr);
    auto v = cli::ParseError::version_requested(nullptr);
    auto u = cli::ParseError::unknown("--nope");

    CHECK(h.is_exit_request());
    CHECK(v.is_exit_request());
    CHECK(!u.is_exit_request());
    CHECK(h.message.empty());
    CHECK(v.message.empty());
    CHECK(contains(u.message, "--nope"));
}

TEST_CASE("App is intentionally non-copyable and non-movable") {
    static_assert(!std::is_copy_constructible_v<cli::App>);
    static_assert(!std::is_copy_assignable_v<cli::App>);
    static_assert(!std::is_move_constructible_v<cli::App>);
    static_assert(!std::is_move_assignable_v<cli::App>);
}

TEST_CASE("v1 option builder registers both names and help") {
    cli::App app("t");
    auto value = app.option<int>("-n", "--num", "A number");
    REQUIRE(app.parse({"-n", "7"}));
    CHECK(*value == 7);
    CHECK(app["--num"].as<int>() == 7);
    CHECK(contains(app.help_text(), "A number"));
}

TEST_CASE("v1 long-only option and long-only flag overloads") {
    cli::App app("t");
    auto value = app.option<std::string>("--name", "Name");
    auto flag  = app.flag("--verbose", "Verbose");

    REQUIRE(app.parse({"--name", "Ada", "--verbose"}));
    CHECK(*value == "Ada");
    CHECK(*flag);
}

TEST_CASE("v1 flag and positional builders") {
    cli::App app("t");
    auto flag = app.flag("-v", "--verbose", "Verbose");
    auto pos  = app.positional<std::string>("file", "Input file");

    REQUIRE(app.parse({"-v", "input.txt"}));
    CHECK(*flag);
    CHECK(*pos == "input.txt");
    CHECK(contains(app.help_text(), "Input file"));
}

TEST_CASE("v2 fluent builders and short_name") {
    cli::App app("t");
    auto number = app.add<int>("--num").short_name("-n").help("Number").default_val(4);
    auto flag   = app.add_flag("--verbose").short_name("-v").help("Verbose");
    auto count  = app.add_count("--debug").short_name("-d").help("Debug level");
    auto file   = app.add_positional<std::string>("file").help("Input");

    REQUIRE(app.parse({"-n", "9", "-v", "-dd", "input.txt"}));
    CHECK(*number == 9);
    CHECK(*flag);
    CHECK(*count == 2u);
    CHECK(*file == "input.txt");
    CHECK(app["-n"].as<int>() == 9);
    CHECK(app["--verbose"].as<bool>());
    CHECK(app["--debug"].as_count() == 2u);
}

TEST_CASE("handles support copies, get(), value(), value_or() and operator->") {
    cli::App app("t");
    auto path = app.add<std::filesystem::path>("--path").short_name("-p");
    REQUIRE(!path);                          // no value yet

    REQUIRE(app.parse({"-p", "/tmp/x"}));
    auto copy = path;
    REQUIRE(copy);
    CHECK(copy->string() == "/tmp/x");
    CHECK(copy.value().string() == "/tmp/x");
    CHECK(copy.value_or(std::filesystem::path{"/fallback"}).string() == "/tmp/x");
    REQUIRE(copy.get().has_value());
    CHECK(copy.get()->string() == "/tmp/x");
    CHECK(copy.is_set());
}

TEST_CASE("positional handle value_or and is_set") {
    cli::App app("t");
    auto pos = app.add_positional<int>("n").required(false);
    REQUIRE(app.parse({}));
    CHECK(!pos);
    CHECK(pos.value_or(5) == 5);
    CHECK(!pos.is_set());

    REQUIRE(app.parse({"8"}));
    CHECK(*pos == 8);
    CHECK(pos.value_or(5) == 8);
    CHECK(pos.is_set());
}

TEST_CASE("flag and count handles expose value semantics") {
    cli::App app("t");
    auto flag  = app.add_flag("--color").default_val(true);
    auto count = app.add_count("--verbose").short_name("-v");

    REQUIRE(app.parse({}));
    CHECK(*flag);
    CHECK(!flag.is_set());

    REQUIRE(app.parse({"--color=false", "-vv"}));
    CHECK(!*flag);
    CHECK(flag.is_set());
    CHECK(*count == 2u);
    CHECK(count.value_or(9u) == 2u);

    REQUIRE(app.parse({}));
    CHECK(count.value_or(9u) == 9u);
}

TEST_CASE("const App supports lookup through operator[]") {
    cli::App app("t");
    (void)app.add<int>("--num").default_val(6);
    REQUIRE(app.parse({}));

    const cli::App& view = app;
    CHECK(view["--num"].found());
    CHECK(view["--num"].as<int>() == 6);
}

TEST_CASE("registration rejects malformed option names") {
    cli::App app("t");

    CHECK_THROWS_AS((void)app.add<int>(""), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("num"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("-"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("--"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("---num"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("--num=1"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("--bad name"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.option<int>("", "", "help"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.flag("", "", "help"), std::invalid_argument);

    // A rejected registration must leave the app fully usable.
    auto good = app.add<int>("--good");
    REQUIRE(app.parse({"--good", "7"}));
    CHECK(*good == 7);
}

TEST_CASE("duplicate option names are rejected transactionally") {
    cli::App app("t");
    auto first = app.add<int>("--num").short_name("-n");

    CHECK_THROWS_AS((void)app.add<int>("--num"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("--other").short_name("-n"), std::invalid_argument);

    // The failed short-name registration did not create a second "-n" entry;
    // the long name it was registered under remains usable.
    CHECK(app["--other"].found());
    CHECK(app["-n"].as<int>() == 0);
    REQUIRE(app.parse({"--num", "3", "-n", "4"}));
    CHECK(*first == 4);
    CHECK(app["-n"].as<int>() == 4);
}

TEST_CASE("command names and aliases are unique among siblings") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "Compile");

    CHECK_THROWS_AS((void)app.subcommand("build"), std::invalid_argument);
    CHECK_THROWS_AS((void)build.alias("build"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.subcommand("--bad"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.subcommand(""), std::invalid_argument);

    build.alias("b");
    CHECK_THROWS_AS((void)build.alias("b"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.subcommand("b"), std::invalid_argument);
    auto& test = app.subcommand("test");
    CHECK_THROWS_AS((void)test.alias("b"), std::invalid_argument);

    REQUIRE(app.parse({"b"}));
    CHECK(app.active_subcommand() == &build);
}

TEST_CASE("positional registration validates names and ordering") {
    cli::App app("t");
    CHECK_THROWS_AS((void)app.add_positional<std::string>(""), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add_positional<std::string>("bad name"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.positional<std::string>("", ""), std::invalid_argument);

    (void)app.positional<std::vector<std::string>>("rest", "Rest");
    CHECK_THROWS_AS((void)app.add_positional<std::string>("late"), std::invalid_argument);
}

TEST_CASE("group references are validated before parsing") {
    cli::App app("t");
    (void)app.add_flag("--a");
    (void)app.add_flag("--b");

    cli::App unknown("unknown");
    (void)unknown.add_flag("--a");
    unknown.mutex_group({"--a", "--missing"});
    CHECK_THROWS_AS((void)unknown.parse({}), std::invalid_argument);

    cli::App duplicate("duplicate");
    (void)duplicate.add_flag("--a");
    duplicate.require_together({"--a", "--a"});
    CHECK_THROWS_AS((void)duplicate.parse({}), std::invalid_argument);

    cli::App aliased("aliased");
    (void)aliased.add_flag("--alpha").short_name("-a");
    aliased.mutex_group({"--alpha", "-a"});
    CHECK_THROWS_AS((void)aliased.parse({}), std::invalid_argument);

    app.mutex_group({"--a", "--b"});
    REQUIRE(app.parse({"--a"}));
}

TEST_CASE("constraint builders reject impossible configurations") {
    {
        cli::App app("t");
        CHECK_THROWS_AS((void)app.add<int>("--num").range(10, 1), std::invalid_argument);
    }
    {
        cli::App app("t");
        CHECK_THROWS_AS((void)app.add<int>("--num").choices({}), std::invalid_argument);
    }
    {
        cli::App app("t");
        CHECK_THROWS_AS((void)app.add_positional<int>("p").range(10, 1), std::invalid_argument);
    }
    {
        cli::App app("t");
        CHECK_THROWS_AS((void)app.add_positional<int>("q").choices({}), std::invalid_argument);
    }

    cli::App app("t");
    auto ok = app.add<int>("--ok").range(1, 10);
    REQUIRE(app.parse({"--ok", "5"}));
    CHECK(*ok == 5);
}

TEST_CASE("environment variable names are validated at registration") {
    cli::App app("t");
    CHECK_THROWS_AS((void)app.add<int>("--a").env(""), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add<int>("--b").env("BAD=NAME"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add_flag("--c").env(""), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add_count("--d").env("BAD=NAME"), std::invalid_argument);
    CHECK_THROWS_AS((void)app.add_positional<int>("p").env(""), std::invalid_argument);

    // A well-formed name is accepted.
    cli::App ok_app("t");
    auto ok = ok_app.add<int>("--ok").env("CLI_TEST_VALID_ENV");
    REQUIRE(ok_app.parse({"--ok", "1"}));
    CHECK(*ok == 1);
}

TEST_CASE("group constraints added after a parse are picked up") {
    cli::App app("t");
    (void)app.add_flag("--a");
    (void)app.add_flag("--b");

    REQUIRE(app.parse({"--a"}));
    app.mutex_group({"--a", "--b"});

    auto r = app.parse({"--a", "--b"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_SUITE_END();
