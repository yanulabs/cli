set(CLI_TEST_SOURCES
    test_main.cpp
    test_api.cpp
    test_tokens.cpp
    test_flags.cpp
    test_options.cpp
    test_values.cpp
    test_positionals.cpp
    test_constraints.cpp
    test_groups.cpp
    test_environment.cpp
    test_subcommands.cpp
    test_errors.cpp
    test_help.cpp
    test_completion.cpp
    test_lookup.cpp
    test_lifecycle.cpp
    test_custom_types.cpp
    test_robustness.cpp
)

add_executable(test_parse ${CLI_TEST_SOURCES})

target_link_libraries(test_parse PRIVATE cli::cli)
target_include_directories(test_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(test_parse PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

set(CLI_TEST_DOMAINS
    api
    completion
    constraints
    custom_types
    environment
    errors
    flags
    groups
    help
    lifecycle
    lookup
    options
    positionals
    robustness
    subcommands
    tokens
    values
)

foreach(domain IN LISTS CLI_TEST_DOMAINS)
    add_test(NAME cli_${domain} COMMAND test_parse --test-suite=${domain})
endforeach()

# Aggregate run, matching the historical test name.
add_test(NAME cli_tests COMMAND test_parse)

# parse_or_exit() terminates the process, so exercise it through a small driver.
add_executable(test_exit_driver test_exit_driver.cpp)
target_link_libraries(test_exit_driver PRIVATE cli::cli)
set_target_properties(test_exit_driver PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

add_test(NAME cli_exit_help COMMAND test_exit_driver help)
set_tests_properties(cli_exit_help PROPERTIES
    PASS_REGULAR_EXPRESSION "USAGE"
)

add_test(NAME cli_exit_sub_help COMMAND test_exit_driver sub_help)
set_tests_properties(cli_exit_sub_help PROPERTIES
    PASS_REGULAR_EXPRESSION "tool build"
)

add_test(NAME cli_exit_version COMMAND test_exit_driver version)
set_tests_properties(cli_exit_version PROPERTIES
    PASS_REGULAR_EXPRESSION "tool 1\\.2\\.3"
)

add_test(NAME cli_exit_sub_version COMMAND test_exit_driver sub_version)
set_tests_properties(cli_exit_sub_version PROPERTIES
    PASS_REGULAR_EXPRESSION "tool build 1\\.2\\.3"
)

add_test(NAME cli_exit_error
    COMMAND ${CMAKE_COMMAND}
        -DDRIVER=$<TARGET_FILE:test_exit_driver>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exit.cmake
)

add_test(NAME cli_exit_root_callback COMMAND test_exit_driver root_callback)
set_tests_properties(cli_exit_root_callback PROPERTIES
    PASS_REGULAR_EXPRESSION "root-callback:root"
)

add_test(NAME cli_exit_child_callback COMMAND test_exit_driver child_callback)
set_tests_properties(cli_exit_child_callback PROPERTIES
    PASS_REGULAR_EXPRESSION "child-callback:release"
    FAIL_REGULAR_EXPRESSION "root-callback"
)

add_test(NAME cli_exit_deep_callback COMMAND test_exit_driver deep_callback)
set_tests_properties(cli_exit_deep_callback PROPERTIES
    PASS_REGULAR_EXPRESSION "add-callback"
    FAIL_REGULAR_EXPRESSION "root-callback"
)

add_test(NAME cli_exit_ancestor_callback COMMAND test_exit_driver ancestor_callback)
set_tests_properties(cli_exit_ancestor_callback PROPERTIES
    PASS_REGULAR_EXPRESSION "remote-callback"
    FAIL_REGULAR_EXPRESSION "root-callback"
)

# libFuzzer is a Clang feature; keep it opt-in so normal builds stay portable.
if(CLI_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CLI_BUILD_FUZZERS requires Clang")
    endif()

    add_executable(fuzz_parse fuzz_parse.cpp)
    target_link_libraries(fuzz_parse PRIVATE cli::cli)
    set_target_properties(fuzz_parse PROPERTIES
        CXX_STANDARD          23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
    if(CLI_FUZZER_SANITIZERS)
        set(CLI_FUZZ_SANITIZE_FLAG "-fsanitize=fuzzer,${CLI_FUZZER_SANITIZERS}")
    else()
        set(CLI_FUZZ_SANITIZE_FLAG "-fsanitize=fuzzer")
    endif()
    target_compile_options(fuzz_parse PRIVATE
        ${CLI_FUZZ_SANITIZE_FLAG}
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(fuzz_parse PRIVATE ${CLI_FUZZ_SANITIZE_FLAG})
endif()
