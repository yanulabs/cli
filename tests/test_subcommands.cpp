#include "test_support.hpp"

TEST_SUITE_BEGIN("subcommands");

TEST_CASE("a subcommand is dispatched and parses its own options") {
    cli::App app("t");
    auto& build  = app.subcommand("build", "Compile sources");
    auto  target = build.add<std::string>("--target").default_val("debug");

    REQUIRE(app.parse({"build", "--target", "release"}));
    REQUIRE(app.active_subcommand());
    CHECK(app.active_subcommand() == &build);
    CHECK(app.is_subcommand("build"));
    CHECK(*target == "release");
}

TEST_CASE("subcommand aliases dispatch to the canonical command") {
    cli::App app("t");
    auto& build  = app.subcommand("build", "Compile").alias("b");
    auto  target = build.add<std::string>("--target").default_val("debug");

    REQUIRE(app.parse({"b", "--target", "release"}));
    CHECK(app.active_subcommand() == &build);
    CHECK(app.is_subcommand("build"));
    CHECK(app.is_subcommand("b"));
    CHECK(!app.is_subcommand("other"));
    CHECK(*target == "release");

    REQUIRE(app.parse({"build"}));
    CHECK(app.active_subcommand() == &build);
    CHECK(*target == "debug");
}

TEST_CASE("an unknown subcommand name falls through to a positional") {
    cli::App app("t");
    (void)app.subcommand("build", "");
    auto file = app.add_positional<std::string>("file");

    REQUIRE(app.parse({"notacommand"}));
    CHECK(*file == "notacommand");
    CHECK(!app.active_subcommand());
}

TEST_CASE("an unknown subcommand without a positional is reported as an unknown command") {
    cli::App app("t");
    (void)app.subcommand("build", "");

    auto r = app.parse({"notacommand"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownCommand);
    CHECK(contains(r.error().message, "notacommand"));
}

TEST_CASE("after -- a surplus token is not treated as an unknown command") {
    cli::App app("t");
    (void)app.subcommand("build", "");

    auto r = app.parse({"--", "notacommand"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
    CHECK(contains(r.error().message, "notacommand"));
}

TEST_CASE("nested subcommands dispatch recursively") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "Manage remotes");
    auto& add    = remote.subcommand("add", "Add a remote");
    auto  url    = add.add<std::string>("--url");

    REQUIRE(app.parse({"remote", "add", "--url", "https://example.com"}));
    CHECK(app.active_subcommand() == &remote);
    REQUIRE(remote.active_subcommand());
    CHECK(remote.active_subcommand() == &add);
    CHECK(remote.is_subcommand("add"));
    CHECK(*url == "https://example.com");
}

TEST_CASE("nested subcommand aliases dispatch recursively") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "");
    auto& add    = remote.subcommand("add", "").alias("a");

    REQUIRE(app.parse({"remote", "a"}));
    CHECK(remote.active_subcommand() == &add);
}

TEST_CASE("child defaults are applied only when the child is active") {
    cli::App app("tool");
    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target").default_val("debug");

    REQUIRE(app.parse({"build"}));
    CHECK(*target == "debug");

    REQUIRE(app.parse({}));
    CHECK(!target);
}

TEST_CASE("parent options before a subcommand are parsed") {
    cli::App app("tool");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target");

    REQUIRE(app.parse({"-v", "build", "--target", "release"}));
    CHECK(*verbose);
    CHECK(*target == "release");
}

TEST_CASE("parent options are not implicitly accepted after a subcommand") {
    cli::App app("tool");
    (void)app.add_flag("--verbose").short_name("-v");
    auto& build = app.subcommand("build", "");

    auto r = app.parse({"build", "--verbose"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(r.error().context == &build);
}

TEST_CASE("a subcommand token used as an option value does not dispatch") {
    cli::App app("tool");
    auto mode = app.add<std::string>("--mode");
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"--mode", "build"}));
    CHECK(*mode == "build");
    CHECK(!app.active_subcommand());
}

