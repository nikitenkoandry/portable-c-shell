#include "sh_port_baremetal.h"

#include <string.h>

/** @brief Initialize a caller-owned nonblocking bare-metal adapter. */
sh_port_baremetal_status_t sh_port_baremetal_init(
    sh_port_baremetal_t *port,
    sh_t *shell,
    sh_port_baremetal_read_fn_t read,
    void *read_ctx,
    sh_port_baremetal_write_fn_t write,
    void *write_ctx)
{
    if (port == NULL || shell == NULL || read == NULL || write == NULL) {
        return SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT;
    }

    memset(port, 0, sizeof(*port));
    port->shell = shell;
    port->read = read;
    port->read_ctx = read_ctx;
    port->write = write;
    port->write_ctx = write_ctx;
    port->transport.write = sh_port_baremetal_transport_write;
    port->transport.flush = NULL;
    port->transport.ctx = port;

    return SH_PORT_BAREMETAL_OK;
}

/** @brief Forward a bounded number of available RX bytes to the shell. */
sh_port_baremetal_poll_result_t sh_port_baremetal_poll(
    sh_port_baremetal_t *port,
    size_t max_bytes_per_poll)
{
    sh_port_baremetal_poll_result_t result;

    result.status = SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT;
    result.bytes_processed = 0u;

    if (port == NULL || port->shell == NULL || port->read == NULL ||
        max_bytes_per_poll == 0u) {
        return result;
    }

    while (result.bytes_processed < max_bytes_per_poll) {
        uint8_t byte = 0u;
        sh_port_baremetal_read_status_t read_status;

        read_status = port->read(port->read_ctx, &byte);
        if (read_status == SH_PORT_BAREMETAL_READ_BYTE) {
            sh_input_byte(port->shell, byte);
            result.bytes_processed++;
            continue;
        }

        if (read_status == SH_PORT_BAREMETAL_READ_NO_DATA) {
            result.status = SH_PORT_BAREMETAL_NO_DATA;
            return result;
        }

        port->read_error_count++;
        result.status = SH_PORT_BAREMETAL_ERR_READ;
        return result;
    }

    result.status = SH_PORT_BAREMETAL_LIMIT_REACHED;
    return result;
}

/** @brief Return the cumulative number of receive callback errors. */
size_t sh_port_baremetal_read_error_count(
    const sh_port_baremetal_t *port)
{
    if (port == NULL) {
        return 0u;
    }

    return port->read_error_count;
}

/** @brief Reset the cumulative receive error counter. */
void sh_port_baremetal_clear_read_errors(
    sh_port_baremetal_t *port)
{
    if (port != NULL) {
        port->read_error_count = 0u;
    }
}

/** @brief Return the stable shell transport embedded in the adapter. */
const sh_transport_t *sh_port_baremetal_transport(
    const sh_port_baremetal_t *port)
{
    if (port == NULL) {
        return NULL;
    }

    return &port->transport;
}

/** @brief Map a bare-metal write callback result to shell transport status. */
sh_transport_status_t sh_port_baremetal_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written)
{
    sh_port_baremetal_t *port = (sh_port_baremetal_t *)ctx;
    sh_port_baremetal_write_status_t write_status;

    if (written != NULL) {
        *written = 0u;
    }

    if (port == NULL || port->write == NULL || written == NULL ||
        (data == NULL && len != 0u)) {
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }

    write_status = port->write(port->write_ctx, data, len, written);
    if (*written > len) {
        *written = 0u;
        return SH_TRANSPORT_ERR_PROTOCOL;
    }

    switch (write_status) {
    case SH_PORT_BAREMETAL_WRITE_OK:
        return SH_TRANSPORT_OK;
    case SH_PORT_BAREMETAL_WRITE_WOULD_BLOCK:
        return SH_TRANSPORT_WOULD_BLOCK;
    case SH_PORT_BAREMETAL_WRITE_ERROR:
        return SH_TRANSPORT_ERR_IO;
    default:
        return SH_TRANSPORT_ERR_PROTOCOL;
    }
}
