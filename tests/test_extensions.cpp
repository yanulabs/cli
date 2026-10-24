#include "test_support.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace {

class TempFile {
public:
    TempFile(std::string_view name, std::string_view contents) {
        static unsigned counter = 0;
        path_ = std::filesystem::temp_directory_path()
              / std::format("cli_test_{}_{}_{}.ini", name, counter++,
#if defined(_WIN32)
                            static_cast<unsigned long>(::_getpid())
#else
                            static_cast<unsigned long>(::getpid())
#endif
              );
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << contents;
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile&)            = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_SUITE_BEGIN("extensions");

// ── nargs ───────────────────────────────────────────────────────────────────

TEST_CASE("nargs consumes an exact number of tokens per occurrence") {
    cli::App app("t");
    auto files = app.add<std::vector<std::string>>("--file").nargs(2);

    REQUIRE(app.parse({"--file", "a", "b", "--file", "c", "d"}));
    REQUIRE(files->size() == 4);
    CHECK((*files)[0] == "a");
    CHECK((*files)[1] == "b");
    CHECK((*files)[2] == "c");
    CHECK((*files)[3] == "d");
}

TEST_CASE("nargs counts an inline value as the first token") {
    cli::App app("t");
    auto nums = app.add<std::vector<int>>("--num").nargs(2);

    REQUIRE(app.parse({"--num=1", "2"}));
    REQUIRE(nums->size() == 2);
    CHECK((*nums)[0] == 1);
    CHECK((*nums)[1] == 2);
}

TEST_CASE("a ranged nargs stops before the next option") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose");
    auto tags    = app.add<std::vector<std::string>>("--tag").nargs(1, 3);

    REQUIRE(app.parse({"--tag", "a", "b", "--verbose"}));
    REQUIRE(tags->size() == 2);
    CHECK((*tags)[0] == "a");
    CHECK((*tags)[1] == "b");
    CHECK(*verbose);
}

TEST_CASE("nargs_at_least consumes until an option boundary") {
    cli::App app("t");
    auto values = app.add<std::vector<std::string>>("--value").nargs_at_least(1);
    (void)app.add_flag("--stop");

    REQUIRE(app.parse({"--value", "a", "b", "c", "--stop"}));
    REQUIRE(values->size() == 3);
    CHECK((*values)[2] == "c");
}

TEST_CASE("nargs treats negative numbers as values") {
    cli::App app("t");
    auto values = app.add<std::vector<int>>("--num").nargs(2);

    REQUIRE(app.parse({"--num", "-1", "-2"}));
    REQUIRE(values->size() == 2);
    CHECK((*values)[0] == -1);
    CHECK((*values)[1] == -2);
}

TEST_CASE("nargs reports a missing value when the command line is short") {
    cli::App app("t");
    (void)app.add<std::vector<std::string>>("--file").nargs(2);

    auto r = app.parse({"--file", "only-one"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::MissingValue);
    CHECK(contains(r.error().message, "--file"));
}

TEST_CASE("nargs rejects invalid configurations") {
    cli::App app("t");
    auto opt = app.add<std::vector<std::string>>("--file");
    CHECK_THROWS_AS(opt.nargs(0), std::invalid_argument);
    CHECK_THROWS_AS(opt.nargs(3, 2), std::invalid_argument);
    CHECK_THROWS_AS(opt.nargs_at_least(0), std::invalid_argument);
}

TEST_CASE("nargs options render a spread meta placeholder in help") {
    cli::App app("tool");
    (void)app.add<std::vector<std::string>>("--file").nargs(2).meta("FILE");
    CHECK(contains(app.help_text(), "<FILE>..."));
}

// ── option aliases ──────────────────────────────────────────────────────────

TEST_CASE("option aliases register additional spellings") {
    cli::App app("t");
    auto color = app.add<std::string>("--color").alias("-c").alias("--colour");

    REQUIRE(app.parse({"--colour", "always"}));
    CHECK(*color == "always");

    REQUIRE(app.parse({"-c", "never"}));
    CHECK(*color == "never");

    CHECK(app.has_option("--colour"));
    CHECK(app.has_option("-c"));
}

TEST_CASE("flag and count aliases work") {
    cli::App app("t");
    auto verbose = app.add_flag("--verbose").alias("-v");
    auto quiet   = app.add_count("--quiet").alias("-q");

    REQUIRE(app.parse({"-v", "-q", "-q"}));
    CHECK(*verbose);
    CHECK(*quiet == 2u);
}

TEST_CASE("duplicate option aliases are rejected") {
    cli::App app("t");
    (void)app.add_flag("--verbose");
    auto quiet = app.add_flag("--quiet");
    quiet.alias("-v");
    CHECK_THROWS_AS(quiet.alias("--verbose"), std::invalid_argument);
    CHECK_THROWS_AS(quiet.alias("-v"), std::invalid_argument); // duplicate
}

// ── extras ──────────────────────────────────────────────────────────────────

TEST_CASE("allow_extras collects unknown options and surplus positionals") {
    cli::App app("t");
    app.allow_extras();
    auto file = app.add_positional<std::string>("file");

    REQUIRE(app.parse({"in.txt", "extra", "--nope", "-Z", "--", "tail"}));
    CHECK(*file == "in.txt");
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 4);
    CHECK(rest[0] == "extra");
    CHECK(rest[1] == "--nope");
    CHECK(rest[2] == "-Z");
    CHECK(rest[3] == "tail");
}

