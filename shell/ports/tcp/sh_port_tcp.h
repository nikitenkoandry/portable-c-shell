#ifndef SH_PORT_TCP_H
#define SH_PORT_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sh_shell.h"
#include "sh_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SH_TCP_RX_CHUNK_SIZE
#define SH_TCP_RX_CHUNK_SIZE 128u
#endif

#if SH_TCP_RX_CHUNK_SIZE < 1u
#error "SH_TCP_RX_CHUNK_SIZE must be at least 1"
#endif

/**
 * The adapter does not call a socket API. The owner maps these results from
 * its TCP stack and decides when callbacks may block. For use with poll(),
 * read should normally be nonblocking.
 *
 * A callback may report partial progress together with WOULD_BLOCK, CLOSED,
 * or ERROR. The byte count always describes confirmed progress.
 */
typedef enum {
    SH_TCP_IO_OK = 0,
    SH_TCP_IO_WOULD_BLOCK = 1,
    SH_TCP_IO_CLOSED = 2,
    SH_TCP_IO_ERROR = -1
} sh_tcp_io_status_t;

/**
 * @brief Receive raw TCP bytes for one session.
 *
 * The callback must initialize @p read_count. Partial progress may accompany
 * WOULD_BLOCK, CLOSED, or ERROR.
 *
 * @param ctx Application-owned connection context.
 * @param data Receive buffer.
 * @param capacity Maximum writable byte count.
 * @param[out] read_count Confirmed bytes placed in @p data.
 * @return Stack-independent TCP I/O status.
 */
typedef sh_tcp_io_status_t (*sh_tcp_read_fn_t)(
    void *ctx,
    uint8_t *data,
    size_t capacity,
    size_t *read_count);

/**
 * @brief Transmit raw TCP bytes for one session.
 * @param ctx Application-owned connection context.
 * @param data Bytes to send.
 * @param len Requested byte count.
 * @param[out] written Confirmed bytes accepted by the TCP stack.
 * @return Stack-independent TCP I/O status.
 */
