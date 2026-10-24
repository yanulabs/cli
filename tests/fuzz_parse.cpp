// libFuzzer harness for the parser's public entry point.
//
// Build with -DCLI_BUILD_FUZZERS=ON on Clang, then run with the checked-in
// seed corpus:
//   ./build/tests/fuzz_parse tests/fuzz_corpus -max_len=4096 -runs=100000
#include <cli.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Small, valid config/response fixtures created once per fuzzer process.
/// Keeping the paths controlled means an adversarial `@...` token cannot make
/// the fuzzer read arbitrary files (for example a never-ending device).
struct FuzzFiles {
    std::filesystem::path config;
    std::filesystem::path response;
    std::filesystem::path nested;

    FuzzFiles() {
        static unsigned counter = 0;
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        const auto base = std::filesystem::temp_directory_path();
        const auto tag  = std::to_string(stamp) + "_" + std::to_string(counter++);
        config   = base / ("cli_fuzz_" + tag + ".ini");
        response = base / ("cli_fuzz_" + tag + ".args");
        nested   = base / ("cli_fuzz_" + tag + "_nested.args");

        {
            std::ofstream out(config, std::ios::binary | std::ios::trunc);
            out << "[default]\n"
                   "verbose = true\n"
                   "jobs = 5\n"
                   "name = config-root\n"
                   "tag = [one, two]\n"
                   "[build]\n"
                   "target = release\n";
        }
        {
            std::ofstream out(nested, std::ios::binary | std::ios::trunc);
            out << "--ratio 0.5\n";
        }
        {
            std::ofstream out(response, std::ios::binary | std::ios::trunc);
            out << "--name \"response name\"\n"
                   "--jobs 7\n"
                   "--tag one,two\n"
                   "-d\n";
            out << '@' << std::quoted(nested.string()) << '\n';
        }
    }

    ~FuzzFiles() {
        std::error_code ec;
        std::filesystem::remove(config, ec);
        std::filesystem::remove(response, ec);
        std::filesystem::remove(nested, ec);
    }

    FuzzFiles(const FuzzFiles&)            = delete;
    FuzzFiles& operator=(const FuzzFiles&) = delete;
};

[[nodiscard]] const FuzzFiles& fuzz_files() {
    static const FuzzFiles files;
    return files;
}

[[nodiscard]] bool deterministic_failure(const cli::ParseResult& first,
                                         const cli::ParseResult& second) {
    if (first.has_value() != second.has_value()) return false;
    if (!first && first.error().kind != second.error().kind) return false;
    return true;
}

} // namespace

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
    app.allow_abbrev();
    app.response_files();
    app.config_file(fuzz_files().config.string());

    (void)app.add_flag("--verbose").short_name("-v").negatable().global();
    (void)app.add_flag("--color").default_val(true).negatable();
    (void)app.add<int>("--jobs").short_name("-j").range(-100, 100)
        .default_val(1).global();
    (void)app.add<double>("--ratio").range(0.0, 1.0);
    (void)app.add<std::string>("--name").short_name("-n").global();
    (void)app.add<std::string>("--level").choices({"debug", "info", "warn"});
    (void)app.add<std::vector<std::string>>("--tag").short_name("-t").delimiter(',');
    (void)app.add_count("--debug").short_name("-d");
    (void)app.add_positional<std::vector<int>>("pair").required(false).nargs(2);
    (void)app.add_positional<std::string>("input").required(false).default_val("in");
    (void)app.add_positional<std::vector<std::string>>("rest").required(false);
    app.mutex_group({"--verbose", "--color"});

    auto& build = app.subcommand("build", "Build the project");
    (void)build.add<std::string>("--target").choices({"debug", "release"})
        .default_val("debug");
    auto& run = app.subcommand("run", "Run the project");
    run.alias("r");
    (void)run.add<int>("--count");

    // Map one marker spelling to the controlled response file.  Other
    // `@...` tokens remain ordinary arguments so the fuzzer cannot read
    // arbitrary paths while still exercising response expansion.
    std::vector<std::string> owned;
    owned.reserve(args.size());
    std::vector<std::string_view> parse_args;
    parse_args.reserve(args.size());
    for (const auto arg : args) {
        if (arg == "@fuzz") {
            owned.push_back("@" + fuzz_files().response.string());
            parse_args.emplace_back(owned.back());
        } else {
            parse_args.emplace_back(arg);
        }
    }

    const auto first = app.parse(std::span<const std::string_view>(parse_args));
    if (!first && !first.error().is_exit_request()) {
        if (first.error().message.empty()) std::abort();
        if (first.error().context == nullptr) std::abort();
    }

    // parse() must fully reset prior state, including active subcommands,
    // inherited globals, config data and response-file expansion.
    const auto second = app.parse(std::span<const std::string_view>(parse_args));
    if (!deterministic_failure(first, second)) std::abort();

    // Parsing a child directly must reset the whole tree and finalize every
    // ancestor's inherited globals just like parsing through the root.
    const auto direct = build.parse(std::span<const std::string_view>(parse_args));
    if (!direct && !direct.error().is_exit_request()) {
        if (direct.error().message.empty()) std::abort();
        if (direct.error().context == nullptr) std::abort();
    }

    return 0;
}
