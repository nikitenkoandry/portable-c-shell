# FreeRTOS Integration

The current FreeRTOS adapter is implemented in:

- `shell/ports/freertos/sh_port_freertos.h`
- `shell/ports/freertos/sh_port_freertos.c`

It uses one RX StreamBuffer and one TX StreamBuffer. It does not create tasks,
create stream buffers, configure a UART, or drain TX. Those objects remain
application-owned.

The current top-level CMake exposes `SH_BUILD_FREERTOS_PORT`, which defaults to
`OFF` because a normal host build does not have production FreeRTOS headers.
Enable it for a firmware/toolchain build and provide the include directories:

```sh
cmake -S . -B build-freertos \
  -DSH_BUILD_FREERTOS_PORT=ON \
  -DSH_FREERTOS_INCLUDE_DIRS='/path/to/FreeRTOS/include;/path/to/portable'
```

The resulting target is `sh_port_freertos`. The application may alternatively
add `shell/ports/freertos/sh_port_freertos.c` to its existing firmware target.

## Required Execution Model

FreeRTOS StreamBuffer objects are single-producer/single-consumer. The adapter
requires this topology:

| Object | Single producer | Single consumer |
|---|---|---|
| RX StreamBuffer | One UART ISR/DMA completion source | Shell task |
| TX StreamBuffer | Shell task through `sh_write()` | One UART TX task/driver |
| `sh_t` | Shell task only | Shell task only |

Command handlers run synchronously in the shell task. A handler must send a
request to a worker task for slow flash, network, sensor, or filesystem work.
Other tasks must not call `sh_input()`, `sh_execute_line()`, history functions,
or shell output functions on the same instance.

If RX has multiple producers or TX has multiple consumers, serialize them in
application code or replace StreamBuffer with an appropriate queue. The adapter
does not add a mutex.

## Complete Static UART Pattern

The example below uses only statically allocated shell storage, StreamBuffer
control blocks/storage, and task stacks. It requires
`configSUPPORT_STATIC_ALLOCATION == 1` and a BSP implementation of
`board_uart.h`.

`board_uart.h`:

```c
#ifndef BOARD_UART_H
#define BOARD_UART_H

#include <stddef.h>
#include <stdint.h>

/* Called by the TX task. Return 0 on success and a negative value on error. */
int board_uart_write_all(const uint8_t *data, size_t len);

/* Enable RX interrupts/DMA only after the shell has been initialized. */
void board_uart_enable_rx(void);

#endif
```

`app_shell_freertos.c`:

