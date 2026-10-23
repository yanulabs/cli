#include "test_support.hpp"

#include <sstream>

TEST_SUITE_BEGIN("completion");

TEST_CASE("bash completion lists visible options, flags and built-ins") {
    cli::App app("tool");
    (void)app.add_flag("--verbose").short_name("-v").help("Verbose");
    (void)app.add<std::string>("--output").short_name("-o").help("Output");

    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "# Bash completion for tool"));
    CHECK(contains(text, "complete -F _tool tool"));
    CHECK(contains(text, "--verbose"));
    CHECK(contains(text, "-v"));
    CHECK(contains(text, "--output"));
    CHECK(contains(text, "-h "));
    CHECK(contains(text, "--help "));
    CHECK(!contains(text, "--version "));
}

TEST_CASE("bash completion sanitizes function names for hyphenated tools") {
    cli::App app("my-tool");
    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "_my_tool()"));
    CHECK(contains(text, "complete -F _my_tool my-tool"));
}

TEST_CASE("bash completion keeps function identifiers valid for unusual names") {
    cli::App app("my tool");
    (void)app.add_flag("--verbose");
    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "_my_tool()"));
    CHECK(contains(text, "complete -F _my_tool 'my tool'"));
}

TEST_CASE("completion headers neutralize control characters in app names") {
    cli::App app("bad\nname");

    std::ostringstream bash_out;
    app.bash_completion(bash_out);
    CHECK(!contains(bash_out.str(), "# Bash completion for bad\nname"));
    CHECK(contains(bash_out.str(), "_bad_name()"));

    std::ostringstream zsh_out;
    app.zsh_completion(zsh_out);
    CHECK(!contains(zsh_out.str(), "#compdef bad\nname"));
    CHECK(contains(zsh_out.str(), "_bad_name()"));
}

TEST_CASE("fish completion quotes unusual command names") {
    cli::App app("my tool");
    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "complete -c 'my tool'"));
    CHECK(!contains(text, "complete -c my tool"));
}

TEST_CASE("zsh completion keeps function identifiers valid for unusual names") {
    cli::App app("my tool");
    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "_my_tool()"));
    CHECK(contains(text, "_my_tool\n"));
}

TEST_CASE("bash completion includes negated flags and excludes hidden options") {
    cli::App app("tool");
    (void)app.add_flag("--color").negatable();
    (void)app.add_flag("--secret").hidden();
    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "--no-color"));
    CHECK(!contains(text, "--secret"));
}

TEST_CASE("bash completion includes subcommands and aliases") {
    cli::App app("tool");
    (void)app.subcommand("build", "Compile").alias("b");
    (void)app.subcommand("test", "Run tests");

    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "subs=\"build b test "));
}

TEST_CASE("bash completion adds version flags only when a version exists") {
    cli::App versioned("tool", "desc", "1.0.0");
    std::ostringstream with_version;
    versioned.bash_completion(with_version);
    CHECK(contains(with_version.str(), "-V "));
    CHECK(contains(with_version.str(), "--version "));

    cli::App plain("tool");
    std::ostringstream without_version;
    plain.bash_completion(without_version);
    CHECK(!contains(without_version.str(), "-V "));
    CHECK(!contains(without_version.str(), "--version "));
}

TEST_CASE("bash completion suppresses built-ins per occupied spelling") {
    cli::App short_only("tool");
    (void)short_only.add_flag("-h");
    std::ostringstream short_out;
    short_only.bash_completion(short_out);
    CHECK(count_occurrences(short_out.str(), "-h ") == 1);
    CHECK(count_occurrences(short_out.str(), "--help ") == 1);

    cli::App long_only("tool");
    (void)long_only.add_flag("--help");
    std::ostringstream long_out;
    long_only.bash_completion(long_out);
    CHECK(count_occurrences(long_out.str(), "-h ") == 1);
    CHECK(count_occurrences(long_out.str(), "--help ") == 1);

    cli::App both("tool", "desc", "1.0.0");
    (void)both.add_flag("-h");
    (void)both.add_flag("--help");
    (void)both.add_flag("-V");
    (void)both.add_flag("--version");
    std::ostringstream both_out;
    both.bash_completion(both_out);
    CHECK(count_occurrences(both_out.str(), "-h ") == 1);
    CHECK(count_occurrences(both_out.str(), "--help ") == 1);
    CHECK(count_occurrences(both_out.str(), "-V ") == 1);
    CHECK(count_occurrences(both_out.str(), "--version ") == 1);
}

TEST_CASE("fish completion marks flags and value-taking options correctly") {
    cli::App app("tool");
    (void)app.add_flag("--verbose").short_name("-v").help("Verbose");
    (void)app.add<std::string>("--output").short_name("-o").help("Output");
    (void)app.add_count("--debug").short_name("-d").help("Debug");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "complete -c tool -s v -l verbose -f"));
    CHECK(contains(text, "complete -c tool -s o -l output -r"));
    CHECK(contains(text, "complete -c tool -s d -l debug -f"));
}

TEST_CASE("fish completion emits choice lists for value options") {
    cli::App app("tool");
    (void)app.add<std::string>("--level").choices({"debug", "info"});
    (void)app.add_count("--verbose");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "-a 'debug info'"));
    CHECK(!contains(text, "-a 'count'"));
}

