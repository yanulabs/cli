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

TEST_CASE("an unknown subcommand without a positional is unexpected") {
    cli::App app("t");
    (void)app.subcommand("build", "");

    auto r = app.parse({"notacommand"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
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

TEST_SUITE_END();