```c
#include "board_uart.h"
#include "sh_port_freertos.h"
#include "sh_shell.h"

#include <stddef.h>
#include <stdint.h>

enum {
    APP_LINE_MAX = 256,
    APP_ARGC_MAX = 32,
    APP_HISTORY_DEPTH = 24,
    APP_RX_STREAM_BYTES = 512,
    APP_TX_STREAM_BYTES = 2048,
    APP_SHELL_STACK_WORDS = 768,
    APP_TX_STACK_WORDS = 384,
    APP_SHELL_PRIORITY = tskIDLE_PRIORITY + 2,
    APP_TX_PRIORITY = tskIDLE_PRIORITY + 3
};

static sh_t g_shell;
static sh_freertos_port_t g_shell_port;
SH_STORAGE_DEFINE(g_shell_storage,
                  APP_LINE_MAX,
                  APP_ARGC_MAX,
                  APP_HISTORY_DEPTH);

static StaticStreamBuffer_t g_rx_stream_control;
static StaticStreamBuffer_t g_tx_stream_control;
static uint8_t g_rx_stream_storage[APP_RX_STREAM_BYTES + 1u];
static uint8_t g_tx_stream_storage[APP_TX_STREAM_BYTES + 1u];
static StreamBufferHandle_t g_rx_stream;
static StreamBufferHandle_t g_tx_stream;

static StaticTask_t g_shell_task_control;
static StaticTask_t g_tx_task_control;
static StackType_t g_shell_task_stack[APP_SHELL_STACK_WORDS];
static StackType_t g_tx_task_stack[APP_TX_STACK_WORDS];

static int cmd_info(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    sh_freertos_stats_t stats;
    int status;

    (void)argc;
    (void)argv;
    (void)user_ctx;

    status = sh_freertos_get_stats(&g_shell_port, &stats);
    if (status != SH_OK) {
        return status;
    }
    return sh_printf(shell,
                     "rx=%lu delivered=%lu dropped=%lu "
                     "tx=%lu rejected=%lu\r\n",
                     (unsigned long)stats.rx_bytes_enqueued,
                     (unsigned long)stats.rx_bytes_delivered,
                     (unsigned long)stats.rx_overflow_bytes,
                     (unsigned long)stats.tx_bytes_enqueued,
                     (unsigned long)stats.tx_overflow_bytes);
}

static int cmd_set_name(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)user_ctx;
    /* Send argv[1] to a worker queue here if persistence may block. */
    return sh_printf(shell, "requested name=%s\r\n", argv[1]);
}

static int cmd_set_password(sh_t *shell, int argc, char **argv,
                            void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    /* Copy only into the credential service; never print argv[1]. */
    return sh_puts(shell, "password update requested\r\n");
}

static const sh_cmd_t set_commands[] = {
    SH_CMD_ARG("name", "<text>", "Set device name",
               cmd_set_name, 2u, 2u),
    SH_CMD_SENSITIVE_ARG("password", "<value>", "Set password",
                         cmd_set_password, 2u, 2u),
};

static const sh_cmd_t root_commands[] = {
    SH_CMD("info", "", "Show shell transport statistics", cmd_info),
    SH_CMD_SUB("set", "", "Change configuration", set_commands),
};

static void shell_task(void *argument)
{
    int status;
    (void)argument;

    status = sh_freertos_run(&g_shell_port, &g_shell);
    if (status != SH_OK) {
        /* Publish a product diagnostic before terminating this task. */
    }
    vTaskDelete(NULL);
}

static void uart_tx_task(void *argument)
{
    uint8_t data[128];
    (void)argument;

    for (;;) {
        size_t received = xStreamBufferReceive(g_tx_stream,
                                               data,
                                               sizeof(data),
                                               portMAX_DELAY);
        if (received != 0u) {
            int status = board_uart_write_all(data, received);
            if (status != 0) {
                /* Replace with the product's UART recovery policy. */
                vTaskDelete(NULL);
            }
        }
    }
}

int app_shell_start(void)
{
    sh_freertos_config_t port_cfg;
    sh_config_t shell_cfg;
    TaskHandle_t shell_task_handle;
    TaskHandle_t tx_task_handle;
    int status;

    g_rx_stream = xStreamBufferCreateStatic(sizeof(g_rx_stream_storage),
                                             1u,
                                             g_rx_stream_storage,
                                             &g_rx_stream_control);
    g_tx_stream = xStreamBufferCreateStatic(sizeof(g_tx_stream_storage),
                                             1u,
                                             g_tx_stream_storage,
                                             &g_tx_stream_control);
    if (g_rx_stream == NULL || g_tx_stream == NULL) {
        return SH_ERR_NO_MEMORY;
    }

    port_cfg.rx_stream = g_rx_stream;
    port_cfg.tx_stream = g_tx_stream;
    port_cfg.read_timeout_ticks = pdMS_TO_TICKS(50u);
    port_cfg.write_timeout_ticks = pdMS_TO_TICKS(20u);
    port_cfg.read_chunk_size = SH_FREERTOS_MAX_READ_CHUNK;

    status = sh_freertos_init(&g_shell_port, &port_cfg);
    if (status != SH_OK) {
        return status;
    }

    sh_default_config(&shell_cfg);
    shell_cfg.prompt = "device> ";
    shell_cfg.line_max = APP_LINE_MAX;
    shell_cfg.argc_max = APP_ARGC_MAX;
    shell_cfg.history_depth = APP_HISTORY_DEPTH;
    shell_cfg.transport = sh_freertos_transport(&g_shell_port);
    shell_cfg.transport_write_attempts = 8u;

    status = sh_init_static(&g_shell, &shell_cfg, &g_shell_storage);
    if (status != SH_OK) {
        return status;
    }

    status = sh_register_commands(&g_shell, root_commands,
                                  SH_ARRAY_SIZE(root_commands));
    if (status != SH_OK) {
        return status;
    }

    tx_task_handle = xTaskCreateStatic(uart_tx_task,
                                      "shell_tx",
                                      APP_TX_STACK_WORDS,
                                      NULL,
                                      APP_TX_PRIORITY,
                                      g_tx_task_stack,
                                      &g_tx_task_control);
    shell_task_handle = xTaskCreateStatic(shell_task,
                                         "shell",
                                         APP_SHELL_STACK_WORDS,
                                         NULL,
                                         APP_SHELL_PRIORITY,
                                         g_shell_task_stack,
                                         &g_shell_task_control);
    if (tx_task_handle == NULL || shell_task_handle == NULL) {
        return SH_ERR_NO_MEMORY;
    }

    board_uart_enable_rx();
    return SH_OK;
}

/* Call this from the one UART RX/DMA ISR producer. */
void app_shell_uart_rx_from_isr(const uint8_t *data, size_t len)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)sh_freertos_rx_from_isr(&g_shell_port,
                                  data,
                                  len,
                                  &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
```