TEST_CASE("argparse parse_known_args: unknown options do not consume positionals") {
    cli::App app("t");
    app.allow_extras();
    auto foo = app.add_flag("--foo");
    auto bar = app.add_positional<std::string>("bar");

    REQUIRE(app.parse({"--foo", "--badger", "BAR", "spam"}));
    CHECK(*foo);
    CHECK(*bar == "BAR");
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0] == "--badger");
    CHECK(rest[1] == "spam");
}

TEST_CASE("argparse parse_known_args: unknown compound option stays whole") {
    cli::App app("t");
    app.allow_extras();
    auto foo = app.add_flag("--foo");
    auto bar = app.add_positional<std::string>("bar");

    REQUIRE(app.parse({"-jc", "BAR"}));
    CHECK(!*foo);
    CHECK(*bar == "BAR");
    const auto& rest = app.remaining_args();
    REQUIRE(rest.size() == 1);
    CHECK(rest[0] == "-jc");
}

TEST_CASE("argparse parse_known_args: extras do not satisfy require_one_of") {
    cli::App app("t");
    app.allow_extras();
    (void)app.add_flag("--first");
    (void)app.add_flag("--second");
    app.require_one_of({"--first", "--second"});

    auto res = app.parse({"--unknown", "value"});
    REQUIRE(!res);
    CHECK(res.error().kind == cli::ParseError::Kind::MissingRequired);
}

TEST_CASE("extras are cleared by the next parse") {
    cli::App app("t");
    app.allow_extras();
    REQUIRE(app.parse({"--nope"}));
    CHECK(app.remaining_args().size() == 1);

    REQUIRE(app.parse({}));
    CHECK(app.remaining_args().empty());
}

