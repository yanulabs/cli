cmake_minimum_required(VERSION 3.25)

project(
    cli
    VERSION 1.0.0
    DESCRIPTION "A modern C++23 single-header command-line argument parser"
    HOMEPAGE_URL "https://github.com/yanujz/cli"
    LANGUAGES CXX
)

# ── Options ──────────────────────────────────────────────────────────────────
option(CLI_BUILD_EXAMPLES "Build example programs"   ON)
option(CLI_BUILD_TESTS    "Build the test suite"     ON)
option(CLI_BUILD_FUZZERS  "Build libFuzzer targets"  OFF)
option(CLI_BUILD_DIFFERENTIAL_TESTS
    "Register upstream differential replay tests (needs Python 3)"
    OFF)
option(CLI_BUILD_BENCHMARKS "Build throughput benchmarks" OFF)
option(CLI_BUILD_COMPARATIVE_BENCHMARKS
    "Build head-to-head benchmarks against CLI11 and cxxopts" OFF)
set(CLI_FUZZER_SANITIZERS "address,undefined" CACHE STRING
    "Comma-separated sanitizers combined with the libFuzzer runtime")
option(CLI_INSTALL        "Generate install targets" ON)

if(CLI_BUILD_FUZZERS AND NOT CLI_BUILD_TESTS)
    message(FATAL_ERROR "CLI_BUILD_FUZZERS requires CLI_BUILD_TESTS=ON")
endif()

if(CLI_BUILD_COMPARATIVE_BENCHMARKS AND NOT CLI_BUILD_BENCHMARKS)
    message(FATAL_ERROR
        "CLI_BUILD_COMPARATIVE_BENCHMARKS requires CLI_BUILD_BENCHMARKS=ON")
endif()

# ── Interface library (header-only) ──────────────────────────────────────────
add_library(cli INTERFACE)
add_library(cli::cli ALIAS cli)

target_include_directories(cli INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(cli INTERFACE cxx_std_23)

# ── Examples ─────────────────────────────────────────────────────────────────
if(CLI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
if(CLI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Benchmarks ────────────────────────────────────────────────────────────────
if(CLI_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# ── Install / package config ──────────────────────────────────────────────────
if(CLI_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS cli EXPORT cliTargets)

    install(
        EXPORT      cliTargets
        FILE        cliTargets.cmake
        NAMESPACE   cli::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cli
    )

    install(
        FILES       include/cli.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    configure_package_config_file(
        cmake/cliConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cliConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cli
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/cliConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/cliConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/cliConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cli
    )
endif()
