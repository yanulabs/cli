#include "test_support.hpp"

TEST_SUITE_BEGIN("environment");

TEST_CASE("an environment value is used when the option is absent") {
    cli::App app("t");
    auto jobs = app.add<int>("--jobs").default_val(2).env("CLI_TEST_ENV_JOBS");
    ScopedEnv env("CLI_TEST_ENV_JOBS", "9");

    REQUIRE(app.parse({}));
    CHECK(*jobs == 9);
    CHECK(!jobs.is_set());
    CHECK(app.has_option("--jobs"));
}

TEST_CASE("an environment value overrides a default") {
    cli::App app("t");
    auto jobs = app.add<int>("--jobs").default_val(2).env("CLI_TEST_ENV_DEFAULT");
    ScopedEnv env("CLI_TEST_ENV_DEFAULT", "8");

    REQUIRE(app.parse({}));
    CHECK(*jobs == 8);
}

TEST_CASE("the command line takes precedence over the environment") {
    cli::App app("t");
    auto jobs = app.add<int>("--jobs").env("CLI_TEST_ENV_CLI_PRECEDENCE");
    ScopedEnv env("CLI_TEST_ENV_CLI_PRECEDENCE", "9");

    REQUIRE(app.parse({"--jobs", "3"}));
    CHECK(*jobs == 3);
    CHECK(jobs.is_set());
}

TEST_CASE("a string environment variable may be empty") {
    cli::App app("t");
    auto name = app.add<std::string>("--name").env("CLI_TEST_ENV_EMPTY_STRING");
    ScopedEnv env("CLI_TEST_ENV_EMPTY_STRING", "");

    REQUIRE(app.parse({}));
    CHECK(name);
    CHECK(name->empty());
    CHECK(!name.is_set());
    CHECK(app.has_option("--name"));
}

TEST_CASE("invalid environment values are reported") {
    cli::App app("t");
    (void)app.add<int>("--jobs").env("CLI_TEST_ENV_INVALID");
    ScopedEnv env("CLI_TEST_ENV_INVALID", "many");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "many"));
}

TEST_CASE("empty environment values are not silently ignored for numbers") {
    cli::App app("t");
    (void)app.add<int>("--jobs").env("CLI_TEST_ENV_EMPTY_INT");
    ScopedEnv env("CLI_TEST_ENV_EMPTY_INT", "");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("boolean flags read environment values") {
    cli::App app("t");
    auto color = app.add_flag("--color").default_val(true).env("CLI_TEST_ENV_COLOR");
    ScopedEnv env("CLI_TEST_ENV_COLOR", "false");

    REQUIRE(app.parse({}));
    CHECK(!*color);
    CHECK(!color.is_set());
}

TEST_CASE("count options read environment values") {
    cli::App app("t");
    auto verbose = app.add_count("--verbose").env("CLI_TEST_ENV_VERBOSITY");
    ScopedEnv env("CLI_TEST_ENV_VERBOSITY", "4");

    REQUIRE(app.parse({}));
    CHECK(*verbose == 4u);
    CHECK(!verbose.is_set());
    CHECK(verbose.value_or(0u) == 4u);
}

TEST_CASE("an explicit zero count from the environment still counts as provided") {
    cli::App app("t");
    auto verbose = app.add_count("--verbose").required().env("CLI_TEST_ENV_ZERO_COUNT");
    ScopedEnv env("CLI_TEST_ENV_ZERO_COUNT", "0");

    REQUIRE(app.parse({}));
    CHECK(*verbose == 0u);
    CHECK(!verbose);
    CHECK(!verbose.is_set());
    CHECK(app["--verbose"].found());
    CHECK(app["--verbose"].as_count() == 0u);
    CHECK(app["--verbose"].as_or<unsigned>(9u) == 0u);
}

TEST_CASE("invalid count environment values are rejected") {
    cli::App app("t");
    (void)app.add_count("--verbose").env("CLI_TEST_ENV_BAD_COUNT");

    {
        ScopedEnv env("CLI_TEST_ENV_BAD_COUNT", "-1");
        auto negative = app.parse({});
        REQUIRE(!negative);
        CHECK(negative.error().kind == cli::ParseError::Kind::InvalidValue);
    }

    {
        ScopedEnv env("CLI_TEST_ENV_BAD_COUNT", "many");
        auto text = app.parse({});
        REQUIRE(!text);
        CHECK(text.error().kind == cli::ParseError::Kind::InvalidValue);
    }
}

TEST_CASE("required options are satisfied by the environment") {
    cli::App app("t");
    (void)app.add<std::string>("--token").required().env("CLI_TEST_ENV_TOKEN");

    {
        ScopedEnv env("CLI_TEST_ENV_TOKEN", "secret");
        REQUIRE(app.parse({}));
    }

    auto missing = app.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
}

TEST_CASE("vector options split comma-separated environment values") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").env("CLI_TEST_ENV_VECTOR");
    ScopedEnv env("CLI_TEST_ENV_VECTOR", "1,2,3");

    REQUIRE(app.parse({}));
    REQUIRE(nums->size() == 3);
    CHECK((*nums)[0] == 1);
    CHECK((*nums)[2] == 3);
}

