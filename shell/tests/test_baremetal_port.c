#include "sh_port_baremetal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "assert failed: %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                               \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define ASSERT_SIZE(expected, actual)                                         \
    ASSERT_TRUE((size_t)(expected) == (size_t)(actual))
#define ASSERT_PORT_STATUS(expected, actual)                                  \
    ASSERT_TRUE((sh_port_baremetal_status_t)(expected) ==                     \
                (sh_port_baremetal_status_t)(actual))
#define ASSERT_TRANSPORT_STATUS(expected, actual)                             \
    ASSERT_TRUE((sh_transport_status_t)(expected) ==                          \
                (sh_transport_status_t)(actual))

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    size_t calls;
    size_t error_on_call;
} fake_rx_t;

typedef struct {
    uint8_t data[32];
    size_t len;
    size_t max_chunk;
    size_t calls;
    sh_port_baremetal_write_status_t status;
} fake_tx_t;

typedef struct {
    sh_t *expected_shell;
    uint8_t data[32];
    size_t len;
} input_capture_t;

static input_capture_t *active_capture;

int sh_input_byte(sh_t *shell, unsigned char byte)
{
    ASSERT_TRUE(active_capture != NULL);
    ASSERT_TRUE(shell == active_capture->expected_shell);
    ASSERT_TRUE(active_capture->len < sizeof(active_capture->data));
    active_capture->data[active_capture->len++] = byte;
    return SH_OK;
}

static sh_port_baremetal_read_status_t fake_read(void *ctx, uint8_t *byte_out)
{
    fake_rx_t *rx = (fake_rx_t *)ctx;

    rx->calls++;
    if (rx->error_on_call != 0u && rx->calls == rx->error_on_call) {
        return SH_PORT_BAREMETAL_READ_ERROR;
    }
    if (rx->pos >= rx->len) {
        return SH_PORT_BAREMETAL_READ_NO_DATA;
    }

    *byte_out = rx->data[rx->pos++];
    return SH_PORT_BAREMETAL_READ_BYTE;
}

static sh_port_baremetal_write_status_t fake_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written)
{
    fake_tx_t *tx = (fake_tx_t *)ctx;
    size_t count = len;

    tx->calls++;
    if (tx->status != SH_PORT_BAREMETAL_WRITE_OK) {
        *written = 0u;
        return tx->status;
    }
    if (count > tx->max_chunk) {
        count = tx->max_chunk;
    }
    if (count > sizeof(tx->data) - tx->len) {
        count = sizeof(tx->data) - tx->len;
    }

    memcpy(tx->data + tx->len, data, count);
    tx->len += count;
    *written = count;
    return SH_PORT_BAREMETAL_WRITE_OK;
}

static void init_port(
    sh_port_baremetal_t *port,
    sh_t *shell,
    fake_rx_t *rx,
    fake_tx_t *tx,
    input_capture_t *capture)
{
    memset(port, 0, sizeof(*port));
    memset(shell, 0, sizeof(*shell));
    memset(capture, 0, sizeof(*capture));
    capture->expected_shell = shell;
    active_capture = capture;

    ASSERT_PORT_STATUS(
        SH_PORT_BAREMETAL_OK,
        sh_port_baremetal_init(port, shell, fake_read, rx, fake_write, tx));
}

static void test_bounded_poll_and_byte_order(void)
{
    static const uint8_t source[] = { 'A', 'B', 'C', 'D', 'E' };
    fake_rx_t rx = { source, sizeof(source), 0u, 0u, 0u };
    fake_tx_t tx = { { 0u }, 0u, 4u, 0u, SH_PORT_BAREMETAL_WRITE_OK };
    input_capture_t capture;
    sh_port_baremetal_t port;
    sh_t shell;
    sh_port_baremetal_poll_result_t result;

    init_port(&port, &shell, &rx, &tx, &capture);

    result = sh_port_baremetal_poll(&port, 3u);
    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_LIMIT_REACHED, result.status);
    ASSERT_SIZE(3u, result.bytes_processed);
    ASSERT_SIZE(3u, rx.calls);
    ASSERT_SIZE(3u, capture.len);
    ASSERT_TRUE(memcmp(capture.data, source, 3u) == 0);

    result = sh_port_baremetal_poll(&port, 3u);
    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_NO_DATA, result.status);
    ASSERT_SIZE(2u, result.bytes_processed);
    ASSERT_SIZE(6u, rx.calls);
    ASSERT_SIZE(sizeof(source), capture.len);
    ASSERT_TRUE(memcmp(capture.data, source, sizeof(source)) == 0);
}