TEST_CASE("parent and child options with the same name are independent") {
    cli::App app("tool");
    auto parent = app.add<std::string>("--target").default_val("root");
    auto& build = app.subcommand("build", "");
    auto child  = build.add<std::string>("--target").default_val("child");

    REQUIRE(app.parse({"build"}));
    CHECK(*parent == "root");
    CHECK(*child == "child");

    REQUIRE(app.parse({"build", "--target", "custom"}));
    CHECK(*parent == "root");
    CHECK(*child == "custom");
}

TEST_CASE("a parent required option is still enforced after a child succeeds") {
    cli::App app("tool");
    (void)app.add<std::string>("--token").required();
    (void)app.subcommand("build", "");

    auto missing = app.parse({"build"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(missing.error().context == &app);

    REQUIRE(app.parse({"--token", "secret", "build"}));
}

TEST_CASE("required parent positionals are skipped when a child is active") {
    cli::App app("tool");
    (void)app.add_positional<std::string>("file");
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"build"}));
    CHECK(app.is_subcommand("build"));
}

TEST_CASE("a child's required option is reported in the child context") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "");
    (void)build.add<std::string>("--target").required();

    auto r = app.parse({"build"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(r.error().context == &build);
    CHECK(contains(r.error().message, "--target"));
}

TEST_CASE("child help bypasses parent and child requirements") {
    cli::App app("tool", "desc", "1.0.0");
    (void)app.add<std::string>("--token").required();
    auto& build = app.subcommand("build", "");
    (void)build.add<std::string>("--target").required();

    auto r = app.parse({"build", "--help"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::HelpRequested);
    CHECK(r.error().context == &build);
}

TEST_CASE("child version bypasses child requirements and inherits the root version") {
    cli::App app("tool", "desc", "1.2.3");
    auto& build = app.subcommand("build", "");
    (void)build.add<std::string>("--target").required();

    auto r = app.parse({"build", "--version"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::VersionRequested);
    CHECK(r.error().context == &build);
}

TEST_CASE("a child version overrides the root version") {
    cli::App app("tool", "desc", "1.0.0");
    auto& build = app.subcommand("build", "");
    build.version("2.0.0");

    const auto help = build.help_text();
    CHECK(contains(help, "v2.0.0"));
    CHECK(!contains(help, "v1.0.0"));
}

TEST_CASE("an intermediate command's required options are enforced") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "");
    (void)remote.add<std::string>("--token").required();
    (void)remote.subcommand("add", "");

    auto r = app.parse({"remote", "add"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(r.error().context == &remote);
}

TEST_CASE("intermediate required positionals are skipped for a grandchild") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "");
    (void)remote.add_positional<std::string>("name");
    (void)remote.subcommand("add", "");

    REQUIRE(app.parse({"remote", "add"}));
    CHECK(remote.active_subcommand());
}

TEST_CASE("is_subcommand is scoped to the direct child") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "");
    (void)remote.subcommand("add", "");

    REQUIRE(app.parse({"remote", "add"}));
    CHECK(app.is_subcommand("remote"));
    CHECK(!app.is_subcommand("add"));
    CHECK(remote.is_subcommand("add"));
}

TEST_CASE("nested subcommands inherit the root version") {
    cli::App app("tool", "desc", "3.4.5");
    auto& remote = app.subcommand("remote", "");
    auto& add    = remote.subcommand("add", "");

    const auto help = add.help_text();
    CHECK(contains(help, "v3.4.5"));
    CHECK(contains(help, "tool remote add"));
}

TEST_CASE("help for a nested subcommand is scoped to its options") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote", "Manage remotes");
    auto& add    = remote.subcommand("add", "Add a remote");
    (void)add.add<std::string>("--url").help("Remote URL");

    const auto help = add.help_text();
    CHECK(contains(help, "Remote URL"));
    CHECK(!contains(help, "Manage remotes"));
}

