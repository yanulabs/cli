#include "test_support.hpp"

#include <sstream>

TEST_SUITE_BEGIN("help");

TEST_CASE("minimal help contains usage, options and built-in help only") {
    cli::App app("tool");
    const auto help = app.help_text();

    CHECK(contains(help, "tool"));
    CHECK(contains(help, "USAGE"));
    CHECK(contains(help, "OPTIONS"));
    CHECK(contains(help, "-h, --help"));
    CHECK(contains(help, "Print help and exit"));
    CHECK(!contains(help, "--version"));
    CHECK(!contains(help, "ARGUMENTS"));
    CHECK(!contains(help, "COMMANDS"));
    CHECK(!contains(help, "EXAMPLES"));
}

TEST_CASE("version rows appear only when a version is configured") {
    cli::App with_version("tool", "desc", "1.2.3");
    const auto versioned = with_version.help_text();
    CHECK(contains(versioned, "v1.2.3"));
    CHECK(contains(versioned, "-V, --version"));
    CHECK(contains(versioned, "Print version and exit"));

    cli::App without_version("tool");
    const auto plain = without_version.help_text();
    CHECK(!contains(plain, "--version"));
    CHECK(!contains(plain, "Print version and exit"));
}

TEST_CASE("description, author and examples are rendered") {
    cli::App app("tool", "A useful tool", "2.0.0");
    app.author("Ada").example("tool --verbose").example("tool build");

    const auto help = app.help_text();
    CHECK(contains(help, "A useful tool"));
    CHECK(contains(help, "Author: Ada"));
    CHECK(contains(help, "EXAMPLES"));
    CHECK(contains(help, "tool --verbose"));
    CHECK(contains(help, "tool build"));
}

TEST_CASE("named sections are rendered before Other Options") {
    cli::App app("tool");
    (void)app.add<std::string>("--host").section("Connection Options").help("Host");
    (void)app.add<std::string>("--port").section("Connection Options").help("Port");
    (void)app.add_flag("--verbose").help("Verbose");

    const auto help = app.help_text();
    const auto connection = help.find("Connection Options:");
    const auto other      = help.find("Other Options:");
    REQUIRE(connection != std::string::npos);
    REQUIRE(other != std::string::npos);
    CHECK(connection < other);
    CHECK(contains(help, "Host"));
    CHECK(contains(help, "Port"));
    CHECK(contains(help, "Verbose"));
}

TEST_CASE("Other Options still hosts built-ins when user options are all named") {
    cli::App app("tool");
    (void)app.add<std::string>("--host").section("Connection Options");
    const auto help = app.help_text();
    CHECK(contains(help, "Connection Options:"));
    CHECK(contains(help, "Other Options:"));
    CHECK(contains(help, "-h, --help"));
}

TEST_CASE("an empty Other Options section is not rendered") {
    cli::App app("tool");
    (void)app.add<std::string>("--host").section("Connection Options");
    // Registering the built-in spellings *inside a named section* suppresses
    // the built-in rows without creating an unnamed section of their own.
    (void)app.add_flag("-h").section("Connection Options");
    (void)app.add_flag("--help").section("Connection Options");

    const auto help = app.help_text();
    CHECK(contains(help, "Connection Options:"));
    CHECK(!contains(help, "Other Options:"));
    CHECK(contains(help, "  -h"));
    CHECK(contains(help, "  --help"));
}

TEST_CASE("a hidden-only section is not rendered") {
    cli::App app("tool");
    (void)app.add<std::string>("--secret").section("Internal").hidden();
    const auto help = app.help_text();
    CHECK(!contains(help, "Internal:"));
    CHECK(!contains(help, "--secret"));
}

TEST_CASE("defaults, choices, env and required annotations are rendered") {
    cli::App app("tool");
    (void)app.add<std::string>("--host").default_val("localhost").env("HOST");
    (void)app.add<int>("--port").required();
    (void)app.add<std::string>("--level").choices({"debug", "info"});

    const auto help = app.help_text();
    CHECK(contains(help, "[default: localhost]"));
    CHECK(contains(help, "[env: HOST]"));
    CHECK(contains(help, "[required]"));
    CHECK(contains(help, "[debug|info]"));
    CHECK(!contains(help, "[default: \"localhost\"]"));
}

