if(NOT DEFINED HOST_SHELL OR NOT DEFINED SCRIPT_FILE)
    message(FATAL_ERROR "HOST_SHELL and SCRIPT_FILE are required")
endif()

execute_process(
    COMMAND "${HOST_SHELL}" --no-ansi --script "${SCRIPT_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "host script failed (${result}): ${error}")
endif()

foreach(required "pong" "arg[0]=Office AP" "stored <hidden>"
                 "arg[2]=three four" "history:")
    string(FIND "${output}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing '${required}' in output: ${output}")
    endif()
endforeach()

string(FIND "${output}${error}" "secret-value" leaked)
if(NOT leaked EQUAL -1)
    message(FATAL_ERROR "sensitive value leaked into host script output")
endif()
