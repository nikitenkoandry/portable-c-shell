# Porting Guide

This document describes the public API that exists in the current headers:

- `shell/include/sh_shell.h`
- `shell/include/sh_command.h`
- `shell/include/sh_transport.h`
- `shell/ports/baremetal/sh_port_baremetal.h`
- `shell/ports/freertos/sh_port_freertos.h`
- `shell/ports/tcp/sh_port_tcp.h`
- `shell/ports/host/sh_port_host.h`

The shell core is C99, does not create tasks, does not allocate heap memory,
and does not know about UART, USB CDC, TCP, interrupts, or an RTOS. The
application owns storage, scheduling, byte transport, and synchronization.

## Choose An Integration

| Environment | Adapter or entry point | Reference |
|---|---|---|
| Cooperative loop / bare metal | `sh_port_baremetal_t` | `shell/examples/baremetal_polling_shell.c` |
| FreeRTOS UART, USB CDC, or network UART | `sh_freertos_port_t` | `docs/freertos.md` |
| One raw TCP shell per client | `sh_tcp_session_t` | `docs/tcp.md` |
| Windows or POSIX terminal | `sh_host_terminal_t` | `shell/examples/host_terminal_shell.c` |
| Existing application queue | `sh_feed()` / `sh_feed_byte()` | Direct RX Feeding below |

Every adapter preserves the same core boundary: the driver receives bytes,
the shell owner feeds them to one `sh_t`, and output leaves through
`sh_transport_t`. UART, DMA, socket, task, authentication, and reconnect policy
remain application responsibilities.

The current lifecycle/input API also provides `sh_start()`, `sh_stop()`,
`sh_feed()`, `sh_feed_byte()`, and `sh_execute_argv()`. The older
`sh_input()`/`sh_input_byte()` names remain public; `sh_feed()` and
`sh_feed_byte()` are status-preserving aliases.

## Integration Boundary

An integration has four responsibilities:

1. Allocate one `sh_t` and all buffers for each independent shell session.
2. Deliver received bytes, in order, through `sh_input_byte()` or `sh_input()`.
3. Provide output through either `sh_config_t.transport` or the legacy
   `sh_config_t.write` callback.
4. Guarantee that only one execution context mutates a given `sh_t`.

`cfg.transport` takes precedence when it is non-NULL. The `cfg.write` callback
has no error or backpressure result and is therefore best limited to simple
blocking/debug ports. New production integrations should normally use
`sh_transport_t`.

Command handlers should write with `sh_write()`, `sh_puts()`, or `sh_printf()`.
They should not call `shell->cfg.write` directly: that callback may be NULL when
the shell is configured with `cfg.transport`.

## Initialization Order

Use this order for every session:

1. Initialize the application transport context.
2. Fill `sh_config_t` by calling `sh_default_config()` first.
3. Override limits, prompt, output, context, and masking policy.
4. Call `sh_init_static()` or `sh_init()` and check its result.
5. Call `sh_register_commands()` and check its result.
6. Enable RX only after the session is ready.
7. Call `sh_start()` once and check that the prompt was written.
8. Feed bytes until the session closes.
9. Call `sh_stop()` to reset ANSI decoder state and flush the transport.

`sh_start()` returns `SH_ERR_BUSY` when already started. `sh_stop()` is
idempotent with respect to the lifecycle flag and returns the transport flush
status. `sh_prompt()` remains available as a low-level void API for adapters
that own a different lifecycle, including the current FreeRTOS and TCP ports.

`sh_default_config()` currently enables echo, ANSI output, built-ins, and
secure clearing. Set `cfg.ansi_enabled`, `cfg.builtins_enabled`, and
`cfg.secure_clear_enabled` explicitly when product policy differs from those
defaults.

The command arrays and all strings referenced by them must remain valid for the
entire registered lifetime. Static `const` tables are the intended model.

## Static Memory

For these runtime values:

