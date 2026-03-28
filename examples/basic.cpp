// ─────────────────────────────────────────────────────────────────────────────
// examples/basic.cpp — cli.hpp v2 fluent API demo
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // 3-arg constructor: name, description, version — no .version() chaining needed
    cli::App app{"mytool", "A demo tool built with cli.hpp", "1.0.0"};
    app.author("Yanujz Labs")
       .example("mytool -v -j8 --output /tmp/out")
       .example("mytool --output ./build --tag foo --tag bar");

    // v2 fluent API: add<T>(long).short_name().help().default_val() ...
    // Note: default_val("info") works — no std::string("info") cast required.
    auto verbose = app.add_flag("--verbose")
                      .short_name("-v")
                      .help("Enable verbose output");

    auto jobs    = app.add<unsigned>("--jobs")
                      .short_name("-j")
                      .help("Number of parallel jobs")
                      .default_val(4u)
                      .range(1u, 256u);

    auto output  = app.add<fs::path>("--output")
                      .short_name("-o")
                      .help("Output directory")
                      .required()
                      .meta("DIR");

    auto tags    = app.add<std::vector<std::string>>("--tag")
                      .help("Tags to apply (repeatable)");

    auto level   = app.add<std::string>("--level")
                      .short_name("-l")
                      .help("Log level")
                      .default_val("info")          // ← implicit const char* → string
                      .choices({"debug", "info", "warn", "error"})
                      .env("LOG_LEVEL");

    // parse_or_exit: prints error and exits(1) on failure, no boilerplate needed
    app.parse_or_exit(argc, argv);

    std::println("verbose : {}", *verbose);
    std::println("jobs    : {}", *jobs);
    std::println("output  : {}", output->string());
    std::println("level   : {}", *level);

    // Dynamic lookup — no handle needed
    if (app.has_flag("--verbose"))
        std::println("[verbose mode active]");

    if (tags) {
        std::print("tags    : ");
        for (auto& t : *tags) std::print("{} ", t);
        std::println();
    }

    // app["--jobs"].as<unsigned>() — type-safe dynamic access
    auto j = app["--jobs"].as<unsigned>();
    std::println("jobs (via proxy): {}", j);

    return 0;
}
