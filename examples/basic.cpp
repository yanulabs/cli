// ─────────────────────────────────────────────────────────────────────────────
// examples/basic.cpp — cli.hpp basic usage demo
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cli::App app("mytool", "A demo tool built with cli.hpp");
    app.version("1.0.0")
       .author("Yanujz Labs")
       .example("mytool -v -j8 --output /tmp/out")
       .example("mytool --output ./build --tag foo --tag bar");

    auto verbose = app.flag("-v", "--verbose", "Enable verbose output");
    auto jobs    = app.option<unsigned>("-j", "--jobs", "Number of parallel jobs")
                      .default_val(4u).range(1u, 256u);
    auto output  = app.option<fs::path>("-o", "--output", "Output directory")
                      .required().meta("DIR");
    auto tags    = app.option<std::vector<std::string>>("--tag", "Tags to apply");
    auto level   = app.option<std::string>("-l", "--level", "Log level")
                      .choices({"debug", "info", "warn", "error"})
                      .default_val(std::string("info"))
                      .env("LOG_LEVEL");

    auto result = app.parse(argc, argv);
    if (!result) {
        app.print_error(result.error());
        return 1;
    }

    std::println("verbose : {}", *verbose);
    std::println("jobs    : {}", *jobs);
    std::println("output  : {}", output->string());
    std::println("level   : {}", *level);

    if (tags) {
        std::print("tags    : ");
        for (auto& t : *tags) std::print("{} ", t);
        std::println();
    }

    return 0;
}
