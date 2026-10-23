#include "test_support.hpp"

TEST_SUITE_BEGIN("options");

TEST_CASE("a default value is applied without marking the option as set") {
    cli::App app("t");
    auto jobs = app.add<unsigned>("--jobs").short_name("-j").default_val(4u);

    REQUIRE(app.parse({}));
    CHECK(*jobs == 4u);
    CHECK(!jobs.is_set());
    CHECK(jobs);
}

TEST_CASE("default values accept explicitly converted types") {
    cli::App app("t");
    auto name = app.add<std::string>("--name").default_val(std::string_view{"guest"});
    auto path = app.add<std::filesystem::path>("--path")
                    .default_val(std::string_view{"/tmp/out"});

    REQUIRE(app.parse({}));
    CHECK(*name == "guest");
    CHECK(path->string() == "/tmp/out");
    CHECK(name.value_or(std::string_view{"fallback"}) == "guest");
}

TEST_CASE("vector defaults accept initializer lists") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag")
                    .default_val({"one", "two"});

    REQUIRE(app.parse({}));
    REQUIRE(tags->size() == 2);
    CHECK((*tags)[0] == "one");
    CHECK((*tags)[1] == "two");

    REQUIRE(app.parse({"--tag", "cli"}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0] == "cli");
}

TEST_CASE("handles expose mutable values after parsing") {
    cli::App app("t");
    auto name = app.add<std::string>("--name").default_val("start");
    auto file = app.add_positional<std::string>("file");

    REQUIRE(app.parse({"in.txt"}));
    *name = "changed";
    name->append("!");
    name.value() = "final";
    CHECK(*name == "final");

    *file = "out.txt";
    file->append(".bak");
    CHECK(*file == "out.txt.bak");
    CHECK(file.value() == "out.txt.bak");
}

TEST_CASE("registered single-dash names longer than one character parse exactly") {
    cli::App app("t");
    (void)app.add_flag("-verbose");
    auto number = app.add<int>("-number");

    REQUIRE(app.parse({"-verbose", "-number", "7"}));
    CHECK(app.has_flag("-verbose"));
    CHECK(*number == 7);

    const auto missing = app.parse({"-number"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingValue);
}

TEST_CASE("long, short and inline option values are equivalent") {
    cli::App app("t");
    auto jobs = app.add<unsigned>("--jobs").short_name("-j").default_val(4u);

    REQUIRE(app.parse({"--jobs", "8"}));
    CHECK(*jobs == 8u);
    CHECK(jobs.is_set());

    REQUIRE(app.parse({"-j", "9"}));
    CHECK(*jobs == 9u);

    REQUIRE(app.parse({"-j10"}));
    CHECK(*jobs == 10u);

    REQUIRE(app.parse({"-j=11"}));
    CHECK(*jobs == 11u);

    REQUIRE(app.parse({"--jobs=12"}));
    CHECK(*jobs == 12u);
}

TEST_CASE("string options preserve spaces and punctuation") {
    cli::App app("t");
    auto message = app.add<std::string>("--message");
    REQUIRE(app.parse({"--message", "hello, world!"}));
    CHECK(*message == "hello, world!");
}

TEST_CASE("path options convert from the token text") {
    cli::App app("t");
    auto output = app.add<std::filesystem::path>("--output").short_name("-o");
    REQUIRE(app.parse({"-o", "/tmp/build"}));
    CHECK(output->string() == "/tmp/build");
}

TEST_CASE("floating-point options parse decimal text") {
    cli::App app("t");
    auto ratio = app.add<double>("--ratio");
    REQUIRE(app.parse({"--ratio", "0.125"}));
    CHECK(*ratio == doctest::Approx(0.125));
}

TEST_CASE("invalid option values retain their option context") {
    cli::App app("t");
    (void)app.add<int>("--num").short_name("-n");
    auto r = app.parse({"-n", "abc"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(r.error().context == &app);
    CHECK(contains(r.error().message, "abc"));
    CHECK(contains(r.error().message, "-n"));
}

TEST_CASE("a missing required option is reported") {
    cli::App app("t");
    (void)app.add<std::string>("--output").short_name("-o").required();

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(r.error().message, "--output"));
}

TEST_CASE("a required option is satisfied by a provided value") {
    cli::App app("t");
    auto output = app.add<std::string>("--output").required();
    REQUIRE(app.parse({"--output", "out.bin"}));
    CHECK(*output == "out.bin");
}

TEST_CASE("an option default satisfies required regardless of builder order") {
    cli::App app("t");
    auto output = app.add<std::string>("--output")
                      .required()
                      .default_val("out.bin");

    REQUIRE(app.parse({}));
    CHECK(*output == "out.bin");
    CHECK(!output.is_set());

    REQUIRE(app.parse({"--output", "custom.bin"}));
    CHECK(*output == "custom.bin");
}

TEST_CASE("required(false) makes an option optional") {
    cli::App app("t");
    (void)app.add<std::string>("--output").required(false);
    REQUIRE(app.parse({}));
}

TEST_CASE("an option can be registered with only a short spelling") {
    cli::App app("t");
    auto n = app.add<int>("-n");
    REQUIRE(app.parse({"-n", "4"}));
    CHECK(*n == 4);
    CHECK(app["-n"].found());
}

TEST_CASE("vector options accumulate repeated occurrences") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag");
    REQUIRE(app.parse({"--tag", "foo", "--tag", "bar", "--tag=baz"}));
    REQUIRE(tags->size() == 3);
    CHECK((*tags)[0] == "foo");
    CHECK((*tags)[1] == "bar");
    CHECK((*tags)[2] == "baz");
    CHECK(tags.is_set());
}

TEST_CASE("vector options do not imply comma splitting") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag");
    REQUIRE(app.parse({"--tag", "a,b"}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0] == "a,b");
}

