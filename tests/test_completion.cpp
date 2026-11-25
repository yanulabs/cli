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

TEST_CASE("fish completion escapes backslashes in quoted words") {
    cli::App app("tool");
    (void)app.add_flag("--trail\\");
    (void)app.add_flag("--back\\slash");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();
    CHECK(contains(text, R"(-l 'trail\\' -f)"));
    CHECK(contains(text, R"(-l 'back\\slash' -f)"));
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

TEST_CASE("subcommand completion includes inherited globals and suppresses built-ins") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add_flag("--verbose").global();
    (void)app.add_flag("-h").global();
    (void)app.add_flag("--help").global();
    (void)app.add_flag("-V").global();
    (void)app.add_flag("--version").global();
    auto& sub = app.subcommand("build", "");

    std::ostringstream bash;
    sub.bash_completion(bash);
    CHECK(count_occurrences(bash.str(), "--verbose ") == 1);
    CHECK(count_occurrences(bash.str(), "-h ") == 1);
    CHECK(count_occurrences(bash.str(), "--help ") == 1);
    CHECK(count_occurrences(bash.str(), "-V ") == 1);
    CHECK(count_occurrences(bash.str(), "--version ") == 1);

    std::ostringstream fish;
    sub.fish_completion(fish);
    CHECK(count_occurrences(fish.str(), "-l verbose") == 1);
    CHECK(count_occurrences(fish.str(), "-s h ") == 1);
    CHECK(count_occurrences(fish.str(), "-l help") == 1);
    CHECK(count_occurrences(fish.str(), "-s V ") == 1);
    CHECK(count_occurrences(fish.str(), "-l version") == 1);

    std::ostringstream zsh;
    sub.zsh_completion(zsh);
    CHECK(count_occurrences(zsh.str(), "'--verbose'") == 1);
    CHECK(count_occurrences(zsh.str(), "'-h'") == 1);
    CHECK(count_occurrences(zsh.str(), "'--help'") == 1);
    CHECK(count_occurrences(zsh.str(), "'-V'") == 1);
    CHECK(count_occurrences(zsh.str(), "'--version'") == 1);
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
    CHECK(contains(text, "'(-o --output)-o:DIR:_default"));
    CHECK(contains(text, "'(-o --output)--output:DIR:_default"));
}

