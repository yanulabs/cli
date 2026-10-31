// Scratch instrumentation: capture every public CLI11 parse invocation.
#include "catch.hpp"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace cli_diff {

inline std::string esc(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    out += '"';
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x80) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

inline std::string str_array(const std::vector<std::string>& values) {
    std::string out = "[";
    bool first = true;
    for (const auto& v : values) {
        if (!first) out += ',';
        first = false;
        out += esc(v);
    }
    out += ']';
    return out;
}

inline std::string hex_array(const std::vector<std::string>& values) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "[";
    bool first = true;
    for (const auto& v : values) {
        if (!first) out += ',';
        first = false;
        out += '"';
        for (unsigned char c : v) {
            out += digits[c >> 4];
            out += digits[c & 0x0F];
        }
        out += '"';
    }
    out += ']';
    return out;
}

inline bool has_validator(CLI::Option* opt) {
    try {
        (void)opt->get_validator(0);
        return true;
    } catch (const CLI::Error&) {
        return false;
    }
}

inline std::string hex_string(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (unsigned char c : value) {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

inline bool validator_accepts(CLI::Validator* validator, const std::string& input) {
    try {
        std::string copy = input;
        return (*validator)(copy).empty();
    } catch (...) {
        return false;
    }
}

inline std::vector<std::string> plain_set_items(const std::string& desc) {
    std::vector<std::string> items;
    if (desc.size() < 3 || desc.front() != '{' || desc.back() != '}') return items;
    if (desc.find("->") != std::string::npos) return items;
    if (desc.find(',') == std::string::npos && desc.size() > 2) { /* still a one-item set */ }
    std::string body = desc.substr(1, desc.size() - 2);
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto comma = body.find(',', start);
        items.push_back(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return items;
}

inline std::string ascii_upper(std::string value) {
    for (char& c : value) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return value;
}

inline std::string ascii_lower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

inline std::string remove_underscores(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    return value;
}

inline std::string insert_after_first(std::string value, char inserted) {
    if (value.size() < 2) return value;
    value.insert(value.begin() + 1, inserted);
    return value;
}

inline void dump_validators(CLI::Option* opt, std::string& out) {
    out += ",\"validators\":[";
    bool first = true;
    for (int index = 0;; ++index) {
        CLI::Validator* validator = nullptr;
        try {
            validator = opt->get_validator(index);
        } catch (const CLI::Error&) {
            break;
        }
        if (validator == nullptr) break;
        if (!first) out += ',';
        first = false;
        const std::string desc = validator->get_description();
        bool probe_case = false;
        bool probe_underscore = false;
        bool probe_space = false;
        const auto items = plain_set_items(desc);
        if (!items.empty()) {
            const std::string& sample = items.front();
            const std::string upper = ascii_upper(sample);
            const bool upper_listed = std::find(items.begin(), items.end(), upper) != items.end();
            if (upper != sample && !upper_listed) {
                probe_case = validator_accepts(validator, upper);
            }
            const std::string lower = ascii_lower(sample);
            const bool lower_listed = std::find(items.begin(), items.end(), lower) != items.end();
            if (!probe_case && lower != sample && !lower_listed) {
                probe_case = validator_accepts(validator, lower);
            }
            if (sample.find('_') != std::string::npos) {
                const std::string squeezed = remove_underscores(sample);
                const bool squeezed_listed = std::find(items.begin(), items.end(), squeezed) != items.end();
                if (squeezed != sample && !squeezed_listed) {
                    probe_underscore = validator_accepts(validator, squeezed);
                }
            } else {
                const std::string expanded = insert_after_first(sample, '_');
                const bool expanded_listed = std::find(items.begin(), items.end(), expanded) != items.end();
                if (expanded != sample && !expanded_listed) {
                    probe_underscore = validator_accepts(validator, expanded);
                }
            }
            const std::string spaced = insert_after_first(sample, ' ');
            const bool spaced_listed = std::find(items.begin(), items.end(), spaced) != items.end();
            if (spaced != sample && !spaced_listed) {
                probe_space = validator_accepts(validator, spaced);
            }
        }
        out += "{\"desc\":" + esc(desc);
        out += ",\"name\":" + esc(validator->get_name());
        out += ",\"index\":" + std::to_string(validator->get_application_index());
        out += ",\"active\":";
        out += validator->get_active() ? "true" : "false";
        out += ",\"modifying\":";
        out += validator->get_modifying() ? "true" : "false";
        out += ",\"ignore_case_probe\":";
        out += probe_case ? "true" : "false";
        out += ",\"ignore_underscore_probe\":";
        out += probe_underscore ? "true" : "false";
        out += ",\"ignore_space_probe\":";
        out += probe_space ? "true" : "false";
        out += '}';
    }
    out += ']';
}

inline void dump_option(CLI::Option* opt, std::string& out) {
    out += "{\"lnames\":" + str_array(opt->get_lnames());
    out += ",\"snames\":" + str_array(opt->get_snames());
    out += ",\"fnames\":" + str_array(opt->get_fnames());
    out += ",\"pname\":" + esc(opt->get_name(true));
    out += ",\"type\":" + esc(opt->get_type_name());
    out += ",\"tsmin\":" + std::to_string(opt->get_type_size_min());
    out += ",\"tsmax\":" + std::to_string(opt->get_type_size_max());
    out += ",\"emin\":" + std::to_string(opt->get_expected_min());
    out += ",\"emax\":" + std::to_string(opt->get_expected_max());
    out += ",\"required\":";
    out += opt->get_required() ? "true" : "false";
    out += ",\"positional\":";
    out += opt->get_positional() ? "true" : "false";
    out += ",\"default\":" + esc(opt->get_default_str());
    out += ",\"multi\":" + std::to_string(static_cast<int>(opt->get_multi_option_policy()));
    out += ",\"configurable\":";
    out += opt->get_configurable() ? "true" : "false";
    out += ",\"env\":" + esc(opt->get_envname());
    out += ",\"env_set\":";
    const char* env_value = opt->get_envname().empty() ? nullptr : std::getenv(opt->get_envname().c_str());
    out += env_value != nullptr ? "true" : "false";
    out += ",\"env_value_hex\":";
    out += env_value != nullptr ? esc(hex_string(env_value)) : "null";
    out += ",\"delimiter\":" + std::to_string(static_cast<unsigned char>(opt->get_delimiter()));
    out += ",\"needs\":[";
    {
        bool first_need = true;
        for (auto* needed : opt->get_needs()) {
            if (!first_need) out += ',';
            first_need = false;
            out += esc(needed->get_name(false));
        }
    }
    out += "],\"excludes\":[";
    {
        bool first_ex = true;
        for (auto* excluded : opt->get_excludes()) {
            if (!first_ex) out += ',';
            first_ex = false;
            out += esc(excluded->get_name(false));
        }
    }
    out += ']';
    out += ",\"ignore_case\":";
    out += opt->get_ignore_case() ? "true" : "false";
    out += ",\"ignore_underscore\":";
    out += opt->get_ignore_underscore() ? "true" : "false";
    out += ",\"allow_extra_args\":";
    out += opt->get_allow_extra_args() ? "true" : "false";
    out += ",\"validator\":";
    out += has_validator(opt) ? "true" : "false";
    dump_validators(opt, out);
    out += ",\"callback\":";
    out += static_cast<bool>(opt->get_callback()) ? "true" : "false";
    out += ",\"user_callback\":";
    out += opt->get_user_callback() ? "true" : "false";
    out += ",\"disable_flag_override\":";
    out += opt->get_disable_flag_override() ? "true" : "false";
    out += ",\"type_id\":";
    out += esc(opt->get_diff_type_id());
    out += ",\"count\":" + std::to_string(opt->count());
    out += ",\"results\":" + str_array(opt->results());
    out += '}';
}

inline void dump_app(CLI::App& app, std::string& out) {
    out += "{\"name\":" + esc(app.get_name());
    out += ",\"aliases\":";
    {
        std::string aliases = "[";
        bool first_alias = true;
        for (const auto& alias : app.get_aliases()) {
            if (!first_alias) aliases += ',';
            first_alias = false;
            aliases += esc(alias);
        }
        aliases += ']';
        out += aliases;
    }
    out += ",\"ignore_case\":";
    out += app.get_ignore_case() ? "true" : "false";
    out += ",\"ignore_underscore\":";
    out += app.get_ignore_underscore() ? "true" : "false";
    out += ",\"disabled\":";
    out += app.get_disabled() ? "true" : "false";
    out += ",\"disabled_by_default\":";
    out += app.get_disabled_by_default() ? "true" : "false";
    out += ",\"enabled_by_default\":";
    out += app.get_enabled_by_default() ? "true" : "false";
    out += ",\"silent\":";
    out += app.get_silent() ? "true" : "false";
    out += ",\"option_group\":";
    out += app.get_is_option_group() ? "true" : "false";
    out += ",\"group\":";
    out += esc(app.get_group());
    out += ",\"version\":";
    out += esc(app.version());
    out += ",\"count\":" + std::to_string(app.count());
    out += ",\"callbacks\":";
    out += (app.has_pre_parse_callback() || app.has_parse_complete_callback() || app.has_final_callback())
        ? "true" : "false";
    out += ",\"need_subcommands\":[";
    {
        bool first_ns = true;
        for (auto* needed : app.get_need_subcommands()) {
            if (!first_ns) out += ',';
            first_ns = false;
            out += esc(needed->get_name());
        }
    }
    out += "],\"exclude_subcommands\":[";
    {
        bool first_es = true;
        for (auto* excluded : app.get_exclude_subcommands()) {
            if (!first_es) out += ',';
            first_es = false;
            out += esc(excluded->get_name());
        }
    }
    out += "],\"need_options\":[";
    {
        bool first_no = true;
        for (auto* needed : app.get_need_options()) {
            if (!first_no) out += ',';
            first_no = false;
            out += esc(needed->get_name(false));
        }
    }
    out += "],\"exclude_options\":[";
    {
        bool first_eo = true;
        for (auto* excluded : app.get_exclude_options()) {
            if (!first_eo) out += ',';
            first_eo = false;
            out += esc(excluded->get_name(false));
        }
    }
    out += ']';
    out += ",\"allow_extras\":";
    out += app.get_allow_extras() ? "true" : "false";
    out += ",\"allow_extras_mode\":";
    out += std::to_string(static_cast<int>(app.get_allow_extras_mode()));
    out += ",\"positionals_at_end\":";
    out += app.get_positionals_at_end() ? "true" : "false";
    out += ",\"subcommand_prefix\":";
    out += app.get_allow_subcommand_prefix_matching() ? "true" : "false";
    out += ",\"required\":";
    out += app.get_required() ? "true" : "false";
    out += ",\"require_subcommand_min\":" + std::to_string(app.get_require_subcommand_min());
    out += ",\"require_subcommand_max\":" + std::to_string(app.get_require_subcommand_max());
    out += ",\"require_option_min\":" + std::to_string(app.get_require_option_min());
    out += ",\"require_option_max\":" + std::to_string(app.get_require_option_max());
    out += ",\"subcommand_fallthrough\":";
    out += app.get_subcommand_fallthrough() ? "true" : "false";
    out += ",\"windows_style\":";
    out += app.get_allow_windows_style_options() ? "true" : "false";
    out += ",\"has_help\":";
    out += app.get_help_ptr() != nullptr ? "true" : "false";
    out += ",\"help_names\":[";
    if (app.get_help_ptr() != nullptr) {
        bool first_help = true;
        for (const auto& n : app.get_help_ptr()->get_lnames()) {
            if (!first_help) out += ',';
            first_help = false;
            out += esc("--" + n);
        }
        for (const auto& n : app.get_help_ptr()->get_snames()) {
            if (!first_help) out += ',';
            first_help = false;
            out += esc("-" + n);
        }
    }
    out += "],\"has_version\":";
    out += app.get_version_ptr() != nullptr ? "true" : "false";
    out += ",\"version_names\":[";
    if (app.get_version_ptr() != nullptr) {
        bool first_ver = true;
        for (const auto& n : app.get_version_ptr()->get_lnames()) {
            if (!first_ver) out += ',';
            first_ver = false;
            out += esc("--" + n);
        }
        for (const auto& n : app.get_version_ptr()->get_snames()) {
            if (!first_ver) out += ',';
            first_ver = false;
            out += esc("-" + n);
        }
    }
    out += ']';
    out += ",\"prefix_command\":";
    out += app.get_prefix_command() ? "true" : "false";
    out += ",\"fallthrough\":";
    out += app.get_fallthrough() ? "true" : "false";
    out += ",\"immediate_callback\":";
    out += app.get_immediate_callback() ? "true" : "false";
    out += ",\"config\":";
    out += app.get_config_ptr() != nullptr ? "true" : "false";
    out += ",\"has_help_all\":";
    out += app.get_help_all_ptr() != nullptr ? "true" : "false";
    out += ",\"parsed_subcommands\":[";
    {
        bool first_ps = true;
        for (auto* parsed_sub : app.get_subcommands()) {
            if (!first_ps) out += ',';
            first_ps = false;
            out += esc(parsed_sub->get_name());
        }
    }
    out += ']';
    out += ",\"options\":[";
    bool first = true;
    for (auto* opt : app.get_direct_options()) {
        if (!first) out += ',';
        first = false;
        dump_option(opt, out);
    }
    out += "],\"subcommands\":[";
    first = true;
    for (auto* sub : app.get_subcommands([](CLI::App*) { return true; })) {
        if (!first) out += ',';
        first = false;
        dump_app(*sub, out);
    }
    out += "]}";
}

void capture_parse(const CLI::App& app,
                   const std::vector<std::string>& args,
                   bool ok,
                   const std::string& error) {
    const char* path = std::getenv("CLI_DIFF_OUT");
    if (path == nullptr || *path == '\0') return;
    std::string out = "{\"test\":" + esc(Catch::getResultCapture().getCurrentTestName());
    out += ",\"args\":" + str_array(args);
    out += ",\"args_hex\":" + hex_array(args);
    out += ",\"outcome\":";
    out += ok ? "\"ok\"" : "\"error\"";
    out += ",\"error\":" + esc(error);
    out += ",\"schema\":";
    dump_app(const_cast<CLI::App&>(app), out);
    out += "}\n";
    std::ofstream file(path, std::ios::app);
    file << out;
}

} // namespace cli_diff