- `L = cfg.line_max`
- `A = cfg.argc_max`
- `H = cfg.history_depth`

the effective shell buffers are:

| Buffer | Required capacity |
|---|---:|
| Editable line | `L + 1` bytes |
| Argument vector | `A * sizeof(char *)` bytes |
| History | `H * (L + 1)` bytes |
| History draft | `L + 1` bytes when `H > 0` |

The trailing byte in every text slot is for `NUL`. `sh_init()` rejects a line
buffer, history slot, or draft that cannot hold `line_max + 1` bytes.

`SH_STORAGE_DEFINE(name, L, A, H)` declares all caller-owned storage and a
`const sh_storage_t`. Its exact buffer allocation is:

```text
(L + 1)
+ A * sizeof(char *)
+ max(H, 1) * (L + 1)
+ (L + 1)
```

The macro intentionally still declares one history slot and one draft when
`H == 0`; both are disconnected from the shell at initialization. To remove
those unused bytes in a history-free build, call `sh_init()` directly with
`history_buffer = NULL`, `history_depth = 0`, `history_slot_size = 0`,
`draft_buffer = NULL`, and `draft_buffer_size = 0`.

The storage calculation above excludes `sizeof(sh_t)`, command tables,
transport queues, task stacks, driver buffers, and temporary stack arrays used
inside the core. Compile-time ceilings still apply:

```text
L <= SH_MAX_LINE_LEN
A <= SH_MAX_ARGC
command nesting <= SH_MAX_CMD_DEPTH
```

Override those macros consistently for every translation unit that includes
the shell headers, for example through target-wide compiler definitions:

```cmake
target_compile_definitions(my_firmware PRIVATE
    SH_MAX_LINE_LEN=512
    SH_MAX_ARGC=64
    SH_HISTORY_DEFAULT_DEPTH=32)
```

Increasing these values also increases stack use in parsing, completion, and
history processing. Measure the final task high-water mark on the target.

The always-built `sh_memory_report` executable prints `sizeof(sh_t)`, pointer
size, descriptor sizes, and several buffer profiles for the active compiler:

```sh
cmake --build build --target sh_memory_report
./build/sh_memory_report
```

Use its output from the actual target ABI when budgeting RAM; host pointer and
alignment sizes may differ from the MCU.

## Complete Bare-Metal UART Pattern

The current repository provides `sh_port_baremetal_t`. The following is a
complete integration translation unit. It compiles once the BSP supplies the
two functions declared in `board_uart.h`.

`board_uart.h`:

```c
#ifndef BOARD_UART_H
#define BOARD_UART_H

#include <stddef.h>
#include <stdint.h>

/* Return 1 for one received byte, 0 for no byte, and -1 for a driver error. */
int board_uart_try_read(uint8_t *byte_out);

/*
 * Set *written to 0..len. Return 1 when the driver accepted data, 0 when the
 * TX path is temporarily full, and -1 on a permanent I/O error.
 */
int board_uart_try_write(const uint8_t *data, size_t len, size_t *written);

#endif
```

`app_shell.c`:

