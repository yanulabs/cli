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
#include <optional>
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

// Length-prefix every scalar and vector element so distinct values cannot
// collide through display separators (for example {"a,b", "c"} and
// {"a", "b,c"}).
template<typename T>
[[nodiscard]] std::string encode_value(const T& value) {
    if constexpr (cli::detail::is_vec_v<T>) {
        std::string out = std::to_string(value.size()) + ':';
        for (const auto& element : value) out += encode_value(element);
        return out;
    } else {
        const auto text = cli::detail::to_display(value);
        return std::to_string(text.size()) + ':' + text;
    }
}

struct Handles {
    cli::Flag                         verbose;
    cli::Flag                         color;
    cli::Option<int>                  jobs;
    cli::Option<double>               ratio;
    cli::Option<std::string>          name;
    cli::Option<std::string>          level;
    cli::Option<std::vector<std::string>> tag;
    cli::Count                        debug;
    cli::Positional<std::vector<int>> pair;
    cli::Positional<std::string>      input;
    cli::Positional<std::vector<std::string>> rest;
    cli::Option<std::string>          target;
    cli::Option<int>                  count;
};

struct Snapshot {
    bool                          ok      = false;
    cli::ParseError::Kind         kind    = cli::ParseError::Kind::HelpRequested;
    std::string                   message;
    const cli::App*               context = nullptr;
    std::vector<std::optional<std::string>> values;
    std::vector<bool>             set;
    std::vector<const cli::App*>  active;
    std::vector<std::vector<std::string>> extras;
    std::vector<const cli::App*>  chain;

    bool operator==(const Snapshot&) const = default;
};

template<typename T>
void capture_handle(std::vector<std::optional<std::string>>& values,
                    std::vector<bool>& set, const cli::Option<T>& handle) {
    const auto& value = handle.get();
    if (value) values.emplace_back(encode_value(*value));
    else       values.emplace_back(std::nullopt);
    set.push_back(handle.is_set());
}

template<typename T>
void capture_handle(std::vector<std::optional<std::string>>& values,
                    std::vector<bool>& set, const cli::Positional<T>& handle) {
    const auto& value = handle.get();
    if (value) values.emplace_back(encode_value(*value));
    else       values.emplace_back(std::nullopt);
    set.push_back(handle.is_set());
}

template<typename T>
void capture_lookup(std::vector<std::optional<std::string>>& values,
                    std::vector<bool>& set, const cli::App& app,
                    std::string_view name) {
    const auto lookup = app[name];
    if (const auto value = lookup.try_as<T>())
        values.emplace_back(encode_value(*value));
    else
        values.emplace_back(std::nullopt);
    set.push_back(lookup.is_set());
}

[[nodiscard]] Snapshot snapshot(const cli::App& app, const cli::App& build,
                                const cli::App& run, const Handles& handles) {
    Snapshot state;
    capture_lookup<bool>(state.values, state.set, app, "--verbose");
    capture_lookup<bool>(state.values, state.set, app, "--color");
    capture_handle(state.values, state.set, handles.jobs);
    capture_handle(state.values, state.set, handles.ratio);
    capture_handle(state.values, state.set, handles.name);
    capture_handle(state.values, state.set, handles.level);
    capture_handle(state.values, state.set, handles.tag);
    capture_lookup<unsigned>(state.values, state.set, app, "--debug");
    capture_handle(state.values, state.set, handles.pair);
    capture_handle(state.values, state.set, handles.input);
    capture_handle(state.values, state.set, handles.rest);
    capture_handle(state.values, state.set, handles.target);
    capture_handle(state.values, state.set, handles.count);

    state.active = {app.active_subcommand(), build.active_subcommand(),
                    run.active_subcommand()};
    state.extras = {app.remaining_args(), build.remaining_args(),
                    run.remaining_args()};
    for (const auto* command = &app; command != nullptr;
         command = command->active_subcommand())
        state.chain.push_back(command);
    return state;
}