TEST_CASE("a delimiter splits one token into multiple vector values") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag").delimiter(',');
    REQUIRE(app.parse({"--tag=a,b,c"}));
    REQUIRE(tags->size() == 3);
    CHECK((*tags)[0] == "a");
    CHECK((*tags)[1] == "b");
    CHECK((*tags)[2] == "c");
}

TEST_CASE("delimiter splitting accumulates across repeated occurrences") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").delimiter(';');
    REQUIRE(app.parse({"--num", "1;2", "--num", "3"}));
    REQUIRE(nums->size() == 3);
    CHECK((*nums)[0] == 1);
    CHECK((*nums)[1] == 2);
    CHECK((*nums)[2] == 3);
}

TEST_CASE("empty delimiter segments are preserved") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag").delimiter(',');

    REQUIRE(app.parse({"--tag=a,,b"}));
    REQUIRE(tags->size() == 3);
    CHECK((*tags)[0] == "a");
    CHECK((*tags)[1].empty());
    CHECK((*tags)[2] == "b");

    REQUIRE(app.parse({"--tag=a,"}));
    REQUIRE(tags->size() == 2);
    CHECK((*tags)[1].empty());
}

TEST_CASE("an empty vector value produces one empty element") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag").delimiter(',');
    REQUIRE(app.parse({"--tag="}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0].empty());
}

TEST_CASE("an invalid delimited element names that element") {
    cli::App app("t");
    (void)app.add<std::vector<int>>("--num").delimiter(',');
    auto r = app.parse({"--num=1,x,3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "'x'"));
}

TEST_CASE("a CLI vector replaces its default instead of merging into it") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag")
                   .default_val(std::vector<std::string>{"base"});

    REQUIRE(app.parse({"--tag", "extra"}));
    REQUIRE(tags->size() == 1);
    CHECK((*tags)[0] == "extra");
}

TEST_CASE("a vector default is applied when the option is absent") {
    cli::App app("t");
    auto tags = app.add<std::vector<std::string>>("--tag")
                   .default_val(std::vector<std::string>{"one", "two"});

    REQUIRE(app.parse({}));
    REQUIRE(tags->size() == 2);
    CHECK((*tags)[0] == "one");
    CHECK((*tags)[1] == "two");
    CHECK(!tags.is_set());
}

TEST_CASE("options can be hidden from help") {
    cli::App app("tool");
    (void)app.add<std::string>("--secret").hidden().help("must not appear");
    const auto help = app.help_text();
    CHECK(!contains(help, "--secret"));
    CHECK(!contains(help, "must not appear"));
}

TEST_CASE("a custom meta name is used in help") {
    cli::App app("tool");
    (void)app.add<std::string>("--output").meta("DIR");
    CHECK(contains(app.help_text(), "<DIR>"));
}

TEST_SUITE_END();