TEST_CASE("without allow_extras unknown options still fail") {
    cli::App app("t");
    auto r = app.parse({"--nope"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

// ── hidden commands and command suggestions ─────────────────────────────────

TEST_CASE("hidden commands run but do not appear in help or completions") {
    cli::App app("tool");
    auto& internal = app.subcommand("internal", "Internal verb").hidden();
    auto  value    = internal.add<std::string>("--value");

    REQUIRE(app.parse({"internal", "--value", "x"}));
    CHECK(app.active_subcommand() == &internal);
    CHECK(*value == "x");

    CHECK(!contains(app.help_text(), "internal"));
    CHECK(!contains(app.help_text(), "Internal verb"));

    std::ostringstream bash;
    app.bash_completion(bash);
    CHECK(!contains(bash.str(), "internal"));

    std::ostringstream fish;
    app.fish_completion(fish);
    CHECK(!contains(fish.str(), "internal"));

    std::ostringstream zsh;
    app.zsh_completion(zsh);
    CHECK(!contains(zsh.str(), "internal"));
}

TEST_CASE("unknown commands suggest close command names") {
    cli::App app("tool");
    (void)app.subcommand("build", "");
    (void)app.subcommand("bench", "");

    auto r = app.parse({"buil"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownCommand);
    CHECK(contains(r.error().message, "did you mean"));
    CHECK(contains(r.error().message, "build"));
}

TEST_CASE("unknown commands suggest aliases too") {
    cli::App app("tool");
    (void)app.subcommand("commit", "").alias("ci");

    auto r = app.parse({"c"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownCommand);
}

// ── config files ────────────────────────────────────────────────────────────

TEST_CASE("config files provide option and positional values") {
    TempFile cfg("basic", R"(
# root options
jobs = 8

[build]
target = release
file = out.bin
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto jobs = app.add<unsigned>("--jobs").default_val(4u);

    auto& build  = app.subcommand("build", "");
    auto  target = build.add<std::string>("--target").default_val("debug");
    auto  file   = build.add_positional<std::string>("file");

    REQUIRE(app.parse({"build"}));
    CHECK(*jobs == 8u);
    CHECK(*target == "release");
    CHECK(*file == "out.bin");
    CHECK(app.has_option("--jobs"));
    CHECK(build.has_option("--target"));
    CHECK(app.is_subcommand("build"));
}

TEST_CASE("config files support inline comments and hashes in values") {
    TempFile cfg("comments", R"(
name = hello world # trailing comment
path = dir#fragment ; another comment
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto name = app.add<std::string>("--name");
    auto path = app.add<std::string>("--path");

    REQUIRE(app.parse({}));
    CHECK(*name == "hello world");
    CHECK(*path == "dir#fragment");
}

TEST_CASE("config bare flags may carry an inline comment") {
    TempFile cfg("bare-flag", R"(
verbose # enable verbose output
cache ; use the cache
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto verbose = app.add_flag("--verbose");
    auto cache   = app.add_flag("--cache");

    REQUIRE(app.parse({}));
    CHECK(*verbose);
    CHECK(*cache);
}

TEST_CASE("CLI11: config section headers may be quoted or use TOML brackets") {
    TempFile quoted("quoted-section", R"(
val = 1
["subcom"] # quoted section
val = 2
)");
    cli::App app("tool");
    app.config_file(quoted.path());
    auto root_val = app.add<unsigned>("--val").default_val(0u);
    auto& sub = app.subcommand("subcom", "");
    auto sub_val = sub.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({"subcom"}));
    CHECK(*root_val == 1u);
    CHECK(*sub_val == 2u);
}

TEST_CASE("CLI11: config section headers ignore trailing comments") {
    TempFile cfg("section-comment", R"(
[default]
val = 1
[subcom] # a comment about the subcommand
val = 2
["other"] ; another comment
val = 3
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto root_val  = app.add<unsigned>("--val").default_val(0u);
    auto& sub      = app.subcommand("subcom", "");
    auto sub_val   = sub.add<unsigned>("--val").default_val(0u);
    auto& other    = app.subcommand("other", "");
    auto other_val = other.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({"subcom"}));
    CHECK(*root_val == 1u);
    CHECK(*sub_val == 2u);

    REQUIRE(app.parse({"other"}));
    CHECK(*other_val == 3u);
}

TEST_CASE("CLI11: config section comments respect quoted section names") {
    TempFile cfg("quoted-comment", R"(
val = 1
[ # not a section
["subcom"] # a 'quoted' comment
val = 2
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto root_val = app.add<unsigned>("--val").default_val(0u);
    auto& sub     = app.subcommand("subcom", "");
    auto sub_val  = sub.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({"subcom"}));
    CHECK(*root_val == 1u);
    CHECK(*sub_val == 2u);
}

TEST_CASE("CLI11: config files written with CRLF line endings parse") {
    TempFile cfg("crlf",
                 "[default]\r\n"
                 "val = 4\r\n"
                 "[subcom]\r\n"
                 "val = 5\r\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto root_val = app.add<unsigned>("--val").default_val(0u);
    auto& sub     = app.subcommand("subcom", "");
    auto sub_val  = sub.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({"subcom"}));
    CHECK(*root_val == 4u);
    CHECK(*sub_val == 5u);
}

TEST_CASE("CLI11: TOML double-bracket sections select subcommands") {
    TempFile cfg("toml-section", "val = 1\n[[subcom]]\nval = 2\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto root_val = app.add<unsigned>("--val").default_val(0u);
    auto& sub = app.subcommand("subcom", "");
    auto sub_val = sub.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({"subcom"}));
    CHECK(*root_val == 1u);
    CHECK(*sub_val == 2u);
}

TEST_CASE("CLI11: the [default] section configures the root command") {
    TempFile cfg("default-section", "[default]\nval = 3\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto val = app.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({}));
    CHECK(*val == 3u);
}

TEST_CASE("config files tolerate a UTF-8 byte-order mark") {
    TempFile cfg("bom", "\xEF\xBB\xBFval = 4\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto val = app.add<unsigned>("--val").default_val(0u);

    REQUIRE(app.parse({}));
    CHECK(*val == 4u);
}

TEST_CASE("config keys may be quoted") {
    TempFile cfg("quoted-key", R"("val" = 5
'section-name' = quoted
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto val = app.add<unsigned>("--val").default_val(0u);
    auto section = app.add<std::string>("--section-name");

    REQUIRE(app.parse({}));
    CHECK(*val == 5u);
    CHECK(*section == "quoted");
}

TEST_CASE("config values sit below CLI and environment but above defaults") {
    TempFile cfg("precedence", "level = config\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto level = app.add<std::string>("--level").default_val("default");

    REQUIRE(app.parse({}));
    CHECK(*level == "config");

    REQUIRE(app.parse({"--level", "cli"}));
    CHECK(*level == "cli");
}

TEST_CASE("environment variables override config values") {
    TempFile cfg("env", "level = config\n");
    ScopedEnv env("CLI_TEST_LEVEL_PRECEDENCE", "environment");

    cli::App app("tool");
    app.config_file(cfg.path());
    auto level = app.add<std::string>("--level")
                     .default_val("default")
                     .env("CLI_TEST_LEVEL_PRECEDENCE");

    REQUIRE(app.parse({}));
    CHECK(*level == "environment");
}

TEST_CASE("config arrays populate vector options and positionals") {
    TempFile cfg("arrays", R"(
tag = [one, two, "three,four"]
[pack]
files = [a.txt, b.txt]
)");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto tags = app.add<std::vector<std::string>>("--tag");

    auto& pack  = app.subcommand("pack", "");
    auto  files = pack.add_positional<std::vector<std::string>>("files");

    REQUIRE(app.parse({"pack"}));
    REQUIRE(tags->size() == 3);
    CHECK((*tags)[0] == "one");
    CHECK((*tags)[1] == "two");
    CHECK((*tags)[2] == "three,four");

    REQUIRE(files->size() == 2);
    CHECK((*files)[1] == "b.txt");
}

TEST_CASE("config can negate a negatable flag") {
    TempFile cfg("negate", "no-color = true\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto color = app.add_flag("--color").default_val(true).negatable();

    REQUIRE(app.parse({}));
    CHECK(!*color);
    CHECK(!color.is_set());
}

TEST_CASE("a config option selects the file for one invocation") {
    TempFile cfg("selected", "jobs = 12\n");
    cli::App app("tool");
    app.config_option();
    auto jobs = app.add<unsigned>("--jobs").default_val(1u);

    REQUIRE(app.parse({"--config", cfg.path()}));
    CHECK(*jobs == 12u);
    CHECK(app["--config"].as<std::string>() == cfg.path());
}

TEST_CASE("a required config file must exist") {
    cli::App app("tool");
    app.config_file("/definitely/not/a/real/cli-config-file.ini", true);
    (void)app.add_flag("--verbose");

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ConfigError);
    CHECK(contains(r.error().message, "cannot open file"));
}

TEST_CASE("a missing optional config file is ignored") {
    cli::App app("tool");
    app.config_file("/definitely/not/a/real/cli-config-file.ini");
    auto jobs = app.add<unsigned>("--jobs").default_val(3u);

    REQUIRE(app.parse({}));
    CHECK(*jobs == 3u);
}

TEST_CASE("clearing config_file stops applying the previous file") {
    TempFile cfg("cleared", "jobs = 8\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    auto jobs = app.add<unsigned>("--jobs").default_val(3u);

    REQUIRE(app.parse({}));
    CHECK(*jobs == 8u);

    app.config_file("");
    REQUIRE(app.parse({}));
    CHECK(*jobs == 3u);
}

TEST_CASE("malformed config files produce a config error") {
    TempFile cfg("malformed", "[unterminated\nkey = value\n");
    cli::App app("tool");
    app.config_file(cfg.path());

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ConfigError);
    CHECK(contains(r.error().message, "unterminated section"));
}

TEST_CASE("config values participate in constraints") {
    TempFile cfg("constraints", "port = 70000\n");
    cli::App app("tool");
    app.config_file(cfg.path());
    (void)app.add<unsigned>("--port").range(1u, 65535u);

    auto r = app.parse({});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::OutOfRange);
}

// ── misc API additions ──────────────────────────────────────────────────────

TEST_CASE("group() is an alias for section()") {
    cli::App app("tool");
    (void)app.add<std::string>("--host").group("Connection");
    const auto help = app.help_text();
    CHECK(contains(help, "Connection:"));
}

TEST_CASE("parse accepts a vector of strings") {
    cli::App app("t");
    auto value = app.add<int>("--value");

    const std::vector<std::string> args{ "--value", "42" };
    REQUIRE(app.parse(args));
    CHECK(*value == 42);
}

// ── require-one groups ──────────────────────────────────────────────────────

TEST_CASE("require_one_of enforces at least one option") {
    cli::App app("tool");
    (void)app.add_flag("--json");
    (void)app.add_flag("--yaml");
    app.require_one_of({"--json", "--yaml"});

    auto missing = app.parse({});
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(contains(missing.error().message, "--json"));
    CHECK(contains(missing.error().message, "--yaml"));

    REQUIRE(app.parse({"--yaml"}));
    REQUIRE(app.parse({"--json"}));
    REQUIRE(app.parse({"--json", "--yaml"})); // at least one, not exactly one
}

TEST_CASE("require_one_of resolves unknown options at parse time") {
    cli::App app("tool");
    (void)app.add_flag("--json");
    app.require_one_of({"--missing"});

    CHECK_THROWS_AS([&] { (void)app.parse({"--json"}); }(), std::invalid_argument);
}

// ── long-option abbreviation ────────────────────────────────────────────────

TEST_CASE("unambiguous long-option prefixes are accepted when enabled") {
    cli::App app("tool");
    app.allow_abbrev();
    auto verbose = app.add_flag("--verbose");
    auto output  = app.add<std::string>("--output");

    REQUIRE(app.parse({"--verb", "--out", "file.txt"}));
    CHECK(*verbose);
    CHECK(*output == "file.txt");
}

TEST_CASE("ambiguous long-option prefixes are rejected") {
    cli::App app("tool");
    app.allow_abbrev();
    (void)app.add_flag("--verbose");
    (void)app.add_flag("--versioned");

    auto r = app.parse({"--ver"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::AmbiguousOption);
    CHECK(contains(r.error().message, "--verbose"));
    CHECK(contains(r.error().message, "--versioned"));
}

TEST_CASE("abbreviation is opt-in") {
    cli::App app("tool");
    (void)app.add_flag("--verbose");

    auto r = app.parse({"--verb"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::UnknownOption);
}

// ── response files ──────────────────────────────────────────────────────────

TEST_CASE("response files expand shell-like tokens") {
    TempFile args("response", R"(
--name "hello world"
--count 3
-v
)");
    cli::App app("tool");
    app.response_files();
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto name    = app.add<std::string>("--name");
    auto count   = app.add<int>("--count");

    REQUIRE(app.parse({"@" + args.path()}));
    CHECK(*verbose);
    CHECK(*name == "hello world");
    CHECK(*count == 3);
}

TEST_CASE("response files support comments and nested files") {
    TempFile inner("inner", "--count 7\n");
    TempFile outer("outer", std::format("# include\n@{}\n--name ok\n", inner.path()));

    cli::App app("tool");
    app.response_files();
    auto name  = app.add<std::string>("--name");
    auto count = app.add<int>("--count");

    REQUIRE(app.parse({"@" + outer.path()}));
    CHECK(*name == "ok");
    CHECK(*count == 7);
}

TEST_CASE("double-at escapes a literal at-token") {
    cli::App app("tool");
    app.response_files();
    auto file = app.add_positional<std::string>("file");

    REQUIRE(app.parse({"@@literal"}));
    CHECK(*file == "@literal");
}

TEST_CASE("response files tolerate a UTF-8 byte-order mark") {
    TempFile args("bom-response", "\xEF\xBB\xBF--name ok\n");
    cli::App app("tool");
    app.response_files();
    auto name = app.add<std::string>("--name");

    REQUIRE(app.parse({"@" + args.path()}));
    CHECK(*name == "ok");
}

TEST_CASE("missing response files are reported") {
    cli::App app("tool");
    app.response_files();
    (void)app.add_flag("--verbose");

    auto r = app.parse({"@/definitely/not/a/response-file.args"});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ResponseFileError);
    CHECK(contains(r.error().message, "cannot open file"));
}

TEST_CASE("unterminated quotes in response files are reported") {
    TempFile args("bad-response", "--name \"unterminated\n");
    cli::App app("tool");
    app.response_files();
    (void)app.add<std::string>("--name");

    auto r = app.parse({"@" + args.path()});
    REQUIRE(!r);
    CHECK(r.error().kind == cli::ParseError::Kind::ResponseFileError);
    CHECK(contains(r.error().message, "unterminated"));
}

TEST_SUITE_END();
