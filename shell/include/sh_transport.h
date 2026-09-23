#ifndef SH_TRANSPORT_H
#define SH_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transport statuses, intentionally independent from shell parser statuses. */
typedef enum {
    SH_TRANSPORT_OK = 0,
    SH_TRANSPORT_WOULD_BLOCK = 1,
    SH_TRANSPORT_ERR_INVALID_ARGUMENT = -1,
    SH_TRANSPORT_ERR_IO = -2,
    SH_TRANSPORT_ERR_NO_PROGRESS = -3,
    SH_TRANSPORT_ERR_PROTOCOL = -4,
    SH_TRANSPORT_ERR_ATTEMPTS_EXHAUSTED = -5
} sh_transport_status_t;

/**
 * @brief Write as many bytes as the backend can accept in one attempt.
 *
 * The callback may consume only part of the request and must always initialize
 * @p written. Partial progress together with SH_TRANSPORT_WOULD_BLOCK or an
 * error is valid. Reporting more than @p len is a protocol violation detected
 * by sh_transport_write_all(). Returning SH_TRANSPORT_OK with zero progress for
 * a non-empty request is treated as SH_TRANSPORT_ERR_NO_PROGRESS.
 *
 * @param ctx Application-owned backend context.
 * @param data Bytes to write; valid only for this call.
 * @param len Number of requested bytes.
 * @param[out] written Number of bytes accepted by this invocation.
 * @return SH_TRANSPORT_OK, SH_TRANSPORT_WOULD_BLOCK, or a negative transport
 *         error.
 */
typedef sh_transport_status_t (*sh_transport_write_fn_t)(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written);

/**
 * @brief Flush pending backend output.
 * @param ctx Application-owned backend context.
 * @return SH_TRANSPORT_OK, SH_TRANSPORT_WOULD_BLOCK, or a negative error.
 */
typedef sh_transport_status_t (*sh_transport_flush_fn_t)(void *ctx);

/** Status-capable output backend borrowed by the shell configuration. */
typedef struct {
    sh_transport_write_fn_t write; /**< Required write callback. */
    sh_transport_flush_fn_t flush; /**< Optional flush callback. */
    void *ctx;                     /**< Application-owned callback context. */
} sh_transport_t;

/**
 * @brief Write a complete buffer with a bounded number of callback attempts.
 *
 * The helper neither sleeps nor allocates memory. It accumulates confirmed
 * progress, including bytes reported together with WOULD_BLOCK or an error.
 * A non-empty write requires @p max_attempts greater than zero.
 *
 * @param transport Backend descriptor with a valid write callback.
 * @param data Bytes to write; may be NULL only when @p len is zero.
 * @param len Total byte count.
 * @param max_attempts Maximum number of write callback invocations.
 * @param[out] written_total Optional destination for confirmed progress.
 * @return SH_TRANSPORT_OK only when all bytes were written, otherwise the
 *         callback error or a helper validation/progress error.
 */
sh_transport_status_t sh_transport_write_all(
    const sh_transport_t *transport,
    const uint8_t *data,
    size_t len,
    size_t max_attempts,
    size_t *written_total);

/**
 * @brief Flush a transport when a flush callback is installed.
 * @param transport Backend descriptor.
 * @return SH_TRANSPORT_OK when flush is absent, otherwise callback status;
 *         SH_TRANSPORT_ERR_INVALID_ARGUMENT for NULL transport.
 */
sh_transport_status_t sh_transport_flush(const sh_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif
