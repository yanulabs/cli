// ─────────────────────────────────────────────────────────────────────────────
// bench/bench_compare.cpp — head-to-head parse benchmarks
//
// Compares the same command lines and equivalent option definitions across
// cli.hpp, CLI11, and cxxopts. Applications are constructed once; only parse()
// is measured. Validation constraints are kept out of the shared scenarios so
// that the comparison measures parser overhead rather than differing feature
// sets. CLI11 additionally supports nested subcommands, so that case is
// reported for cli.hpp and CLI11 only.
//
// Build:
//   cmake -S . -B build -DCLI_BUILD_BENCHMARKS=ON \
//         -DCLI_BUILD_COMPARATIVE_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
// Run:
//   ./build/bench/bench_compare [iterations]
// Every timed iteration must parse successfully; value prechecks and any timed
// failure make the benchmark exit nonzero.
// ─────────────────────────────────────────────────────────────────────────────
#include <cli.hpp>

#ifdef CLI_BENCH_SINGLE_HEADERS
#  include <CLI11.hpp>
#else
#  include <CLI/CLI.hpp>
#endif
#include <cxxopts.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

volatile std::uint64_t sink = 0;

struct Result {
    double ns_per_parse = 0;
    bool   correct      = false;
};

template<typename Fn>
[[nodiscard]] Result measure(size_t iterations, Fn&& fn) {
    for (size_t i = 0; i < std::min<size_t>(iterations, 2'000); ++i)
        sink += fn();

    const auto start = std::chrono::steady_clock::now();
    bool correct = true;
    for (size_t i = 0; i < iterations; ++i) {
        const auto value = fn();
        if (value == 0) correct = false;
        sink += value;
    }
    const auto stop = std::chrono::steady_clock::now();

    const auto ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return { ns / static_cast<double>(iterations), correct };
}

// ── cli.hpp scenarios ────────────────────────────────────────────────────────

Result cli_flags(size_t iterations, const std::vector<std::string_view>& args) {
    cli::App app("bench");
    auto v = app.add_flag("--verbose").short_name("-v");
    auto q = app.add_flag("--quiet").short_name("-q");
    auto f = app.add_flag("--force").short_name("-f");
    auto n = app.add_flag("--dry-run").short_name("-n");
    auto c = app.add_flag("--color").short_name("-c");
    auto a = app.add_flag("--all").short_name("-a");
    auto p = app.add_flag("--porcelain").short_name("-p");
    auto s = app.add_flag("--short").short_name("-s");

    const bool valid = [&] {
        const auto r = app.parse(args);
        return r && *v && *q && *f && *n && *c && *a && *p && *s;
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(app.parse(args).has_value());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli_options(size_t iterations, const std::vector<std::string_view>& args) {
    cli::App app("bench");
    auto jobs   = app.add<unsigned>("--jobs").short_name("-j");
    auto output = app.add<std::string>("--output").short_name("-o");
    auto level  = app.add<std::string>("--level");
    auto ratio  = app.add<double>("--ratio");
    auto count  = app.add<int>("--count");
    auto name   = app.add<std::string>("--name");

    const bool valid = [&] {
        const auto r = app.parse(args);
        return r && *jobs == 16 && *output == "out.bin" && *level == "info"
               && *ratio == 0.75 && *count == 42 && *name == "release";
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(app.parse(args).has_value());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli_mixed(size_t iterations, const std::vector<std::string_view>& args) {
    cli::App app("bench");
    auto verbose = app.add_flag("--verbose").short_name("-v");
    auto jobs    = app.add<unsigned>("--jobs").short_name("-j");
    auto output  = app.add<std::string>("--output").short_name("-o");
    auto input   = app.add_positional<std::string>("input");

    const bool valid = [&] {
        const auto r = app.parse(args);
        return r && *verbose && *jobs == 8 && *output == "out.bin"
               && *input == "src/main.cpp";
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(app.parse(args).has_value());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli_subcommand(size_t iterations, const std::vector<std::string_view>& args,
                      const std::vector<std::string_view>& nested, bool nested_case) {
    cli::App app("bench", "desc", "1.0.0");
    (void)app.add_flag("--verbose").short_name("-v");
    auto& build  = app.subcommand("build", "Compile");
    auto  target = build.add<std::string>("--target");
    auto  jobs   = build.add<unsigned>("--jobs").short_name("-j");
    auto& remote = app.subcommand("remote", "Manage remotes");
    auto& add    = remote.subcommand("add", "Add one");
    auto  url    = add.add<std::string>("--url");

    auto parse = [&]() -> bool {
        const auto& input = nested_case ? nested : args;
        const auto r = app.parse(input);
        if (!r) return false;
        return nested_case ? *url == "https://example.com"
                           : *target == "release" && *jobs == 8;
    };
    const bool valid = parse();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

// ── CLI11 scenarios ──────────────────────────────────────────────────────────
//
// CLI11's parse(vector<string>&) documents that it expects a reversed vector
// and consumes it, so each timed iteration refills a preallocated scratch
// vector from the same reversed argument list. cli.hpp and cxxopts reuse
// immutable argument views.

class Cli11Args {
public:
    explicit Cli11Args(std::vector<std::string> values)
        : template_(std::move(values)) {
        std::reverse(template_.begin(), template_.end());
        scratch_.reserve(template_.size());
    }

    void reset() { scratch_.assign(template_.begin(), template_.end()); }
    [[nodiscard]] std::vector<std::string>& scratch() { return scratch_; }

private:
    std::vector<std::string> template_;
    std::vector<std::string> scratch_;
};

Result cli11_flags(size_t iterations, const std::vector<std::string>& args) {
    CLI::App app{"bench"};
    Cli11Args input{args};
    bool verbose = false, quiet = false, force = false, dry_run = false;
    bool color = false, all = false, porcelain = false, short_flag = false;
    app.add_flag("-v,--verbose", verbose);
    app.add_flag("-q,--quiet", quiet);
    app.add_flag("-f,--force", force);
    app.add_flag("-n,--dry-run", dry_run);
    app.add_flag("-c,--color", color);
    app.add_flag("-a,--all", all);
    app.add_flag("-p,--porcelain", porcelain);
    app.add_flag("-s,--short", short_flag);

    auto parse = [&] -> bool {
        input.reset();
        try {
            app.parse(input.scratch());
        } catch (const CLI::ParseError&) {
            return false;
        }
        return true;
    };
    const bool valid = parse()
        && verbose && quiet && force && dry_run && color && all
        && porcelain && short_flag;

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli11_options(size_t iterations, const std::vector<std::string>& args) {
    CLI::App app{"bench"};
    Cli11Args input{args};
    unsigned jobs = 0;
    std::string output, level, name;
    double ratio = 0.0;
    int count = 0;
    app.add_option("-j,--jobs", jobs);
    app.add_option("-o,--output", output);
    app.add_option("--level", level);
    app.add_option("--ratio", ratio);
    app.add_option("--count", count);
    app.add_option("--name", name);

    auto parse = [&] -> bool {
        input.reset();
        try {
            app.parse(input.scratch());
        } catch (const CLI::ParseError&) {
            return false;
        }
        return true;
    };
    const bool valid = parse()
        && jobs == 16 && output == "out.bin" && level == "info"
        && ratio == 0.75 && count == 42 && name == "release";

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli11_mixed(size_t iterations, const std::vector<std::string>& args) {
    CLI::App app{"bench"};
    Cli11Args input{args};
    bool verbose = false;
    unsigned jobs = 0;
    std::string output, input_path;
    app.add_flag("-v,--verbose", verbose);
    app.add_option("-j,--jobs", jobs);
    app.add_option("-o,--output", output);
    app.add_option("input", input_path);

    auto parse = [&] -> bool {
        input.reset();
        try {
            app.parse(input.scratch());
        } catch (const CLI::ParseError&) {
            return false;
        }
        return true;
    };
    const bool valid = parse()
        && verbose && jobs == 8 && output == "out.bin" && input_path == "src/main.cpp";

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cli11_subcommand(size_t iterations, const std::vector<std::string>& args,
                        const std::vector<std::string>& nested, bool nested_case) {
    CLI::App app{"bench"};
    Cli11Args input{args};
    Cli11Args nested_input{nested};
    app.add_flag("-v,--verbose");
    auto* build = app.add_subcommand("build");
    std::string target;
    unsigned jobs = 0;
    build->add_option("--target", target);
    build->add_option("-j,--jobs", jobs);
    auto* remote = app.add_subcommand("remote");
    auto* add = remote->add_subcommand("add");
    std::string url;
    add->add_option("--url", url);

    auto parse = [&](bool use_nested) -> bool {
        auto& source = use_nested ? nested_input : input;
        source.reset();
        try {
            app.parse(source.scratch());
        } catch (const CLI::ParseError&) {
            return false;
        }
        return true;
    };
    const bool valid = parse(nested_case)
        && (nested_case ? url == "https://example.com"
                        : target == "release" && jobs == 8);

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse(nested_case));
    });
    result.correct = valid && result.correct;
    return result;
}

// ── cxxopts scenarios ────────────────────────────────────────────────────────

struct CxxArgs {
    std::vector<std::string>        storage;
    std::vector<const char*>        argv;

    explicit CxxArgs(std::vector<std::string> values) : storage(std::move(values)) {
        argv.reserve(storage.size() + 1);
        argv.push_back("bench");
        for (const auto& value : storage) argv.push_back(value.c_str());
    }

    [[nodiscard]] int argc() const { return static_cast<int>(argv.size()); }
};

std::vector<std::string> to_owned(const std::vector<std::string_view>& args) {
    std::vector<std::string> owned;
    owned.reserve(args.size());
    for (const auto value : args) owned.emplace_back(value);
    return owned;
}

Result cxxopts_flags(size_t iterations, const std::vector<std::string>& args) {
    CxxArgs input{args};
    cxxopts::Options options("bench");
    options.add_options()
        ("v,verbose", "verbose")
        ("q,quiet", "quiet")
        ("f,force", "force")
        ("n,dry-run", "dry run")
        ("c,color", "color")
        ("a,all", "all")
        ("p,porcelain", "porcelain")
        ("s,short", "short");

    auto parse = [&] -> bool {
        try {
            (void)options.parse(input.argc(), input.argv.data());
        } catch (const std::exception&) {
            return false;
        }
        return true;
    };
    const bool valid = [&] {
        try {
            const auto result = options.parse(input.argc(), input.argv.data());
            return result["verbose"].as<bool>() && result["quiet"].as<bool>()
                   && result["force"].as<bool>() && result["dry-run"].as<bool>()
                   && result["color"].as<bool>() && result["all"].as<bool>()
                   && result["porcelain"].as<bool>() && result["short"].as<bool>();
        } catch (const std::exception&) {
            return false;
        }
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cxxopts_options(size_t iterations, const std::vector<std::string>& args) {
    CxxArgs input{args};
    cxxopts::Options options("bench");
    options.add_options()
        ("j,jobs", "jobs", cxxopts::value<unsigned>())
        ("o,output", "output", cxxopts::value<std::string>())
        ("level", "level", cxxopts::value<std::string>())
        ("ratio", "ratio", cxxopts::value<double>())
        ("count", "count", cxxopts::value<int>())
        ("name", "name", cxxopts::value<std::string>());

    auto parse = [&] -> bool {
        try {
            (void)options.parse(input.argc(), input.argv.data());
        } catch (const std::exception&) {
            return false;
        }
        return true;
    };
    const bool valid = [&] {
        try {
            const auto result = options.parse(input.argc(), input.argv.data());
            return result["jobs"].as<unsigned>() == 16
                   && result["output"].as<std::string>() == "out.bin"
                   && result["level"].as<std::string>() == "info"
                   && result["ratio"].as<double>() == 0.75
                   && result["count"].as<int>() == 42
                   && result["name"].as<std::string>() == "release";
        } catch (const std::exception&) {
            return false;
        }
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

Result cxxopts_mixed(size_t iterations, const std::vector<std::string>& args) {
    CxxArgs input{args};
    cxxopts::Options options("bench");
    options.add_options()
        ("v,verbose", "verbose")
        ("j,jobs", "jobs", cxxopts::value<unsigned>())
        ("o,output", "output", cxxopts::value<std::string>())
        ("input", "input", cxxopts::value<std::string>());
    options.parse_positional("input");

    auto parse = [&] -> bool {
        try {
            (void)options.parse(input.argc(), input.argv.data());
        } catch (const std::exception&) {
            return false;
        }
        return true;
    };
    const bool valid = [&] {
        try {
            const auto result = options.parse(input.argc(), input.argv.data());
            return result["verbose"].as<bool>() && result["jobs"].as<unsigned>() == 8
                   && result["output"].as<std::string>() == "out.bin"
                   && result["input"].as<std::string>() == "src/main.cpp";
        } catch (const std::exception&) {
            return false;
        }
    }();

    auto result = measure(iterations, [&] {
        return static_cast<std::uint64_t>(parse());
    });
    result.correct = valid && result.correct;
    return result;
}

bool print_row(std::string_view name, Result cli, Result cli11, Result cxx,
               std::string_view note = {}) {
    std::cout << std::format("{:<24} {:>11.1f} {:>11.1f} {:>11.1f}  {}\n",
                             name, cli.ns_per_parse, cli11.ns_per_parse,
                             cxx.ns_per_parse, note);
    if (!cli.correct || !cli11.correct || !cxx.correct) {
        std::cout << "  validation failed:";
        if (!cli.correct) std::cout << " cli.hpp";
        if (!cli11.correct) std::cout << " CLI11";
        if (!cxx.correct) std::cout << " cxxopts";
        std::cout << '\n';
    }
    return cli.correct && cli11.correct && cxx.correct;
}

} // namespace

int main(int argc, char** argv) {
    size_t iterations = 200'000;
    if (argc > 1) {
        const long long parsed = std::atoll(argv[1]);
        if (parsed > 0) iterations = static_cast<size_t>(parsed);
    }

    const std::vector<std::string_view> flag_args{
        "-v", "-q", "-f", "-n", "-c", "-a", "-p", "-s"
    };
    const std::vector<std::string_view> option_args{
        "-j", "16", "-o", "out.bin", "--level", "info",
        "--ratio", "0.75", "--count", "42", "--name", "release"
    };
    const std::vector<std::string_view> mixed_args{
        "-v", "-j", "8", "-o", "out.bin", "src/main.cpp"
    };
    const std::vector<std::string_view> sub_args{
        "-v", "build", "--target", "release", "-j", "8"
    };
    const std::vector<std::string_view> nested_args{
        "remote", "add", "--url", "https://example.com"
    };

    const auto owned_flags = to_owned(flag_args);
    const auto owned_options = to_owned(option_args);
    const auto owned_mixed = to_owned(mixed_args);
    const auto owned_sub = to_owned(sub_args);
    const auto owned_nested = to_owned(nested_args);

    std::cout << std::format("head-to-head parse benchmark — {} iterations\n", iterations);
    std::cout << std::format("cli.hpp 1.2.0 | CLI11 {} | cxxopts {}.{}.{}\n\n",
                             CLI11_VERSION, cxxopts::version.major,
                             cxxopts::version.minor, cxxopts::version.patch);
    std::cout << std::format("{:<24} {:>11} {:>11} {:>11}\n",
                             "case (ns/parse)", "cli.hpp", "CLI11", "cxxopts");
    std::cout << std::string(61, '-') << '\n';

    bool all_correct = true;
    all_correct &= print_row("8 short flags",
                             cli_flags(iterations, flag_args),
                             cli11_flags(iterations, owned_flags),
                             cxxopts_flags(iterations, owned_flags));

    all_correct &= print_row("6 typed options",
                             cli_options(iterations, option_args),
                             cli11_options(iterations, owned_options),
                             cxxopts_options(iterations, owned_options));

    all_correct &= print_row("mixed + positional",
                             cli_mixed(iterations, mixed_args),
                             cli11_mixed(iterations, owned_mixed),
                             cxxopts_mixed(iterations, owned_mixed));

    auto report_subcommand = [&](std::string_view name, bool nested_case) {
        const auto cli_sub = cli_subcommand(iterations, sub_args, nested_args, nested_case);
        const auto cli11_sub = cli11_subcommand(iterations, owned_sub, owned_nested, nested_case);
        std::cout << std::format("{:<24} {:>11.1f} {:>11.1f} {:>11}  {}\n",
                                 name, cli_sub.ns_per_parse, cli11_sub.ns_per_parse, "n/a",
                                 "cxxopts has no subcommands");
        if (!cli_sub.correct || !cli11_sub.correct) {
            std::cout << "  validation failed:";
            if (!cli_sub.correct) std::cout << " cli.hpp";
            if (!cli11_sub.correct) std::cout << " CLI11";
            std::cout << '\n';
        }
        return cli_sub.correct && cli11_sub.correct;
    };
    all_correct &= report_subcommand("subcommand + options", false);
    all_correct &= report_subcommand("nested subcommand", true);

    const auto checksum = static_cast<std::uint64_t>(sink);
    std::cout << std::format("\n(checksum {})\n", checksum);
    if (!all_correct) {
        std::cerr << "benchmark validation failed: a parser produced unexpected values\n";
        return 2;
    }
    return 0;
}