static void test_no_data(void)
{
    fake_rx_t rx = { NULL, 0u, 0u, 0u, 0u };
    fake_tx_t tx = { { 0u }, 0u, 4u, 0u, SH_PORT_BAREMETAL_WRITE_OK };
    input_capture_t capture;
    sh_port_baremetal_t port;
    sh_t shell;
    sh_port_baremetal_poll_result_t result;

    init_port(&port, &shell, &rx, &tx, &capture);
    result = sh_port_baremetal_poll(&port, 8u);

    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_NO_DATA, result.status);
    ASSERT_SIZE(0u, result.bytes_processed);
    ASSERT_SIZE(1u, rx.calls);
    ASSERT_SIZE(0u, capture.len);
    ASSERT_SIZE(0u, sh_port_baremetal_read_error_count(&port));
}

static void test_read_error_is_counted(void)
{
    static const uint8_t source[] = { 'x', 'y' };
    fake_rx_t rx = { source, sizeof(source), 0u, 0u, 2u };
    fake_tx_t tx = { { 0u }, 0u, 4u, 0u, SH_PORT_BAREMETAL_WRITE_OK };
    input_capture_t capture;
    sh_port_baremetal_t port;
    sh_t shell;
    sh_port_baremetal_poll_result_t result;

    init_port(&port, &shell, &rx, &tx, &capture);
    result = sh_port_baremetal_poll(&port, 8u);

    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_ERR_READ, result.status);
    ASSERT_SIZE(1u, result.bytes_processed);
    ASSERT_SIZE(1u, capture.len);
    ASSERT_SIZE(1u, sh_port_baremetal_read_error_count(&port));

    rx.error_on_call = 3u;
    result = sh_port_baremetal_poll(&port, 8u);
    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_ERR_READ, result.status);
    ASSERT_SIZE(0u, result.bytes_processed);
    ASSERT_SIZE(2u, sh_port_baremetal_read_error_count(&port));

    sh_port_baremetal_clear_read_errors(&port);
    ASSERT_SIZE(0u, sh_port_baremetal_read_error_count(&port));
}

static void test_partial_tx_preserves_output(void)
{
    static const uint8_t output[] = { 'h', 'e', 'l', 'l', 'o' };
    fake_rx_t rx = { NULL, 0u, 0u, 0u, 0u };
    fake_tx_t tx = { { 0u }, 0u, 2u, 0u, SH_PORT_BAREMETAL_WRITE_OK };
    input_capture_t capture;
    sh_port_baremetal_t port;
    sh_t shell;
    const sh_transport_t *transport;
    size_t offset = 0u;

    init_port(&port, &shell, &rx, &tx, &capture);
    transport = sh_port_baremetal_transport(&port);
    ASSERT_TRUE(transport != NULL);

    while (offset < sizeof(output)) {
        size_t written = 99u;
        sh_transport_status_t status;

        status = transport->write(
            transport->ctx,
            output + offset,
            sizeof(output) - offset,
            &written);
        ASSERT_TRANSPORT_STATUS(SH_TRANSPORT_OK, status);
        ASSERT_TRUE(written > 0u);
        ASSERT_TRUE(written <= 2u);
        offset += written;
    }

    ASSERT_SIZE(3u, tx.calls);
    ASSERT_SIZE(sizeof(output), tx.len);
    ASSERT_TRUE(memcmp(tx.data, output, sizeof(output)) == 0);

    tx.status = SH_PORT_BAREMETAL_WRITE_WOULD_BLOCK;
    {
        size_t written = 99u;
        ASSERT_TRANSPORT_STATUS(
            SH_TRANSPORT_WOULD_BLOCK,
            transport->write(transport->ctx, output, sizeof(output), &written));
        ASSERT_SIZE(0u, written);
    }
}

static void test_invalid_arguments(void)
{
    fake_rx_t rx = { NULL, 0u, 0u, 0u, 0u };
    fake_tx_t tx = { { 0u }, 0u, 1u, 0u, SH_PORT_BAREMETAL_WRITE_OK };
    input_capture_t capture;
    sh_port_baremetal_t port;
    sh_t shell;
    sh_port_baremetal_poll_result_t result;

    ASSERT_PORT_STATUS(
        SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT,
        sh_port_baremetal_init(NULL, &shell, fake_read, &rx, fake_write, &tx));

    init_port(&port, &shell, &rx, &tx, &capture);
    result = sh_port_baremetal_poll(&port, 0u);
    ASSERT_PORT_STATUS(SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT, result.status);
    ASSERT_SIZE(0u, result.bytes_processed);
    ASSERT_SIZE(0u, rx.calls);
}

int main(void)
{
    test_bounded_poll_and_byte_order();
    test_no_data();
    test_read_error_is_counted();
    test_partial_tx_preserves_output();
    test_invalid_arguments();

    puts("test_baremetal_port: all tests passed");
    return 0;
}
