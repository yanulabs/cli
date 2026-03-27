// ─────────────────────────────────────────────────────────────────────────────
// examples/subcommands.cpp — cli.hpp subcommand demo
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cli::App app("cargo", "A minimal Cargo-like build tool");
    app.version("0.1.0")
       .author("Yanujz Labs")
       .example("cargo build --target release -j4")
       .example("cargo run --bin myapp -- arg1 arg2")
       .example("cargo test --filter my_test");

    auto verbose = app.flag("-v", "--verbose", "Verbose output");

    // ── build subcommand ─────────────────────────────────────────────────────
    auto& build  = app.subcommand("build", "Compile the project");
    auto  target = build.option<std::string>("-t", "--target", "Build profile")
                       .choices({"debug", "release"})
                       .default_val(std::string("debug"));
    auto  jobs   = build.option<unsigned>("-j", "--jobs", "Parallel jobs")
                       .default_val(std::thread::hardware_concurrency())
                       .range(1u, 512u);
    auto  bquiet = build.flag("-q", "--quiet", "Suppress build output");

    // ── run subcommand ───────────────────────────────────────────────────────
    auto& run     = app.subcommand("run", "Build and run a binary");
    auto  bin     = run.option<std::string>("--bin", "Binary name to run").required();
    auto  runargs = run.positional<std::vector<std::string>>("args",
                                   "Arguments forwarded to the binary");

    // ── test subcommand ──────────────────────────────────────────────────────
    auto& test    = app.subcommand("test", "Run the test suite");
    auto  filter  = test.option<std::string>("-f", "--filter", "Test name filter")
                       .default_val(std::string{});
    auto  nofail  = test.flag("--no-fail-fast", "Do not stop on first failure");

    auto result = app.parse(argc, argv);
    if (!result) {
        app.print_error(result.error());
        return 1;
    }

    if (!app.active_subcommand()) {
        app.print_help();
        return 0;
    }

    if (*verbose) std::println("[verbose] selected: {}", app.active_subcommand()->name());

    if (app.is_subcommand("build")) {
        std::println("Building in '{}' mode with {} jobs{}",
            *target, *jobs, *bquiet ? " (quiet)" : "");

    } else if (app.is_subcommand("run")) {
        std::print("Running '{}'", *bin);
        if (runargs && !runargs->empty()) {
            std::print(" with args:");
            for (auto& a : *runargs) std::print(" {}", a);
        }
        std::println();

    } else if (app.is_subcommand("test")) {
        std::println("Running tests{}{}",
            filter->empty() ? "" : std::format(" (filter: {})", *filter),
            *nofail ? " [no-fail-fast]" : "");
    }

    return 0;
}