```c
#include "board_uart.h"
#include "sh_port_baremetal.h"
#include "sh_shell.h"

#include <stddef.h>
#include <stdint.h>

enum {
    APP_SHELL_LINE_MAX = 192,
    APP_SHELL_ARGC_MAX = 32,
    APP_SHELL_HISTORY_DEPTH = 24,
    APP_SHELL_RX_BUDGET = 32
};

static sh_t g_shell;
static sh_port_baremetal_t g_port;
SH_STORAGE_DEFINE(g_shell_storage,
                  APP_SHELL_LINE_MAX,
                  APP_SHELL_ARGC_MAX,
                  APP_SHELL_HISTORY_DEPTH);

static int cmd_status(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return sh_printf(shell, "history=%u/%u\r\n",
                     (unsigned)sh_history_count(shell),
                     (unsigned)sh_history_capacity(shell));
}

static int cmd_set_name(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)user_ctx;
    /* SH_CMD_ARG below guarantees argc == 2. */
    return sh_printf(shell, "name=%s\r\n", argv[1]);
}

static int cmd_set_token(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    /* Do not print the credential. */
    return sh_puts(shell, "token accepted\r\n");
}

static const sh_cmd_t set_commands[] = {
    SH_CMD_ARG("name", "<text>", "Set device name", cmd_set_name, 2u, 2u),
    SH_CMD_SENSITIVE_ARG("token", "<value>", "Set access token",
                         cmd_set_token, 2u, 2u),
};

static const sh_cmd_t root_commands[] = {
    SH_CMD("status", "", "Show shell status", cmd_status),
    SH_CMD_SUB("set", "", "Change settings", set_commands),
};

static sh_port_baremetal_read_status_t uart_read(void *ctx,
                                                  uint8_t *byte_out)
{
    int rc;
    (void)ctx;
    rc = board_uart_try_read(byte_out);
    if (rc > 0) {
        return SH_PORT_BAREMETAL_READ_BYTE;
    }
    if (rc == 0) {
        return SH_PORT_BAREMETAL_READ_NO_DATA;
    }
    return SH_PORT_BAREMETAL_READ_ERROR;
}

static sh_port_baremetal_write_status_t uart_write(void *ctx,
                                                    const uint8_t *data,
                                                    size_t len,
                                                    size_t *written)
{
    int rc;
    (void)ctx;
    rc = board_uart_try_write(data, len, written);
    if (rc > 0) {
        return SH_PORT_BAREMETAL_WRITE_OK;
    }
    if (rc == 0) {
        return SH_PORT_BAREMETAL_WRITE_WOULD_BLOCK;
    }
    return SH_PORT_BAREMETAL_WRITE_ERROR;
}

int app_shell_init(void)
{
    sh_config_t cfg;
    int status;

    status = sh_port_baremetal_init(&g_port, &g_shell,
                                    uart_read, NULL,
                                    uart_write, NULL);
    if (status != SH_PORT_BAREMETAL_OK) {
        return status;
    }

    sh_default_config(&cfg);
    cfg.prompt = "device> ";
    cfg.line_max = APP_SHELL_LINE_MAX;
    cfg.argc_max = APP_SHELL_ARGC_MAX;
    cfg.history_depth = APP_SHELL_HISTORY_DEPTH;
    cfg.transport = sh_port_baremetal_transport(&g_port);
    cfg.transport_write_attempts = 8u;

    status = sh_init_static(&g_shell, &cfg, &g_shell_storage);
    if (status != SH_OK) {
        return status;
    }

    status = sh_register_commands(&g_shell, root_commands,
                                  SH_ARRAY_SIZE(root_commands));
    if (status != SH_OK) {
        return status;
    }

    return sh_start(&g_shell);
}

void app_shell_poll(void)
{
    sh_port_baremetal_poll_result_t result;

    result = sh_port_baremetal_poll(&g_port, APP_SHELL_RX_BUDGET);
    if (result.status == SH_PORT_BAREMETAL_ERR_READ) {
        /* Record/recover the UART error in the application diagnostics. */
    }
}

int app_shell_stop(void)
{
    return sh_stop(&g_shell);
}
```

Add both implementation files to the firmware target:

```cmake
target_sources(my_firmware PRIVATE
    shell/src/sh_shell.c
    shell/src/sh_tokenizer.c
    shell/src/sh_command.c
    shell/src/sh_history.c
    shell/src/sh_completion.c
    shell/src/sh_ansi.c
    shell/src/sh_transport.c
    shell/ports/baremetal/sh_port_baremetal.c
    app_shell.c)

target_include_directories(my_firmware PRIVATE
    shell/include
    shell/ports/baremetal)
```

`app_shell_poll()` is nonblocking. Call it frequently enough that the hardware
RX FIFO cannot overflow. `APP_SHELL_RX_BUDGET` bounds work per scheduler pass.

