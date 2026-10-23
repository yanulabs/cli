#include "test_support.hpp"

TEST_SUITE_BEGIN("positionals");

TEST_CASE("positionals are consumed in declaration order") {
    cli::App app("t");
    auto src = app.add_positional<std::string>("src");
    auto dst = app.add_positional<std::string>("dst");

    REQUIRE(app.parse({"input.txt", "output.txt"}));
    CHECK(*src == "input.txt");
    CHECK(*dst == "output.txt");
    CHECK(src.is_set());
    CHECK(dst.is_set());
}

TEST_CASE("a required positional must be present") {
    cli::App app("t");
    (void)app.add_positional<std::string>("src");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(r.error().message, "<src>"));
}

TEST_CASE("default_val makes a positional optional and supplies a value") {
    cli::App app("t");
    auto dst = app.add_positional<std::string>("dst").default_val("out.txt");

    REQUIRE(app.parse({}));
    CHECK(*dst == "out.txt");
    CHECK(!dst.is_set());
    CHECK(contains(app.help_text(), "[dst]"));
}

TEST_CASE("a default satisfies required() regardless of builder order") {
    cli::App app("t");
    auto dst = app.add_positional<std::string>("dst")
                   .required()
                   .default_val("out.txt");

    REQUIRE(app.parse({}));
    CHECK(*dst == "out.txt");
    CHECK(!dst.is_set());

    REQUIRE(app.parse({"custom"}));
    CHECK(*dst == "custom");
}

TEST_CASE("required(false) leaves an absent positional without a value") {
    cli::App app("t");
    auto dst = app.add_positional<std::string>("dst").required(false);

    REQUIRE(app.parse({}));
    CHECK(!dst);
    CHECK(!dst.is_set());

    REQUIRE(app.parse({"x"}));
    CHECK(*dst == "x");
}

TEST_CASE("a single optional positional still rejects extra arguments") {
    cli::App app("t");
    (void)app.add_positional<std::string>("first").required(false);

    auto r = app.parse({"one", "two"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
    CHECK(contains(r.error().message, "two"));
}

TEST_CASE("optional positionals fall back independently") {
    cli::App app("t");
    auto first  = app.add_positional<std::string>("first").default_val("one");
    auto second = app.add_positional<std::string>("second").default_val("two");

    REQUIRE(app.parse({"custom"}));
    CHECK(*first == "custom");
    CHECK(first.is_set());
    CHECK(*second == "two");
    CHECK(!second.is_set());
}

TEST_CASE("vector positionals consume every remaining token") {
    cli::App app("t");
    auto files = app.add_positional<std::vector<std::string>>("files");

    REQUIRE(app.parse({"a.txt", "b.txt", "c.txt"}));
    REQUIRE(files->size() == 3);
    CHECK((*files)[0] == "a.txt");
    CHECK((*files)[2] == "c.txt");
}

TEST_CASE("a variadic positional must be registered last") {
    cli::App app("t");
    (void)app.add_positional<std::vector<std::string>>("files");

    auto add_scalar = [&] { (void)app.add_positional<std::string>("unreachable"); };
    auto add_vector = [&] { (void)app.add_positional<std::vector<int>>("more"); };
    REQUIRE_THROWS_AS(add_scalar(), std::invalid_argument);
    REQUIRE_THROWS_AS(add_vector(), std::invalid_argument);
}

TEST_CASE("a required vector positional needs at least one token") {
    cli::App app("t");
    (void)app.add_positional<std::vector<std::string>>("files");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
}

TEST_CASE("an optional vector positional may be empty") {
    cli::App app("t");
    auto files = app.add_positional<std::vector<std::string>>("files").required(false);

    REQUIRE(app.parse({}));
    CHECK(!files);
    CHECK(!files.is_set());
}

TEST_CASE("a vector positional can follow scalar positionals") {
    cli::App app("t");
    auto first  = app.add_positional<std::string>("first");
    auto second = app.add_positional<std::string>("second");
    auto rest   = app.add_positional<std::vector<std::string>>("rest");

    REQUIRE(app.parse({"a", "b", "c", "d"}));
    CHECK(*first == "a");
    CHECK(*second == "b");
    REQUIRE(rest->size() == 2);
    CHECK((*rest)[0] == "c");
    CHECK((*rest)[1] == "d");
}

TEST_CASE("a vector positional supports a custom delimiter") {
    cli::App app("t");
    auto nums = app.add_positional<std::vector<int>>("nums").delimiter(';');

    REQUIRE(app.parse({"1;2", "3"}));
    REQUIRE(nums->size() == 3);
    CHECK((*nums)[0] == 1);
    CHECK((*nums)[1] == 2);
    CHECK((*nums)[2] == 3);
}

TEST_CASE("empty delimiter segments in positionals are preserved") {
    cli::App app("t");
    auto parts = app.add_positional<std::vector<std::string>>("parts").delimiter('|');

    REQUIRE(app.parse({"a||b"}));
    REQUIRE(parts->size() == 3);
    CHECK((*parts)[0] == "a");
    CHECK((*parts)[1].empty());
    CHECK((*parts)[2] == "b");
}

TEST_CASE("an empty positional token is accepted for strings") {
    cli::App app("t");
    auto value = app.add_positional<std::string>("value");
    REQUIRE(app.parse({""}));
    CHECK(value.is_set());
    CHECK(value->empty());
}

TEST_CASE("boolean positionals use the same parser as options") {
    cli::App app("t");
    auto value = app.add_positional<bool>("value");

    REQUIRE(app.parse({"yes"}));
    CHECK(*value);

    REQUIRE(app.parse({"off"}));
    CHECK(!*value);
}

TEST_CASE("path positionals convert from text") {
    cli::App app("t");
    auto path = app.add_positional<std::filesystem::path>("path");
    REQUIRE(app.parse({"/tmp/file"}));
    CHECK(path->string() == "/tmp/file");
}

TEST_CASE("end-of-options allows option-looking positionals") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file");
    REQUIRE(app.parse({"--", "--not-an-option"}));
    CHECK(*file == "--not-an-option");
}

TEST_CASE("a lone dash is a positional") {
    cli::App app("t");
    auto file = app.add_positional<std::string>("file");
    REQUIRE(app.parse({"-"}));
    CHECK(*file == "-");
}

TEST_CASE("a vector positional default is applied when absent") {
    cli::App app("t");
    auto values = app.add_positional<std::vector<std::string>>("values")
                      .default_val(std::vector<std::string>{"a", "b"});

    REQUIRE(app.parse({}));
    REQUIRE(values->size() == 2);
    CHECK((*values)[0] == "a");
    CHECK((*values)[1] == "b");
    CHECK(!values.is_set());
}

TEST_CASE("a provided vector positional replaces its default") {
    cli::App app("t");
    auto values = app.add_positional<std::vector<std::string>>("values")
                      .default_val(std::vector<std::string>{"default"});

    REQUIRE(app.parse({"actual"}));
    REQUIRE(values->size() == 1);
    CHECK((*values)[0] == "actual");
}

TEST_CASE("a positional parse error reports the positional name") {
    cli::App app("t");
    (void)app.add_positional<int>("count");
    auto r = app.parse({"many"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "<count>"));
}

TEST_SUITE_END();