TEST_CASE("vector options honor a custom delimiter for environment values") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").delimiter('|').env("CLI_TEST_ENV_VECTOR_DELIM");
    ScopedEnv env("CLI_TEST_ENV_VECTOR_DELIM", "4|5|6");

    REQUIRE(app.parse({}));
    REQUIRE(nums->size() == 3);
    CHECK((*nums)[0] == 4);
    CHECK((*nums)[2] == 6);
}

TEST_CASE("an empty environment variable yields an empty vector") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag").env("CLI_TEST_ENV_EMPTY_VECTOR");
    ScopedEnv env("CLI_TEST_ENV_EMPTY_VECTOR", "");

    REQUIRE(app.parse({}));
    REQUIRE(tags);
    CHECK(tags->empty());
    CHECK(!tags.is_set());
    CHECK(app.has_option("--tag"));
}

TEST_CASE("an empty environment vector satisfies a required vector option") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").required()
                    .env("CLI_TEST_ENV_EMPTY_VECTOR_REQUIRED");
    ScopedEnv env("CLI_TEST_ENV_EMPTY_VECTOR_REQUIRED", "");

    REQUIRE(app.parse({}));
    REQUIRE(nums);
    CHECK(nums->empty());
}

TEST_CASE("an empty inline vector value still produces one empty element") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag").delimiter(',');

    REQUIRE(app.parse({"--tag="}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0].empty());
    CHECK(tags.is_set());
}

TEST_CASE("an invalid vector environment element is reported") {
    cli::App app("t");
    (void)app.add<std::vector<int>>("--num").env("CLI_TEST_ENV_VECTOR_BAD");
    ScopedEnv env("CLI_TEST_ENV_VECTOR_BAD", "1,x,3");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "x"));
}

TEST_CASE("positional vector environment values honor the delimiter") {
    cli::App app("t");
    auto nums = app.add_positional<std::vector<int>>("nums")
                    .delimiter('|')
                    .env("CLI_TEST_ENV_POSITIONAL_DELIM");
    ScopedEnv env("CLI_TEST_ENV_POSITIONAL_DELIM", "1|2|3");

    REQUIRE(app.parse({}));
    REQUIRE(nums->size() == 3);
    CHECK((*nums)[1] == 2);
    CHECK(!nums.is_set());
}

TEST_CASE("positional environment values participate in validation") {
    cli::App app("t");
    (void)app.add_positional<int>("num").range(1, 3).env("CLI_TEST_ENV_POSITIONAL_RANGE");
    ScopedEnv env("CLI_TEST_ENV_POSITIONAL_RANGE", "9");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("environment values participate in range validation") {
    cli::App app("t");
    (void)app.add<int>("--num").range(1, 5).env("CLI_TEST_ENV_RANGE");
    ScopedEnv env("CLI_TEST_ENV_RANGE", "9");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("environment values participate in choice validation") {
    cli::App app("t");
    (void)app.add<std::string>("--level").choices({"debug", "info"})
        .env("CLI_TEST_ENV_CHOICE");
    ScopedEnv env("CLI_TEST_ENV_CHOICE", "trace");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
}

TEST_CASE("environment values are transformed") {
    cli::App app("t");
    auto name = app.add<std::string>("--name").env("CLI_TEST_ENV_TRANSFORM")
                    .transform([](std::string v) { return "[" + v + "]"; });
    ScopedEnv env("CLI_TEST_ENV_TRANSFORM", "value");

    REQUIRE(app.parse({}));
    CHECK(*name == "[value]");
}

TEST_CASE("environment values are used for positionals") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file").env("CLI_TEST_ENV_POSITIONAL");
    ScopedEnv env("CLI_TEST_ENV_POSITIONAL", "from-env.txt");

    REQUIRE(app.parse({}));
    CHECK(*file == "from-env.txt");
    CHECK(!file.is_set());
}

TEST_CASE("a positional argument takes precedence over its environment variable") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file").env("CLI_TEST_ENV_POSITIONAL_CLI");
    ScopedEnv env("CLI_TEST_ENV_POSITIONAL_CLI", "from-env.txt");

    REQUIRE(app.parse({"from-cli.txt"}));
    CHECK(*file == "from-cli.txt");
    CHECK(file.is_set());
}

TEST_CASE("a required positional is satisfied by the environment") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file").env("CLI_TEST_ENV_REQ_POSITIONAL");
    ScopedEnv env("CLI_TEST_ENV_REQ_POSITIONAL", "from-env.txt");

    REQUIRE(app.parse({}));
    CHECK(*file == "from-env.txt");
}

TEST_CASE("environment state does not leak between parses") {
    cli::App app("t");
    auto jobs = app.add<int>("--jobs").default_val(2).env("CLI_TEST_ENV_LEAK");

    {
        ScopedEnv env("CLI_TEST_ENV_LEAK", "9");
        REQUIRE(app.parse({}));
        CHECK(*jobs == 9);
    }

    REQUIRE(app.parse({}));
    CHECK(*jobs == 2);
    CHECK(!jobs.is_set());
}

TEST_SUITE_END();
