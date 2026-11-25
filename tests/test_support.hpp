#pragma once

#include "doctest.h"
#include <cli.hpp>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  include <stdlib.h>
#  include <windows.h>
#endif

// Builds an argc/argv pair from string literals.  The program name is added
// automatically, matching what a real entry point receives.
struct Args {
    std::vector<std::string> storage;
    std::vector<char*>       argv_;
    int                      argc_ = 0;

    explicit Args(std::initializer_list<std::string_view> tokens) {
        storage.emplace_back("test");
        for (auto t : tokens) storage.emplace_back(t);
        for (auto& s : storage) argv_.push_back(s.data());
        argc_ = static_cast<int>(argv_.size());
    }

    Args(const Args&)            = delete;
    Args& operator=(const Args&) = delete;

    [[nodiscard]] int    argc() noexcept { return argc_; }
    [[nodiscard]] char** argv() noexcept { return argv_.data(); }
};

// Sets an environment variable for the lifetime of the object and restores the
// previous value afterwards, including when a REQUIRE() exits the test early.
class ScopedEnv {
public:
    ScopedEnv(std::string_view name, std::string_view value)
        : name_(name) {
        if (const char* previous = std::getenv(name_.c_str()))
            previous_ = previous;
        set(value);
    }

    ~ScopedEnv() {
        if (previous_)
            set(*previous_);
        else
            unset();
    }

    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    static void set_raw(const char* name, const char* value) {
#ifdef _WIN32
        SetEnvironmentVariableA(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    static void unset_raw(const char* name) {
#ifdef _WIN32
        SetEnvironmentVariableA(name, nullptr);
#else
        unsetenv(name);
#endif
    }

    void set(std::string_view value) {
        const std::string text(value);
        set_raw(name_.c_str(), text.c_str());
    }

    void unset() {
        unset_raw(name_.c_str());
    }

    std::string                name_;
    std::optional<std::string> previous_;
};

[[nodiscard]] inline bool contains(std::string_view haystack,
                                   std::string_view needle) noexcept {
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] inline std::size_t count_occurrences(std::string_view haystack,
                                                   std::string_view needle) noexcept {
    std::size_t count = 0;
    for (std::size_t pos = 0;
         (pos = haystack.find(needle, pos)) != std::string_view::npos;
         pos += needle.size())
        ++count;
    return count;
}