TEST_CASE("zsh completion marks vector options as repeatable") {
    cli::App app("tool");
    (void)app.add<std::vector<std::string>>("--tag").help("Tag");

    std::ostringstream out;
    app.zsh_completion(out);
    CHECK(contains(out.str(), "'*--tag[Tag]:TEXT:_default'"));
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

TEST_CASE("shadowing one inherited spelling keeps other completion spellings") {
    cli::App app("tool");
    (void)app.add_flag("--verbose")
        .short_name("-v")
        .help("Root verbose")
        .global();
    auto& sub = app.subcommand("build", "");
    (void)sub.add_flag("-v").help("Sub short v");

    std::ostringstream bash;
    sub.bash_completion(bash);
    CHECK(count_occurrences(bash.str(), "--verbose ") == 1);
    CHECK(count_occurrences(bash.str(), "-v ") == 1);

    std::ostringstream fish;
    sub.fish_completion(fish);
    CHECK(count_occurrences(fish.str(), "-l verbose") == 1);
    CHECK(count_occurrences(fish.str(), "-s v ") == 1);

    std::ostringstream zsh;
    sub.zsh_completion(zsh);
    CHECK(contains(zsh.str(), "'--verbose[Root verbose]'"));
    CHECK(contains(zsh.str(), "'-v[Sub short v]'"));
}

TEST_CASE("completions include option aliases") {
    cli::App app("tool");
    (void)app.add_flag("--color")
        .short_name("-c")
        .alias("--colour")
        .alias("--col")
        .alias("-C")
        .help("Color")
        .negatable();

    std::ostringstream bash;
    app.bash_completion(bash);
    CHECK(contains(bash.str(), "--colour "));
    CHECK(contains(bash.str(), "--col "));
    CHECK(contains(bash.str(), "-C "));
    CHECK(contains(bash.str(), "--no-color "));
    CHECK(contains(bash.str(), "--no-colour "));
    CHECK(contains(bash.str(), "--no-col "));

    std::ostringstream fish;
    app.fish_completion(fish);
    CHECK(contains(fish.str(), "complete -c tool -s c -l color -f -d 'Color'"));
    CHECK(contains(fish.str(), "complete -c tool -l colour -f -d 'Color'"));
    CHECK(contains(fish.str(), "complete -c tool -l col -f -d 'Color'"));
    CHECK(contains(fish.str(), "complete -c tool -s C -f -d 'Color'"));
    CHECK(contains(fish.str(), "complete -c tool -l no-color -f"));
    CHECK(contains(fish.str(), "complete -c tool -l no-colour -f"));
    CHECK(contains(fish.str(), "complete -c tool -l no-col -f"));

    std::ostringstream zsh;
    app.zsh_completion(zsh);
    CHECK(contains(zsh.str(),
                   "'(-c --color --colour --col -C)-c[Color]'"));
    CHECK(contains(zsh.str(),
                   "'(-c --color --colour --col -C)--colour[Color]'"));
    CHECK(contains(zsh.str(), "--no-colour[Disable --colour]"));
}

TEST_CASE("negated alias completions defer to exact option spellings") {
    cli::App app("tool");
    (void)app.add_flag("--color").alias("--colour").negatable();
    (void)app.add_flag("--no-colour");

    std::ostringstream bash;
    app.bash_completion(bash);
    CHECK(count_occurrences(bash.str(), "--no-colour ") == 1);
    CHECK(contains(bash.str(), "--no-color "));

    std::ostringstream fish;
    app.fish_completion(fish);
    CHECK(count_occurrences(fish.str(), "-l no-colour ") == 1);
    CHECK(contains(fish.str(), "-l no-color "));

    std::ostringstream zsh;
    app.zsh_completion(zsh);
    CHECK(count_occurrences(zsh.str(), "--no-colour") == 1);
    CHECK(contains(zsh.str(), "--no-color[Disable --color]"));
}

TEST_CASE("root bash completion switches options by command path") {
    cli::App app("tool");
    (void)app.add_flag("--root-only");
    (void)app.add_flag("--enable");
    (void)app.add_flag("--verbose").short_name("-v").global();
    (void)app.add<std::string>("--output").short_name("-o");
    (void)app.add<std::vector<std::string>>("--pair").nargs(2);
    auto& build = app.subcommand("build", "").alias("b");
    (void)build.add<std::string>("--target");
    auto& remote = app.subcommand("remote", "").alias("r");
    auto& add = remote.subcommand("add", "").alias("a");
    (void)add.add<std::string>("--url");
    (void)app.subcommand("hidden", "").hidden().add_flag("--secret");

    std::ostringstream out;
    app.bash_completion(out);
    const auto text = out.str();
    CHECK(contains(text, "case \"$path|$word\" in"));
    CHECK(contains(text, "_tool_option_info()"));
    CHECK(contains(text, "if ((skip_max != 0))"));
    CHECK(contains(text, "[[ ${COMP_WORDS[i+1]} == = ]]"));
    CHECK(contains(text, "'|--pair') opt_kind=1; skip_min=2; skip_max=2"));
    CHECK(contains(text, "'|-o') opt_kind=1; skip_min=1; skip_max=1"));
    CHECK(contains(text, "'|--verbose') opt_kind=0; skip_min=0; skip_max=0"));
    CHECK(contains(text, "'|--enable') opt_kind=0; skip_min=0; skip_max=0"));
    CHECK(contains(text, "if ((opt_kind == 0))"));
    CHECK(contains(text, "'|build') path=build"));
    CHECK(contains(text, "'remote|add') path='remote add'"));
    CHECK(contains(text, "build) opts=\""));
    CHECK(contains(text, "--target"));
    CHECK(contains(text, "--url"));
    CHECK(!contains(text, "--secret"));
}

TEST_CASE("root fish completion scopes options to command paths") {
    cli::App app("tool");
    (void)app.add_flag("--root-only");
    (void)app.add<std::vector<std::string>>("--pair").nargs(2);
    auto& build = app.subcommand("build", "").alias("b");
    (void)build.add<std::string>("--target");
    auto& remote = app.subcommand("remote", "").alias("r");
    auto& add = remote.subcommand("add", "").alias("a");
    (void)add.add<std::string>("--url");

    std::ostringstream out;
    app.fish_completion(out);
    const auto text = out.str();
    CHECK(contains(text, "test (__tool_command_context) = -"));
    CHECK(contains(text, "function __tool_option_info"));
    CHECK(contains(text, "set -l tokens (commandline -opc)"));
    CHECK(!contains(text, "$tokens[(math $i + 1)] = ="));
    CHECK(contains(text, "case '|--pair'\n            printf '%s\\n' 1 2 2"));
    CHECK(contains(text, "-l target -r -n 'test (__tool_command_context) = build'"));
    CHECK(contains(text, R"(-l url -r -n 'test (__tool_command_context) = '"'"'remote add'"'"'')"));
    CHECK(contains(text, "-l root-only -f -n 'test (__tool_command_context) = -'"));
}

TEST_CASE("root zsh completion dispatches per-command option specs") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "").alias("b");
    (void)build.add<std::string>("--target");
    auto& remote = app.subcommand("remote", "").alias("r");
    auto& add = remote.subcommand("add", "").alias("a");
    (void)add.add<std::string>("--url");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();
    CHECK(contains(text, "_tool_build_2()"));
    CHECK(contains(text, "_arguments -C \"${args[@]}\""));
    CHECK(contains(text, "_tool_remote_3()"));
    CHECK(contains(text, "_tool_remote_add_4()"));
    CHECK(contains(text, "'build'|'b') _tool_build_2"));
    CHECK(contains(text, "'add'|'a') _tool_remote_add_4"));
    CHECK(contains(text, "'--target:TEXT:_default'"));
    CHECK(contains(text, "'--url:TEXT:_default'"));
}

