// ─────────────────────────────────────────────────────────────────────────────
// examples/completions.cpp — shell completion generator
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>

int main(int argc, char** argv) {
    cli::App selector{"completions", "Generate shell completion scripts"};
    auto shell = selector.add<std::string>("--shell")
                         .short_name("-s")
                         .help("Target shell")
                         .required()
                         .choices({"bash", "fish", "zsh"});
    selector.parse_or_exit(argc, argv);

    // Build the "real" app whose completions we generate
    cli::App app{"mytool", "A demo tool", "1.0.0"};
    app.add_flag("--verbose").short_name("-v").help("Verbose output");
    app.add<std::string>("--output").short_name("-o").help("Output path").meta("PATH");
    app.add<std::string>("--level").short_name("-l")
       .help("Log level").choices({"debug","info","warn","error"});
    app.subcommand("build", "Compile sources").alias("b");
    app.subcommand("test",  "Run tests").alias("t");

    if (*shell == "bash")      app.bash_completion();
    else if (*shell == "fish") app.fish_completion();
    else                       app.zsh_completion();

    return 0;
}
