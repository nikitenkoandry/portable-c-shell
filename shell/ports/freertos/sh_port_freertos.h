#ifndef SH_PORT_FREERTOS_H
#define SH_PORT_FREERTOS_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"

#include "sh_shell.h"
#include "sh_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum stack space used by sh_freertos_run() for one receive operation.
 * Applications may override this definition before including this header.
 */
#ifndef SH_FREERTOS_MAX_READ_CHUNK
#define SH_FREERTOS_MAX_READ_CHUNK 64u
#endif

#if SH_FREERTOS_MAX_READ_CHUNK < 1u
#error "SH_FREERTOS_MAX_READ_CHUNK must be at least 1"
#endif

/**
 * FreeRTOS stream buffers are single-writer/single-reader objects:
 *
 * - one dedicated shell task is the only owner of the mutable sh_t and the
 *   only caller of sh_freertos_run();
 * - one UART ISR/DMA ISR producer calls sh_freertos_rx_from_isr();
 * - the shell task is the only TX stream producer through sh_write();
 * - one UART TX task/driver is the only TX stream consumer.
 *
 * Command handlers run in the shell task. They must delegate long-running
 * work to application worker tasks. The ISR never calls shell core APIs.
 * Multiple producers or consumers require serialization owned by the
 * application; this adapter deliberately does not hide a mutex or create a
 * task.
 */
typedef struct {
    StreamBufferHandle_t rx_stream; /**< ISR-to-shell-task receive stream. */
    StreamBufferHandle_t tx_stream; /**< Shell-task-to-driver transmit stream. */
    TickType_t read_timeout_ticks;  /**< Finite nonzero stop-observation period. */
    TickType_t write_timeout_ticks; /**< Maximum wait for TX stream capacity. */
    size_t read_chunk_size;         /**< Bytes read per iteration, within configured limit. */
} sh_freertos_config_t;

/** Saturating runtime counters available to task-context diagnostics. */
typedef struct {
    uint32_t rx_bytes_enqueued;
    uint32_t rx_bytes_delivered;
    uint32_t rx_overflow_bytes;
    uint32_t rx_errors;
    uint32_t tx_bytes_enqueued;
    /* Bytes rejected by individual enqueue attempts; retries may recover. */
    uint32_t tx_overflow_bytes;
    uint32_t tx_errors;
} sh_freertos_stats_t;

/** Caller-owned FreeRTOS adapter state. Do not modify fields after init. */
typedef struct {
    sh_freertos_config_t config;
    sh_transport_t transport;
    volatile BaseType_t stop_requested;
    volatile BaseType_t running;
    volatile sh_freertos_stats_t stats;
} sh_freertos_port_t;

/**
 * @brief Initialize caller-owned FreeRTOS adapter state.
 *
 * Both stream buffers must already exist. read_timeout_ticks must be finite
 * and nonzero so a task-context stop request is observed within a bounded
 * interval. read_chunk_size must be 1..SH_FREERTOS_MAX_READ_CHUNK.
 * Initialization creates no task, queue, stream, mutex, or heap allocation.
 *
 * @param[out] port Adapter state to initialize.
 * @param[in] config Stream handles, timeouts and receive chunk size to copy.
 * @return SH_OK or SH_ERR_INVALID_CONFIG.
 */
int sh_freertos_init(sh_freertos_port_t *port,
                     const sh_freertos_config_t *config);

/**
 * @brief Get the stable transport used for shell output.
 *
 * Store this pointer in sh_config_t::transport before calling sh_init().
 *
 * @param port Initialized adapter.
 * @return Borrowed transport pointer, or NULL for an invalid adapter.
 */
const sh_transport_t *sh_freertos_transport(sh_freertos_port_t *port);

/**
 * @brief Run the blocking shell receive loop in the application shell task.
 *
 * No task is created or deleted by the adapter. The supplied shell must use
 * the exact transport returned by sh_freertos_transport(@p port). The function
 * emits the initial prompt, receives bounded chunks, and invokes sh_input() in
 * this task. Command handlers therefore execute in the same task and should
 * delegate long-running work to application workers.
 *
 * @param port Initialized adapter owned by this task while running.
 * @param shell Initialized shell configured with the adapter transport.
 * @return SH_OK after a requested stop, SH_ERR_INVALID_CONFIG, SH_ERR_BUSY, or
 *         the first non-SH_OK status returned by sh_input().
 */
int sh_freertos_run(sh_freertos_port_t *port, sh_t *shell);

/**
 * @brief Request task-loop termination from task context.
 *
 * With no RX data, the loop observes the request within one configured read
 * timeout. The request remains latched until sh_freertos_clear_stop().
 *
 * @param port Adapter state. NULL is ignored.
 */
void sh_freertos_request_stop(sh_freertos_port_t *port);

/**
 * @brief Clear a latched stop request before restarting the run loop.
 * @param port Adapter state.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_BUSY while running.
 */
int sh_freertos_clear_stop(sh_freertos_port_t *port);

/**
 * @brief Query whether sh_freertos_run() currently owns the adapter.
 * @return pdTRUE while running; pdFALSE otherwise or for NULL.
 */
BaseType_t sh_freertos_is_running(const sh_freertos_port_t *port);

/**
 * @brief Enqueue received UART/network-UART bytes from interrupt context.
 *
 * The ISR never enters the shell core. pdPASS means every byte was queued;
 * pdFAIL means invalid input or partial/full overflow. Dropped bytes are added
 * to saturating statistics. The caller must perform the normal
 * portYIELD_FROM_ISR() using @p higher_priority_task_woken and initialize that
 * value to pdFALSE before the first FromISR operation in the ISR.
 *
 * @param port Initialized adapter.
 * @param data Received bytes; may be NULL only when @p len is zero.
 * @param len Number of bytes to enqueue.
 * @param[in,out] higher_priority_task_woken Optional FreeRTOS wake flag.
 * @return pdPASS only when the complete input was accepted, otherwise pdFAIL.
 */
BaseType_t sh_freertos_rx_from_isr(
    sh_freertos_port_t *port,
    const uint8_t *data,
    size_t len,
    BaseType_t *higher_priority_task_woken);

/**
 * @brief Enqueue shell output into the TX stream buffer from task context.
 *
 * This callback is normally reached through sh_freertos_transport(). Partial
 * writes return SH_TRANSPORT_WOULD_BLOCK and report confirmed progress.
 *
 * @param ctx Pointer to initialized sh_freertos_port_t.
 * @param data Output bytes; may be NULL only when @p len is zero.
 * @param len Number of requested bytes.
 * @param[out] written Number of bytes enqueued.
 * @return SH_TRANSPORT_OK, SH_TRANSPORT_WOULD_BLOCK, or validation error.
 */
sh_transport_status_t sh_freertos_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

/**
 * @brief Copy a consistent task-context snapshot of saturating counters.
 * @param port Initialized adapter.
 * @param[out] stats Destination snapshot.
 * @return SH_OK or SH_ERR_INVALID_ARG.
 */
int sh_freertos_get_stats(const sh_freertos_port_t *port,
                          sh_freertos_stats_t *stats);

/**
 * @brief Reset diagnostics while the run loop and ISR producers are stopped.
 * @param port Initialized adapter.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_BUSY while running.
 */
int sh_freertos_reset_stats(sh_freertos_port_t *port);

#ifdef __cplusplus
}
#endif

#endif
