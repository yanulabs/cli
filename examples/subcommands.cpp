// ─────────────────────────────────────────────────────────────────────────────
// examples/subcommands.cpp — cli.hpp v2: callback-based subcommand dispatch
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>
#include <thread>

int main(int argc, char** argv) {
    cli::App app{"cargo", "A minimal Cargo-like build tool", "0.1.0"};
    app.author("Yanu Labs")
       .example("cargo build --target release -j4")
       .example("cargo run --bin myapp")
       .example("cargo test --filter my_test");

    // Global flag captured by reference into the on_execute closures below
    auto verbose = app.add_flag("--verbose").short_name("-v").help("Verbose output");

    // ── build ──────────────────────────────────────────────────────────────
    // subcommand() returns App&: store it, chain v2 builders, register callback.
    auto& build = app.subcommand("build", "Compile the project");
    auto  target = build.add<std::string>("--target").short_name("-t")
                       .help("Build profile").choices({"debug","release"}).default_val("debug");
    auto  jobs   = build.add<unsigned>("--jobs").short_name("-j")
                       .help("Parallel jobs")
                       .default_val(std::thread::hardware_concurrency()).range(1u, 512u);
    auto  bquiet = build.add_flag("--quiet").short_name("-q").help("Suppress output");

    build.on_execute([&verbose, &target, &jobs, &bquiet](cli::App&) {
        if (*verbose) std::println("[verbose] running 'build'");
        std::println("Building '{}' with {} job(s){}",
                     *target, *jobs, *bquiet ? " (quiet)" : "");
    });

    // ── run ────────────────────────────────────────────────────────────────
    auto& run   = app.subcommand("run", "Build and run a binary");
    auto  bin   = run.add<std::string>("--bin").help("Binary name to run").required();
    auto  rargs = run.add_positional<std::vector<std::string>>("args")
                     .help("Arguments forwarded to the binary");

    run.on_execute([&verbose, &bin, &rargs](cli::App&) {
        if (*verbose) std::println("[verbose] running 'run'");
        std::print("Running '{}'", *bin);
        if (rargs && !rargs->empty()) {
            std::print(" with args:");
            for (auto& a : *rargs) std::print(" {}", a);
        }
        std::println();
    });

    // ── test ───────────────────────────────────────────────────────────────
    auto& test   = app.subcommand("test", "Run the test suite");
    auto  filter = test.add<std::string>("--filter").short_name("-f")
                       .help("Test name filter").default_val("");
    auto  nofail = test.add_flag("--no-fail-fast").help("Do not stop on first failure");

    test.on_execute([&filter, &nofail](cli::App&) {
        std::println("Running tests{}{}",
            filter->empty() ? "" : std::format(" (filter: {})", *filter),
            *nofail ? " [no-fail-fast]" : "");
    });

    // parse_or_exit selects and fires the right on_execute — no if/else chains!
    app.parse_or_exit(argc, argv);

    if (!app.active_subcommand())
        app.print_help();

    return 0;
}