## Direct RX Feeding

An application may omit the bare-metal helper and feed its own queue:

```c
void shell_service_rx(sh_t *shell)
{
    uint8_t chunk[32];
    size_t received;

    while ((received = app_rx_queue_read(chunk, sizeof(chunk))) != 0u) {
        int status = sh_feed(shell, chunk, received);
        if (status != SH_OK) {
            /* Record the first parser/editor/handler status from this chunk. */
        }
    }
}
```

ANSI sequences may be split across calls; the decoder state is stored in
`sh_t`. Preserve byte order and do not transform ESC bytes. CR, LF, and CRLF
are accepted; CRLF executes once.

`sh_feed()` processes the entire supplied chunk and returns the first non-OK
status observed. A command handler's return value, line overflow, ambiguous
completion, and parser errors can therefore reach the RX loop. Do not treat
every non-OK status as transport corruption; classify it with
`sh_status_string()` and apply the product policy.

`sh_execute_line()` tokenizes, adds eligible history, and dispatches a text
line without terminal editing. `sh_execute_argv()` dispatches a caller-owned
argv directly; it does not tokenize or add history and rejects counts above
the shell's argv capacity.

## Ownership And Thread Safety

The public API does not contain a mutex. Treat each `sh_t` as single-owner
mutable state.

| Operation | Required owner |
|---|---|
| `sh_feed_byte`, `sh_feed` (`sh_input*` aliases) | Shell task/main loop only |
| `sh_execute_line` | Same shell owner only |
| `sh_execute_argv` | Same shell owner; caller keeps argv valid during call |
| `sh_prompt`, history APIs, echo setters | Same shell owner only |
| Command handlers | Run synchronously in the shell owner |
| UART/network ISR | Driver queue only; never call shell core |
| TX driver | May consume an application queue independently |

If another task needs to invoke a command, send a message to the shell owner or
call the underlying application service directly. A mutex around isolated API
calls is not sufficient if command handlers block or output is shared.

Long-running handlers should enqueue work and return. Otherwise input editing,
history navigation, and transport servicing stop until the handler returns.

For multiple UARTs or network clients, allocate one complete session per
connection. Never share one editable line, history buffer, or `sh_t` between
clients.

## Transport Contract

A `sh_transport_write_fn_t` callback must:

- set `*written` on every return;
- report a value from zero through `len`;
- preserve byte order;
- return `SH_TRANSPORT_OK` when progress was made normally;
- return `SH_TRANSPORT_WOULD_BLOCK` for temporary backpressure;
- return `SH_TRANSPORT_ERR_IO` for disconnect/permanent failure.

Partial progress is valid with any of those three statuses.
`sh_transport_write_all()` performs no allocation, sleep, or unbounded retry.
It makes at most `cfg.transport_write_attempts` immediate callback calls. The
default is 8, including when a supplied value is zero during initialization.

At the shell layer:

- complete output returns `SH_OK`;
- temporary backpressure or exhausted attempts returns `SH_ERR_WOULD_BLOCK`;
- protocol and I/O errors return `SH_ERR_TRANSPORT`.

Internal editor/help output does not expose its write status to the caller.
Size the TX queue for the largest expected burst or use a bounded blocking
transport. Application handlers can and should inspect the return from
`sh_puts()` and `sh_printf()`.

`sh_printf()` uses a 256-byte local formatting buffer. If formatted output
needs 256 bytes or more, it writes the first 255 bytes and returns
`SH_ERR_TRUNCATED`. Split large output into bounded records or use
`sh_write()`.

## Commands And Large Argument Counts

Handler arguments follow this exact contract:

```text
argv[0] = resolved leaf command name
argv[1]..argv[argc - 1] = payload arguments
```

Parent command names are not included. For `net route add 10.0.0.0/8`, a
handler attached to `add` receives `argv[0] == "add"` and
`argv[1] == "10.0.0.0/8"`.

