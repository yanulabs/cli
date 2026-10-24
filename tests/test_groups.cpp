#include "test_support.hpp"

TEST_SUITE_BEGIN("groups");

TEST_CASE("mutex group allows zero or one explicit option") {
    cli::App app("t");
    (void)app.add_flag("--verbose").short_name("-v");
    (void)app.add_flag("--quiet").short_name("-q");
    app.mutex_group({"--verbose", "--quiet"});

    REQUIRE(app.parse({}));
    REQUIRE(app.parse({"-v"}));
    REQUIRE(app.parse({"--quiet"}));
}

TEST_CASE("mutex group rejects two explicit options") {
    cli::App app("t");
    (void)app.add_flag("--verbose").short_name("-v");
    (void)app.add_flag("--quiet").short_name("-q");
    app.mutex_group({"--verbose", "--quiet"});

    auto r = app.parse({"-v", "-q"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
    CHECK(contains(r.error().message, "--verbose"));
    CHECK(contains(r.error().message, "--quiet"));
    CHECK(r.error().context == &app);
}

TEST_CASE("value validation is reported before a mutex conflict") {
    cli::App app("t");
    auto a = app.add<int>("--a").validate([](int v) { return v > 0; },
                                         "must be positive");
    auto b = app.add_flag("--b");
    app.mutex_group({"--a", "--b"});

    auto r = app.parse({"--a", "-1", "--b"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(*a == -1);
    CHECK(*b);
}

TEST_CASE("mutex groups work with three options") {
    cli::App app("t");
    (void)app.add_flag("--json");
    (void)app.add_flag("--yaml");
    (void)app.add_flag("--toml");
    app.mutex_group({"--json", "--yaml", "--toml"});

    REQUIRE(app.parse({"--yaml"}));

    auto r = app.parse({"--json", "--toml"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("mutex groups may refer to short names") {
    cli::App app("t");
    (void)app.add_flag("--verbose").short_name("-v");
    (void)app.add_flag("--quiet").short_name("-q");
    app.mutex_group({"-v", "-q"});

    auto r = app.parse({"-v", "-q"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("mutex groups ignore defaults") {
    cli::App app("t");
    (void)app.add_flag("--color").default_val(true);
    (void)app.add_flag("--mono").default_val(true);
    app.mutex_group({"--color", "--mono"});

    REQUIRE(app.parse({}));
}

TEST_CASE("mutex groups include environment-provided values") {
    cli::App app("t");
    (void)app.add_flag("--color").env("CLI_TEST_MUTEX_COLOR");
    (void)app.add_flag("--mono").env("CLI_TEST_MUTEX_MONO");
    app.mutex_group({"--color", "--mono"});

    ScopedEnv color("CLI_TEST_MUTEX_COLOR", "true");
    ScopedEnv mono("CLI_TEST_MUTEX_MONO", "true");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("mutex groups combine CLI and environment values") {
    cli::App app("t");
    (void)app.add_flag("--color").env("CLI_TEST_MUTEX_COLOR_CLI");
    (void)app.add_flag("--mono");
    app.mutex_group({"--color", "--mono"});

    ScopedEnv color("CLI_TEST_MUTEX_COLOR_CLI", "true");

    auto r = app.parse({"--mono"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("require-together accepts all options or none") {
    cli::App app("t");
    (void)app.add<std::string>("--output");
    (void)app.add<std::string>("--format");
    app.require_together({"--output", "--format"});

    REQUIRE(app.parse({}));
    REQUIRE(app.parse({"--output", "out.bin", "--format", "json"}));
}

TEST_CASE("require-together rejects a partial set") {
    cli::App app("t");
    (void)app.add<std::string>("--output").required();
    (void)app.add<std::string>("--format");
    app.require_together({"--output", "--format"});

    auto r = app.parse({"--output", "out.bin"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::RequiresOption);
    CHECK(contains(r.error().message, "--output"));
    CHECK(contains(r.error().message, "--format"));
}

TEST_CASE("require-together handles three options") {
    cli::App app("t");
    (void)app.add<std::string>("--host");
    (void)app.add<std::string>("--port");
    (void)app.add<std::string>("--user");
    app.require_together({"--host", "--port", "--user"});

    REQUIRE(app.parse({"--host", "h", "--port", "1", "--user", "u"}));

    auto r = app.parse({"--host", "h"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::RequiresOption);
}

TEST_CASE("require-together may refer to short names") {
    cli::App app("t");
    (void)app.add<std::string>("--output").short_name("-o");
    (void)app.add<std::string>("--format").short_name("-f");
    app.require_together({"-o", "-f"});

    auto r = app.parse({"-o", "out"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::RequiresOption);
}

TEST_CASE("require-together treats environment values as provided") {
    cli::App app("t");
    (void)app.add<std::string>("--output").env("CLI_TEST_REQ_OUTPUT");
    (void)app.add<std::string>("--format").env("CLI_TEST_REQ_FORMAT");
    app.require_together({"--output", "--format"});

    ScopedEnv output("CLI_TEST_REQ_OUTPUT", "out.bin");

    auto missing = app.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::RequiresOption);

    ScopedEnv format("CLI_TEST_REQ_FORMAT", "json");
    REQUIRE(app.parse({}));
}

TEST_CASE("require-together ignores pure defaults") {
    cli::App app("t");
    (void)app.add<std::string>("--output").default_val("out.bin");
    (void)app.add<std::string>("--format").default_val("json");
    app.require_together({"--output", "--format"});

    REQUIRE(app.parse({}));
}

TEST_CASE("required checks take precedence over require-together") {
    cli::App app("t");
    (void)app.add<std::string>("--output").required();
    (void)app.add<std::string>("--format").required();
    app.require_together({"--output", "--format"});

    auto r = app.parse({"--output", "out.bin"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(r.error().message, "--format"));
}

TEST_CASE("group constraints are re-evaluated after reset") {
    cli::App app("t");
    (void)app.add_flag("--a");
    (void)app.add_flag("--b");
    app.mutex_group({"--a", "--b"});

    auto bad = app.parse({"--a", "--b"});
    REQUIRE(!bad);
    REQUIRE(app.parse({"--a"}));
}

TEST_SUITE_END();
