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

TEST_CASE("a fixed-arity vector positional keeps following slots reachable") {
    cli::App app("t");
    auto nums  = app.add_positional<std::vector<int>>("nums").nargs(2);
    auto label = app.add_positional<std::string>("label");

    REQUIRE(app.parse({"1", "2", "three"}));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[0] == 1);
    CHECK((*nums)[1] == 2);
    CHECK(*label == "three");
}

TEST_CASE("a fixed-arity vector positional reports too few values") {
    cli::App app("t");
    (void)app.add_positional<std::vector<int>>("nums").nargs(2);

    auto r = app.parse({"1"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingValue);
    CHECK(contains(r.error().message, "<nums>"));
    CHECK(contains(r.error().message, "at least 2"));
}

TEST_CASE("a fixed-arity positional rejects delimited overflow") {
    cli::App app("t");
    (void)app.add_positional<std::vector<int>>("nums").nargs(2).delimiter(',');

    REQUIRE(app.parse({"1,2"}));
    auto r = app.parse({"1,2,3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnexpectedValue);
    CHECK(contains(r.error().message, "at most 2"));
}

TEST_CASE("a ranged positional consumes up to its upper bound") {
    cli::App app("t");
    auto nums    = app.add_positional<std::vector<int>>("nums").nargs(1, 3);
    auto verbose = app.add_flag("--verbose").short_name("-v");

    REQUIRE(app.parse({"1", "2", "-v"}));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[1] == 2);
    CHECK(*verbose);

    auto too_many = app.parse({"1", "2", "3", "4"});
    REQUIRE(!too_many);
    CHECK(too_many.error().kind == cli::ParseError::Kind::TooManyPositionals);
}

TEST_CASE("nargs_at_least enforces its lower bound and consumes the rest") {
    cli::App app("t");
    auto nums = app.add_positional<std::vector<int>>("nums").nargs_at_least(2);

    REQUIRE(app.parse({"1", "2", "3", "4"}));
    REQUIRE(nums->size() == 4);
    CHECK((*nums)[3] == 4);

    auto r = app.parse({"7"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingValue);
    CHECK(contains(r.error().message, "at least 2"));
}

TEST_CASE("positional nargs rejects invalid configurations") {
    cli::App app("t");
    auto nums = app.add_positional<std::vector<int>>("nums");

    CHECK_THROWS_AS(nums.nargs(0), std::invalid_argument);
    CHECK_THROWS_AS(nums.nargs(3, 2), std::invalid_argument);
    CHECK_THROWS_AS(nums.nargs_at_least(0), std::invalid_argument);
}

TEST_CASE("only variable-arity positionals must be registered last") {
    cli::App fixed("fixed");
    (void)fixed.add_positional<std::vector<int>>("nums").nargs(2);
    (void)fixed.add_positional<std::string>("label");

    cli::App ranged("ranged");
    (void)ranged.add_positional<std::vector<int>>("nums").nargs(1, 3);
    REQUIRE_THROWS_AS(
        (void)ranged.add_positional<std::string>("label"),
        std::invalid_argument);
}

TEST_CASE("later parses do not inherit a positional arity count") {
    cli::App app("t");
    auto nums  = app.add_positional<std::vector<int>>("nums").nargs(2);
    auto label = app.add_positional<std::string>("label");

    REQUIRE(app.parse({"1", "2", "first"}));
    REQUIRE(app.parse({"3", "4", "second"}));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[0] == 3);
    CHECK(*label == "second");
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

TEST_CASE("quoted delimiter-separated positional values may contain the delimiter") {
    cli::App app("t");
    auto items = app.add_positional<std::vector<std::string>>("items").delimiter(',');

    REQUIRE(app.parse({R"(a,"b,c",d)"}));
    REQUIRE(items->size() == 3);
    CHECK((*items)[1] == "b,c");
}

TEST_SUITE_END();
