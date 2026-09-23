cmake_minimum_required(VERSION 3.20)
project(portable_c_shell VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SH_BUILD_EXAMPLES "Build shell examples" ON)
option(SH_BUILD_TESTS "Build shell tests" ON)
option(SH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SH_BUILD_BAREMETAL_PORT "Build the generic bare-metal adapter" ON)
option(SH_BUILD_HOST_PORT "Build the native host terminal adapter" ON)
option(SH_BUILD_TCP_PORT "Build the platform-neutral raw TCP adapter" ON)
option(SH_BUILD_FREERTOS_PORT "Build the FreeRTOS adapter" OFF)
option(SH_BUILD_FUZZ_HARNESS "Compile the libFuzzer entry point" OFF)
option(SH_ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation" OFF)
option(SH_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SH_ENABLE_GCC_ANALYZER "Enable GCC static analyzer" OFF)
set(SH_FREERTOS_INCLUDE_DIRS "" CACHE STRING
    "Semicolon-separated FreeRTOS include directories")

if(SH_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR "SH_ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()

if(SH_ENABLE_GCC_ANALYZER)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fanalyzer)
    else()
        message(FATAL_ERROR "SH_ENABLE_GCC_ANALYZER requires GCC")
    endif()
endif()

if(SH_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "SH_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

set(SH_CORE_SOURCES
    shell/src/sh_shell.c
    shell/src/sh_tokenizer.c
    shell/src/sh_command.c
    shell/src/sh_history.c
    shell/src/sh_completion.c
    shell/src/sh_ansi.c
    shell/src/sh_transport.c
)

add_library(sh_shell ${SH_CORE_SOURCES})
add_library(portable_shell::core ALIAS sh_shell)

target_include_directories(sh_shell PUBLIC shell/include)

function(sh_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(SH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(SH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

sh_enable_warnings(sh_shell)

if(SH_BUILD_BAREMETAL_PORT)
    add_library(sh_port_baremetal shell/ports/baremetal/sh_port_baremetal.c)
    add_library(portable_shell::baremetal ALIAS sh_port_baremetal)
    target_include_directories(sh_port_baremetal PUBLIC shell/ports/baremetal)
    target_link_libraries(sh_port_baremetal PUBLIC sh_shell)
    sh_enable_warnings(sh_port_baremetal)
endif()

if(SH_BUILD_HOST_PORT)
    if(WIN32)
        set(SH_HOST_PORT_SOURCE shell/ports/host/sh_port_windows.c)
    else()
        set(SH_HOST_PORT_SOURCE shell/ports/host/sh_port_posix.c)
    endif()
    add_library(sh_port_host ${SH_HOST_PORT_SOURCE})
    target_include_directories(sh_port_host PUBLIC shell/ports/host)
    sh_enable_warnings(sh_port_host)
endif()

if(SH_BUILD_TCP_PORT)
    add_library(sh_port_tcp shell/ports/tcp/sh_port_tcp.c)
    add_library(portable_shell::tcp ALIAS sh_port_tcp)
    target_include_directories(sh_port_tcp PUBLIC shell/ports/tcp)
    target_link_libraries(sh_port_tcp PUBLIC sh_shell)
    sh_enable_warnings(sh_port_tcp)
endif()

if(SH_BUILD_FREERTOS_PORT)
    if(NOT SH_FREERTOS_INCLUDE_DIRS)
        message(FATAL_ERROR
            "SH_BUILD_FREERTOS_PORT requires SH_FREERTOS_INCLUDE_DIRS")
    endif()
    add_library(sh_port_freertos shell/ports/freertos/sh_port_freertos.c)
    add_library(portable_shell::freertos ALIAS sh_port_freertos)
    target_include_directories(sh_port_freertos PUBLIC shell/ports/freertos
        PRIVATE ${SH_FREERTOS_INCLUDE_DIRS})
    target_link_libraries(sh_port_freertos PUBLIC sh_shell)
    sh_enable_warnings(sh_port_freertos)
endif()

add_executable(sh_memory_report shell/tools/sh_memory_report.c)
target_link_libraries(sh_memory_report PRIVATE sh_shell)
sh_enable_warnings(sh_memory_report)

add_executable(sh_lookup_benchmark shell/tools/sh_lookup_benchmark.c)
target_link_libraries(sh_lookup_benchmark PRIVATE sh_shell)
sh_enable_warnings(sh_lookup_benchmark)

if(SH_BUILD_FUZZ_HARNESS)
    add_library(sh_fuzz_shell_input OBJECT shell/fuzz/fuzz_shell_input.c)
    target_link_libraries(sh_fuzz_shell_input PRIVATE sh_shell)
    sh_enable_warnings(sh_fuzz_shell_input)
endif()

if(SH_BUILD_EXAMPLES AND SH_BUILD_HOST_PORT)
    add_executable(host_terminal_shell shell/examples/host_terminal_shell.c)
    target_link_libraries(host_terminal_shell PRIVATE sh_shell)
    target_link_libraries(host_terminal_shell PRIVATE sh_port_host)
    sh_enable_warnings(host_terminal_shell)
    if(SH_BUILD_TESTS)
        add_test(
            NAME host_terminal_script
            COMMAND ${CMAKE_COMMAND}
                -DHOST_SHELL=$<TARGET_FILE:host_terminal_shell>
                -DSCRIPT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/shell/tests/fixtures/host_script.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/shell/tests/run_host_script.cmake)
    endif()
endif()

if(SH_BUILD_EXAMPLES AND SH_BUILD_TCP_PORT)
    add_executable(tcp_loopback_shell shell/examples/tcp_loopback_shell.c)
    target_link_libraries(tcp_loopback_shell PRIVATE sh_port_tcp)
    sh_enable_warnings(tcp_loopback_shell)
endif()

if(SH_BUILD_EXAMPLES AND SH_BUILD_BAREMETAL_PORT)
    add_library(baremetal_reference_compile OBJECT
        shell/examples/baremetal_polling_shell.c)
    target_link_libraries(baremetal_reference_compile PRIVATE
        sh_port_baremetal)
    sh_enable_warnings(baremetal_reference_compile)
endif()

if(SH_BUILD_TESTS)
    enable_testing()

    function(add_shell_test name)
        add_executable(${name} shell/tests/${name}.c)
        target_link_libraries(${name} PRIVATE sh_shell)
        sh_enable_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_shell_test(test_tokenizer)
    add_shell_test(test_history)
    add_shell_test(test_history_extended)
    add_shell_test(test_init_config)
    add_shell_test(test_command_dispatch)
    add_shell_test(test_command_registry)
    add_shell_test(test_command_sets)
    add_shell_test(test_completion)
    add_shell_test(test_completion_dynamic)
    add_shell_test(test_completion_scale)
    add_shell_test(test_line_editor)
    add_shell_test(test_line_editor_extended)
    add_shell_test(test_integration)
    add_shell_test(test_large_args)
    add_shell_test(test_ansi)
    add_shell_test(test_transport)
    add_shell_test(test_shell_transport)
    add_shell_test(test_regression_core)
    add_shell_test(test_public_api)
    add_shell_test(test_coverage_edges)
    add_shell_test(test_fuzz_stream)

    if(SH_BUILD_TCP_PORT)
        add_executable(test_tcp_port shell/tests/test_tcp_port.c)
        target_link_libraries(test_tcp_port PRIVATE sh_port_tcp)
        sh_enable_warnings(test_tcp_port)
        add_test(NAME test_tcp_port COMMAND test_tcp_port)
    endif()

    if(SH_BUILD_BAREMETAL_PORT)
        add_executable(test_baremetal_port
            shell/tests/test_baremetal_port.c
            shell/ports/baremetal/sh_port_baremetal.c)
        target_include_directories(test_baremetal_port PRIVATE
            shell/include shell/ports/baremetal)
        sh_enable_warnings(test_baremetal_port)
        add_test(NAME test_baremetal_port COMMAND test_baremetal_port)
    endif()

    add_executable(test_freertos_port
        shell/tests/test_freertos_port.c
        shell/tests/freertos_fakes/freertos_fakes.c
        shell/ports/freertos/sh_port_freertos.c)
    target_include_directories(test_freertos_port PRIVATE
        shell/include
        shell/ports/freertos
        shell/tests/freertos_fakes)
    sh_enable_warnings(test_freertos_port)
    add_test(NAME test_freertos_port COMMAND test_freertos_port)

    add_library(freertos_reference_compile OBJECT
        shell/examples/freertos_uart_shell.c)
    target_include_directories(freertos_reference_compile PRIVATE
        shell/include
        shell/ports/freertos
        shell/tests/freertos_fakes)
    sh_enable_warnings(freertos_reference_compile)

    add_library(sh_shell_extended STATIC ${SH_CORE_SOURCES})
    target_include_directories(sh_shell_extended PUBLIC shell/include)
    target_compile_definitions(sh_shell_extended PUBLIC
        SH_MAX_LINE_LEN=512 SH_MAX_ARGC=64 SH_HISTORY_DEFAULT_DEPTH=16)
    sh_enable_warnings(sh_shell_extended)

    add_executable(test_large_args_extended shell/tests/test_large_args.c)
    target_link_libraries(test_large_args_extended PRIVATE sh_shell_extended)
    sh_enable_warnings(test_large_args_extended)
    add_test(NAME test_large_args_extended COMMAND test_large_args_extended)
endif()