Argument limits in command descriptors include `argv[0]`. Therefore exactly
20 payload values require `min_args = 21` and `max_args = 21`:

```c
static const sh_cmd_t commands[] = {
    SH_CMD_ARG("bulk", "<v1> ... <v20>", "Apply twenty values",
               cmd_bulk, 21u, 21u),
    SH_CMD_ARG("many", "<values...>", "Apply one or more values",
               cmd_many, 2u, SH_ARGS_ANY),
};
```

`SH_CMD()` uses `min_args = 1` and `max_args = SH_ARGS_ANY`. Use
`SH_CMD_ARG()` when count validation matters. Runtime `cfg.argc_max`, argv
storage capacity, and compile-time `SH_MAX_ARGC` must all be large enough. A
shell configured for 64 total arguments needs all three set to at least 64;
the command name consumes one slot.

The always-built `sh_lookup_benchmark` measures the current linear root-command
lookup for 16, 64, 128, and 256 commands using host `clock()` timing:

```sh
cmake --build build --target sh_lookup_benchmark
./build/sh_lookup_benchmark
```

Treat it as a comparative host benchmark, not a target latency guarantee.
Measure the actual MCU/compiler and worst-case command position before setting
a command-count performance limit.

Other registration rules enforced by `sh_register_commands()`:

- names must be nonempty and contain no whitespace/control bytes;
- sibling names must be unique;
- root names `clear`, `echo`, `help`, and `history` are reserved while
  `cfg.builtins_enabled` is true;
- a subcommand pointer and count must agree;
- command depth cannot exceed `SH_MAX_CMD_DEPTH`;
- a handler descriptor cannot use `min_args == 0`;
- finite `max_args` cannot be less than `min_args`.

Per-command `user_ctx` overrides `sh_config_t.user_ctx`; when it is NULL, the
handler and completion provider receive the shell-wide context.

Large projects can register independently owned root tables without copying
them into one array:

```c
static const sh_cmd_t system_commands[] = {
    SH_CMD_ARG("status", "", "Show status", cmd_status, 1u, 1u),
};

static const sh_cmd_t network_commands[] = {
    SH_CMD_SUB("net", "", "Network commands", net_subcommands),
};

static const sh_command_set_t command_sets[] = {
    SH_COMMAND_SET(system_commands),
    SH_COMMAND_SET(network_commands),
};

int status = sh_register_command_sets(shell, command_sets,
                                      SH_ARRAY_SIZE(command_sets));
```

`sh_register_command_sets()` validates every table and rejects duplicate root
names across sets. It replaces a prior flat registration; conversely,
`sh_register_commands()` replaces prior command sets. Both use explicit
counts. `SH_CMD_END` exists as a descriptor initializer, but the current
registration API is not sentinel-scanning: never include that NULL-name entry
in the count passed to either registration function.

Built-ins default from compile-time `SH_ENABLE_BUILTINS` and may be changed per
session with `cfg.builtins_enabled` before initialization. When disabled, their
reserved names become available to the application. `cfg.ansi_enabled = false`
selects append-only redraws and suppresses ANSI clear/cursor output for dumb
terminals; it does not disable recognition of incoming editor keys.

## History, Editing, Completion, And Secrets

History is a fixed-depth ring:

- `sh_history_capacity()` returns configured slots;
- `sh_history_count()` returns filled slots;
- consecutive duplicate lines are not added twice;
- the oldest entry is overwritten when full;
- `sh_history_get_newest(shell, 0)` returns the newest entry;
- `sh_history_clear()` resets entries, navigation, and the saved draft.

Arrow Up selects newer-to-older history; Arrow Down moves back toward the
saved draft. Editing a recalled line does not mutate the stored entry.

