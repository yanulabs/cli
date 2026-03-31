// ─────────────────────────────────────────────────────────────────────────────
// tests/test_parse.cpp — cli.hpp unit tests (doctest)
// ─────────────────────────────────────────────────────────────────────────────
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cli.hpp>

// Helper: build argv from string literals
struct Args {
    std::vector<std::string>  storage;
    std::vector<char*>        argv_;
    int                       argc_;

    explicit Args(std::initializer_list<std::string_view> tokens) {
        storage.emplace_back("test");   // program name
        for (auto t : tokens) storage.emplace_back(t);
        for (auto& s : storage) argv_.push_back(s.data());
        argc_ = static_cast<int>(argv_.size());
    }

    int    argc() { return argc_; }
    char** argv() { return argv_.data(); }
};

// ── §1  Flag ──────────────────────────────────────────────────────────────────

TEST_CASE("Flag: absent → false") {
    cli::App app("t");
    auto flag = app.flag("-v", "--verbose", "");
    auto r = app.parse(Args{}.argc(), Args{}.argv());
    REQUIRE(r);
    CHECK(!*flag);
    CHECK(!flag.is_set());
}

TEST_CASE("Flag: present long") {
    cli::App app("t");
    auto flag = app.flag("-v", "--verbose", "");
    Args a{"--verbose"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*flag);
    CHECK(flag.is_set());
}