void capture_result(Snapshot& state, const cli::ParseResult& result) {
    state.ok = result.has_value();
    if (!result) {
        state.kind    = result.error().kind;
        state.message = result.error().message;
        state.context = result.error().context;
    }
}

void check_error(const cli::ParseResult& result) {
    if (!result && !result.error().is_exit_request()) {
        if (result.error().message.empty()) std::abort();
        if (result.error().context == nullptr) std::abort();
    }
}

[[nodiscard]] bool equivalent_direct_state(const Snapshot& direct,
                                           const Snapshot& rooted) {
    return direct == rooted;
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
    app.allow_extras();
    app.response_files();
    app.config_file(fuzz_files().config.string());

    auto& build = app.subcommand("build", "Build the project");
    auto& run   = app.subcommand("run", "Run the project");
    run.alias("r");

    Handles handles{
        app.add_flag("--verbose").short_name("-v").negatable().global(),
        app.add_flag("--color").default_val(true).negatable(),
        app.add<int>("--jobs").short_name("-j").range(-100, 100)
            .default_val(1).global(),
        app.add<double>("--ratio").range(0.0, 1.0),
        app.add<std::string>("--name").short_name("-n").global(),
        app.add<std::string>("--level").choices({"debug", "info", "warn"}),
        app.add<std::vector<std::string>>("--tag").short_name("-t").delimiter(','),
        app.add_count("--debug").short_name("-d"),
        app.add_positional<std::vector<int>>("pair").required(false).nargs(2),
        app.add_positional<std::string>("input").required(false).default_val("in"),
        app.add_positional<std::vector<std::string>>("rest").required(false),
        build.add<std::string>("--target").choices({"debug", "release"})
            .default_val("debug"),
        run.add<int>("--count"),
    };
    app.mutex_group({"--verbose", "--color"});

    // Map one marker spelling to the controlled response file.  Escape every
    // other @ prefix so adversarial paths remain literal and cannot make the
    // fuzzer read arbitrary files while still exercising response expansion.
    std::vector<std::string> owned;
    owned.reserve(args.size());
    std::vector<std::string_view> parse_args;
    parse_args.reserve(args.size());
    for (const auto arg : args) {
        if (arg == "@fuzz") {
            owned.push_back("@" + fuzz_files().response.string());
            parse_args.emplace_back(owned.back());
        } else if (arg.starts_with('@')) {
            // response_files() expands every @token.  Double the prefix so
            // adversarial paths stay literal instead of opening arbitrary
            // files, devices, or pipes in the fuzzer process.
            owned.emplace_back("@@");
            owned.back().append(arg);
            parse_args.emplace_back(owned.back());
        } else {
            parse_args.emplace_back(arg);
        }
    }

    // parse() must fully reset values, source flags, extras, and command state.
    const auto first = app.parse(std::span<const std::string_view>(parse_args));
    check_error(first);
    auto first_state = snapshot(app, build, run, handles);
    capture_result(first_state, first);

    const auto second = app.parse(std::span<const std::string_view>(parse_args));
    check_error(second);
    auto second_state = snapshot(app, build, run, handles);
    capture_result(second_state, second);
    if (first_state != second_state) std::abort();

    // Direct child parsing and root parsing with the same child arguments must
    // leave the same values, extras, errors, and active command chain.
    const auto direct = build.parse(std::span<const std::string_view>(parse_args));
    check_error(direct);
    auto direct_state = snapshot(app, build, run, handles);
    capture_result(direct_state, direct);

    std::vector<std::string_view> rooted_args;
    rooted_args.reserve(parse_args.size() + 1);
    rooted_args.push_back("build");
    rooted_args.insert(rooted_args.end(), parse_args.begin(), parse_args.end());
    const auto via_root = app.parse(std::span<const std::string_view>(rooted_args));
    check_error(via_root);
    auto rooted_state = snapshot(app, build, run, handles);
    capture_result(rooted_state, via_root);
    if (!equivalent_direct_state(direct_state, rooted_state)) std::abort();

    return 0;
}