TEST_CASE("root help requested before a subcommand is scoped to the root") {
    cli::App app("tool");
    (void)app.subcommand("build", "");

    auto r = app.parse({"--help", "build"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::HelpRequested);
    CHECK(r.error().context == &app);
}

TEST_CASE("a positional before a subcommand token prevents dispatch") {
    cli::App app("tool");
    auto file = app.add_positional<std::string>("file");
    (void)app.subcommand("build", "");

    auto r = app.parse({"notes.txt", "build"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
    CHECK(*file == "notes.txt");
    CHECK(!app.active_subcommand());
}

TEST_CASE("parent positionals are not finalized when a subcommand is active") {
    cli::App app("tool");
    auto file = app.add_positional<std::string>("file").default_val("root.txt");
    auto& build = app.subcommand("build", "");

    REQUIRE(app.parse({"build"}));
    CHECK(!file);
    CHECK(app.active_subcommand() == &build);

    REQUIRE(build.parse({}));
    CHECK(!file);
    CHECK(app.active_subcommand() == &build);
}

TEST_CASE("end-of-options prevents subcommand dispatch") {
    cli::App app("tool");
    auto file = app.add_positional<std::string>("file");
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"--", "build"}));
    CHECK(*file == "build");
    CHECK(!app.active_subcommand());
}

TEST_CASE("child parse errors are not masked by parent requirements") {
    cli::App app("tool");
    (void)app.add<std::string>("--token").required();
    auto& build = app.subcommand("build", "");

    auto r = app.parse({"build", "--nope"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(r.error().context == &build);
}

TEST_CASE("subcommand aliases appear in help") {
    cli::App app("tool");
    auto& build = app.subcommand("build", "Compile").alias("b").alias("bld");
    (void)build;

    const auto help = app.help_text();
    CHECK(contains(help, "build, b, bld"));
    CHECK(contains(help, "Compile"));
}

TEST_CASE("on_execute callbacks can be fired manually for the active child") {
    cli::App app("t");
    auto& sub = app.subcommand("build", "");
    (void)sub.add<std::string>("--target").default_val("debug");

    bool fired = false;
    std::string seen;
    sub.on_execute([&](cli::App& cmd) {
        fired = true;
        seen = cmd["--target"].as<std::string>();
    });

    REQUIRE(app.parse({"build", "--target", "release"}));
    REQUIRE(app.active_subcommand());
    REQUIRE(app.active_subcommand()->has_callback());
    app.active_subcommand()->fire_callback();
    CHECK(fired);
    CHECK(seen == "release");
}

TEST_CASE("fire_callback without a callback is a no-op") {
    cli::App app("t");
    auto& sub = app.subcommand("build", "");
    CHECK(!sub.has_callback());
    sub.fire_callback();
}

TEST_CASE("require_subcommand enforces a command when configured") {
    cli::App app("tool");
    (void)app.add_flag("--verbose");
    auto& build = app.subcommand("build", "Compile");
    app.require_subcommand();

    auto missing = app.parse({"--verbose"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingSubcommand);
    CHECK(missing.error().context == &app);
    CHECK(contains(missing.error().message, "command is required"));

    REQUIRE(app.parse({"build"}));
    CHECK(app.active_subcommand() == &build);

    // Help still wins over the missing-command requirement.
    auto help = app.parse({"--help"});
    REQUIRE(!help);
    CHECK(help.error().kind == cli::ParseError::Kind::HelpRequested);
}

TEST_CASE("require_subcommand is scoped to the command that declares it") {
    cli::App app("tool");
    auto& remote = app.subcommand("remote");
    auto& add    = remote.subcommand("add");
    (void)add;
    (void)remote.subcommand("remove");
    remote.require_subcommand();

    auto missing = app.parse({"remote"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingSubcommand);
    CHECK(missing.error().context == &remote);

    remote.require_subcommand(false);
    REQUIRE(app.parse({"remote"}));

    remote.require_subcommand(true);
    REQUIRE(app.parse({"remote", "add"}));
    CHECK(remote.active_subcommand() == &add);
}

TEST_CASE("a global root flag is accepted before and after a subcommand") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").short_name("-v").global();
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"--verbose", "build"}));
    CHECK(*verbose);

    REQUIRE(app.parse({"build", "--verbose"}));
    CHECK(*verbose);

    REQUIRE(app.parse({"build", "-v"}));
    CHECK(*verbose);

    REQUIRE(app.parse({"build"}));
    CHECK(!*verbose);
}

