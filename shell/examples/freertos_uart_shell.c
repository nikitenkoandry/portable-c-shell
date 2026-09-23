#include "sh_port_freertos.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Reference integration for a UART-backed FreeRTOS shell.
 *
 * The shell task is the sole owner of sh_t and the sole RX stream consumer.
 * A UART ISR is the sole RX stream producer. The shell transport is the sole
 * TX stream producer, and app_shell_tx_task() is the sole TX consumer.
 *
 * Implement these hooks in the board support package. They deliberately avoid
 * any vendor UART type or API:
 *
 * - platform_shell_uart_write() starts or performs a UART write and returns
 *   the number of bytes accepted, in the range 0..len;
 * - platform_shell_uart_wait_tx_ready() blocks/yields until another write can
 *   make progress;
 * - platform_shell_yield_from_isr() performs the port-specific ISR yield when
 *   higher_priority_task_woken is non-zero.
 */
/** @brief Submits a byte range to the board UART transmitter. */
size_t platform_shell_uart_write(const uint8_t *data, size_t len);

/** @brief Waits until the board UART transmitter can accept more data. */
void platform_shell_uart_wait_tx_ready(void);

/** @brief Performs the board-specific context switch requested by an ISR. */
void platform_shell_yield_from_isr(BaseType_t higher_priority_task_woken);

#ifndef APP_SHELL_RX_STREAM_BYTES
#define APP_SHELL_RX_STREAM_BYTES 256u
#endif

#ifndef APP_SHELL_TX_STREAM_BYTES
#define APP_SHELL_TX_STREAM_BYTES 512u
#endif

#ifndef APP_SHELL_IO_CHUNK_BYTES
#define APP_SHELL_IO_CHUNK_BYTES 64u
#endif

#ifndef APP_SHELL_READ_TIMEOUT_TICKS
#define APP_SHELL_READ_TIMEOUT_TICKS ((TickType_t)20u)
#endif

#ifndef APP_SHELL_WRITE_TIMEOUT_TICKS
#define APP_SHELL_WRITE_TIMEOUT_TICKS ((TickType_t)20u)
#endif

#if APP_SHELL_RX_STREAM_BYTES < 1u
#error "APP_SHELL_RX_STREAM_BYTES must be at least 1"
#endif

#if APP_SHELL_TX_STREAM_BYTES < 1u
#error "APP_SHELL_TX_STREAM_BYTES must be at least 1"
#endif

#if APP_SHELL_IO_CHUNK_BYTES < 1u
#error "APP_SHELL_IO_CHUNK_BYTES must be at least 1"
#endif

#if APP_SHELL_IO_CHUNK_BYTES > SH_FREERTOS_MAX_READ_CHUNK
#error "APP_SHELL_IO_CHUNK_BYTES exceeds SH_FREERTOS_MAX_READ_CHUNK"
#endif

typedef struct {
    sh_t shell;
    sh_freertos_port_t port;

    StaticStreamBuffer_t rx_stream_control;
    StaticStreamBuffer_t tx_stream_control;
    /* FreeRTOS keeps one stream-buffer byte unused to distinguish full/empty. */
    uint8_t rx_stream_storage[APP_SHELL_RX_STREAM_BYTES + 1u];
    uint8_t tx_stream_storage[APP_SHELL_TX_STREAM_BYTES + 1u];

    char line[SH_MAX_LINE_LEN + 1u];
    char *argv[SH_MAX_ARGC];
    char history[SH_HISTORY_DEFAULT_DEPTH][SH_MAX_LINE_LEN + 1u];
    char draft[SH_MAX_LINE_LEN + 1u];
} app_shell_t;

/** @brief Initializes the FreeRTOS streams, adapter, shell, and commands. */
int app_shell_init(app_shell_t *app,
                   const sh_cmd_t *commands,
                   size_t command_count)
{
    sh_freertos_config_t port_config;
    sh_config_t shell_config;
    StreamBufferHandle_t rx_stream;
    StreamBufferHandle_t tx_stream;
    int status;

    if (app == NULL || (commands == NULL && command_count != 0u)) {
        return SH_ERR_INVALID_ARG;
    }

    memset(app, 0, sizeof(*app));
    rx_stream = xStreamBufferCreateStatic(
        sizeof(app->rx_stream_storage),
        1u,
        app->rx_stream_storage,
        &app->rx_stream_control);
    tx_stream = xStreamBufferCreateStatic(
        sizeof(app->tx_stream_storage),
        1u,
        app->tx_stream_storage,
        &app->tx_stream_control);
    if (rx_stream == NULL || tx_stream == NULL) {
        return SH_ERR_NO_MEMORY;
    }

    port_config.rx_stream = rx_stream;
    port_config.tx_stream = tx_stream;
    port_config.read_timeout_ticks = APP_SHELL_READ_TIMEOUT_TICKS;
    port_config.write_timeout_ticks = APP_SHELL_WRITE_TIMEOUT_TICKS;
    port_config.read_chunk_size = APP_SHELL_IO_CHUNK_BYTES;
    status = sh_freertos_init(&app->port, &port_config);
    if (status != SH_OK) {
        return status;
    }

    sh_default_config(&shell_config);
    shell_config.prompt = "rtos> ";
    shell_config.transport = sh_freertos_transport(&app->port);
    status = sh_init(&app->shell,
                     &shell_config,
                     app->line,
                     sizeof(app->line),
                     app->argv,
                     SH_ARRAY_SIZE(app->argv),
                     &app->history[0][0],
                     SH_ARRAY_SIZE(app->history),
                     sizeof(app->history[0]),
                     app->draft,
                     sizeof(app->draft));
    if (status != SH_OK) {
        return status;
    }

    return sh_register_commands(&app->shell, commands, command_count);
}

/** @brief Passes one UART RX chunk from an interrupt into the shell stream. */
BaseType_t app_shell_uart_rx_from_isr(app_shell_t *app,
                                      const uint8_t *data,
                                      size_t len)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    BaseType_t result;

    if (app == NULL) {
        return pdFAIL;
    }

    result = sh_freertos_rx_from_isr(&app->port,
                                     data,
                                     len,
                                     &higher_priority_task_woken);
    if (higher_priority_task_woken != pdFALSE) {
        platform_shell_yield_from_isr(higher_priority_task_woken);
    }
    return result;
}

/** @brief Runs the shell parser task until the adapter is stopped. */
void app_shell_task(void *arg)
{
    app_shell_t *app = (app_shell_t *)arg;

    if (app != NULL) {
        (void)sh_freertos_run(&app->port, &app->shell);
    }
}

/**
 * @brief Drains shell output to UART while handling partial writes.
 *
 * The platform write hook may accept a partial chunk; zero means temporary
 * backpressure and must be followed by a blocking or yielding wait hook.
 */
void app_shell_tx_task(void *arg)
{
    app_shell_t *app = (app_shell_t *)arg;
    uint8_t data[APP_SHELL_IO_CHUNK_BYTES];

    if (app == NULL) {
        return;
    }

    for (;;) {
        size_t received = xStreamBufferReceive(app->port.config.tx_stream,
                                               data,
                                               sizeof(data),
                                               portMAX_DELAY);
        size_t offset = 0u;

        while (offset < received) {
            size_t remaining = received - offset;
            size_t written = platform_shell_uart_write(data + offset,
                                                        remaining);

            if (written == 0u || written > remaining) {
                platform_shell_uart_wait_tx_ready();
            } else {
                offset += written;
            }
        }
    }
}
