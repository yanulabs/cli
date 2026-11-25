#include "test_support.hpp"

#include <array>
#include <span>
#include <stdexcept>

TEST_SUITE_BEGIN("lifecycle");

TEST_CASE("parse(initializer_list) parses options and defaults") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto jobs    = app.add<int>("--jobs").short_name("-j").default_val(4);

    REQUIRE(app.parse({"-v", "--jobs", "8"}));
    CHECK(*verbose);
    CHECK(*jobs == 8);

    REQUIRE(app.parse({}));
    CHECK(!*verbose);
    CHECK(*jobs == 4);
}

TEST_CASE("parse(0, nullptr) behaves like an empty command line") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(7);

    REQUIRE(app.parse(0, nullptr));
    CHECK(*num == 7);
}

TEST_CASE("an argc containing only the program name is empty") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(7);
    Args args{};

    REQUIRE(app.parse(args.argc(), args.argv()));
    CHECK(*num == 7);
}

TEST_CASE("parse accepts a const argv array") {
    const char* argv[] = {"t", "--num", "13"};
    cli::App app("t");
    auto num = app.add<int>("--num");

    REQUIRE(app.parse(3, argv));
    CHECK(*num == 13);
}

TEST_CASE("parse accepts const char pointers with a null token") {
    const char* argv[] = {"t", "--name", nullptr};
    cli::App app("t");
    auto name = app.add<std::string>("--name");

    REQUIRE(app.parse(3, argv));
    CHECK(name->empty());
}

TEST_CASE("parse(span) parses the supplied tokens") {
    cli::App app("t");
    auto num = app.add<int>("--num");
    const std::array<std::string_view, 2> tokens{"--num", "11"};

    REQUIRE(app.parse(std::span<const std::string_view>(tokens)));
    CHECK(*num == 11);
}

TEST_CASE("every parse clears state from the previous run") {
    cli::App app("t");
    auto tags    = app.add<std::vector<std::string>>("--tag");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto jobs    = app.add<int>("--jobs").default_val(4);

    REQUIRE(app.parse({"--tag", "a", "-v", "--jobs", "8"}));
    REQUIRE(tags->size() == 1);
    CHECK(*verbose);
    CHECK(*jobs == 8);

    REQUIRE(app.parse({"--tag", "b"}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0] == "b");
    CHECK(!*verbose);
    CHECK(*jobs == 4);
}

TEST_CASE("manual reset clears values and count state") {
    cli::App app("t");
    auto flag  = app.add_flag("--verbose").short_name("-v");
    auto count = app.add_count("--debug").short_name("-d");

    REQUIRE(app.parse({"-v", "-ddd"}));
    CHECK(*flag);
    CHECK(*count == 3u);

    app.reset();
    CHECK(!*flag);
    CHECK(*count == 0u);
    CHECK(!flag.is_set());
    CHECK(!count.is_set());

    REQUIRE(app.parse({"-d"}));
    CHECK(*count == 1u);
}

TEST_CASE("defaults are reapplied after a reset") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(4);

    REQUIRE(app.parse({"--num", "9"}));
    CHECK(*num == 9);

    app.reset();
    CHECK(!num);  // reset alone does not apply defaults

    REQUIRE(app.parse({}));
    CHECK(*num == 4);
    CHECK(!num.is_set());
}

TEST_CASE("transforms are not cumulative across parses") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(2)
                   .transform([](int n) { return n * 2; });

    REQUIRE(app.parse({}));
    CHECK(*num == 4);

    REQUIRE(app.parse({}));
    CHECK(*num == 4);

    REQUIRE(app.parse({"--num", "3"}));
    CHECK(*num == 6);
}

TEST_CASE("a failed parse does not poison the next parse") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(4);

    auto bad = app.parse({"--num", "9", "--unknown"});
    REQUIRE(!bad);
    CHECK(!num);
    CHECK(!num.is_set());

    REQUIRE(app.parse({}));
    CHECK(*num == 4);
}

