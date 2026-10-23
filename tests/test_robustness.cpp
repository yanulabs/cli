#include "test_support.hpp"

#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

void configure_adversarial_app(cli::App& app) {
    (void)app.add_flag("--verbose").short_name("-v").negatable();
    (void)app.add_flag("--color").default_val(true).negatable();
    (void)app.add<int>("--jobs").short_name("-j").range(-100, 100).default_val(1);
    (void)app.add<double>("--ratio").range(0.0, 1.0);
    (void)app.add<std::string>("--name").short_name("-n");
    (void)app.add<std::string>("--level").choices({"debug", "info", "warn"});
    (void)app.add<std::vector<std::string>>("--tag").short_name("-t").delimiter(',');
    (void)app.add_count("--debug").short_name("-d");
    (void)app.add_positional<std::string>("input").required(false).default_val("in");
    (void)app.add_positional<std::vector<std::string>>("rest").required(false);
    app.mutex_group({"--verbose", "--color"});

    auto& build = app.subcommand("build", "Build the project");
    (void)build.add<std::string>("--target").choices({"debug", "release"}).default_val("debug");
    auto& run = app.subcommand("run", "Run the project");
    run.alias("r");
    (void)run.add<int>("--count");
}

std::string random_token(std::mt19937& rng) {
    static constexpr std::string_view alphabet =
        "-+=0123456789abcXYZ_.,:/[]'\"\\ \t";
    std::uniform_int_distribution<std::size_t> length_dist(0, 24);
    std::uniform_int_distribution<std::size_t> char_dist(0, alphabet.size() - 1);
    std::string token;
    token.reserve(length_dist(rng));
    const auto length = length_dist(rng);
    for (std::size_t i = 0; i < length; ++i)
        token += alphabet[char_dist(rng)];
    return token;
}

} // namespace

TEST_SUITE_BEGIN("robustness");

TEST_CASE("random token sequences are deterministic and produce valid errors") {
    static constexpr std::string_view fixed[] = {
        "--verbose", "-v", "--no-verbose", "--color", "--no-color",
        "--jobs", "-j", "--jobs=7", "-j-5", "-j=8", "42", "-5", "-0",
        "--jobs", "0x10", "--jobs", "1e3", "--ratio", "0.5", "--ratio", "nan",
        "--name", "value", "--name=", "--level", "debug", "--level", "bad",
        "--tag", "a,b", "--tag", "a,,b", "--debug", "-ddd", "-d=1",
        "build", "--target", "release", "--target", "bad", "run", "r",
        "--count", "3", "--", "-", "--help", "-h", "--version", "-V",
        "--unknown", "--unknown=value", "", "=", "-", "--",
    };

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<std::size_t> count_dist(0, 8);
    std::uniform_int_distribution<std::size_t> fixed_dist(0, std::size(fixed) - 1);

    cli::App app("fuzz", "Adversarial parser test", "1.0.0");
    configure_adversarial_app(app);

    for (std::size_t iteration = 0; iteration < 4000; ++iteration) {
        std::vector<std::string> storage;
        const auto count = count_dist(rng);
        storage.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if ((rng() % 3) == 0)
                storage.push_back(random_token(rng));
            else
                storage.emplace_back(fixed[fixed_dist(rng)]);
        }

        std::vector<std::string_view> args;
        args.reserve(storage.size());
        for (const auto& token : storage) args.emplace_back(token);

        const auto first = app.parse(std::span<const std::string_view>(args));
        const auto* first_sub = app.active_subcommand();
        if (!first && !first.error().is_exit_request()) {
            CHECK(!first.error().message.empty());
            CHECK(first.error().context != nullptr);
        }

        const auto second = app.parse(std::span<const std::string_view>(args));
        CHECK_MESSAGE(first.has_value() == second.has_value(), "iteration " << iteration);
        if (!first && !second) {
            CHECK_MESSAGE(first.error().kind == second.error().kind,
                          "iteration " << iteration);
            CHECK_MESSAGE(first.error().message == second.error().message,
                          "iteration " << iteration);
        }
        CHECK_MESSAGE(first_sub == app.active_subcommand(), "iteration " << iteration);
    }

    CHECK(!app.help_text().empty());
}

TEST_CASE("a very long unknown option is rejected without pathological work") {
    cli::App app("t");
    (void)app.add_flag("--verbose");

    const std::string token = "--" + std::string(200000, 'x');
    const auto result = app.parse({token});
    REQUIRE(!result);
    CHECK(result.error().kind == cli::ParseError::Kind::UnknownOption);
    CHECK(contains(result.error().message, "unknown option"));
}

TEST_CASE("null argv is treated as an empty command line") {
    cli::App app("t");
    (void)app.add<std::string>("--name");

    CHECK(app.parse(1, nullptr));
    CHECK(app.parse(0, nullptr));
    CHECK(app.parse(-7, nullptr));

    cli::App required("required");
    (void)required.add<std::string>("--name").required();
    const auto missing = required.parse(3, nullptr);
    REQUIRE(!missing);
    CHECK(missing.error().kind == cli::ParseError::Kind::MissingRequired);
    CHECK(missing.error().context == &required);

    cli::App positional("positional");
    auto value = positional.add_positional<std::string>("value");
    char* argv[] = {const_cast<char*>("tool"), nullptr};
    REQUIRE(positional.parse(2, argv));
    CHECK(*value == "");
}

TEST_CASE("deeply nested subcommands parse and render help") {
    cli::App app("deep");
    cli::App* node = &app;
    std::vector<std::string> names;
    std::vector<std::string_view> args;
    names.reserve(48);
    args.reserve(48);

    for (int i = 0; i < 48; ++i) {
        names.push_back("level" + std::to_string(i));
        node = &node->subcommand(names.back(), "Nested command");
        args.emplace_back(names.back());
    }

    REQUIRE(app.parse(std::span<const std::string_view>(args)));
    CHECK(app.active_subcommand() != nullptr);
    CHECK(contains(node->help_text(), "level47"));
}

TEST_CASE("a large flat option set remains parseable") {
    cli::App app("wide");
    std::vector<std::string> names;
    names.reserve(256);
    for (int i = 0; i < 256; ++i) {
        names.push_back("--flag" + std::to_string(i));
        (void)app.add_flag(names.back());
    }

    std::vector<std::string_view> args;
    args.reserve(128);
    for (int i = 0; i < 256; i += 2) args.emplace_back(names[static_cast<std::size_t>(i)]);

    REQUIRE(app.parse(std::span<const std::string_view>(args)));
    CHECK(app.has_flag(names.front()));
    CHECK(!app.has_flag(names[1]));
    CHECK(!app.help_text().empty());
}

TEST_SUITE_END();