TEST_CASE("fish completion includes negated flags and excludes hidden options") {
    cli::App app("tool");
    (void)app.add_flag("--color").negatable();
    (void)app.add_flag("--secret").hidden();

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "-l no-color"));
    CHECK(!contains(text, "--secret"));
    CHECK(!contains(text, "-l secret"));
}

TEST_CASE("fish completion escapes apostrophes in help text") {
    cli::App app("tool");
    (void)app.add_flag("--force").help("It's required");

    std::ostringstream out;
    app.fish_completion(out);
    CHECK(contains(out.str(), "It'\"'\"'s required"));
}

TEST_CASE("fish completion includes subcommands and aliases") {
    cli::App app("tool");
    (void)app.subcommand("build", "Compile").alias("b");
    (void)app.subcommand("test", "Run tests");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "-a 'build' -d 'Compile'"));
    CHECK(contains(text, "-a 'b' -d 'Compile (→ build)'"));
    CHECK(contains(text, "-a 'test' -d 'Run tests'"));
}

TEST_CASE("fish completion distinguishes short and old-style options") {
    cli::App app("tool");
    (void)app.add_flag("-v");
    (void)app.add_flag("-verbose");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, " -s v "));
    CHECK(contains(text, " -o verbose "));
}

TEST_CASE("fish completion includes version only when configured") {
    cli::App versioned("tool", "desc", "1.0.0");
    std::ostringstream with_version;
    versioned.fish_completion(with_version);
    CHECK(contains(with_version.str(), "-s V"));
    CHECK(contains(with_version.str(), "-l version"));

    cli::App plain("tool");
    std::ostringstream without_version;
    plain.fish_completion(without_version);
    CHECK(!contains(without_version.str(), "-s V"));
    CHECK(!contains(without_version.str(), "-l version"));
}

TEST_CASE("fish completion suppresses built-ins per occupied spelling") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("-h").help("custom short help");
    (void)app.add_flag("--version").help("custom long version");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "-s h -f -d 'custom short help'"));
    CHECK(count_occurrences(text, "-l help") == 1);
    CHECK(contains(text, "-l version -f -d 'custom long version'"));
    CHECK(count_occurrences(text, "-s V") == 1);
}

TEST_CASE("zsh completion emits value specs and choice actions") {
    cli::App app("tool");
    (void)app.add<std::string>("--level").choices({"debug", "info"});
    (void)app.add<std::string>("--output").short_name("-o").meta("DIR");
    (void)app.add_count("--verbose").short_name("-v");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "#compdef tool"));
    CHECK(contains(text, ":TEXT:(debug info)"));
    CHECK(contains(text, ":DIR:_default"));
    CHECK(contains(text, "'(-o --output)'{-o,--output}"));
}

TEST_CASE("zsh completion marks vector options as repeatable") {
    cli::App app("tool");
    (void)app.add<std::vector<std::string>>("--tag").help("Tag");

    std::ostringstream out;
    app.zsh_completion(out);
    CHECK(contains(out.str(), "*'--tag[Tag]':TEXT:_default"));
}

TEST_CASE("zsh completion includes negated flags and excludes hidden options") {
    cli::App app("tool");
    (void)app.add_flag("--color").negatable();
    (void)app.add_flag("--secret").hidden();

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "--no-color[Disable --color]"));
    CHECK(!contains(text, "--secret"));
}

TEST_CASE("zsh completion escapes special characters in help text") {
    cli::App app("tool");
    (void)app.add_flag("--force").help("Input [file]: it's");

    std::ostringstream out;
    app.zsh_completion(out);
    CHECK(contains(out.str(), "Input \\[file\\]\\: it'\\''s"));
}

TEST_CASE("zsh completion includes subcommands and aliases") {
    cli::App app("tool");
    (void)app.subcommand("build", "Compile").alias("b");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "'build:Compile'"));
    CHECK(contains(text, "'b:Compile (alias)'"));
    CHECK(contains(text, "_describe 'commands' subcmds"));
}

TEST_CASE("zsh completion includes version only when configured") {
    cli::App versioned("tool", "desc", "1.0.0");
    std::ostringstream with_version;
    versioned.zsh_completion(with_version);
    CHECK(contains(with_version.str(), "-V"));
    CHECK(contains(with_version.str(), "--version"));

    cli::App plain("tool");
    std::ostringstream without_version;
    plain.zsh_completion(without_version);
    CHECK(!contains(without_version.str(), "--version"));
}

TEST_CASE("zsh completion suppresses built-ins per occupied spelling") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("-h").help("custom help");
    (void)app.add_flag("--version").help("custom version");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();

    CHECK(contains(text, "custom help"));
    CHECK(count_occurrences(text, "--help") == 1);
    CHECK(contains(text, "custom version"));
    CHECK(count_occurrences(text, "-V") == 1);
    CHECK(!contains(text, "'(-h --help)'"));
    CHECK(!contains(text, "'(-V --version)'"));
}

TEST_CASE("completions work for an app with no user options") {
    cli::App app("tool");

    std::ostringstream bash;
    app.bash_completion(bash);
    CHECK(contains(bash.str(), "--help"));

    std::ostringstream fish;
    app.fish_completion(fish);
    CHECK(contains(fish.str(), "-l help"));

    std::ostringstream zsh;
    app.zsh_completion(zsh);
    CHECK(contains(zsh.str(), "{-h,--help}"));
}

TEST_SUITE_END();
