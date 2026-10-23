#include "test_support.hpp"

#include <limits>

TEST_SUITE_BEGIN("constraints");

TEST_CASE("range boundaries are inclusive") {
    cli::App app("t");
    auto num = app.add<int>("--num").range(1, 10);

    REQUIRE(app.parse({"--num", "1"}));
    CHECK(*num == 1);

    REQUIRE(app.parse({"--num", "10"}));
    CHECK(*num == 10);
}

TEST_CASE("values outside the range are rejected with bounds in the message") {
    cli::App app("t");
    (void)app.add<int>("--num").range(1, 10);

    auto low = app.parse({"--num", "0"});
    REQUIRE(!low);
    CHECK(low.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(low.error().message, "0"));
    CHECK(contains(low.error().message, "[1, 10]"));

    auto high = app.parse({"--num", "11"});
    REQUIRE(!high);
    CHECK(high.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("unsigned ranges handle zero") {
    cli::App app("t");
    auto num = app.add<unsigned>("--num").range(0u, 0u);
    REQUIRE(app.parse({"--num", "0"}));
    CHECK(*num == 0u);
}

TEST_CASE("NaN never satisfies a range constraint") {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    cli::App option_app("t");
    (void)option_app.add<double>("--ratio").range(0.0, 1.0);
    auto cli = option_app.parse({"--ratio", "nan"});
    REQUIRE(!cli);
    CHECK(cli.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(cli.error().message, "nan"));

    cli::App default_app("t");
    (void)default_app.add<double>("--ratio").range(0.0, 1.0).default_val(nan);
    auto def = default_app.parse({});
    REQUIRE(!def);
    CHECK(def.error().kind == cli::ParseError::Kind::OutOfRange);

    cli::App positional_app("t");
    (void)positional_app.add_positional<double>("ratio").range(0.0, 1.0);
    auto pos = positional_app.parse({"nan"});
    REQUIRE(!pos);
    CHECK(pos.error().kind == cli::ParseError::Kind::OutOfRange);

    cli::App vector_app("t");
    (void)vector_app.add<std::vector<double>>("--ratio").delimiter(',')
        .range(0.0, 1.0);
    auto vec = vector_app.parse({"--ratio=0.5,nan"});
    REQUIRE(!vec);
    CHECK(vec.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("choices accept listed values and reject others") {
    cli::App app("t");
    auto level = app.add<std::string>("--level").choices({"debug", "info", "warn"});

    REQUIRE(app.parse({"--level", "debug"}));
    CHECK(*level == "debug");

    auto r = app.parse({"--level", "trace"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "trace"));
    CHECK(contains(r.error().message, "debug|info|warn"));
}

TEST_CASE("an empty string can be a valid choice") {
    cli::App app("t");
    auto level = app.add<std::string>("--level").choices({"", "auto"});

    REQUIRE(app.parse({"--level="}));
    CHECK(level->empty());

    auto r = app.parse({"--level", "manual"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "|auto"));
}

TEST_CASE("range is checked before choices") {
    cli::App app("t");
    (void)app.add<int>("--num").range(1, 5).choices({2, 3});

    auto outside = app.parse({"--num", "9"});
    REQUIRE(!outside);
    CHECK(outside.error().kind == cli::ParseError::Kind::OutOfRange);

    auto outside_choices = app.parse({"--num", "4"});
    REQUIRE(!outside_choices);
    CHECK(outside_choices.error().kind == cli::ParseError::Kind::BadChoice);
}

TEST_CASE("custom validators receive the parsed value") {
    cli::App app("t");
    auto num = app.add<int>("--num")
                   .validate([](const int& n) { return n % 2 == 0; }, "must be even");

    REQUIRE(app.parse({"--num", "4"}));
    CHECK(*num == 4);

    auto r = app.parse({"--num", "3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(contains(r.error().message, "must be even"));
}

TEST_CASE("vector elements are checked individually against range") {
    cli::App app("t");
    (void)app.add<std::vector<int>>("--num").delimiter(',').range(1, 5);

    REQUIRE(app.parse({"--num=1,2,5"}));
    auto r = app.parse({"--num=3,9"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(r.error().message, "'9'"));
}

TEST_CASE("vector elements are checked individually against choices") {
    cli::App app("t");
    (void)app.add<std::vector<std::string>>("--level").delimiter(',')
        .choices({"debug", "info", "warn"});

    REQUIRE(app.parse({"--level=debug,info"}));
    auto r = app.parse({"--level=debug,trace"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "trace"));
}

TEST_CASE("whole-vector validators run after element constraints") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").delimiter(',')
                    .validate([](const std::vector<int>& v) { return v.size() <= 2; },
                              "at most two values");

    REQUIRE(app.parse({"--num=1,2"}));
    CHECK(nums->size() == 2);

    auto r = app.parse({"--num=1,2,3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(contains(r.error().message, "at most two values"));
}

TEST_CASE("validators do not run when an earlier constraint fails") {
    cli::App app("t");
    bool validator_called = false;
    (void)app.add<int>("--num").range(1, 5).validate(
        [&validator_called](const int&) {
            validator_called = true;
            return true;
        });

    auto r = app.parse({"--num", "9"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(!validator_called);
}

TEST_CASE("transforms run after validation and before value access") {
    cli::App app("t");
    auto num = app.add<int>("--num").range(1, 10)
                   .transform([](int n) { return n * 2; });

    REQUIRE(app.parse({"--num", "8"}));
    CHECK(*num == 16);

    auto r = app.parse({"--num", "20"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("a transform cannot rescue a value rejected by a validator") {
    cli::App app("t");
    (void)app.add<int>("--num")
        .validate([](const int& n) { return n % 2 == 0; }, "must be even")
        .transform([](int n) { return n * 2; });

    auto r = app.parse({"--num", "3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
}

TEST_CASE("a transform is applied to a default value") {
    cli::App app("t");
    auto name = app.add<std::string>("--name").default_val("world")
                    .transform([](std::string v) { return "~" + v + "~"; });

    REQUIRE(app.parse({}));
    CHECK(*name == "~world~");
}

TEST_CASE("a transform is applied to every vector element as a whole value") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").delimiter(',')
                    .transform([](std::vector<int> v) {
                        for (auto& n : v) n *= 10;
                        return v;
                    });

    REQUIRE(app.parse({"--num=1,2"}));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[0] == 10);
    CHECK((*nums)[1] == 20);
}

TEST_CASE("a transform is applied to positional values") {
    cli::App app("t");
    auto num = app.add_positional<int>("num").transform([](int n) { return n + 1; });
    REQUIRE(app.parse({"41"}));
    CHECK(*num == 42);
}

TEST_CASE("positional constraints and transforms compose") {
    cli::App app("t");
    auto num = app.add_positional<int>("num")
                   .range(1, 10)
                   .choices({2, 4, 6, 8})
                   .validate([](const int& v) { return v > 0; }, "positive")
                   .transform([](int v) { return v * 10; });

    auto bad_choice = app.parse({"5"});
    REQUIRE(!bad_choice);
    CHECK(bad_choice.error().kind == cli::ParseError::Kind::BadChoice);

    auto out_of_range = app.parse({"11"});
    REQUIRE(!out_of_range);
    CHECK(out_of_range.error().kind == cli::ParseError::Kind::OutOfRange);

    REQUIRE(app.parse({"4"}));
    CHECK(*num == 40);
}

TEST_CASE("a default outside its range is rejected") {
    cli::App app("t");
    (void)app.add<int>("--jobs").default_val(0).range(1, 10);

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

TEST_CASE("a default outside its choices is rejected") {
    cli::App app("t");
    (void)app.add<std::string>("--level").default_val("trace").choices({"debug", "info"});

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
}

TEST_CASE("a default rejected by a custom validator is reported") {
    cli::App app("t");
    (void)app.add<int>("--num").default_val(3)
        .validate([](const int& n) { return n % 2 == 0; }, "must be even");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
}

TEST_CASE("a valid default passes all constraints and is transformed") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(4).range(1, 10)
                   .validate([](const int& n) { return n % 2 == 0; }, "even")
                   .transform([](int n) { return n * 3; });

    REQUIRE(app.parse({}));
    CHECK(*num == 12);
}

TEST_CASE("vector positional elements are checked individually") {
    cli::App app("t");
    (void)app.add_positional<std::vector<int>>("nums").delimiter(',').range(1, 5);

    REQUIRE(app.parse({"1,2,5"}));
    auto r = app.parse({"3,9"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(r.error().message, "'9'"));
}

TEST_CASE("a positional validator failure is reported") {
    cli::App app("t");
    (void)app.add_positional<int>("num")
        .validate([](const int& n) { return n % 2 == 0; }, "must be even");

    REQUIRE(app.parse({"4"}));
    auto r = app.parse({"3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(contains(r.error().message, "must be even"));
}

TEST_CASE("an invalid default vector element is named in the error") {
    cli::App app("t");
    (void)app.add<std::vector<int>>("--num")
        .default_val(std::vector<int>{1, 9})
        .range(1, 5);

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(r.error().message, "'9'"));
}

TEST_SUITE_END();
