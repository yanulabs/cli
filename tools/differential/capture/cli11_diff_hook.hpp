// Capture-only declaration used by the CLI11 differential instrumentation.
// Included from include/CLI/impl/App_inl.hpp when CLI_DIFF_CAPTURE is defined.
#pragma once

#include <string>
#include <vector>

namespace CLI {
class App;
}

namespace cli_diff {
void capture_parse(const CLI::App& app,
                   const std::vector<std::string>& args,
                   bool ok,
                   const std::string& error);
}