The extra byte in each static StreamBuffer storage array is reserved by the
FreeRTOS ring implementation to distinguish full from empty. Passing
`sizeof(storage)` gives the intended usable capacity of
`APP_RX_STREAM_BYTES` or `APP_TX_STREAM_BYTES`. This is also the pattern used
by the repository's compile-checked `shell/examples/freertos_uart_shell.c`.

Add the adapter and shell core to the firmware target:

```cmake
target_sources(my_firmware PRIVATE
    shell/src/sh_shell.c
    shell/src/sh_tokenizer.c
    shell/src/sh_command.c
    shell/src/sh_history.c
    shell/src/sh_completion.c
    shell/src/sh_ansi.c
    shell/src/sh_transport.c
    shell/ports/freertos/sh_port_freertos.c
    app_shell_freertos.c)

target_include_directories(my_firmware PRIVATE
    shell/include
    shell/ports/freertos)
```

The FreeRTOS target must already export `FreeRTOS.h`, `task.h`, and
`stream_buffer.h` through its normal include path.

## Current Host-Side Adapter Tests

With `SH_BUILD_TESTS=ON`, the regular host build compiles the adapter against
the fakes in `shell/tests/freertos_fakes` and registers
`test_freertos_port`. That test exercises configuration rejection, ISR RX
partial/overflow accounting, TX partial progress/backpressure, run/stop state,
statistics, and transport identity. The build also compiles
`shell/examples/freertos_uart_shell.c` as the object target
`freertos_reference_compile`.

These checks validate adapter logic and reference-example compilation. They do
not validate a vendor UART, actual interrupt priority rules, DMA completion,
target timing, or task stack margin.

## RX Rules

`sh_freertos_rx_from_isr()`:

- accepts a complete received chunk;
- returns `pdPASS` only if all bytes were queued;
- returns `pdFAIL` for invalid input or partial/full overflow;
- records accepted and dropped byte counts;
- never calls shell core APIs.

Initialize `higher_priority_task_woken` to `pdFALSE` once per ISR entry, pass it
through all `...FromISR` calls, then execute the platform's normal
`portYIELD_FROM_ISR()` operation.

Do not enable the RX interrupt before `sh_freertos_init()`, `sh_init_static()`,
and command registration finish. If a DMA callback can run from task context
rather than ISR context, do not call an `...FromISR` API from that task; enqueue
through the appropriate task-context StreamBuffer API in an application-owned
adapter.