TEST_CASE("vector defaults are not double-bracketed") {
    cli::App app("tool");
    (void)app.add<std::vector<std::string>>("--tag")
        .default_val(std::vector<std::string>{"one", "two"});

    const auto help = app.help_text();
    CHECK(contains(help, "[default: one, two]"));
    CHECK(!contains(help, "[default: [one, two]]"));
}

TEST_CASE("vector defaults preserve empty elements when joined") {
    cli::App app("tool");
    (void)app.add<std::vector<std::string>>("--tag").default_val({"", "two", ""});

    const auto help = app.help_text();
    CHECK(contains(help, "[default: , two, ]"));
}

TEST_CASE("choices preserve separators around empty values") {
    cli::App app("tool");
    (void)app.add<std::string>("--level").choices({"", "auto"});

    const auto help = app.help_text();
    CHECK(contains(help, "[|auto]"));
}

TEST_CASE("a false flag default is not advertised") {
    cli::App app("tool");
    (void)app.add_flag("--color").default_val(false);
    const auto help = app.help_text();
    CHECK(!contains(help, "[default: false]"));
}

TEST_CASE("a true flag default is advertised") {
    cli::App app("tool");
    (void)app.add_flag("--color").default_val(true);
    const auto help = app.help_text();
    CHECK(contains(help, "[default: true]"));
}

TEST_CASE("type metadata can be overridden") {
    cli::App app("tool");
    (void)app.add<std::string>("--output").meta("DIR");
    (void)app.add<int>("--count");

    const auto help = app.help_text();
    CHECK(contains(help, "--output <DIR>"));
    CHECK(contains(help, "--count <INT>"));
}

TEST_CASE("option spellings are aligned and combined") {
    cli::App app("tool");
    (void)app.add<std::string>("--output").short_name("-o").help("Output path");
    (void)app.add_flag("--verbose").help("Verbose");
    (void)app.add_flag("-q").help("Quiet");

    const auto help = app.help_text();
    CHECK(contains(help, "-o, --output <TEXT>"));
    CHECK(contains(help, "--verbose"));
    CHECK(contains(help, "  -q "));
}

TEST_CASE("negatable flags show their negated spelling") {
    cli::App app("tool");
    (void)app.add_flag("--color").negatable().help("Color output");
    const auto help = app.help_text();
    CHECK(contains(help, "--[no-]color"));
}

TEST_CASE("count options are annotated as repeatable") {
    cli::App app("tool");
    (void)app.add_count("--verbose").short_name("-v").help("Verbosity");
    const auto help = app.help_text();
    CHECK(contains(help, "[count]"));
}

TEST_CASE("hidden options do not appear in help") {
    cli::App app("tool");
    (void)app.add<std::string>("--secret").hidden().help("classified");
    const auto help = app.help_text();
    CHECK(!contains(help, "--secret"));
    CHECK(!contains(help, "classified"));
}

TEST_CASE("positional usage distinguishes required, optional and variadic") {
    cli::App app("tool");
    (void)app.add_positional<std::string>("required");
    (void)app.add_positional<std::string>("optional").required(false);
    (void)app.add_positional<std::vector<std::string>>("many");

    const auto usage = app.help_text();
    CHECK(contains(usage, "<required>"));
    CHECK(contains(usage, "[optional]"));
    CHECK(contains(usage, "<many...>"));
}

TEST_CASE("an optional variadic positional is shown in brackets") {
    cli::App app("tool");
    (void)app.add_positional<std::vector<std::string>>("files").required(false);

    const auto help = app.help_text();
    CHECK(contains(help, "[files...]"));
    CHECK(!contains(help, "<files...>"));
}

TEST_CASE("the arguments section renders positional annotations") {
    cli::App app("tool");
    (void)app.add_positional<std::string>("file")
        .help("Input file")
        .default_val("input.txt")
        .env("INPUT");

    const auto help = app.help_text();
    CHECK(contains(help, "ARGUMENTS"));
    CHECK(contains(help, "Input file"));
    CHECK(contains(help, "[default: input.txt]"));
    CHECK(contains(help, "[env: INPUT]"));
}

