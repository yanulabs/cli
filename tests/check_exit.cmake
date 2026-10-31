# Runs the exit driver in its parse-error mode and verifies the public
# parse_or_exit() contract: diagnostics go to stderr and the process exits 1.
if(NOT DEFINED DRIVER)
    message(FATAL_ERROR "DRIVER must point at test_exit_driver")
endif()

execute_process(
    COMMAND "${DRIVER}" error
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT result EQUAL 1)
    message(FATAL_ERROR "expected exit code 1, got '${result}'\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT stderr MATCHES "invalid value 'not-a-number' for '--num'")
    message(FATAL_ERROR "missing diagnostic on stderr:\n${stderr}")
endif()

if(NOT stderr MATCHES "Try 'tool --help'")
    message(FATAL_ERROR "missing help hint on stderr:\n${stderr}")
endif()
