# Testing And Coverage

This document describes the test targets and build behavior present in the
current repository. It intentionally does not claim that a command passed on a
particular machine unless a dated test report records that run.

## Host Build

The project requires CMake 3.20 or newer and requests strict C99 with compiler
extensions disabled. `SH_WARNINGS_AS_ERRORS` defaults to `ON`.

On Windows, the repository path may contain non-ASCII characters. The provided
script copies `CMakeLists.txt` and `shell/` to an ASCII-only temporary path,
configures with MinGW Makefiles, builds, runs CTest, and then copies the host
terminal executable to `build_host_artifacts`:

```powershell
Set-Location "C:\path\to\SERIAL SHELL"
.\scripts\build_host.ps1
```

Optional parameters:

```powershell
.\scripts\build_host.ps1 `
    -BuildRoot "$env:TEMP\serial_shell_build_src" `
    -Generator "MinGW Makefiles"
```

The script throws on configure, build, or test failure. Do not infer a passing
result merely because an older executable exists in `build_host_artifacts`.

On an ASCII-only path on Windows or on a POSIX host:

```sh
cmake -S . -B build \
  -DSH_BUILD_TESTS=ON \
  -DSH_BUILD_EXAMPLES=ON \
  -DSH_BUILD_BAREMETAL_PORT=ON \
  -DSH_BUILD_HOST_PORT=ON \
  -DSH_BUILD_TCP_PORT=ON \
  -DSH_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run one test by exact name:

```sh
ctest --test-dir build --output-on-failure -R '^test_transport$'
```

Or run a test executable directly to debug it:

```text
build/test_tokenizer
build/test_history_extended
build/test_line_editor_extended
```

Executable suffixes and multi-configuration subdirectories depend on the
selected generator.

### Verified Snapshot

On 2026-09-23, the current source was copied to a new ASCII-only temporary
directory and configured with CMake using MinGW Makefiles, GCC 13.2.0, strict
warnings-as-errors, examples, and the default port options. The clean build
completed and CTest reported 26/26 tests passed. The separate coverage build
measured 96.37% core line coverage and 86.34% core branch coverage. This
snapshot does not include a successful sanitizer runtime run, a real FreeRTOS
kernel, real sockets, or hardware.

## Current Automated Test Inventory

With the current default CMake options, these tests are registered:

| Test | Current scope |
|---|---|
| `test_tokenizer` | whitespace, quotes, escapes, parser errors |
| `test_history` | basic capacity/order/clear behavior |
| `test_history_extended` | wrap-around, duplicate policy, draft restore, quoted recall, editing recalled lines |
| `test_init_config` | static storage, disabled history, invalid dimensions |
| `test_command_dispatch` | nested dispatch, help, sensitive history masking |
| `test_command_registry` | argv contract, contexts, count limits, invalid tables, no-history flag |
| `test_command_sets` | modular root tables, help/completion across sets, duplicate rejection |
| `test_completion` | command and subcommand completion |
| `test_completion_dynamic` | argument provider, context, common prefix, alternatives |
| `test_completion_scale` | 128 root commands, display cap, omitted-count message, exact high-index match |
| `test_line_editor` | baseline editor and history key behavior |
| `test_line_editor_extended` | cursor editing, Ctrl keys, line endings, split/malformed ANSI, overflow recovery, echo off, secret masking |
| `test_integration` | end-to-end shell command behavior |
| `test_large_args` | 32 total parsed arguments with default compile-time limit |
| `test_ansi` | ANSI decoder states, supported keys, malformed sequences |
| `test_transport` | full/partial writes, would-block, attempt limits, protocol errors, flush |
| `test_shell_transport` | shell-to-transport mapping and backpressure result |
| `test_regression_core` | CRLF, quoted history, cursor-aware Tab, built-ins |
| `test_public_api` | start/stop, feed status, execute-argv, dumb-terminal mode, disabled built-ins |
| `test_coverage_edges` | invalid configuration, error mapping, help, masking, completion, history, and lifecycle branches |
| `test_fuzz_stream` | deterministic randomized chunk stream with canaries and state invariants |
| `test_tcp_port` | isolated sessions, fragmented RX, partial/backpressured TX, close/error states |
| `test_baremetal_port` | nonblocking RX polling, limits, errors, TX status mapping |
| `test_freertos_port` | fake-kernel config, ISR overflow, TX pressure, run/stop and stats |
| `host_terminal_script` | host executable script mode and expected command flow |
| `test_large_args_extended` | same argument-boundary source rebuilt with `SH_MAX_LINE_LEN=512`, `SH_MAX_ARGC=64` |

`test_baremetal_port` is registered only when
`SH_BUILD_BAREMETAL_PORT=ON`. `test_tcp_port` requires
`SH_BUILD_TCP_PORT=ON`, and `host_terminal_script` requires both examples and
the host port. `test_freertos_port` always compiles the adapter against the
repository fakes when tests are enabled. `freertos_reference_compile` is an
object build check, not a CTest case. `test_large_args_extended` links a second
core build whose public compile definitions are 512/64.

The current CMake test graph does not compile or execute:

- a real FreeRTOS kernel/target test;
- real BSD/lwIP/FreeRTOS+TCP sockets or a Telnet protocol adapter;
- host raw-terminal key translation as an automated UI test;
- real UART, USB CDC, DMA, or network hardware;
- concurrent scheduler/race behavior under a real RTOS or network stack;
- a local sanitizer runtime on the current MinGW installation.

Those are test gaps, not implied passing areas.

`shell/tests/test_coverage_edges.c` is registered in CTest and is included in
the 26-test verified snapshot.

## Test Input And Output Pattern

Most core tests use `shell/tests/test_common.h`. The output callback appends
bytes to a fixed test buffer. Tests then feed bytes or complete lines and check
all three observable layers:

1. returned status;
2. shell state (`line`, cursor, history, count);
3. emitted byte stream.

A minimal new test follows this pattern:

```c
#include "test_common.h"

