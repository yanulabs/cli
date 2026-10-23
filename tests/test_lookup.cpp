#include "test_support.hpp"

TEST_SUITE_BEGIN("lookup");

TEST_CASE("lookup finds options by long and short spelling") {
    cli::App app("t");
    (void)app.add<int>("--num").short_name("-n");
    REQUIRE(app.parse({"-n", "42"}));

    CHECK(app["--num"].found());
    CHECK(app["-n"].found());
    CHECK(app["--num"].as<int>() == 42);
    CHECK(app["-n"].as<int>() == 42);
}

TEST_CASE("a missing lookup is distinguishable from a zero value") {
    cli::App app("t");
    (void)app.add<int>("--num");
    REQUIRE(app.parse({}));

    CHECK(!app["--missing"].found());
    CHECK(!app["--missing"]);
    CHECK(!app["--missing"].try_as<int>());
    CHECK(app["--missing"].as<int>() == 0);
    CHECK(app["--missing"].as_or<int>(7) == 7);
}

TEST_CASE("looking up the wrong type is safe") {
    cli::App app("t");
    (void)app.add<int>("--num");
    REQUIRE(app.parse({"--num", "9"}));

    CHECK(!app["--num"].try_as<std::string>());
    CHECK(app["--num"].as<std::string>().empty());
    CHECK(app["--num"].as_or<std::string>("fallback") == "fallback");
}

TEST_CASE("try_as, as and as_or expose the parsed value") {
    cli::App app("t");
    (void)app.add<int>("--num").default_val(5);
    REQUIRE(app.parse({}));

    auto value = app["--num"].try_as<int>();
    REQUIRE(value);
    CHECK(*value == 5);
    CHECK(app["--num"].as<int>() == 5);
    CHECK(app["--num"].as_or<int>(9) == 5);
}

TEST_CASE("lookup presence reflects defaults but is_set does not") {
    cli::App app("t");
    (void)app.add<int>("--num").default_val(5);
    REQUIRE(app.parse({}));

    CHECK(app["--num"]);
    CHECK(app["--num"].found());
    CHECK(!app["--num"].is_set());
}

TEST_CASE("explicitly false flags still carry a value") {
    cli::App app("t");
    (void)app.add_flag("--color").default_val(true);
    REQUIRE(app.parse({"--color=false"}));

    CHECK(app["--color"]);
    CHECK(app["--color"].is_set());
    CHECK(!app["--color"].as<bool>());
    CHECK(!app.has_flag("--color"));
}

TEST_CASE("an absent flag has no lookup value") {
    cli::App app("t");
    (void)app.add_flag("--color");
    REQUIRE(app.parse({}));

    CHECK(!app["--color"]);
    CHECK(!app["--color"].as<bool>());
    CHECK(!app["--color"].is_set());
    CHECK(!app.has_flag("--color"));
}

TEST_CASE("a default-true flag reports true through has_flag") {
    cli::App app("t");
    (void)app.add_flag("--color").default_val(true);
    REQUIRE(app.parse({}));

    CHECK(app.has_flag("--color"));
    CHECK(!app["--color"].is_set());
}

TEST_CASE("count options expose count through every count accessor") {
    cli::App app("t");
    (void)app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse({"-vvv"}));

    CHECK(app["--verbose"].as_count() == 3u);
    CHECK(app["-v"].as_count() == 3u);
    CHECK(app["--verbose"].as<unsigned>() == 3u);
    CHECK(app["--verbose"].as<int>() == 3);
    CHECK(app["--verbose"].as<bool>());
    CHECK(app["--verbose"].is_set());
    CHECK(app.has_flag("--verbose"));
}

TEST_CASE("an absent count option has no value") {
    cli::App app("t");
    (void)app.add_count("--verbose");
    REQUIRE(app.parse({}));

    CHECK(app["--verbose"].as_count() == 0u);
    CHECK(!app["--verbose"].as<bool>());
    CHECK(!app["--verbose"]);
    CHECK(!app.has_flag("--verbose"));
    CHECK(app["--verbose"].as_or<unsigned>(5u) == 5u);
}

TEST_CASE("as_count on a non-count option returns zero") {
    cli::App app("t");
    (void)app.add<int>("--num");
    REQUIRE(app.parse({"--num", "4"}));
    CHECK(app["--num"].as_count() == 0u);
}

TEST_CASE("vector values are returned whole through lookup") {
    cli::App app("t");
    (void)app.add<std::vector<std::string>>("--tag");
    REQUIRE(app.parse({"--tag", "a", "--tag", "b"}));

    auto tags = app["--tag"].try_as<std::vector<std::string>>();
    REQUIRE(tags);
    REQUIRE(tags->size() == 2);
    CHECK((*tags)[0] == "a");
    CHECK((*tags)[1] == "b");
}

TEST_CASE("has_option recognizes explicit, default and environment values") {
    cli::App app("t");
    (void)app.add<int>("--explicit");
    (void)app.add<int>("--defaulted").default_val(1);
    (void)app.add<int>("--from-env").env("CLI_TEST_LOOKUP_ENV");
    (void)app.add<int>("--absent");

    ScopedEnv env("CLI_TEST_LOOKUP_ENV", "3");
    REQUIRE(app.parse({"--explicit", "2"}));

    CHECK(app.has_option("--explicit"));
    CHECK(app.has_option("--defaulted"));
    CHECK(app.has_option("--from-env"));
    CHECK(!app.has_option("--absent"));
    CHECK(!app.has_option("--missing"));
}

TEST_CASE("an empty string value is still present in lookup") {
    cli::App app("t");
    auto name = app.add<std::string>("--name");
    REQUIRE(app.parse({"--name="}));

    CHECK(name);
    CHECK(app["--name"]);
    CHECK(app["--name"].is_set());
    CHECK(app["--name"].as<std::string>().empty());
    CHECK(app.has_option("--name"));
}

TEST_CASE("lookup reflects reset state") {
    cli::App app("t");
    (void)app.add<int>("--num").default_val(5);
    (void)app.add_flag("--verbose");

    REQUIRE(app.parse({"--num", "9", "--verbose"}));
    CHECK(app["--num"].as<int>() == 9);
    CHECK(app["--verbose"].as<bool>());

    app.reset();
    REQUIRE(app.parse({}));
    CHECK(app["--num"].as<int>() == 5);
    CHECK(!app["--verbose"].as<bool>());
    CHECK(!app["--verbose"].is_set());
}

TEST_CASE("a default-constructed lookup is safely empty") {
    cli::App::Lookup lookup{};
    CHECK(!lookup);
    CHECK(!lookup.found());
    CHECK(!lookup.is_set());
    CHECK(!lookup.try_as<int>());
    CHECK(lookup.as<int>() == 0);
    CHECK(lookup.as_or<int>(7) == 7);
    CHECK(lookup.as_count() == 0u);
}

TEST_CASE("lookup works through a const App reference") {
    cli::App app("t");
    (void)app.add<std::string>("--name").default_val("Ada");
    REQUIRE(app.parse({}));

    const cli::App& view = app;
    CHECK(view["--name"].found());
    CHECK(view["--name"].as<std::string>() == "Ada");
}

TEST_SUITE_END();
