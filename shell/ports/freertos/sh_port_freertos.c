#include "sh_port_freertos.h"

#include <limits.h>
#include <string.h>

static void counter_add(volatile uint32_t *counter, size_t amount)
{
    uint32_t current;

    if (counter == NULL || amount == 0u) {
        return;
    }

    current = *counter;
    if (amount > (size_t)(UINT32_MAX - current)) {
        *counter = UINT32_MAX;
    } else {
        *counter = current + (uint32_t)amount;
    }
}

static BaseType_t stop_is_requested(const sh_freertos_port_t *port)
{
    BaseType_t requested;

    taskENTER_CRITICAL();
    requested = port->stop_requested;
    taskEXIT_CRITICAL();
    return requested;
}

int sh_freertos_init(sh_freertos_port_t *port,
                     const sh_freertos_config_t *config)
{
    if (port == NULL || config == NULL ||
        config->rx_stream == NULL || config->tx_stream == NULL ||
        config->read_timeout_ticks == (TickType_t)0 ||
        config->read_timeout_ticks == portMAX_DELAY ||
        config->read_chunk_size == 0u ||
        config->read_chunk_size > (size_t)SH_FREERTOS_MAX_READ_CHUNK) {
        return SH_ERR_INVALID_CONFIG;
    }

    memset(port, 0, sizeof(*port));
    port->config = *config;
    port->transport.write = sh_freertos_transport_write;
    port->transport.flush = NULL;
    port->transport.ctx = port;
    return SH_OK;
}

const sh_transport_t *sh_freertos_transport(sh_freertos_port_t *port)
{
    if (port == NULL || port->transport.write == NULL) {
        return NULL;
    }
    return &port->transport;
}

int sh_freertos_run(sh_freertos_port_t *port, sh_t *shell)
{
    uint8_t input[SH_FREERTOS_MAX_READ_CHUNK];

    if (port == NULL || shell == NULL ||
        port->config.rx_stream == NULL ||
        port->config.tx_stream == NULL ||
        port->config.read_chunk_size == 0u ||
        port->config.read_chunk_size > sizeof(input) ||
        shell->cfg.transport != &port->transport) {
        return SH_ERR_INVALID_CONFIG;
    }

    taskENTER_CRITICAL();
    if (port->running != pdFALSE) {
        taskEXIT_CRITICAL();
        return SH_ERR_BUSY;
    }
    port->running = pdTRUE;
    taskEXIT_CRITICAL();

    if (!stop_is_requested(port)) {
        sh_prompt(shell);
    }

    while (!stop_is_requested(port)) {
        size_t received = xStreamBufferReceive(
            port->config.rx_stream,
            input,
            port->config.read_chunk_size,
            port->config.read_timeout_ticks);

        if (stop_is_requested(port)) {
            break;
        }

        if (received != 0u) {
            int status;

            taskENTER_CRITICAL();
            counter_add(&port->stats.rx_bytes_delivered, received);
            taskEXIT_CRITICAL();

            status = sh_input(shell, input, received);
            if (status != SH_OK) {
                taskENTER_CRITICAL();
                counter_add(&port->stats.rx_errors, 1u);
                port->running = pdFALSE;
                taskEXIT_CRITICAL();
                return status;
            }
        }
    }

    taskENTER_CRITICAL();
    port->running = pdFALSE;
    taskEXIT_CRITICAL();
    return SH_OK;
}

void sh_freertos_request_stop(sh_freertos_port_t *port)
{
    if (port == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    port->stop_requested = pdTRUE;
    taskEXIT_CRITICAL();
}

int sh_freertos_clear_stop(sh_freertos_port_t *port)
{
    if (port == NULL) {
        return SH_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL();
    if (port->running != pdFALSE) {
        taskEXIT_CRITICAL();
        return SH_ERR_BUSY;
    }
    port->stop_requested = pdFALSE;
    taskEXIT_CRITICAL();
    return SH_OK;
}

BaseType_t sh_freertos_is_running(const sh_freertos_port_t *port)
{
    BaseType_t running;

    if (port == NULL) {
        return pdFALSE;
    }

    taskENTER_CRITICAL();
    running = port->running;
    taskEXIT_CRITICAL();
    return running;
}

BaseType_t sh_freertos_rx_from_isr(
    sh_freertos_port_t *port,
    const uint8_t *data,
    size_t len,
    BaseType_t *higher_priority_task_woken)
{
    BaseType_t local_woken = pdFALSE;
    BaseType_t *woken = higher_priority_task_woken;
    size_t sent;

    if (woken == NULL) {
        woken = &local_woken;
    }

    if (port == NULL || port->config.rx_stream == NULL ||
        (data == NULL && len != 0u)) {
        if (port != NULL) {
            counter_add(&port->stats.rx_errors, 1u);
        }
        return pdFAIL;
    }

    if (len == 0u) {
        return pdPASS;
    }

    sent = xStreamBufferSendFromISR(port->config.rx_stream,
                                    data,
                                    len,
                                    woken);
    counter_add(&port->stats.rx_bytes_enqueued, sent);
    if (sent != len) {
        counter_add(&port->stats.rx_overflow_bytes, len - sent);
        return pdFAIL;
    }

    return pdPASS;
}

sh_transport_status_t sh_freertos_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written)
{
    sh_freertos_port_t *port = (sh_freertos_port_t *)ctx;
    size_t sent;

    if (written != NULL) {
        *written = 0u;
    }

    if (port == NULL || written == NULL ||
        port->config.tx_stream == NULL ||
        (data == NULL && len != 0u)) {
        if (port != NULL) {
            taskENTER_CRITICAL();
            counter_add(&port->stats.tx_errors, 1u);
            taskEXIT_CRITICAL();
        }
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }

    if (len == 0u) {
        return SH_TRANSPORT_OK;
    }

    sent = xStreamBufferSend(port->config.tx_stream,
                             data,
                             len,
                             port->config.write_timeout_ticks);
    *written = sent;

    taskENTER_CRITICAL();
    counter_add(&port->stats.tx_bytes_enqueued, sent);
    if (sent != len) {
        counter_add(&port->stats.tx_overflow_bytes, len - sent);
    }
    taskEXIT_CRITICAL();

    if (sent == len) {
        return SH_TRANSPORT_OK;
    }
    return SH_TRANSPORT_WOULD_BLOCK;
}

int sh_freertos_get_stats(const sh_freertos_port_t *port,
                          sh_freertos_stats_t *stats)
{
    if (port == NULL || stats == NULL) {
        return SH_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL();
    stats->rx_bytes_enqueued = port->stats.rx_bytes_enqueued;
    stats->rx_bytes_delivered = port->stats.rx_bytes_delivered;
    stats->rx_overflow_bytes = port->stats.rx_overflow_bytes;
    stats->rx_errors = port->stats.rx_errors;
    stats->tx_bytes_enqueued = port->stats.tx_bytes_enqueued;
    stats->tx_overflow_bytes = port->stats.tx_overflow_bytes;
    stats->tx_errors = port->stats.tx_errors;
    taskEXIT_CRITICAL();
    return SH_OK;
}

int sh_freertos_reset_stats(sh_freertos_port_t *port)
{
    if (port == NULL) {
        return SH_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL();
    if (port->running != pdFALSE) {
        taskEXIT_CRITICAL();
        return SH_ERR_BUSY;
    }
    memset((void *)&port->stats, 0, sizeof(port->stats));
    taskEXIT_CRITICAL();
    return SH_OK;
}