static int cmd_ok(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return sh_puts(shell, "OK\r\n");
}

int main(void)
{
    static const sh_cmd_t commands[] = {
        SH_CMD_ARG("ok", "", "Return OK", cmd_ok, 1u, 1u),
    };
    static const unsigned char input[] = { 'o', 'k', '\r' };
    test_shell_t test;

    test_shell_init(&test);
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&test.shell, commands,
                                       SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_OK, sh_feed(&test.shell, input, sizeof(input)));
    ASSERT_CONTAINS(test.out.data, "OK\r\n");
    ASSERT_EQ_INT(1, (int)sh_history_count(&test.shell));
    ASSERT_STREQ("ok", sh_history_get_newest(&test.shell, 0u));
    return 0;
}
```

When adding a test source named `shell/tests/test_example.c`, register it in the
existing CMake test block with:

```cmake
add_shell_test(test_example)
```

Transport tests should use a scripted fake callback that records:

- each callback invocation;
- requested and accepted lengths;
- exact byte order;
- callback status;
- flush count.

Always test partial progress combined with `SH_TRANSPORT_WOULD_BLOCK` and
`SH_TRANSPORT_ERR_IO`, not only all-or-nothing writes.

## Required Boundary Cases

Core parser/editor changes should preserve tests for:

- empty input and whitespace-only input;
- `line_max - 1`, `line_max`, and one byte beyond `line_max`;
- `argc_max - 1`, `argc_max`, and one token beyond `argc_max`;
- empty quoted arguments, concatenated quoted fragments, escaped spaces;
- trailing backslash and unterminated quotes;
- CR, LF, and CRLF split across separate `sh_input()` calls;
- ANSI sequences split at every byte boundary;
- malformed and overlong CSI recovery;
- editing at column zero, middle, and end;
- history empty, partially full, full, and wrapped;
- duplicate newest command and draft restoration;
- exact, ambiguous, missing, nested, hidden, and dynamic completion;
- command registration collisions and malformed subcommand trees;
- exact argument-count boundaries including `SH_ARGS_ANY`;
- secret absence from output and history;
- no-history commands;
- full, partial, stalled, would-block, and failed writes.

For large argument configurations, rebuild the library and tests with the same
target-wide compile definition used by firmware. A runtime `cfg.argc_max = 64`
cannot exceed a library compiled with `SH_MAX_ARGC = 32`.

## GCC/Clang Coverage

The current CMake project provides instrumentation option
`SH_ENABLE_COVERAGE`, default `OFF`. It adds `--coverage -O0` compile flags and
`--coverage` link flags for GCC or Clang, and rejects other compiler IDs. Use a
separate build so coverage objects do not contaminate release output:

```sh
cmake -S . -B build-coverage \
  -DSH_BUILD_TESTS=ON \
  -DSH_BUILD_EXAMPLES=OFF \
  -DSH_ENABLE_COVERAGE=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure
```

With GCC's `gcov` available, generate per-file line and branch reports from the
produced note/data files. One portable starting point is:

```sh
mkdir -p coverage-gcov
cd coverage-gcov
find ../build-coverage -name '*.gcno' -exec gcov -b -c '{}' \;
```

On PowerShell:

```powershell
New-Item -ItemType Directory -Force coverage-gcov | Out-Null
Push-Location coverage-gcov
Get-ChildItem ..\build-coverage -Recurse -Filter *.gcno | ForEach-Object {
    & gcov -b -c $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "gcov failed for $($_.FullName)" }
}
Pop-Location
```

Toolchain-specific object naming may require passing the object directory with
`gcov -o`. Inspect the coverage command output and ensure all files under
`shell/src` are represented. A report that only includes tests is invalid.

If `gcovr` is already part of the project's controlled toolchain, it can create
an aggregate report:

```sh
python -m gcovr \
  --root . \
  --filter 'shell/src/' \
  --exclude 'shell/tests/' \
  --branches \
  --html-details coverage.html \
  build-coverage
