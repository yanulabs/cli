// ─────────────────────────────────────────────────────────────────────────────
// examples/completions.cpp — shell completion generator demo
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>

int main(int argc, char** argv) {
    cli::App app("mytool", "Demo tool");
    app.version("1.0.0");

    app.flag("-v", "--verbose", "Verbose output");
    app.option<unsigned>("-j", "--jobs", "Parallel jobs").default_val(4u);
    app.option<std::string>("-o", "--output", "Output path").required().meta("PATH");
    app.option<std::string>("-l", "--level", "Log level")
       .choices({"debug", "info", "warn", "error"});

    auto& sub = app.subcommand("build", "Compile sources");
    sub.option<std::string>("-t", "--target", "Profile")
       .choices({"debug", "release"});

    // ── shell selector ───────────────────────────────────────────────────────
    cli::App ctl("completion-gen", "Generate shell completions");
    auto shell = ctl.option<std::string>("--shell", "Shell type (bash|fish)")
                    .choices({"bash", "fish"}).default_val(std::string("bash"));

    auto r = ctl.parse(argc, argv);
    if (!r) { ctl.print_error(r.error()); return 1; }

    if (*shell == "fish")
        app.fish_completion(std::cout);
    else
        app.bash_completion(std::cout);

    return 0;
}