TEST_CASE("a failed parse leaves all partial values reset") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose");
    auto debug   = app.add_count("--debug");
    auto tags    = app.add<std::vector<std::string>>("--tag");
    auto num     = app.add<int>("--num").default_val(4);

    auto bad = app.parse({"--verbose", "-dd", "--tag", "x", "--num", "9", "--unknown"});
    REQUIRE(!bad);
    CHECK(!verbose);
    CHECK(!verbose.is_set());
    CHECK(*debug == 0u);
    CHECK(!debug.is_set());
    CHECK(!tags);
    CHECK(!num);
    CHECK(!num.is_set());
}

TEST_CASE("finalization failures roll back defaults and parsed values") {
    cli::App app("t");
    auto num = app.add<int>("--num").default_val(4);
    (void)app.add<std::string>("--required").required();

    auto missing = app.parse({"--num", "9"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(!num);
}

TEST_CASE("a failed child parse clears root and child state") {
    cli::App app("tool");
    auto verbose = app.add_flag("--verbose").global();
    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target");

    auto bad = app.parse({"--verbose", "build", "--target", "release", "--unknown"});
    REQUIRE(!bad);
    CHECK(!verbose);
    CHECK(!target);
    CHECK(!app.active_subcommand());
    CHECK(!build.active_subcommand());
}

TEST_CASE("validator failures and exceptions roll back parsed values") {
    cli::App invalid("invalid");
    auto value = invalid.add<int>("--num")
        .validate([](int v) { return v > 0; }, "must be positive");
    auto bad = invalid.parse({"--num", "-1"});
    REQUIRE(!bad);
    CHECK(!value);

    cli::App throwing("throwing");
    auto transformed = throwing.add<int>("--num")
        .transform([](int) -> int { throw std::runtime_error("transform failed"); });
    CHECK_THROWS_AS((void)throwing.parse({"--num", "3"}), std::runtime_error);
    CHECK(!transformed);
}

TEST_CASE("an active subcommand is cleared by the next parse") {
    cli::App app("tool");
    (void)app.add_flag("--verbose");
    (void)app.subcommand("build", "");
    auto& test = app.subcommand("test", "");
    (void)test;

    REQUIRE(app.parse({"build"}));
    CHECK(app.active_subcommand());

    REQUIRE(app.parse({}));
    CHECK(!app.active_subcommand());
}

TEST_CASE("nested subcommand state is cleared recursively") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "");
    auto& add    = remote.subcommand("add", "");
    auto  url    = add.add<std::string>("--url");

    REQUIRE(app.parse({"remote", "add", "--url", "x"}));
    CHECK(*url == "x");
    CHECK(remote.active_subcommand() == &add);

    REQUIRE(app.parse({"remote"}));
    CHECK(!remote.active_subcommand());
    CHECK(!url);
}

TEST_CASE("child values do not leak into a later root parse") {
    cli::App app("tool");
    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target").default_val("debug");

    REQUIRE(app.parse({"build", "--target", "release"}));
    CHECK(*target == "release");

    REQUIRE(app.parse({}));
    CHECK(!target);
}

TEST_CASE("reset preserves registrations and metadata") {
    cli::App app("tool", "description", "1.0.0");
    auto num = app.add<int>("--num").default_val(4);
    auto& sub = app.subcommand("build", "Compile");
    (void)sub;

    app.reset();
    CHECK(app.name() == "tool");
    CHECK(app.description() == "description");
    CHECK(app.ver() == "1.0.0");
    CHECK(app.has_option("--num"));  // a registered default still counts
    CHECK(app.help_text().find("build") != std::string::npos);
    CHECK(!num);

    REQUIRE(app.parse({"--num", "8"}));
    CHECK(*num == 8);
}

TEST_CASE("subcommand callbacks survive reset") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "");
    build.on_execute([](cli::App&) {});

    REQUIRE(app.parse({"build"}));
    app.reset();
    CHECK(build.has_callback());
}

TEST_CASE("alternating parse overloads share the same reset behavior") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").delimiter(',');

    REQUIRE(app.parse({"--num=1,2"}));
    REQUIRE(nums->size() == 2);

    Args args{"--num", "3"};
    REQUIRE(app.parse(args.argc(), args.argv()));
    REQUIRE(nums->size() == 1);
    CHECK((*nums)[0] == 3);

    const std::array<std::string_view, 1> tokens{"--num=4,5"};
    REQUIRE(app.parse(std::span<const std::string_view>(tokens)));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[1] == 5);
}

TEST_SUITE_END();
