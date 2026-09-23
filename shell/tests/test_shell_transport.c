#include "test_common.h"
#include "sh_transport.h"

typedef struct {
    char data[128];
    size_t len;
    size_t chunk;
    sh_transport_status_t status;
    size_t calls;
} shell_transport_capture_t;

static sh_transport_status_t capture_write(void *ctx, const uint8_t *data,
                                           size_t len, size_t *written)
{
    shell_transport_capture_t *capture = (shell_transport_capture_t *)ctx;
    size_t amount = len;

    capture->calls++;
    if (capture->status == SH_TRANSPORT_WOULD_BLOCK) {
        *written = 0u;
        return capture->status;
    }
    if (capture->chunk != 0u && amount > capture->chunk) {
        amount = capture->chunk;
    }
    if (amount > sizeof(capture->data) - capture->len) {
        amount = sizeof(capture->data) - capture->len;
    }
    memcpy(capture->data + capture->len, data, amount);
    capture->len += amount;
    *written = amount;
    return capture->status;
}

static void init_with_transport(test_shell_t *ts, sh_transport_t *transport,
                                shell_transport_capture_t *capture)
{
    memset(ts, 0, sizeof(*ts));
    memset(capture, 0, sizeof(*capture));
    capture->status = SH_TRANSPORT_OK;
    transport->write = capture_write;
    transport->flush = NULL;
    transport->ctx = capture;

    sh_default_config(&ts->cfg);
    ts->cfg.transport = transport;
    ts->cfg.transport_write_attempts = 8u;
    ASSERT_EQ_INT(SH_OK, sh_init(&ts->shell, &ts->cfg,
                                 ts->line, sizeof(ts->line),
                                 ts->argv, SH_ARRAY_SIZE(ts->argv),
                                 &ts->history[0][0], SH_ARRAY_SIZE(ts->history),
                                 sizeof(ts->history[0]),
                                 ts->draft, sizeof(ts->draft)));
}

static void test_shell_uses_partial_transport(void)
{
    test_shell_t ts;
    sh_transport_t transport;
    shell_transport_capture_t capture;

    init_with_transport(&ts, &transport, &capture);
    capture.chunk = 2u;

    ASSERT_EQ_INT(SH_OK, sh_puts(&ts.shell, "abcdef"));
    ASSERT_EQ_INT(6, (int)capture.len);
    ASSERT_EQ_INT(3, (int)capture.calls);
    ASSERT_TRUE(memcmp(capture.data, "abcdef", 6u) == 0);
}

static void test_shell_reports_backpressure(void)
{
    test_shell_t ts;
    sh_transport_t transport;
    shell_transport_capture_t capture;

    init_with_transport(&ts, &transport, &capture);
    capture.status = SH_TRANSPORT_WOULD_BLOCK;
    ts.cfg.transport_write_attempts = 2u;
    ts.shell.cfg.transport_write_attempts = 2u;

    ASSERT_EQ_INT(SH_ERR_WOULD_BLOCK, sh_puts(&ts.shell, "abc"));
    ASSERT_EQ_INT(2, (int)capture.calls);
    ASSERT_EQ_INT(0, (int)capture.len);
}

int main(void)
{
    test_shell_uses_partial_transport();
    test_shell_reports_backpressure();
    return 0;
}
