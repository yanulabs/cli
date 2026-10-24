add_executable(bench_parse bench_parse.cpp)
target_link_libraries(bench_parse PRIVATE cli::cli)
set_target_properties(bench_parse PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

if(CLI_BUILD_COMPARATIVE_BENCHMARKS)
    set(CLI_COMPARE_UPSTREAM_DIR "" CACHE PATH
        "Directory containing CLI11.hpp and cxxopts.hpp; skips FetchContent")

    if(CLI_COMPARE_UPSTREAM_DIR)
        foreach(header CLI11.hpp cxxopts.hpp)
            if(NOT EXISTS "${CLI_COMPARE_UPSTREAM_DIR}/${header}")
                message(FATAL_ERROR
                    "CLI_COMPARE_UPSTREAM_DIR is missing ${header}: ${CLI_COMPARE_UPSTREAM_DIR}")
            endif()
        endforeach()
        add_library(cli_bench_cli11 INTERFACE)
        target_include_directories(cli_bench_cli11 INTERFACE "${CLI_COMPARE_UPSTREAM_DIR}")
        add_library(cli_bench_cxxopts INTERFACE)
        target_include_directories(cli_bench_cxxopts INTERFACE "${CLI_COMPARE_UPSTREAM_DIR}")
        set(CLI_BENCH_CLI11 cli_bench_cli11)
        set(CLI_BENCH_CXXOPTS cli_bench_cxxopts)
        set(CLI_BENCH_SINGLE_HEADERS ON)
    else()
        include(FetchContent)
        set(CLI11_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
        set(CLI11_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
        set(CLI11_BUILD_DOCS        OFF CACHE BOOL "" FORCE)
        set(CXXOPTS_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
        set(CXXOPTS_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
        set(CXXOPTS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(cli11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
            GIT_TAG        v2.7.2
            GIT_SHALLOW    TRUE
        )
        FetchContent_Declare(cxxopts
            GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
            GIT_TAG        v3.3.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(cli11 cxxopts)
        set(CLI_BENCH_CLI11 CLI11::CLI11)
        set(CLI_BENCH_CXXOPTS cxxopts::cxxopts)
        set(CLI_BENCH_SINGLE_HEADERS OFF)
    endif()

    add_executable(bench_compare bench_compare.cpp)
    target_link_libraries(bench_compare PRIVATE
        cli::cli ${CLI_BENCH_CLI11} ${CLI_BENCH_CXXOPTS})
    target_compile_features(bench_compare PRIVATE cxx_std_23)
    if(CLI_BENCH_SINGLE_HEADERS)
        target_compile_definitions(bench_compare PRIVATE CLI_BENCH_SINGLE_HEADERS)
    endif()
    set_target_properties(bench_compare PROPERTIES
        CXX_STANDARD          23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
endif()