typedef sh_tcp_io_status_t (*sh_tcp_write_fn_t)(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

/**
 * @brief Release or close the application-owned connection.
 * @param ctx Application-owned connection context.
 */
typedef void (*sh_tcp_close_fn_t)(void *ctx);

/** Lifetime state of one TCP shell session. */
typedef enum {
    SH_TCP_SESSION_UNINITIALIZED = 0,
    SH_TCP_SESSION_OPEN,
    SH_TCP_SESSION_PEER_CLOSED,
    SH_TCP_SESSION_IO_ERROR,
    SH_TCP_SESSION_LOCAL_CLOSED
} sh_tcp_session_state_t;

/** Result statuses returned by TCP session operations. */
typedef enum {
    SH_TCP_OK = 0,
    SH_TCP_IDLE = 1,
    SH_TCP_RX_DATA = 2,
    SH_TCP_DISCONNECTED = 3,
    SH_TCP_ERR_INVALID_ARGUMENT = -1,
    SH_TCP_ERR_CLOSED = -2,
    SH_TCP_ERR_IO = -3,
    SH_TCP_ERR_PROTOCOL = -4,
    SH_TCP_ERR_SHELL = -5
} sh_tcp_status_t;

/** Configuration used once by sh_tcp_session_init(). */
typedef struct {
    const sh_config_t *shell_config; /**< Optional base shell configuration. */
    const sh_storage_t *storage;     /**< Required session-unique shell storage. */
    const sh_cmd_t *commands;        /**< Root command table. */
    size_t command_count;            /**< Number of root commands. */
    sh_tcp_read_fn_t read;           /**< Required receive callback. */
    sh_tcp_write_fn_t write;         /**< Required transmit callback. */
    sh_tcp_close_fn_t close;         /**< Required one-time close callback. */
    void *io_ctx;                    /**< Application-owned connection context. */
    void *auth_user_ctx;             /**< Per-client identity/application context. */
} sh_tcp_session_config_t;

/** Result of one sh_tcp_session_poll() call. */
typedef struct {
    sh_tcp_status_t status; /**< Session/poll result. */
    size_t bytes_received;  /**< Confirmed bytes passed to the shell. */
    int shell_status;       /**< Exact sh_input() status, normally SH_OK. */
} sh_tcp_poll_result_t;

/**
 * One instance represents one connection. The sh_t object, storage descriptor,
 * transport, editor state, history, and authentication context are not shared
 * with another session. The buffers referenced by storage remain caller-owned
 * and must be unique to the session for its entire lifetime.
 */
typedef struct {
    sh_t shell;
    sh_storage_t storage;
    sh_transport_t transport;
    sh_tcp_read_fn_t read;
    sh_tcp_write_fn_t write;
    sh_tcp_close_fn_t close;
    void *io_ctx;
    void *auth_user_ctx;
    sh_tcp_session_state_t state;
    int last_shell_status;
    bool close_notified;
    uint8_t rx_buffer[SH_TCP_RX_CHUNK_SIZE];
} sh_tcp_session_t;

/**
 * @brief Initialize one independent raw-TCP shell session.
 *
 * No I/O is performed. The adapter is transport-neutral and does not implement
 * Telnet negotiation. It copies the storage descriptor but borrows all buffers,
 * callbacks, command descriptors, strings and contexts. auth_user_ctx replaces
 * sh_config_t::user_ctx so handlers receive the per-client context by default.
 *
 * @param[out] session Caller-owned session state.
 * @param[in] config Session configuration and unique storage.
 * @return SH_TCP_OK, SH_TCP_ERR_INVALID_ARGUMENT, or SH_TCP_ERR_SHELL. On a
 *         shell error, inspect session::last_shell_status.
 */
sh_tcp_status_t sh_tcp_session_init(
    sh_tcp_session_t *session,
    const sh_tcp_session_config_t *config);

/**
 * @brief Write the configured prompt to an open session.
 * @param session Initialized session.
 * @return SH_OK, SH_ERR_INVALID_ARG, SH_ERR_TRANSPORT, or an output error.
 */
int sh_tcp_session_prompt(sh_tcp_session_t *session);

/**
 * @brief Perform one owner-scheduled receive step.
 *
 * The read callback is invoked exactly once and every confirmed byte is fed to
 * the shell, including bytes returned together with CLOSED or ERROR. A shell
 * error is reported without automatically closing the session, allowing the
 * owner to decide whether it is recoverable. Waiting, retries and reconnect
 * policy remain outside the adapter.
 *
 * @param session Open session.
 * @return Poll result with transport status, byte count and exact shell status.
 */
sh_tcp_poll_result_t sh_tcp_session_poll(sh_tcp_session_t *session);

/**
 * @brief Close a session locally and notify the owner at most once.
 * @param session Initialized session.
 * @return SH_TCP_OK, SH_TCP_ERR_INVALID_ARGUMENT, or SH_TCP_ERR_CLOSED for an
 *         uninitialized session.
 */
sh_tcp_status_t sh_tcp_session_close(sh_tcp_session_t *session);

/**
 * @brief Read the current session state.
 * @return Current state, or SH_TCP_SESSION_UNINITIALIZED for NULL.
 */
sh_tcp_session_state_t sh_tcp_session_state(
    const sh_tcp_session_t *session);

/**
 * @brief Access the shell embedded in an initialized session.
 * @return Borrowed shell pointer, or NULL when unavailable.
 */
sh_t *sh_tcp_session_shell(sh_tcp_session_t *session);

/**
 * @brief Access the per-client authentication/application context.
 * @return Original auth_user_ctx value, or NULL for a NULL session.
 */
void *sh_tcp_session_auth_user_ctx(const sh_tcp_session_t *session);

/**
 * @brief Access the output transport embedded in a session.
 * @return Borrowed transport pointer, or NULL when unavailable.
 */
const sh_transport_t *sh_tcp_session_transport(
    const sh_tcp_session_t *session);

/**
 * @brief Adapt the session write callback to sh_transport_write_fn_t.
 *
 * Applications normally reach this through sh_tcp_session_transport().
 */
sh_transport_status_t sh_tcp_session_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

#ifdef __cplusplus
}
#endif

#endif