TEST_CASE("lookup on a subcommand finds inherited global options") {
    cli::App app("t");
    (void)app.add_flag("--verbose").global();
    (void)app.add_flag("--root-only");
    auto& build = app.subcommand("build", "");

    REQUIRE(app.parse({"build", "--verbose"}));
    CHECK(build["--verbose"].as<bool>());
    CHECK(build.has_option("--verbose"));
    CHECK(!build["--root-only"].found());
    CHECK(!build.has_option("--root-only"));
}

TEST_CASE("a global value option is inherited by nested subcommands") {
    cli::App app("t");
    auto cfg = app.add<std::string>("--config").global();
    auto& remote = app.subcommand("remote", "");
    (void)remote.subcommand("add", "");

    REQUIRE(app.parse({"remote", "add", "--config", "x.ini"}));
    CHECK(*cfg == "x.ini");

    REQUIRE(app.parse({"remote", "add", "--config=y.ini"}));
    CHECK(*cfg == "y.ini");
}

TEST_CASE("non-global root options are still rejected after a subcommand") {
    cli::App app("t");
    (void)app.add_flag("--verbose");
    (void)app.subcommand("build", "");

    auto r = app.parse({"build", "--verbose"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("a subcommand option shadows a global option of the same name") {
    cli::App app("t");
    auto root = app.add<std::string>("--name").default_val("root").global();
    auto& sub = app.subcommand("run", "");
    auto own  = sub.add<std::string>("--name").default_val("sub");

    REQUIRE(app.parse({"run", "--name", "custom"}));
    CHECK(*own == "custom");
    CHECK(*root == "root");
}

TEST_CASE("a required global option is enforced and satisfiable after the verb") {
    cli::App app("t");
    (void)app.add<std::string>("--token").required().global();
    (void)app.subcommand("push", "");

    auto missing = app.parse({"push"});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);

    CHECK(app.parse({"push", "--token", "abc"}));
}

TEST_CASE("global counts and negatable flags work after a subcommand") {
    cli::App app("t");
    auto level = app.add_count("--verbose").short_name("-v").global();
    auto color = app.add_flag("--color").negatable().default_val(true).global();
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"build", "-vv", "--no-color"}));
    CHECK(*level == 2u);
    CHECK(!*color);
}

TEST_CASE("typo suggestions include inherited global options") {
    cli::App app("t");
    (void)app.add_flag("--verbose").global();
    (void)app.subcommand("build", "");

    auto r = app.parse({"build", "--verbos"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "--verbose"));
}

TEST_CASE("a global user --help or --version option wins over the built-in in children") {
    cli::App app("tool", "desc", "1.0.0");
    auto help    = app.add_flag("--help").global();
    auto version = app.add_flag("--version").global();
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"build", "--help"}));
    CHECK(*help);
    CHECK(!*version);

    REQUIRE(app.parse({"build", "--version"}));
    CHECK(*version);
}

TEST_CASE("CLI11: a root allow_abbrev setting applies inside a subcommand") {
    cli::App app("t");
    app.allow_abbrev();
    auto cfg = app.add<std::string>("--config").global();
    (void)app.subcommand("build", "");

    REQUIRE(app.parse({"build", "--conf", "x.ini"}));
    CHECK(*cfg == "x.ini");
}

