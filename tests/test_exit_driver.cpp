// Process-level checks for parse_or_exit(), whose success paths and exit
// requests terminate the process and therefore cannot run inside doctest.
#include <cli.hpp>

#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

void run(cli::App& app, std::initializer_list<std::string_view> tokens) {
    std::vector<std::string> storage;
    storage.emplace_back(app.name());
    for (auto token : tokens) storage.emplace_back(token);

    std::vector<char*> argv;
    for (auto& token : storage) argv.push_back(token.data());

    app.parse_or_exit(static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";

    if (mode == "help") {
        cli::App app("tool", "A test tool", "1.2.3");
        (void)app.add<std::string>("--token").required();
        run(app, {"--help"});
    } else if (mode == "sub_help") {
        cli::App app("tool", "A test tool", "1.2.3");
        (void)app.add<std::string>("--token").required();
        auto& build = app.subcommand("build", "Compile sources");
        (void)build.add<std::string>("--target").required();
        run(app, {"build", "--help"});
    } else if (mode == "version") {
        cli::App app("tool", "A test tool", "1.2.3");
        run(app, {"--version"});
    } else if (mode == "sub_version") {
        cli::App app("tool", "A test tool", "1.2.3");
        auto& build = app.subcommand("build", "Compile sources");
        (void)build.add<std::string>("--target").required();
        run(app, {"build", "--version"});
    } else if (mode == "error") {
        cli::App app("tool", "A test tool", "1.2.3");
        (void)app.add<int>("--num");
        run(app, {"--num", "not-a-number"});
    } else if (mode == "root_callback") {
        cli::App app("tool");
        (void)app.add<std::string>("--mode");
        app.on_execute([](cli::App& command) {
            std::println("root-callback:{}", command["--mode"].as<std::string>());
        });
        run(app, {"--mode", "root"});
    } else if (mode == "child_callback") {
        cli::App app("tool");
        app.on_execute([](cli::App&) { std::println("root-callback"); });
        auto& build = app.subcommand("build", "Compile sources");
        (void)build.add<std::string>("--target");
        build.on_execute([](cli::App& command) {
            std::println("child-callback:{}", command["--target"].as<std::string>());
        });
        run(app, {"build", "--target", "release"});
    } else if (mode == "deep_callback") {
        cli::App app("tool");
        app.on_execute([](cli::App&) { std::println("root-callback"); });
        auto& remote = app.subcommand("remote", "Manage remotes");
        auto& add    = remote.subcommand("add", "Add a remote");
        add.on_execute([](cli::App&) { std::println("add-callback"); });
        run(app, {"remote", "add"});
    } else if (mode == "ancestor_callback") {
        cli::App app("tool");
        app.on_execute([](cli::App&) { std::println("root-callback"); });
        auto& remote = app.subcommand("remote", "Manage remotes");
        remote.on_execute([](cli::App&) { std::println("remote-callback"); });
        (void)remote.subcommand("add", "Add a remote");
        run(app, {"remote", "add"});
    } else {
        std::println("unknown mode: {}", mode);
        return 2;
    }

    return 0;
}
