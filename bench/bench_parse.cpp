// ─────────────────────────────────────────────────────────────────────────────
// bench/bench_parse.cpp — throughput microbenchmarks for cli.hpp
//
// Build:  cmake -S . -B build -DCLI_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
// Run:    ./build/bench/bench_parse [iterations]
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

volatile std::uint64_t sink = 0;

struct Result {
    std::string name;
    double      ns_per_parse = 0;
    double      tokens       = 0;
};

template<typename Fn>
[[nodiscard]] Result measure(std::string name, size_t iterations, size_t token_count,
                             Fn&& fn) {
    // Warm up caches, branch predictors, and the option tables.
    for (size_t i = 0; i < std::min<size_t>(iterations, 2000); ++i)
        sink += fn();

    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i)
        sink += fn();
    const auto stop = std::chrono::steady_clock::now();

    const auto ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return { std::move(name), ns / static_cast<double>(iterations),
             static_cast<double>(token_count) };
}

[[nodiscard]] Result bench_flags(size_t iterations) {
    cli::App app("bench");
    auto v = app.add_flag("--verbose").short_name("-v");
    auto q = app.add_flag("--quiet").short_name("-q");
    auto f = app.add_flag("--force").short_name("-f");
    auto n = app.add_flag("--dry-run").short_name("-n");
    auto c = app.add_flag("--color").short_name("-c");
    auto a = app.add_flag("--all").short_name("-a");
    auto p = app.add_flag("--porcelain").short_name("-p");
    auto s = app.add_flag("--short").short_name("-s");

    const std::vector<std::string_view> args{
        "-v", "-q", "-f", "-n", "-c", "-a", "-p", "-s"
    };
    return measure("8 short flags", iterations, args.size(), [&] {
        auto r = app.parse(args);
        return static_cast<std::uint64_t>(
            r && *v && !*q && *f && !*n && *c && *a && *p && *s);
    });
}

[[nodiscard]] Result bench_options(size_t iterations) {
    cli::App app("bench");
    auto jobs   = app.add<unsigned>("--jobs").short_name("-j").default_val(4u);
    auto output = app.add<std::string>("--output").short_name("-o");
    auto level  = app.add<std::string>("--level").choices({"debug", "info", "warn"});
    auto ratio  = app.add<double>("--ratio").range(0.0, 1.0);
    auto count  = app.add<int>("--count").range(0, 1'000'000);
    auto name   = app.add<std::string>("--name");

    const std::vector<std::string_view> args{
        "-j", "16", "-o", "/tmp/build/out.bin", "--level", "info",
        "--ratio", "0.75", "--count", "42", "--name", "release"
    };
    return measure("6 typed options", iterations, args.size(), [&] {
        auto r = app.parse(args);
        return static_cast<std::uint64_t>(r && *jobs == 16 && !output->empty()
                                          && *ratio == 0.75 && *count == 42);
    });
}

[[nodiscard]] Result bench_mixed(size_t iterations) {
    cli::App app("bench");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto jobs    = app.add<unsigned>("--jobs").short_name("-j").default_val(4u);
    auto output  = app.add<std::string>("--output").short_name("-o");
    auto tags    = app.add<std::vector<std::string>>("--tag");
    auto input   = app.add_positional<std::string>("input");

    const std::vector<std::string_view> args{
        "-v", "-j8", "--tag", "alpha", "--tag=beta", "-o", "out.bin", "src/main.cpp"
    };
    return measure("mixed (5 option kinds)", iterations, args.size(), [&] {
        auto r = app.parse(args);
        return static_cast<std::uint64_t>(
            r && *verbose && *jobs == 8 && tags->size() == 2
              && *input == "src/main.cpp" && !output->empty());
    });
}

[[nodiscard]] Result bench_subcommand(size_t iterations) {
    cli::App app("bench", "desc", "1.0.0");
    (void)app.add_flag("--verbose").short_name("-v");
    auto& build  = app.subcommand("build", "Compile");
    auto  target = build.add<std::string>("--target").default_val("debug");
    auto  jobs   = build.add<unsigned>("--jobs").short_name("-j").default_val(4u);
    auto& remote = app.subcommand("remote", "Manage remotes");
    auto& add    = remote.subcommand("add", "Add one");
    auto  url    = add.add<std::string>("--url");

    const std::vector<std::string_view> args{
        "-v", "build", "--target", "release", "-j", "8"
    };
    const std::vector<std::string_view> nested{
        "remote", "add", "--url", "https://example.com"
    };
    return measure("subcommand + options", iterations, args.size() + nested.size(), [&] {
        auto r = app.parse(args);
        auto n = app.parse(nested);
        return static_cast<std::uint64_t>(r && n && *target == "release"
                                          && *jobs == 8 && *url == "https://example.com");
    });
}

} // namespace

int main(int argc, char** argv) {
    size_t iterations = 200'000;
    if (argc > 1) {
        const long long parsed = std::atoll(argv[1]);
        if (parsed > 0) iterations = static_cast<size_t>(parsed);
    }

    std::vector<Result> results;
    results.push_back(bench_flags(iterations));
    results.push_back(bench_options(iterations));
    results.push_back(bench_mixed(iterations));
    results.push_back(bench_subcommand(iterations));

    std::cout << std::format("cli.hpp parse benchmark — {} iterations\n\n", iterations);
    std::cout << std::format("{:<26} {:>12} {:>14} {:>14}\n",
                             "case", "ns/parse", "parses/sec", "tokens/parse");
    std::cout << std::string(70, '-') << '\n';
    for (const auto& r : results) {
        const double per_sec = 1e9 / r.ns_per_parse;
        std::cout << std::format("{:<26} {:>12.1f} {:>14.0f} {:>14.0f}\n",
                                 r.name, r.ns_per_parse, per_sec, r.tokens);
    }
    const auto checksum = static_cast<std::uint64_t>(sink);
    std::cout << std::format("\n(checksum {})\n", checksum);
    return 0;
}