TEST_CASE("CLI11: abbreviations consider inherited globals and local options together") {
    cli::App app("t");
    app.allow_abbrev();
    (void)app.add_flag("--verbose").global();
    auto& sub = app.subcommand("build", "");
    (void)sub.add_flag("--versioned");

    auto r = app.parse({"build", "--ver"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::AmbiguousOption);
    CHECK(contains(r.error().message, "--verbose"));
    CHECK(contains(r.error().message, "--versioned"));
}

TEST_CASE("CLI11 SubComExtras: extras before a subcommand do not block dispatch") {
    cli::App app("tool");
    app.allow_extras();
    auto& run = app.subcommand("run", "");
    auto known = run.add_flag("--known");

    REQUIRE(app.parse({"--extra", "x", "run", "--known"}));
    CHECK(*known);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--extra");
    CHECK(rest[1] == "x");
}

TEST_CASE("argparse parse_known_args: extras after a subcommand are inherited") {
    cli::App app("tool");
    app.allow_extras();
    auto count = app.add<int>("--count").global();
    auto& run = app.subcommand("run", "");
    auto known = run.add_flag("--known");

    REQUIRE(app.parse({"run", "--extra", "value", "--count", "3", "--known", "-q"}));
    CHECK(*count == 3);
    CHECK(*known);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 3);
    CHECK(rest[0] == "--extra");
    CHECK(rest[1] == "value");
    CHECK(rest[2] == "-q");
    // The child still exposes the tokens it forwarded itself.
    CHECK(run.remaining_args() == rest);
}

TEST_CASE("argparse parse_known_args: root sees extras in command-line order") {
    cli::App app("tool");
    app.allow_extras();
    auto& run = app.subcommand("run", "");
    auto known = run.add_flag("--known");

    REQUIRE(app.parse({"--before", "run", "--after", "--known"}));
    CHECK(*known);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--before");
    CHECK(rest[1] == "--after");
}

TEST_CASE("argparse parse_known_args: nested subcommand extras reach the root") {
    cli::App app("tool");
    app.allow_extras();
    auto& mid  = app.subcommand("mid", "");
    auto& leaf = mid.subcommand("leaf", "");
    auto known = leaf.add_flag("--known");

    REQUIRE(app.parse({"mid", "leaf", "--deep", "x", "--known"}));
    CHECK(*known);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--deep");
    CHECK(rest[1] == "x");
}

TEST_CASE("argparse parse_known_args: extras at every nesting level keep their order") {
    cli::App app("tool");
    app.allow_extras();
    auto& run  = app.subcommand("run", "");
    auto& leaf = run.subcommand("leaf", "");
    auto known = leaf.add<std::string>("--known");

    REQUIRE(app.parse({"--parent", "P", "run", "--child", "C",
                       "leaf", "--known", "K", "--leaf", "L"}));
    CHECK(*known == "K");
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 6);
    CHECK(rest[0] == "--parent");
    CHECK(rest[1] == "P");
    CHECK(rest[2] == "--child");
    CHECK(rest[3] == "C");
    CHECK(rest[4] == "--leaf");
    CHECK(rest[5] == "L");
}

TEST_CASE("argparse parse_known_args: unknown compound option is forwarded whole") {
    cli::App app("tool");
    app.allow_extras();
    auto& run = app.subcommand("run", "");

    REQUIRE(app.parse({"-xy", "run", "tail"}));
    CHECK(app.active_subcommand() == &run);
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "-xy");
    CHECK(rest[1] == "tail");
}

TEST_CASE("argparse parse_known_args: extras survive both argc/argv levels") {
    cli::App app("tool");
    app.allow_extras();
    (void)app.subcommand("run", "");

    const char* argv[] = {"tool", "--parent", "run", "--child"};
    REQUIRE(app.parse(4, argv));
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--parent");
    CHECK(rest[1] == "--child");
}

TEST_CASE("argparse parse_known_args: extras do not hide child errors") {
    cli::App app("tool");
    app.allow_extras();
    auto& run = app.subcommand("run", "");
    (void)run.add<int>("--count");

    auto res = app.parse({"--unknown", "run", "--count", "not-a-number", "--extra"});
    REQUIRE(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::InvalidValue);
}