Tab completion operates at the cursor. One match completes the token and adds
a space. Multiple matches extend the common prefix; if no further extension is
possible, up to `SH_COMPLETION_MAX_MATCHES` names are printed. Hidden commands
are excluded. A completion provider is called with a zero-based candidate
index until it returns NULL or reaches `SH_COMPLETION_PROVIDER_MAX_ITEMS`.
`arg_index` is the payload position where 1 identifies the first payload
argument.

```c
static const char *mode_complete(sh_t *shell, size_t arg_index,
                                 size_t candidate_index, void *user_ctx)
{
    static const char *const modes[] = { "fast", "safe", "service" };
    (void)shell;
    (void)user_ctx;
    if (arg_index != 1u || candidate_index >= SH_ARRAY_SIZE(modes)) {
        return NULL;
    }
    return modes[candidate_index];
}

static const sh_cmd_t commands[] = {
    SH_CMD_COMPLETE("mode", "<name>", "Select mode",
                    cmd_mode, mode_complete),
};
```

`SH_CMD_SENSITIVE` and `SH_CMD_SENSITIVE_ARG` mask every payload argument while
the line is redrawn and store `<hidden>` in history. The handler still receives
the original value. `SH_CMD_NO_HISTORY` stores no line at all.

For selective masking, set `cfg.should_mask_arg`. It receives the resolved
space-separated command path, the token index from the original parsed line,
the unmasked token, and `cfg.mask_ctx`. Returning true masks that token in both
editor output and history.

`cfg.secure_clear_enabled` defaults to true. In the current core it uses a
volatile byte loop to clear the full editable line and local execution buffer
after Enter, clears line/draft on Ctrl+C or Ctrl+U, and clears parser/history
temporaries plus the argv pointer array when `sh_execute_line()` exits.

This is bounded cleanup, not universal credential zeroization. It does not
clear application RX/DMA buffers, transport queues, the caller's string passed
to `sh_execute_line()`, data copied by a handler, or all stale bytes immediately
after every Backspace/Delete edit. `sh_stop()` resets lifecycle/ANSI state and
flushes output but does not erase line/history storage. Before destroying or
reusing a session, the owner should explicitly wipe its complete caller-owned
storage according to the platform's non-optimizable secure-clear policy.

Masking and secure clearing are complementary. The secret still reaches the
handler in cleartext. Do not log it, retain it unnecessarily, or include it in
crash dumps. Disabling `secure_clear_enabled` is appropriate only when the
product has accepted the residual-data risk or provides stronger external
clearing.

## Supported Input Controls

The current editor recognizes:

| Input | Behavior |
|---|---|
| Up / Down | Navigate history and restore draft |
| Left / Right | Move cursor |
| Home / End | Move to line start/end |
| Delete | Delete at cursor |
| Backspace or DEL | Delete before cursor |
| Tab | Complete command/subcommand/argument |
| Ctrl+C | Cancel line and print a new prompt |
| Ctrl+U | Clear the input line |
| Ctrl+W | Delete the word left of cursor |
| Ctrl+L | Clear screen and redraw |

Recognized ANSI encodings are `ESC [ A/B/C/D`, `ESC [ H/F`, and
`ESC [ 1~/3~/4~`. A host, UART terminal, or network protocol adapter must
deliver these bytes unchanged.

## Port Completion Checklist

- All initialization and registration return values are checked.
- RX is disabled until shell and transport initialization complete.
- Exactly one context owns `sh_t` and invokes handlers.
- ISR code only enqueues RX and wakes the owner.
- TX callback handles partial writes and reports `*written` correctly.
- Backpressure has a bounded, measured policy.
- Each network client has independent state and history.
- Compile-time limits are target-wide and storage dimensions match runtime.
- Sensitive commands never echo or log cleartext payloads.
- `secure_clear_enabled` policy and final session-storage wipe are verified.
- Command handlers have bounded execution time or delegate work.
- Terminal behavior is accepted using `docs/manual_acceptance.md`.
- Host and target tests are run using `docs/testing.md`.