TEST_CASE("zsh completion quotes punctuated option aliases") {
    cli::App app("tool");
    (void)app.add_flag("--base")
        .alias("--comma,name")
        .alias("--cash$HOME")
        .alias("--brace{a,b}");
    (void)app.add_flag("--quoted")
        .alias("--single'quote")
        .alias("--double\"quote")
        .alias("--back\\slash")
        .negatable();
    (void)app.add_flag("--paren").alias("--paren(name)");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();
    CHECK(contains(text, "'(--base --comma,name --cash$HOME --brace{a,b})--base'"));
    CHECK(contains(text, "'(--base --comma,name --cash$HOME --brace{a,b})--comma,name'"));
    CHECK(contains(text, "'(--base --comma,name --cash$HOME --brace{a,b})--cash$HOME'"));
    CHECK(contains(text, "--single'\\''quote"));
    CHECK(contains(text, "--double\"quote"));
    CHECK(contains(text, "--back\\\\slash"));
    CHECK(contains(text, "--no-back\\\\slash[Disable --back\\\\slash]"));
    CHECK(contains(text, "'--paren(name)'"));
    CHECK(!contains(text, "(--paren --paren(name))"));
    CHECK(!contains(text, "--comma,name}"));
}

TEST_CASE("zsh completion disambiguates sanitized command names") {
    cli::App app("tool");
    (void)app.subcommand("foo-bar", "").add_flag("--hyphen");
    (void)app.subcommand("foo_bar", "").add_flag("--underscore");

    std::ostringstream out;
    app.zsh_completion(out);
    const auto text = out.str();
    CHECK(contains(text, "_tool_foo_bar_2()"));
    CHECK(contains(text, "_tool_foo_bar_3()"));
    CHECK(contains(text, "'foo-bar') _tool_foo_bar_2"));
    CHECK(contains(text, "'foo_bar') _tool_foo_bar_3"));
}

TEST_SUITE_END();