TEST_CASE("subcommands and aliases are listed with descriptions") {
    cli::App app("tool");
    (void)app.add_flag("--verbose");
    (void)app.subcommand("build", "Compile sources").alias("b");
    (void)app.subcommand("test", "Run tests");

    const auto help = app.help_text();
    CHECK(contains(help, "COMMANDS"));
    CHECK(contains(help, "build, b"));
    CHECK(contains(help, "Compile sources"));
    CHECK(contains(help, "test"));
    CHECK(contains(help, "Run tests"));
    CHECK(contains(help, "Run 'tool <COMMAND> --help'"));
    CHECK(contains(help, "[COMMAND]"));
    CHECK(!contains(help, "[OPTIONS] <COMMAND>"));

    cli::App required("required");
    (void)required.subcommand("build");
    required.require_subcommand();
    CHECK(contains(required.help_text(), "[OPTIONS] <COMMAND>"));
}

TEST_CASE("subcommand help uses the full command path and inherited version") {
    cli::App app("tool", "Root description", "2.0.0");
    auto& remote = app.subcommand("remote", "Manage remotes");
    auto& add    = remote.subcommand("add", "Add a remote");

    const auto help = add.help_text();
    CHECK(contains(help, "tool remote add"));
    CHECK(contains(help, "v2.0.0"));
    CHECK(contains(help, "Add a remote"));
    CHECK(!contains(help, "Root description"));
}

TEST_CASE("a custom -h suppresses the built-in short help spelling") {
    cli::App app("tool");
    (void)app.add_flag("-h").help("custom short help");

    const auto help = app.help_text();
    CHECK(contains(help, "custom short help"));
    CHECK(contains(help, "--help"));
    CHECK(!contains(help, "-h, --help"));
}

TEST_CASE("a custom --help suppresses the built-in long help spelling") {
    cli::App app("tool");
    (void)app.add_flag("--help").help("custom long help");

    const auto help = app.help_text();
    CHECK(contains(help, "custom long help"));
    CHECK(contains(help, "  -h"));
    CHECK(!contains(help, "-h, --help"));
}

TEST_CASE("custom help and version options suppress the built-ins entirely") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("-h").help("short help");
    (void)app.add_flag("--help").help("long help");
    (void)app.add_flag("-V").help("short version");
    (void)app.add_flag("--version").help("long version");

    const auto help = app.help_text();
    CHECK(!contains(help, "Print help and exit"));
    CHECK(!contains(help, "Print version and exit"));
    CHECK(contains(help, "short help"));
    CHECK(contains(help, "long version"));
}

TEST_CASE("custom version spellings suppress built-in version rows per name") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("-V").help("custom short version");

    const auto help = app.help_text();
    CHECK(contains(help, "custom short version"));
    CHECK(contains(help, "--version"));
    CHECK(!contains(help, "-V, --version"));
}

TEST_CASE("print_help writes the rendered text to a stream") {
    cli::App app("tool", "description");
    std::ostringstream out;
    app.print_help(out);
    CHECK(contains(out.str(), "USAGE"));
    CHECK(contains(out.str(), "description"));
    CHECK(out.str() == app.help_text());
}

TEST_CASE("help text is stable across calls") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add<int>("--num").default_val(4);
    CHECK(app.help_text() == app.help_text());
}

TEST_CASE("path, integer and boolean defaults render through to_display") {
    cli::App app("tool");
    (void)app.add<std::filesystem::path>("--path").default_val("/tmp/data");
    (void)app.add<int>("--num").default_val(7);
    (void)app.add<bool>("--feature").default_val(true);

    const auto help = app.help_text();
    CHECK(contains(help, "[default: /tmp/data]"));
    CHECK(contains(help, "[default: 7]"));
    CHECK(contains(help, "[default: true]"));
}

TEST_CASE("vector positional defaults appear in arguments") {
    cli::App app("tool");
    (void)app.add_positional<std::vector<std::string>>("files")
        .default_val(std::vector<std::string>{"a.txt", "b.txt"});

    const auto help = app.help_text();
    CHECK(contains(help, "[default: a.txt, b.txt]"));
    CHECK(!contains(help, "[default: [a.txt, b.txt]]"));
}

TEST_CASE("positional choices appear in arguments") {
    cli::App app("tool");
    (void)app.add_positional<std::string>("level")
        .choices({"debug", "info", "warn"});

    const auto help = app.help_text();
    CHECK(contains(help, "[debug|info|warn]"));
}

TEST_SUITE_END();