## TX And Backpressure

`sh_freertos_transport_write()` attempts to enqueue into the TX StreamBuffer
for at most `write_timeout_ticks` per callback invocation. The shell transport
helper may invoke it up to `cfg.transport_write_attempts` times for one
`sh_write()` call. Therefore a conservative upper bound for a fully blocked
write is approximately:

```text
write_timeout_ticks * transport_write_attempts
```

plus scheduler and driver overhead. Choose both values from the maximum shell
latency allowed by the product.

If only part of a record fits, the callback reports partial progress and
`SH_TRANSPORT_WOULD_BLOCK`. The helper retries the remaining suffix. If all
attempts are consumed, public output functions return `SH_ERR_WOULD_BLOCK`.
Internal prompt/help/editor writes do not propagate that status, so the TX
StreamBuffer must be sized for the largest expected burst.

Only the TX consumer touches the UART transmit driver. If the driver is DMA
based, `board_uart_write_all()` should wait on or coordinate with the driver's
completion primitive without allowing another producer to interleave bytes.

## Stop, Restart, And Diagnostics

`sh_freertos_run()` is a blocking task body. It prints the initial prompt,
receives bounded chunks, and returns after a stop request or an input error.
It does not delete the task.

```c
sh_freertos_request_stop(&g_shell_port);
```

The loop observes the request no later than one finite RX timeout when no data
arrives. `read_timeout_ticks` must be nonzero and cannot be `portMAX_DELAY`.

Before restarting the same task body:

```c
int status = sh_freertos_clear_stop(&g_shell_port);
```

It returns `SH_ERR_BUSY` if `sh_freertos_run()` is still active. Use
`sh_freertos_is_running()` only as diagnostics, not as a synchronization
primitive.

`sh_freertos_get_stats()` snapshots these saturating counters:

- RX bytes enqueued and delivered;
- RX overflow bytes and errors;
- TX bytes enqueued, rejected bytes, and errors.

`sh_freertos_reset_stats()` is allowed only while `run()` is stopped and ISR
producers are quiet. It returns `SH_ERR_BUSY` while running.

## Configuration Validation

`sh_freertos_init()` returns `SH_ERR_INVALID_CONFIG` when:

- either StreamBuffer handle is NULL;
- `read_timeout_ticks` is zero or `portMAX_DELAY`;
- `read_chunk_size` is zero;
- `read_chunk_size > SH_FREERTOS_MAX_READ_CHUNK`.

`SH_FREERTOS_MAX_READ_CHUNK` defaults to 64 and is also the size of the local
RX array on the `sh_freertos_run()` stack. A target may override it before
including the header or through a target-wide compiler definition. Keep the
definition identical in the adapter and application translation units.

`sh_freertos_run()` also verifies that the shell was initialized with exactly
the transport returned by `sh_freertos_transport(port)`.

## FreeRTOS Port Checklist

- `configSUPPORT_STATIC_ALLOCATION` matches the selected task/buffer pattern.
- StreamBuffer storage includes the required implementation overhead byte.
- RX has exactly one ISR producer and one shell-task consumer.
- TX has exactly one shell-task producer and one driver consumer.
- UART RX starts only after all shell state is initialized.
- RX overflow is measured and treated as loss of command-stream integrity.
- TX capacity covers help, completion lists, redraws, and command output.
- Read timeout permits bounded stop latency.
- Handler worst-case execution time is bounded.
- Shell and TX task stack high-water marks are measured on target.
- Secrets are masked in shell output and excluded from application logs.
- `cfg.secure_clear_enabled` remains enabled unless an equivalent platform
  wipe is documented; RX/TX StreamBuffers and driver DMA buffers are cleared
  separately before credential-bearing storage is reused.
- Hardware UART/DMA behavior is tested in addition to host unit tests.
