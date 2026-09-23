#ifndef SH_PORT_BAREMETAL_H
#define SH_PORT_BAREMETAL_H

#include <stddef.h>
#include <stdint.h>

#include "sh_shell.h"
#include "sh_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Results returned by a nonblocking receive callback. */
typedef enum {
    SH_PORT_BAREMETAL_READ_BYTE = 0,
    SH_PORT_BAREMETAL_READ_NO_DATA = 1,
    SH_PORT_BAREMETAL_READ_ERROR = -1
} sh_port_baremetal_read_status_t;

/** Results returned by a nonblocking transmit callback. */
typedef enum {
    SH_PORT_BAREMETAL_WRITE_OK = 0,
    SH_PORT_BAREMETAL_WRITE_WOULD_BLOCK = 1,
    SH_PORT_BAREMETAL_WRITE_ERROR = -1
} sh_port_baremetal_write_status_t;

/** Port-level polling and validation statuses. */
typedef enum {
    SH_PORT_BAREMETAL_OK = 0,
    SH_PORT_BAREMETAL_NO_DATA = 1,
    SH_PORT_BAREMETAL_LIMIT_REACHED = 2,
    SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT = -1,
    SH_PORT_BAREMETAL_ERR_READ = -2
} sh_port_baremetal_status_t;

/**
 * @brief Try to receive one byte without blocking.
 * @param ctx Application-owned receive context.
 * @param[out] byte_out Destination used only when READ_BYTE is returned.
 * @return READ_BYTE, READ_NO_DATA, or READ_ERROR.
 */
typedef sh_port_baremetal_read_status_t (*sh_port_baremetal_read_fn_t)(
    void *ctx,
    uint8_t *byte_out);

/**
 * @brief Try to transmit bytes without blocking.
 *
 * The callback may consume fewer than @p len bytes. It must initialize
 * @p written, including when returning WOULD_BLOCK or ERROR.
 *
 * @param ctx Application-owned transmit context.
 * @param data Bytes to transmit.
 * @param len Number of requested bytes.
 * @param[out] written Number of bytes accepted by this invocation.
 * @return WRITE_OK, WRITE_WOULD_BLOCK, or WRITE_ERROR.
 */
typedef sh_port_baremetal_write_status_t (*sh_port_baremetal_write_fn_t)(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

/** Result of one bounded sh_port_baremetal_poll() call. */
typedef struct {
    sh_port_baremetal_status_t status;
    size_t bytes_processed;
} sh_port_baremetal_poll_result_t;

/**
 * Bare-metal adapter state. The application owns this object, the referenced
 * shell, and both callback contexts for the complete adapter lifetime.
 */
typedef struct {
    sh_t *shell;
    sh_port_baremetal_read_fn_t read;
    void *read_ctx;
    sh_port_baremetal_write_fn_t write;
    void *write_ctx;
    size_t read_error_count;
    sh_transport_t transport;
} sh_port_baremetal_t;

/**
 * @brief Initialize a nonblocking bare-metal shell adapter.
 *
 * Initialization performs no I/O. Pass sh_port_baremetal_transport() in the
 * shell configuration to route shell output through the write callback.
 *
 * @param[out] port Adapter state to initialize.
 * @param shell Initialized shell instance processed by poll calls.
 * @param read Required nonblocking receive callback.
 * @param read_ctx Application-owned receive context.
 * @param write Required nonblocking transmit callback.
 * @param write_ctx Application-owned transmit context.
 * @return SH_PORT_BAREMETAL_OK or SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT.
 */
sh_port_baremetal_status_t sh_port_baremetal_init(
    sh_port_baremetal_t *port,
    sh_t *shell,
    sh_port_baremetal_read_fn_t read,
    void *read_ctx,
    sh_port_baremetal_write_fn_t write,
    void *write_ctx);

/**
 * @brief Forward a bounded number of received bytes into the shell.
 *
 * The function never waits and invokes the read callback at most
 * @p max_bytes_per_poll times that produce bytes. Shell input statuses are not
 * returned by this lightweight adapter; command-specific reporting should use
 * application state or shell output.
 *
 * @param port Initialized adapter.
 * @param max_bytes_per_poll Positive work limit for this invocation.
 * @return Poll status and exact number of bytes passed to sh_input_byte().
 */
sh_port_baremetal_poll_result_t sh_port_baremetal_poll(
    sh_port_baremetal_t *port,
    size_t max_bytes_per_poll);

/**
 * @brief Read the cumulative receive callback error count.
 * @return Error count, or zero for NULL.
 */
size_t sh_port_baremetal_read_error_count(
    const sh_port_baremetal_t *port);

/**
 * @brief Reset the cumulative receive error count.
 * @param port Adapter state. NULL is ignored.
 */
void sh_port_baremetal_clear_read_errors(
    sh_port_baremetal_t *port);

/**
 * @brief Get the stable sh_transport_t embedded in an adapter.
 * @param port Initialized adapter.
 * @return Borrowed transport pointer, or NULL for a NULL adapter.
 */
const sh_transport_t *sh_port_baremetal_transport(
    const sh_port_baremetal_t *port);

/**
 * @brief Adapt the port write callback to sh_transport_write_fn_t.
 *
 * Applications normally use this indirectly through
 * sh_port_baremetal_transport().
 */
sh_transport_status_t sh_port_baremetal_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

#ifdef __cplusplus
}
#endif

#endif
