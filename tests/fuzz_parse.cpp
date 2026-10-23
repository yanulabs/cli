// libFuzzer harness for the parser's public entry point.
//
// Build with -DCLI_BUILD_FUZZERS=ON on Clang, then run:
//   ./build/tests/fuzz_parse -max_len=4096 -runs=100000
#include <cli.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // Keep individual inputs small enough that failures reproduce quickly.
    if (size == 0 || size > 65536) return 0;

    std::vector<std::string_view> args;
    std::size_t start = 0;
    while (start <= size && args.size() < 64) {
        std::size_t end = start;
        while (end < size && data[end] != 0) ++end;
        args.emplace_back(reinterpret_cast<const char*>(data + start), end - start);
        if (end == size) break;
        start = end + 1;
    }

    cli::App app("fuzz", "Fuzz target", "1.0.0");
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

    const auto first = app.parse(std::span<const std::string_view>(args));
    if (!first && !first.error().is_exit_request()) {
        if (first.error().message.empty()) std::abort();
        if (first.error().context == nullptr) std::abort();
    }

    // parse() must fully reset prior state, including active subcommands.
    const auto second = app.parse(std::span<const std::string_view>(args));
    if (first.has_value() != second.has_value()) std::abort();
    if (!first && first.error().kind != second.error().kind) std::abort();

    return 0;
}
