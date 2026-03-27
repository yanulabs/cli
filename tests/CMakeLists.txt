add_executable(test_parse test_parse.cpp)

target_link_libraries(test_parse PRIVATE cli::cli)
target_include_directories(test_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(test_parse PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

add_test(NAME cli_tests COMMAND test_parse)
