// ─────────────────────────────────────────────────────────────────────────────
// examples/config.cpp — config files, response files, nargs and extras
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>
#include <print>

int main(int argc, char** argv) {
    cli::App app{"configdemo", "Config/response-file demo", "1.0.0"};

    // Defaults come from configdemo.ini when present; --config overrides it.
    app.config_file("configdemo.ini");
    app.config_option();
    // `@file` expands shell-like arguments from a file.
    app.response_files();
    // Unknown options are collected instead of failing.
    app.allow_extras();

    auto files = app.add<std::vector<std::string>>("--file")
                     .nargs_at_least(1)
                     .help("Input files (consume until the next option)");
    auto jobs  = app.add<unsigned>("--jobs")
                     .short_name("-j")
                     .default_val(4u)
                     .range(1u, 256u);
    auto out   = app.add<std::string>("--output")
                     .short_name("-o")
                     .default_val("build/out.bin");

    app.parse_or_exit(argc, argv);

    std::println("jobs   : {}", *jobs);
    std::println("output : {}", *out);
    std::print("files  :");
    if (files)
        for (const auto& f : *files) std::print(" {}", f);
    std::println();
    if (!app.remaining_args().empty()) {
        std::print("extras :");
        for (const auto& a : app.remaining_args()) std::print(" {}", a);
        std::println();
    }
}