TEST_CASE("Flag: present short") {
    cli::App app("t");
    auto flag = app.flag("-v", "--verbose", "");
    Args a{"-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*flag);
}

TEST_CASE("Flag: grouped short flags -vqd") {
    cli::App app("t");
    auto v = app.flag("-v", "--verbose", "");
    auto q = app.flag("-q", "--quiet",   "");
    auto d = app.flag("-d", "--debug",   "");
    Args a{"-vqd"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*v); CHECK(*q); CHECK(*d);
}

// ── §2  Option<T> ─────────────────────────────────────────────────────────────

TEST_CASE("Option: default value applied") {
    cli::App app("t");
    auto jobs = app.option<unsigned>("-j", "--jobs", "").default_val(4u);
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(*jobs == 4u);
    CHECK(!jobs.is_set());
}

TEST_CASE("Option: long --jobs 8") {
    cli::App app("t");
    auto jobs = app.option<unsigned>("-j", "--jobs", "").default_val(4u);
    Args a{"--jobs", "8"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*jobs == 8u);
    CHECK(jobs.is_set());
}

TEST_CASE("Option: inline --jobs=16") {
    cli::App app("t");
    auto jobs = app.option<unsigned>("-j", "--jobs", "").default_val(4u);
    Args a{"--jobs=16"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*jobs == 16u);
}

TEST_CASE("Option: inline short -j32") {
    cli::App app("t");
    auto jobs = app.option<unsigned>("-j", "--jobs", "").default_val(4u);
    Args a{"-j32"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*jobs == 32u);
}

TEST_CASE("Option: required missing → error") {
    cli::App app("t");
    app.option<std::string>("-o", "--out", "").required();
    auto r = app.parse(Args{}.argc(), Args{}.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
}

TEST_CASE("Option: string value") {
    cli::App app("t");
    auto out = app.option<std::string>("-o", "--out", "").required();
    Args a{"--out", "hello"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*out == "hello");
}

TEST_CASE("Option: path value") {
    cli::App app("t");
    auto p = app.option<std::filesystem::path>("-p", "--path", "").required();
    Args a{"--path", "/tmp/test"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(p->string() == "/tmp/test");
}

TEST_CASE("Option: float value") {
    cli::App app("t");
    auto pi = app.option<double>("-x", "--pi", "").required();
    Args a{"--pi", "3.14"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*pi == doctest::Approx(3.14));
}

TEST_CASE("Option: invalid integer → error") {
    cli::App app("t");
    auto n = app.option<int>("-n", "--num", "").required();
    Args a{"--num", "abc"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
}

// ── §3  Range constraint ──────────────────────────────────────────────────────

TEST_CASE("Range: value in range passes") {
    cli::App app("t");
    auto n = app.option<int>("-n", "--num", "").required().range(1, 10);
    Args a{"-n", "5"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*n == 5);
}

TEST_CASE("Range: value out of range → error") {
    cli::App app("t");
    app.option<int>("-n", "--num", "").required().range(1, 10);
    Args a{"-n", "99"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

// ── §4  Choices constraint ────────────────────────────────────────────────────

TEST_CASE("Choices: valid → passes") {
    cli::App app("t");
    auto lvl = app.option<std::string>("-l", "--level", "")
                   .choices({"debug", "info", "warn"})
                   .default_val(std::string("info"));
    Args a{"--level", "debug"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*lvl == "debug");
}

TEST_CASE("Choices: invalid → error") {
    cli::App app("t");
    app.option<std::string>("-l", "--level", "")
       .choices({"debug", "info", "warn"}).required();
    Args a{"--level", "trace"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::BadChoice);
}

// ── §5  Multi-value (vector) ──────────────────────────────────────────────────

TEST_CASE("Vector option: multiple tokens accumulated") {
    cli::App app("t");
    auto tags = app.option<std::vector<std::string>>("--tag", "");
    Args a{"--tag", "foo", "--tag", "bar", "--tag", "baz"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    REQUIRE(tags);
    REQUIRE(tags->size() == 3);
    CHECK((*tags)[0] == "foo");
    CHECK((*tags)[2] == "baz");
}

// ── §6  Unknown option / suggestion ───────────────────────────────────────────

TEST_CASE("Unknown option → error") {
    cli::App app("t");
    Args a{"--foobar"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

TEST_CASE("Unknown close option → did-you-mean suggestion") {
    cli::App app("t");
    // "flg" vs "flag" stripped = distance 2 (≤ threshold 3)
    app.option<std::string>("--flag", "").required();
    Args a{"--flg"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(r.error().message.find("--flag") != std::string::npos);
}

// ── §7  End-of-options -- ─────────────────────────────────────────────────────

TEST_CASE("End-of-options marker: -- passes remainder as positionals") {
    cli::App app("t");
    auto file = app.positional<std::string>("file", "");
    Args a{"--", "--not-an-option"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*file == "--not-an-option");
}

// ── §8  Positionals ───────────────────────────────────────────────────────────

TEST_CASE("Positional: consumed in order") {
    cli::App app("t");
    auto src = app.positional<std::string>("src", "");
    auto dst = app.positional<std::string>("dst", "");
    Args a{"hello", "world"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*src == "hello");
    CHECK(*dst == "world");
}

TEST_CASE("Positional: required missing → error") {
    cli::App app("t");
    (void)app.positional<std::string>("src", "");
    auto r = app.parse(Args{}.argc(), Args{}.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingRequired);
}

TEST_CASE("Positional: variadic accumulates all extra") {
    cli::App app("t");
    auto files = app.positional<std::vector<std::string>>("files", "");
    Args a{"a.txt", "b.txt", "c.txt"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    REQUIRE(files->size() == 3);
}

TEST_CASE("Positional: too many → error") {
    cli::App app("t");
    (void)app.positional<std::string>("src", "");
    Args a{"hello", "unexpected"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
}

// ── §9  Subcommands ───────────────────────────────────────────────────────────

TEST_CASE("Subcommand: dispatched correctly") {
    cli::App app("t");
    auto& build  = app.subcommand("build", "");
    auto  target = build.option<std::string>("-t", "--target", "")
                       .default_val(std::string("debug"));
    Args a{"build", "--target", "release"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    REQUIRE(app.active_subcommand());
    CHECK(app.active_subcommand()->name() == "build");
    CHECK(*target == "release");
}

TEST_CASE("Subcommand: unknown name falls to positional") {
    cli::App app("t");
    (void)app.subcommand("build", "");
    auto f = app.positional<std::string>("file", "");
    Args a{"notacommand"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*f == "notacommand");
}

// ── §10  Boolean parsing ──────────────────────────────────────────────────────

TEST_CASE("Bool option: true/false/yes/no/1/0/on/off") {
    const std::pair<std::string, bool> cases[] = {
        {"true",  true},  {"false", false},
        {"yes",   true},  {"no",    false},
        {"1",     true},  {"0",     false},
        {"on",    true},  {"off",   false},
    };
    for (auto& [s, expected] : cases) {
        cli::App app("t");
        auto b = app.option<bool>("-b", "--bool", "").required();
        Args a{"--bool", s};
        REQUIRE(app.parse(a.argc(), a.argv()));
        CHECK(*b == expected);
    }
}

TEST_CASE("Bool option: invalid → error") {
    cli::App app("t");
    app.option<bool>("-b", "--bool", "").required();
    Args a{"--bool", "maybe"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::InvalidValue);
}

// ── §11  Validator ────────────────────────────────────────────────────────────

TEST_CASE("Validator: passes when predicate is true") {
    cli::App app("t");
    auto n = app.option<int>("-n", "--num", "").required()
                .validate([](const int& v){ return v % 2 == 0; }, "must be even");
    Args a{"-n", "4"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*n == 4);
}

TEST_CASE("Validator: fails when predicate is false") {
    cli::App app("t");
    app.option<int>("-n", "--num", "").required()
       .validate([](const int& v){ return v % 2 == 0; }, "must be even");
    Args a{"-n", "3"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ValidationFailed);
}

// ── §12  value_or ─────────────────────────────────────────────────────────────

TEST_CASE("value_or: returns fallback when not set") {
    cli::App app("t");
    auto n = app.option<int>("-n", "--num", "");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(n.value_or(42) == 42);
}

TEST_CASE("value_or: returns actual value when set") {
    cli::App app("t");
    auto n = app.option<int>("-n", "--num", "").required();
    Args a{"-n", "7"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(n.value_or(42) == 7);
}

// ── §13  v2 fluent add<T> builder ────────────────────────────────────────────

TEST_CASE("add<T>: registers option, short_name works") {
    cli::App app("t");
    auto n = app.add<int>("--num").short_name("-n").help("A number").default_val(99);
    Args a{"-n", "7"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*n == 7);
    CHECK(n.is_set());
}

TEST_CASE("add<T>: default_val implicit string conversion (no cast needed)") {
    cli::App app("t");
    auto s = app.add<std::string>("--level").default_val("info");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(*s == "info");
}

TEST_CASE("add_flag: registers flag, short_name works") {
    cli::App app("t");
    auto f = app.add_flag("--verbose").short_name("-v").help("verbose");
    Args a{"-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*f);
    CHECK(f.is_set());
}

TEST_CASE("add_flag: absent → false") {
    cli::App app("t");
    auto f = app.add_flag("--verbose").short_name("-v");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(!*f);
}

TEST_CASE("add_positional: works with help chained") {
    cli::App app("t");
    auto p = app.add_positional<std::string>("src").help("source file");
    Args a{"hello.txt"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*p == "hello.txt");
}

// ── §14  App::operator[] Lookup proxy ────────────────────────────────────────

TEST_CASE("Lookup: as<T> returns set value") {
    cli::App app("t");
    app.add<int>("--num").short_name("-n").required();
    Args a{"--num", "42"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(app["--num"].as<int>() == 42);
    CHECK(app["-n"].as<int>() == 42);  // short alias also works
}

TEST_CASE("Lookup: as_or returns fallback when not set") {
    cli::App app("t");
    app.add<int>("--num").default_val(0);
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(app["--num"].as_or<int>(99) == 0);  // has default, so set
    CHECK(app["--missing"].as_or<int>(77) == 77);  // not found → fallback
}

TEST_CASE("Lookup: found() reflects registration") {
    cli::App app("t");
    (void)app.add<int>("--num");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(app["--num"].found());
    CHECK(!app["--other"].found());
}

// ── §15  has_flag / has_option ────────────────────────────────────────────────

TEST_CASE("has_flag: true when flag was explicitly set") {
    cli::App app("t");
    app.add_flag("--verbose").short_name("-v");
    Args a{"-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(app.has_flag("--verbose"));
    CHECK(app.has_flag("-v"));
}

TEST_CASE("has_flag: false when flag was not passed") {
    cli::App app("t");
    (void)app.add_flag("--verbose");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(!app.has_flag("--verbose"));
}

TEST_CASE("has_option: true when option has a value (default or explicit)") {
    cli::App app("t");
    app.add<int>("--num").default_val(5);
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(app.has_option("--num"));
}

// ── §16  3-arg App constructor ────────────────────────────────────────────────

TEST_CASE("3-arg constructor sets version") {
    cli::App app("t", "desc", "2.0.0");
    CHECK(app.ver() == "2.0.0");
    CHECK(app.name() == "t");
    CHECK(app.description() == "desc");
}

// ── §17  on_execute callback ──────────────────────────────────────────────────

TEST_CASE("on_execute: fires for selected subcommand") {
    cli::App app("t");
    auto& sub  = app.subcommand("build", "");
    sub.add<std::string>("--target").default_val("debug");

    bool fired = false;
    std::string seen;
    sub.on_execute([&fired, &seen](cli::App& cmd) {
        fired = true;
        seen  = cmd["--target"].as<std::string>();
    });

    Args a{"build", "--target", "release"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    // Manually fire (parse_or_exit would also do this automatically)
    if (auto* s = app.active_subcommand(); s && s->has_callback())
        s->fire_callback();

    REQUIRE(fired);
    CHECK(seen == "release");
}

// ── §18  Count option ─────────────────────────────────────────────────────────

TEST_CASE("Count: absent → 0") {
    cli::App app("t");
    auto v = app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(*v == 0u);
    CHECK(!v.is_set());
}

TEST_CASE("Count: single -v → 1") {
    cli::App app("t");
    auto v = app.add_count("--verbose").short_name("-v");
    Args a{"-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*v == 1u);
    CHECK(v.is_set());
}

TEST_CASE("Count: three separate -v → 3") {
    cli::App app("t");
    auto v = app.add_count("--verbose").short_name("-v");
    Args a{"-v", "-v", "-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*v == 3u);
}

TEST_CASE("Count: grouped -vvv → 3") {
    cli::App app("t");
    auto v = app.add_count("--verbose").short_name("-v");
    Args a{"-vvv"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*v == 3u);
}

TEST_CASE("Count: value_or returns fallback when absent") {
    cli::App app("t");
    auto v = app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(v.value_or(7u) == 7u);
}

// ── §19  Transform ────────────────────────────────────────────────────────────

TEST_CASE("Transform: applied to parsed string value") {
    cli::App app("t");
    auto s = app.add<std::string>("--name").required()
                .transform([](std::string v) {
                    std::ranges::transform(v, v.begin(),
                        [](unsigned char c){ return (char)std::toupper(c); });
                    return v;
                });
    Args a{"--name", "hello"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*s == "HELLO");
}

TEST_CASE("Transform: applied to default value") {
    cli::App app("t");
    auto s = app.add<std::string>("--name").default_val("world")
                .transform([](std::string v) { return "~" + v + "~"; });
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
    CHECK(*s == "~world~");
}

TEST_CASE("Transform: applied to int value") {
    cli::App app("t");
    auto n = app.add<int>("--num").required()
                .transform([](int v){ return v * 2; });
    Args a{"--num", "21"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(*n == 42);
}

// ── §20  Mutex group ──────────────────────────────────────────────────────────

TEST_CASE("Mutex group: only one set → ok") {
    cli::App app("t");
    app.add_flag("--verbose").short_name("-v");
    app.add_flag("--quiet").short_name("-q");
    app.mutex_group({"--verbose", "--quiet"});
    Args a{"-v"};
    REQUIRE(app.parse(a.argc(), a.argv()));
}

TEST_CASE("Mutex group: both set → error") {
    cli::App app("t");
    app.add_flag("--verbose").short_name("-v");
    app.add_flag("--quiet").short_name("-q");
    app.mutex_group({"--verbose", "--quiet"});
    Args a{"-v", "-q"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MutuallyExclusive);
}

TEST_CASE("Mutex group: neither set → ok") {
    cli::App app("t");
    (void)app.add_flag("--verbose");
    (void)app.add_flag("--quiet");
    app.mutex_group({"--verbose", "--quiet"});
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
}

// ── §21  Require-together ─────────────────────────────────────────────────────

TEST_CASE("Require-together: all set → ok") {
    cli::App app("t");
    app.add<std::string>("--output").required();
    app.add<std::string>("--format").required();
    app.require_together({"--output", "--format"});
    Args a{"--output", "out.bin", "--format", "json"};
    REQUIRE(app.parse(a.argc(), a.argv()));
}

TEST_CASE("Require-together: none set → ok") {
    cli::App app("t");
    (void)app.add<std::string>("--output");
    (void)app.add<std::string>("--format");
    app.require_together({"--output", "--format"});
    REQUIRE(app.parse(Args{}.argc(), Args{}.argv()));
}

TEST_CASE("Require-together: partial set → error") {
    cli::App app("t");
    (void)app.add<std::string>("--output").required();
    (void)app.add<std::string>("--format");
    app.require_together({"--output", "--format"});
    Args a{"--output", "out.bin"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::RequiresOption);
}

// ── §22  Subcommand alias ─────────────────────────────────────────────────────

TEST_CASE("Subcommand alias: short form dispatched correctly") {
    cli::App app("t");
    auto& build = app.subcommand("build", "Compile").alias("b");
    auto target = build.add<std::string>("--target").default_val("debug");
    Args a{"b", "--target", "release"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    REQUIRE(app.active_subcommand());
    CHECK(app.active_subcommand()->name() == "build");
    CHECK(*target == "release");
}

TEST_CASE("Subcommand alias: canonical name still works") {
    cli::App app("t");
    app.subcommand("build", "").alias("b");
    Args a{"build"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(app.active_subcommand()->name() == "build");
}

// ── §23  Negatable flag ───────────────────────────────────────────────────────

TEST_CASE("Negatable flag: --no-FLAG sets false when flag is absent") {
    cli::App app("t");
    app.add_flag("--color").negatable();
    Args a{"--no-color"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(!app.has_flag("--color"));
}

TEST_CASE("Negatable flag: --FLAG then --no-FLAG → false (last wins)") {
    cli::App app("t");
    auto f = app.add_flag("--color").negatable();
    Args a{"--color", "--no-color"};
    REQUIRE(app.parse(a.argc(), a.argv()));
    CHECK(!*f);
}

TEST_CASE("Negatable flag: non-negatable --no-FLAG → unknown option error") {
    cli::App app("t");
    (void)app.add_flag("--color");  // NOT negatable
    Args a{"--no-color"};
    auto r = app.parse(a.argc(), a.argv());
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

// ── §24  parse(initializer_list) ──────────────────────────────────────────────

TEST_CASE("parse(initializer_list): basic flag") {
    cli::App app("t");
    auto v = app.add_flag("--verbose").short_name("-v");
    REQUIRE(app.parse({"-v"}));
    CHECK(*v);
}

TEST_CASE("parse(initializer_list): option with value") {
    cli::App app("t");
    auto j = app.add<int>("--jobs").short_name("-j");
    REQUIRE(app.parse({"--jobs", "8"}));
    CHECK(*j == 8);
}

TEST_CASE("parse(initializer_list): empty args → defaults apply") {
    cli::App app("t");
    auto j = app.add<int>("--jobs").default_val(4);
    REQUIRE(app.parse({}));
    CHECK(*j == 4);
}

// ── §25  Negative number positionals ─────────────────────────────────────────

TEST_CASE("Negative number: integer positional accepts -1") {
    cli::App app("t");
    auto n = app.add_positional<int>("num").help("number");
    REQUIRE(app.parse({"-1"}));
    CHECK(*n == -1);
}

TEST_CASE("Negative number: float positional accepts -3.14") {
    cli::App app("t");
    auto x = app.add_positional<double>("x").help("x");
    REQUIRE(app.parse({"-3"}));
    CHECK(*x == -3.0);
}

TEST_CASE("Negative number: still errors when no positional registered") {
    cli::App app("t");
    auto r = app.parse({"-42"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::TooManyPositionals);
}

// ── §26  Lookup::as_count and as<unsigned> for Count ─────────────────────────

TEST_CASE("Lookup::as_count: returns correct count via []") {
    cli::App app("t");
    (void)app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse({"-vvv"}));
    CHECK(app["--verbose"].as_count() == 3u);
}

TEST_CASE("Lookup::as<unsigned>: works for Count option") {
    cli::App app("t");
    (void)app.add_count("--verbose").short_name("-v");
    REQUIRE(app.parse({"-v", "-v"}));
    CHECK(app["--verbose"].as<unsigned>() == 2u);
}

TEST_CASE("Lookup::as_count: returns 0 when absent") {
    cli::App app("t");
    (void)app.add_count("--verbose");
    REQUIRE(app.parse({}));
    CHECK(app["--verbose"].as_count() == 0u);
}

// ── §27  App::reset() ─────────────────────────────────────────────────────────

TEST_CASE("reset: clears parsed state for reuse") {
    cli::App app("t");
    auto v = app.add_flag("--verbose").short_name("-v");
    auto j = app.add<int>("--jobs").default_val(4);

    REQUIRE(app.parse({"-v", "--jobs", "8"}));
    CHECK(*v);
    CHECK(*j == 8);

    app.reset();
    REQUIRE(app.parse({}));
    CHECK(!*v);
    CHECK(*j == 4);  // default re-applied after reset
}

TEST_CASE("reset: count option resets to 0") {
    cli::App app("t");
    auto c = app.add_count("--verbose").short_name("-v");

    REQUIRE(app.parse({"-vvv"}));
    CHECK(*c == 3u);

    app.reset();
    REQUIRE(app.parse({"-v"}));
    CHECK(*c == 1u);
}