```

On Windows, the reproducible ASCII-staging command is:

```powershell
.\scripts\run_coverage.ps1
```

It runs the full suite, aggregates only `shell/src`, writes
`build_host_artifacts/coverage_report.txt`, and enforces at least 90% line and
85% branch coverage. The verified 2026-09-23 result is 1115/1157 lines
(96.37%) and 853/988 branches (86.34%).

## Sanitizers

The current CMake option `SH_ENABLE_SANITIZERS` adds AddressSanitizer,
UndefinedBehaviorSanitizer, and frame pointers for GCC/Clang. Use a separate
supported build:

```sh
cmake -S . -B build-sanitize \
  -DSH_BUILD_TESTS=ON \
  -DSH_BUILD_EXAMPLES=OFF \
  -DSH_ENABLE_SANITIZERS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-O1 -g'
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Sanitizer availability is compiler/platform dependent. An unsupported runtime
is not equivalent to a clean sanitizer run.

## GCC Static Analyzer

The current CMake option `SH_ENABLE_GCC_ANALYZER` adds GCC's `-fanalyzer` to
all compiled targets and rejects non-GNU compilers. Keep this check in its own
build directory:

```sh
cmake -S . -B build-analyzer \
  -DSH_BUILD_TESTS=ON \
  -DSH_BUILD_EXAMPLES=ON \
  -DSH_ENABLE_GCC_ANALYZER=ON
cmake --build build-analyzer
ctest --test-dir build-analyzer --output-on-failure
```

The 2026-09-23 Windows ASCII-staging run with GCC 13.2.0 completed without
analyzer diagnostics. Re-run `scripts/run_static_analysis.ps1` for each release
candidate because this result is source-revision specific.

## Fuzzing

The current repository has two fuzz-oriented targets:

- `test_fuzz_stream` deterministically generates 4096 randomized byte streams,
  feeds randomized chunk boundaries, and checks canaries plus shell/tokenizer
  invariants after every chunk;
- `shell/fuzz/fuzz_shell_input.c` exports `LLVMFuzzerTestOneInput` and is
  compiled as object target `sh_fuzz_shell_input` when
  `SH_BUILD_FUZZ_HARNESS=ON`.

The optional object target does not by itself create or run a libFuzzer
executable. A Clang/libFuzzer build must supply the sanitizer/fuzzer compile and
link flags and a corpus. Example toolchain integration:

```sh
cmake -S . -B build-fuzz \
  -DSH_BUILD_TESTS=OFF \
  -DSH_BUILD_EXAMPLES=OFF \
  -DSH_BUILD_FUZZ_HARNESS=ON \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=fuzzer,address,undefined'
cmake --build build-fuzz --target sh_fuzz_shell_input
```

Because the current CMake target is an object library, a consuming executable
and `-fsanitize=fuzzer` link step are still required. Do not record a fuzz pass
from compilation alone.

Additional high-value fuzz targets are command-tree validation and scripted
transport status/progress sequences.

Useful invariants include:

- buffers always remain NUL-terminated;
- `cursor <= line_len < line_capacity`;
- history indices stay within configured slots;
- callback progress never advances beyond requested length;
- secrets never occur in captured output/history;
- malformed input recovers for the next valid command.

## FreeRTOS And Hardware Tests

A production FreeRTOS port needs tests beyond the host suite:

- compile adapter against the exact FreeRTOS version/configuration;
- RX ISR partial/full StreamBuffer behavior;
- `portYIELD_FROM_ISR` wakeup path;
- stop latency at the configured finite read timeout;
- second simultaneous `sh_freertos_run()` returns `SH_ERR_BUSY`;
- stats saturation/reset rules;
- TX saturation and measured worst-case handler latency;
- UART FIFO/DMA overflow visibility;
- shell and TX task stack high-water marks;
- sustained paste and slow-terminal tests;
- reset/disconnect recovery without stale ANSI state.

For network transports, add partial `send`, receive fragmentation, half-close,
reset, slow-client, idle-timeout, authentication, and multi-session isolation
tests. Telnet needs IAC sequences split at every receive boundary.

## Recording A Verifiable Result

For every release candidate, record:

```text
Commit/revision:
Compiler and version:
CMake generator:
Compile definitions:
Configure command:
Build command and result:
CTest command and pass/total count:
Coverage tool/version and line/branch totals:
Sanitizer command and result:
Target board, UART/network driver, and firmware revision:
Manual acceptance operator/date/result:
Known skipped tests or limitations:
```

Keep command output or CI artifacts with the report. A statement such as
"covered by tests" is insufficient without the exact build configuration and
test result.
