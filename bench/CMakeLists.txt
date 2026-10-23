add_executable(bench_parse bench_parse.cpp)
target_link_libraries(bench_parse PRIVATE cli::cli)
set_target_properties(bench_parse PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)
