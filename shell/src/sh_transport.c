#include "sh_transport.h"

/** @brief Check whether a callback returned a supported transport status. */
static int is_callback_status(sh_transport_status_t status)
{
    return status == SH_TRANSPORT_OK ||
           status == SH_TRANSPORT_WOULD_BLOCK ||
           status == SH_TRANSPORT_ERR_IO;
}

/** @brief Complete a write using a bounded number of partial attempts. */
sh_transport_status_t sh_transport_write_all(
    const sh_transport_t *transport,
    const uint8_t *data,
    size_t len,
    size_t max_attempts,
    size_t *written_total)
{
    size_t total = 0u;
    size_t attempts = 0u;
    sh_transport_status_t last_status = SH_TRANSPORT_OK;

    if (written_total != NULL) {
        *written_total = 0u;
    }

    if (transport == NULL || transport->write == NULL ||
        (data == NULL && len != 0u)) {
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }

    if (len == 0u) {
        return SH_TRANSPORT_OK;
    }

    if (max_attempts == 0u) {
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }

    while (total < len && attempts < max_attempts) {
        const size_t remaining = len - total;
        size_t written = 0u;
        sh_transport_status_t status;

        status = transport->write(transport->ctx,
                                  data + total,
                                  remaining,
                                  &written);
        attempts++;

        if (written > remaining) {
            return SH_TRANSPORT_ERR_PROTOCOL;
        }

        total += written;
        if (written_total != NULL) {
            *written_total = total;
        }

        if (!is_callback_status(status)) {
            return SH_TRANSPORT_ERR_PROTOCOL;
        }

        if (status == SH_TRANSPORT_ERR_IO) {
            return SH_TRANSPORT_ERR_IO;
        }

        if (total == len) {
            return SH_TRANSPORT_OK;
        }

        if (status == SH_TRANSPORT_OK && written == 0u) {
            return SH_TRANSPORT_ERR_NO_PROGRESS;
        }

        last_status = status;
    }

    if (last_status == SH_TRANSPORT_WOULD_BLOCK) {
        return SH_TRANSPORT_WOULD_BLOCK;
    }

    return SH_TRANSPORT_ERR_ATTEMPTS_EXHAUSTED;
}

/** @brief Flush a transport or succeed when no flush callback is installed. */
sh_transport_status_t sh_transport_flush(const sh_transport_t *transport)
{
    sh_transport_status_t status;

    if (transport == NULL) {
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }

    if (transport->flush == NULL) {
        return SH_TRANSPORT_OK;
    }

    status = transport->flush(transport->ctx);
    if (!is_callback_status(status)) {
        return SH_TRANSPORT_ERR_PROTOCOL;
    }

    return status;
}