TEST_CASE("argparse parse_known_args: aggregated extras are cleared by the next parse") {
    cli::App app("tool");
    app.allow_extras();
    auto& run = app.subcommand("run", "");

    REQUIRE(app.parse({"run", "--extra", "value"}));
    REQUIRE(app.remaining_args().size() == 2);

    REQUIRE(app.parse({"run"}));
    CHECK(app.remaining_args().empty());
    CHECK(run.remaining_args().empty());
}

TEST_CASE("without root allow_extras a child still rejects unknown options") {
    cli::App app("tool");
    auto& run = app.subcommand("run", "");
    (void)run.add_flag("--known");

    auto res = app.parse({"run", "--nope"});
    REQUIRE(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("a local spelling shadows only the same inherited spelling") {
    cli::App app("t");
    auto root_verbose = app.add_flag("--verbose").short_name("-v").global();
    auto& build = app.subcommand("build", "");
    auto local_v = build.add_flag("-v");

    REQUIRE(app.parse({"build", "--verbose"}));
    CHECK(*root_verbose);
    CHECK(!*local_v);

    REQUIRE(app.parse({"build", "-v"}));
    CHECK(!*root_verbose);
    CHECK(*local_v);
}

TEST_CASE("abbreviations ignore an inherited spelling shadowed by a local option") {
    cli::App app("t");
    app.allow_abbrev();
    auto root_verbose = app.add_flag("--verbose").global();
    auto& build = app.subcommand("build", "");
    auto local_verbose = build.add_flag("--verbose");

    REQUIRE(app.parse({"build", "--verb"}));
    CHECK(*local_verbose);
    CHECK(!*root_verbose);
}

TEST_CASE("a local alias shadows only the same inherited spelling") {
    cli::App app("t");
    auto root_verbose = app.add_flag("--verbose").short_name("-v").global();
    auto& build = app.subcommand("build", "");
    auto local  = build.add_flag("--local").alias("-v");

    REQUIRE(app.parse({"build", "--verbose"}));
    CHECK(*root_verbose);
    CHECK(!*local);

    REQUIRE(app.parse({"build", "-v"}));
    CHECK(!*root_verbose);
    CHECK(*local);
}

TEST_CASE("the nearest ancestor global wins when spellings collide") {
    cli::App app("t");
    auto root_name = app.add<std::string>("--name").default_val("root").global();
    auto& mid      = app.subcommand("mid", "");
    auto mid_name  = mid.add<std::string>("--name").default_val("mid").global();
    auto& leaf     = mid.subcommand("leaf", "");

    REQUIRE(leaf.parse({"--name", "leaf"}));
    CHECK(*mid_name == "leaf");
    CHECK(*root_name == "root");
    CHECK(leaf["--name"].as<std::string>() == "leaf");

    REQUIRE(app.parse({"mid", "leaf", "--name", "again"}));
    CHECK(*mid_name == "again");
    CHECK(*root_name == "root");
}

TEST_CASE("typo suggestions use inherited spellings that remain visible") {
    cli::App app("t");
    auto root_verbose = app.add_flag("--verbose").short_name("-v").global();
    auto& build = app.subcommand("build", "");
    (void)build.add_flag("-v");
    (void)root_verbose;

    auto r = app.parse({"build", "--verbos"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(r.error().message, "--verbose"));
}

TEST_CASE("direct subcommand parsing finalizes inherited globals") {
    cli::App app("tool");
    auto jobs = app.add<int>("--jobs")
                    .default_val(4)
                    .validate([](int v) { return v % 2 == 0; },
                              "jobs must be even")
                    .transform([](int v) { return v * 2; })
                    .global();
    auto token = app.add<std::string>("--token").required().global();
    auto& build = app.subcommand("build", "");

    auto missing = build.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(missing.error().context == &app);
    CHECK(contains(missing.error().message, "--token"));

    auto invalid = build.parse({"--token", "x", "--jobs", "5"});
    REQUIRE(!invalid);
    CHECK(invalid.error().kind == cli::ParseError::Kind::ValidationFailed);
    CHECK(contains(invalid.error().message, "jobs must be even"));

    REQUIRE(build.parse({"--token", "x"}));
    CHECK(*token == "x");
    CHECK(*jobs == 8);
}

TEST_CASE("direct subcommand parsing ignores non-global ancestor requirements") {
    cli::App app("tool");
    auto token = app.add<std::string>("--token").required();
    auto mode  = app.add<std::string>("--mode").default_val("root");
    auto& build = app.subcommand("build", "");

    REQUIRE(build.parse({}));
    CHECK(!token);
    CHECK(*mode == "root");
    CHECK(app.active_subcommand() == &build);

    auto root_missing = app.parse({"build"});
    REQUIRE(!root_missing);
    CHECK(root_missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(root_missing.error().message, "--token"));

    auto child_unknown = build.parse({"--token", "x"});
    REQUIRE(!child_unknown);
    CHECK(child_unknown.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("direct subcommand parsing enforces only global ancestor groups") {
    cli::App app("tool");
    auto verbose = app.add_flag("--verbose").global();
    auto quiet   = app.add_flag("--quiet").global();
    (void)verbose;
    (void)quiet;
    (void)app.add_flag("--json");
    (void)app.add_flag("--yaml");
    app.mutex_group({"--verbose", "--quiet"});
    app.require_one_of({"--json", "--yaml"});
    auto& build = app.subcommand("build", "");

    REQUIRE(build.parse({}));
    auto conflict = build.parse({"--verbose", "--quiet"});
    REQUIRE(!conflict);
    CHECK(conflict.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("a failed direct child parse clears inherited global state") {
    cli::App app("tool");
    auto verbose = app.add_flag("--verbose").global();
    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target");

    auto bad = build.parse({"--verbose", "--target", "release", "--unknown"});
    REQUIRE(!bad);
    CHECK(!verbose);
    CHECK(!target);
    CHECK(!app.active_subcommand());
    CHECK(!build.active_subcommand());
}

TEST_CASE("parsing a nested subcommand directly finalizes every ancestor") {
    cli::App app("tool");
    auto root_value = app.add<int>("--root").default_val(1).global();
    auto& mid = app.subcommand("mid", "");
    auto mid_value = mid.add<int>("--mid").default_val(2).global();
    auto& leaf = mid.subcommand("leaf", "");

    REQUIRE(leaf.parse({}));
    CHECK(*root_value == 1);
    CHECK(*mid_value == 2);
}

TEST_CASE("direct subcommand parsing resets stale ancestor state") {
    cli::App app("tool");
    auto verbose = app.add_flag("--verbose").global();
    auto& build = app.subcommand("build", "");

    REQUIRE(app.parse({"build", "--verbose"}));
    CHECK(*verbose);

    REQUIRE(build.parse({}));
    CHECK(!*verbose);
}

TEST_CASE("direct subcommand parsing records the active command chain") {
    cli::App app("tool");
    auto& mid  = app.subcommand("mid", "");
    auto& leaf = mid.subcommand("leaf", "");

    REQUIRE(leaf.parse({}));
    CHECK(app.active_subcommand() == &mid);
    CHECK(mid.active_subcommand() == &leaf);
    CHECK(app.is_subcommand("mid"));
    CHECK(mid.is_subcommand("leaf"));
    CHECK(!app.is_subcommand("leaf"));
}

TEST_CASE("direct subcommand parsing aggregates extras on every ancestor") {
    cli::App app("tool");
    app.allow_extras();
    auto& mid = app.subcommand("mid", "");
    (void)mid.add_flag("--known");
    auto& leaf = mid.subcommand("leaf", "");

    REQUIRE(app.parse({"mid", "leaf", "--extra", "value"}));
    const auto expected = app.remaining_args();
    REQUIRE(expected.size() == 2);

    REQUIRE(leaf.parse({"--extra", "value"}));
    CHECK(app.remaining_args() == expected);
    CHECK(mid.remaining_args() == expected);
    CHECK(leaf.remaining_args() == expected);
}

TEST_SUITE_END();
