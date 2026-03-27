add_executable(basic       basic.cpp)
add_executable(subcommands subcommands.cpp)
add_executable(completions completions.cpp)

foreach(target basic subcommands completions)
    target_link_libraries(${target} PRIVATE cli::cli)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD          23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
endforeach()
