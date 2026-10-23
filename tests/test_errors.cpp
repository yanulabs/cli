#include "test_support.hpp"

#include <iostream>
#include <sstream>

TEST_SUITE_BEGIN("errors");

TEST_CASE("unknown options are reported with their spelling and context") {
    cli::App app("t");
    auto r = app.parse({"--nope"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(r.error().context == &app);
    CHECK(contains(r.error().message, "--nope"));
}

TEST_CASE("edit distance handles inserts, deletions and substitutions") {
    CHECK(cli::detail::edit_distance("", "") == 0);
    CHECK(cli::detail::edit_distance("abc", "") == 3);
    CHECK(cli::detail::edit_distance("", "abc") == 3);
    CHECK(cli::detail::edit_distance("same", "same") == 0);
    CHECK(cli::detail::edit_distance("kitten", "sitting") == 3);
    CHECK(cli::detail::edit_distance("no-colr", "no-color") == 1);
}

TEST_CASE("a close unknown option suggests the intended spelling") {
    cli::App app("t");
    (void)app.add<std::string>("--flag");
    auto r = app.parse({"--flg"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "did you mean"));
    CHECK(contains(r.error().message, "--flag"));
}

TEST_CASE("a one-edit typo suggests the intended option") {
    cli::App app("t");
    (void)app.add_flag("--color");
    auto r = app.parse({"--colr"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "--color"));
}

TEST_CASE("a distant unknown option has no suggestion") {
    cli::App app("t");
    (void)app.add<std::string>("--flag");
    auto r = app.parse({"--zzzzzz"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(!contains(r.error().message, "did you mean"));
}

TEST_CASE("hidden options are not suggested") {
    cli::App app("t");
    (void)app.add<std::string>("--secret").hidden();
    auto r = app.parse({"--secre"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(!contains(r.error().message, "--secret"));
}

TEST_CASE("--no-X typos suggest the negated spelling") {
    cli::App app("t");
    (void)app.add_flag("--color").negatable();
    auto r = app.parse({"--no-colr"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "--no-color"));
}

TEST_CASE("missing option values are diagnosed separately from invalid values") {
    cli::App app("t");
    (void)app.add<std::string>("--name");
    auto r = app.parse({"--name"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingValue);
    CHECK(contains(r.error().message, "--name"));
    CHECK(contains(r.error().message, "requires a value"));
}

TEST_CASE("unexpected values are diagnosed for count options") {
    cli::App app("t");
    (void)app.add_count("--verbose");
    auto r = app.parse({"--verbose=3"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnexpectedValue);
    CHECK(contains(r.error().message, "--verbose"));
    CHECK(contains(r.error().message, "does not take a value"));
}

TEST_CASE("invalid values include the value, option and reason") {
    cli::App app("t");
    (void)app.add<int>("--num").short_name("-n");
    auto r = app.parse({"-n", "many"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "many"));
    CHECK(contains(r.error().message, "-n"));
    CHECK(contains(r.error().message, "not a valid integer"));
}

TEST_CASE("invalid booleans list the accepted spellings") {
    cli::App app("t");
    (void)app.add<bool>("--flag");
    auto r = app.parse({"--flag", "maybe"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(r.error().message, "true/false"));
    CHECK(contains(r.error().message, "yes/no"));
}

TEST_CASE("missing required options name the option") {
    cli::App app("t");
    (void)app.add<std::string>("--token").required();
    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(r.error().message, "--token"));
    CHECK(contains(r.error().message, "not provided"));
}

TEST_CASE("missing required positionals use the MissingRequired kind") {
    cli::App app("t");
    (void)app.add_positional<std::string>("file");
    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(r.error().message, "<file>"));
    CHECK(contains(r.error().message, "required argument"));
}

TEST_CASE("extra positionals name the offending argument") {
    cli::App app("t");
    (void)app.add_positional<std::string>("first");
    auto r = app.parse({"one", "two"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
    CHECK(contains(r.error().message, "two"));
}

TEST_CASE("range errors include the value and inclusive bounds") {
    cli::App app("t");
    (void)app.add<int>("--num").range(2, 8);
    auto r = app.parse({"--num", "10"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
    CHECK(contains(r.error().message, "10"));
    CHECK(contains(r.error().message, "[2, 8]"));
}

TEST_CASE("choice errors list every valid choice") {
    cli::App app("t");
    (void)app.add<std::string>("--level").choices({"debug", "info", "warn"});
    auto r = app.parse({"--level", "trace"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
    CHECK(contains(r.error().message, "trace"));
    CHECK(contains(r.error().message, "debug|info|warn"));
}

TEST_CASE("validation errors include the custom description") {
    cli::App app("t");
    (void)app.add<int>("--num")
        .validate([](const int& n) { return n > 0; }, "must be positive");
    auto r = app.parse({"--num", "0"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(contains(r.error().message, "must be positive"));
}

TEST_CASE("help and version requests carry empty messages and their context") {
    cli::App app("tool", "desc", "1.0.0");
    auto help = app.parse({"--help"});
    REQUIRE(!help);
    CHECK(help.error().kind == cli::ParseError::Kind::HelpRequested);
    CHECK(help.error().message.empty());
    CHECK(help.error().context == &app);
    CHECK(help.error().is_exit_request());

    auto version = app.parse({"--version"});
    REQUIRE(!version);
    CHECK(version.error().kind == cli::ParseError::Kind::VersionRequested);
    CHECK(version.error().message.empty());
    CHECK(version.error().context == &app);
    CHECK(version.error().is_exit_request());
}

TEST_CASE("an error inside a child points at the child") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "");
    auto r = app.parse({"build", "--bogus"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(r.error().context == &build);
}

TEST_CASE("ParseError factories can be used directly") {
    auto unknown = cli::ParseError::unknown("--misspelled", "--misspelling");
    CHECK(unknown.kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(unknown.message, "did you mean '--misspelling'"));

    auto missing = cli::ParseError::missing_value("--out");
    CHECK(missing.kind == cli::ParseError::Kind::MissingValue);

    auto unexpected = cli::ParseError::unexpected_value("--verbose", "3");
    CHECK(unexpected.kind == cli::ParseError::Kind::UnexpectedValue);

    auto invalid = cli::ParseError::invalid("--num", "x", "not a number");
    CHECK(invalid.kind == cli::ParseError::Kind::InvalidValue);
    CHECK(contains(invalid.message, "not a number"));

    auto required = cli::ParseError::missing_required("--token");
    CHECK(required.kind == cli::ParseError::Kind::MissingRequired);

    auto range = cli::ParseError::out_of_range("--num", "9", "1", "5");
    CHECK(range.kind == cli::ParseError::Kind::OutOfRange);

    auto choice = cli::ParseError::bad_choice("--level", "x", "a|b");
    CHECK(choice.kind == cli::ParseError::Kind::BadChoice);

    auto validation = cli::ParseError::validation("--num", "must be even");
    CHECK(validation.kind == cli::ParseError::Kind::ValidationFailed);

    auto too_many = cli::ParseError::too_many("extra");
    CHECK(too_many.kind == cli::ParseError::Kind::TooManyPositionals);

    auto mutex = cli::ParseError::mutually_exclusive("--a", "--b");
    CHECK(mutex.kind == cli::ParseError::Kind::MutuallyExclusive);

    auto requires_err = cli::ParseError::requires_option("--a", "--b");
    CHECK(requires_err.kind == cli::ParseError::Kind::RequiresOption);
}

TEST_CASE("print_error writes the message and a help hint to stderr") {
    cli::App app("tool");
    (void)app.add<int>("--num");
    auto result = app.parse({"--num", "x"});
    REQUIRE(!result);

    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    app.print_error(result.error());
    std::cerr.rdbuf(previous);

    const auto text = captured.str();
    CHECK(contains(text, "error:"));
    CHECK(contains(text, "invalid value 'x'"));
    CHECK(contains(text, "Try 'tool --help'"));
}

TEST_CASE("print_error uses the child command's path in its help hint") {
    cli::App app("tool");
    (void)app.subcommand("build", "");
    auto result = app.parse({"build", "--nope"});
    REQUIRE(!result);

    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    app.print_error(result.error());
    std::cerr.rdbuf(previous);

    const auto text = captured.str();
    CHECK(contains(text, "error:"));
    CHECK(contains(text, "Try 'tool build --help'"));
}

TEST_CASE("exit requests are not treated as failures of the application") {
    cli::App app("tool", "desc", "1.0.0");
    auto help = app.parse({"-h"});
    REQUIRE(!help);
    CHECK(help.error().is_exit_request());

    auto version = app.parse({"-V"});
    REQUIRE(!version);
    CHECK(version.error().is_exit_request());

    (void)app.add<int>("--num");
    auto invalid = app.parse({"--num", "x"});
    REQUIRE(!invalid);
    CHECK(!invalid.error().is_exit_request());
}

TEST_SUITE_END();
