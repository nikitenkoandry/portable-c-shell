# Portable C Shell

[![CI](https://github.com/nikitenkoandry/portable-c-shell/actions/workflows/ci.yml/badge.svg)](https://github.com/nikitenkoandry/portable-c-shell/actions/workflows/ci.yml)

A C99, heap-free shell core with Zephyr-like command ergonomics for FreeRTOS,
bare-metal firmware, UART/USB CDC, raw TCP sessions, and desktop terminals.
The core has no OS, socket, UART, or hardware dependencies.

Start here:

- [Add commands](docs/adding_commands.md)
- [Port to a new target or transport](docs/porting.md)
- [Integrate with FreeRTOS](docs/freertos.md)
- [Build, test, analyze, and measure coverage](docs/testing.md)
- [Release history](CHANGELOG.md)

## Features

- Static root and nested command trees, plus independent command sets.
- Zephyr-style handler contract: leaf name in `argv[0]`.
- Runtime argument validation and default/extended 32/64 argument profiles.
- Quoted and escaped arguments with deterministic error recovery.
- Caller-owned history ring with capacity/count, Up/Down, and draft restore.
- Cursor-aware Tab completion, common prefixes, bounded candidate display, and
  dynamic argument providers.
- Left/Right/Home/End/Delete/Backspace and Ctrl+C/U/W/L line editing.
- Sensitive input masking, redacted or disabled history, and secure buffer
  clearing.
- Partial writes, backpressure, flush, and transport error propagation.
- Raw Windows/POSIX terminal, FreeRTOS StreamBuffer, bare-metal polling, and
  platform-neutral raw TCP adapters.

## Build And Try

The Windows helper stages the source in an ASCII-only temporary path, builds
with strict warnings, runs all CTest targets, and copies runnable tools back:

```powershell
.\scripts\build_host.ps1
.\build_host_artifacts\host_terminal_shell.exe
```

Useful terminal checks:

```text
<Tab>
w<Tab>
wifi <Tab>
wifi s<Tab>
wifi set ssid "My Home WiFi"
wifi set pass secret
history
history status
tag config set interval 1000
args a01 a02 a03 a04
echo off
echo on
exit
```

Arrow Up/Down navigates history. Home, End, Delete, Backspace, Ctrl+C, Ctrl+U,
Ctrl+W, and Ctrl+L are supported.

The same runner supports deterministic and dumb-terminal modes:

```powershell
.\build_host_artifacts\host_terminal_shell.exe --limits
.\build_host_artifacts\host_terminal_shell.exe --no-ansi --script shell\tests\fixtures\host_script.txt
.\build_host_artifacts\sh_memory_report.exe
.\build_host_artifacts\sh_lookup_benchmark.exe
```

## Embed The Core

```c
#include "sh_shell.h"

SH_STORAGE_DEFINE(app_storage, 128u, 16u, 8u);

static const sh_cmd_t app_commands[] = {
    SH_CMD_ARG("status", "", "Show status", status_handler, 1u, 1u),
};

static sh_t app_shell;

int app_shell_init(const sh_transport_t *transport)
{
    sh_config_t config;

    sh_default_config(&config);
    config.prompt = "device> ";
    config.line_max = 128u;
    config.argc_max = 16u;
    config.history_depth = 8u;
    config.transport = transport;

    if (sh_init_static(&app_shell, &config, &app_storage) != SH_OK) {
        return -1;
    }
    if (sh_register_commands(&app_shell, app_commands,
                             SH_ARRAY_SIZE(app_commands)) != SH_OK) {
        return -1;
    }
    return sh_start(&app_shell);
}

void app_uart_rx(const uint8_t *data, size_t length)
{
    (void)sh_feed(&app_shell, data, length);
}
```

Use one `sh_t` and one storage block per UART or network session. Exactly one
execution context must mutate a session. Production ports should use
`sh_transport_t`; the legacy void `cfg.write` callback is intended for
simple host/test output.

## Add Commands

A handler receives the resolved leaf name in `argv[0]`; payload starts at
`argv[1]`. Argument limits include that leaf entry:

```c
static int cmd_version(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return sh_puts(shell, "firmware 1.0.0\r\n");
}

static const sh_cmd_t system_commands[] = {
    SH_CMD_ARG("version", "", "Show firmware version",
               cmd_version, 1u, 1u),
};
```

Use `SH_CMD_SUB` for nested trees, `SH_CMD_SENSITIVE_ARG` for passwords or
tokens, and `SH_CMD_COMPLETE` for argument-specific Tab candidates. The full
[command guide](docs/adding_commands.md) covers modular command sets, contexts,
large argument counts, completion, masking, and unit tests.

## Port To A Target

The core only needs ordered RX bytes and an output callback. Choose the
smallest adapter matching the application:

| Target | Adapter |
|---|---|
| Main loop or bare metal | `shell/ports/baremetal` |
| FreeRTOS task plus ISR RX | `shell/ports/freertos` |
| Independent raw TCP clients | `shell/ports/tcp` |
| Windows/POSIX terminal | `shell/ports/host` |

Keep one `sh_t` and one storage block per session. ISR and driver callbacks
enqueue bytes; exactly one task or loop owns parsing and command execution.
See the [porting guide](docs/porting.md) for storage sizing, initialization
order, transport backpressure, thread ownership, and a complete UART example.

## Verification

```powershell
.\scripts\build_host.ps1
.\scripts\run_coverage.ps1
.\scripts\run_static_analysis.ps1
.\scripts\run_sanitizers.ps1
```

For a normal Linux/macOS/ASCII-path build:

```sh
cmake -S . -B build -DSH_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The coverage script enforces 90% core line and 85% core branch coverage. The
sanitizer script requires a GCC/Clang installation that provides ASan and UBSan
runtime libraries; the Linux CI job supplies them.

## Documentation

The public headers contain Doxygen contracts for every API function, callback,
ownership rule, execution context, and return status. Generate the browsable
reference locally with:

```powershell
doxygen docs\Doxyfile
start build-docs\html\index.html
```

- [Porting guide](docs/porting.md)
- [Adding commands](docs/adding_commands.md)
- [FreeRTOS integration](docs/freertos.md)
- [Raw TCP sessions](docs/tcp.md)
- [Testing and coverage](docs/testing.md)
- [Manual acceptance](docs/manual_acceptance.md)
- [Detailed production specification](docs/production_ready_spec_ru.md)

Raw TCP is deliberately not called Telnet: Telnet negotiation bytes must be
filtered by a separate protocol adapter. TLS, authentication policy, UART
drivers, task creation, and asynchronous log serialization remain application
responsibilities.
