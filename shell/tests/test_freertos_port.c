#include "sh_port_freertos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expression)                                                \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "assert failed: %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #expression);                          \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_INT(expected, actual)                                           \
    ASSERT_TRUE((int)(expected) == (int)(actual))
#define ASSERT_SIZE(expected, actual)                                          \
    ASSERT_TRUE((size_t)(expected) == (size_t)(actual))

typedef struct {
    sh_freertos_port_t port;
    StaticStreamBuffer_t rx_control;
    StaticStreamBuffer_t tx_control;
    uint8_t rx_storage[8u + 1u];
    uint8_t tx_storage[4u + 1u];
} port_fixture_t;

static unsigned int prompt_calls;
static unsigned int input_calls;
static uint8_t input_data[64];
static size_t input_length;
static int input_status;

void sh_prompt(sh_t *shell)
{
    ASSERT_TRUE(shell != NULL);
    prompt_calls++;
}

int sh_input(sh_t *shell, const unsigned char *data, size_t len)
{
    ASSERT_TRUE(shell != NULL);
    ASSERT_TRUE(data != NULL || len == 0u);
    ASSERT_TRUE(len <= sizeof(input_data) - input_length);
    memcpy(input_data + input_length, data, len);
    input_length += len;
    input_calls++;
    return input_status;
}

static void reset_shell_spy(void)
{
    prompt_calls = 0u;
    input_calls = 0u;
    input_length = 0u;
    input_status = SH_OK;
    memset(input_data, 0, sizeof(input_data));
}

static void fixture_init(port_fixture_t *fixture,
                         size_t read_chunk_size)
{
    sh_freertos_config_t config;

    memset(fixture, 0, sizeof(*fixture));
    config.rx_stream = xStreamBufferCreateStatic(
        sizeof(fixture->rx_storage),
        1u,
        fixture->rx_storage,
        &fixture->rx_control);
    config.tx_stream = xStreamBufferCreateStatic(
        sizeof(fixture->tx_storage),
        1u,
        fixture->tx_storage,
        &fixture->tx_control);
    config.read_timeout_ticks = (TickType_t)3u;
    config.write_timeout_ticks = (TickType_t)2u;
    config.read_chunk_size = read_chunk_size;
    ASSERT_INT(SH_OK, sh_freertos_init(&fixture->port, &config));
}

static void assert_critical_state_clean(void)
{
    ASSERT_SIZE(0u, fake_freertos_critical_depth());
    ASSERT_SIZE(0u, fake_freertos_critical_errors());
}

static void test_init_and_transport(void)
{
    port_fixture_t fixture;
    sh_freertos_port_t port;
    sh_freertos_config_t config;
    StaticStreamBuffer_t rx_control;
    StaticStreamBuffer_t tx_control;
    uint8_t rx_storage[2u + 1u];
    uint8_t tx_storage[2u + 1u];

    fake_freertos_reset();
    memset(&port, 0, sizeof(port));
    memset(&config, 0, sizeof(config));
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(NULL, &config));
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, NULL));
    ASSERT_TRUE(sh_freertos_transport(NULL) == NULL);
    ASSERT_TRUE(sh_freertos_transport(&port) == NULL);

    config.rx_stream = xStreamBufferCreateStatic(sizeof(rx_storage), 1u,
                                                  rx_storage, &rx_control);
    config.tx_stream = xStreamBufferCreateStatic(sizeof(tx_storage), 1u,
                                                  tx_storage, &tx_control);
    config.read_timeout_ticks = (TickType_t)1u;
    config.write_timeout_ticks = (TickType_t)0u;
    config.read_chunk_size = 1u;
    ASSERT_INT(SH_OK, sh_freertos_init(&port, &config));
    ASSERT_TRUE(sh_freertos_transport(&port) == &port.transport);
    ASSERT_TRUE(port.transport.ctx == &port);
    ASSERT_TRUE(port.transport.write == sh_freertos_transport_write);

    config.read_timeout_ticks = (TickType_t)0u;
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, &config));
    config.read_timeout_ticks = portMAX_DELAY;
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, &config));
    config.read_timeout_ticks = (TickType_t)1u;
    config.read_chunk_size = 0u;
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, &config));
    config.read_chunk_size = (size_t)SH_FREERTOS_MAX_READ_CHUNK + 1u;
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, &config));
    config.read_chunk_size = 1u;
    config.rx_stream = NULL;
    ASSERT_INT(SH_ERR_INVALID_CONFIG, sh_freertos_init(&port, &config));

    fixture_init(&fixture, 2u);
    ASSERT_TRUE(sh_freertos_transport(&fixture.port) != NULL);
    assert_critical_state_clean();
}

static void test_isr_rx_partial_and_overflow(void)
{
    port_fixture_t fixture;
    static const uint8_t first[] = { 1u, 2u, 3u, 4u, 5u, 6u };
    static const uint8_t second[] = { 7u, 8u, 9u, 10u };
    sh_freertos_stats_t stats;
    BaseType_t woken = pdFALSE;

    fake_freertos_reset();
    fixture_init(&fixture, 2u);

    ASSERT_INT(pdPASS, sh_freertos_rx_from_isr(&fixture.port,
                                               first,
                                               sizeof(first),
                                               &woken));
    ASSERT_INT(pdTRUE, woken);

    woken = pdFALSE;
    ASSERT_INT(pdFAIL, sh_freertos_rx_from_isr(&fixture.port,
                                               second,
                                               sizeof(second),
                                               &woken));
    ASSERT_INT(pdTRUE, woken);
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(8u, stats.rx_bytes_enqueued);
    ASSERT_SIZE(2u, stats.rx_overflow_bytes);
    ASSERT_SIZE(0u, stats.rx_errors);

    ASSERT_INT(pdPASS, sh_freertos_rx_from_isr(&fixture.port,
                                               NULL,
                                               0u,
                                               NULL));
    ASSERT_INT(pdFAIL, sh_freertos_rx_from_isr(&fixture.port,
                                               NULL,
                                               1u,
                                               NULL));
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(1u, stats.rx_errors);
    assert_critical_state_clean();
}

static void test_tx_partial_and_backpressure(void)
{
    port_fixture_t fixture;
    static const uint8_t data[] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    uint8_t drained[4];
    sh_freertos_stats_t stats;
    size_t written = 99u;

    fake_freertos_reset();
    fixture_init(&fixture, 2u);

    ASSERT_INT(SH_TRANSPORT_WOULD_BLOCK,
               sh_freertos_transport_write(&fixture.port,
                                           data,
                                           sizeof(data),
                                           &written));
    ASSERT_SIZE(4u, written);

    ASSERT_INT(SH_TRANSPORT_WOULD_BLOCK,
               sh_freertos_transport_write(&fixture.port,
                                           data,
                                           1u,
                                           &written));
    ASSERT_SIZE(0u, written);
    ASSERT_SIZE(4u, xStreamBufferReceive(fixture.port.config.tx_stream,
                                        drained,
                                        sizeof(drained),
                                        0u));
    ASSERT_TRUE(memcmp(drained, data, sizeof(drained)) == 0);

    ASSERT_INT(SH_TRANSPORT_OK,
               sh_freertos_transport_write(&fixture.port,
                                           data + 4u,
                                           2u,
                                           &written));
    ASSERT_SIZE(2u, written);
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(6u, stats.tx_bytes_enqueued);
    ASSERT_SIZE(3u, stats.tx_overflow_bytes);
    ASSERT_SIZE(0u, stats.tx_errors);

    ASSERT_INT(SH_TRANSPORT_ERR_INVALID_ARGUMENT,
               sh_freertos_transport_write(&fixture.port,
                                           data,
                                           1u,
                                           NULL));
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(1u, stats.tx_errors);
    assert_critical_state_clean();
}

static void stop_wait_hook(StreamBufferHandle_t stream,
                           TickType_t wait_ticks,
                           void *ctx)
{
    sh_freertos_port_t *port = (sh_freertos_port_t *)ctx;

    ASSERT_TRUE(stream == port->config.rx_stream);
    ASSERT_TRUE(wait_ticks == port->config.read_timeout_ticks);
    sh_freertos_request_stop(port);
}

static void test_run_loop(void)
{
    port_fixture_t fixture;
    sh_t shell;
    static const uint8_t data[] = { 'h', 'e', 'l', 'p' };
    sh_freertos_stats_t stats;
    BaseType_t woken = pdFALSE;

    fake_freertos_reset();
    reset_shell_spy();
    fixture_init(&fixture, 3u);
    memset(&shell, 0, sizeof(shell));
    shell.cfg.transport = sh_freertos_transport(&fixture.port);

    ASSERT_INT(pdPASS, sh_freertos_rx_from_isr(&fixture.port,
                                               data,
                                               sizeof(data),
                                               &woken));
    fake_stream_buffer_set_wait_hook(stop_wait_hook, &fixture.port);
    ASSERT_INT(SH_OK, sh_freertos_run(&fixture.port, &shell));
    ASSERT_SIZE(1u, prompt_calls);
    ASSERT_SIZE(2u, input_calls);
    ASSERT_SIZE(sizeof(data), input_length);
    ASSERT_TRUE(memcmp(input_data, data, sizeof(data)) == 0);
    ASSERT_INT(pdFALSE, sh_freertos_is_running(&fixture.port));

    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(sizeof(data), stats.rx_bytes_enqueued);
    ASSERT_SIZE(sizeof(data), stats.rx_bytes_delivered);
    ASSERT_SIZE(0u, stats.rx_errors);
    assert_critical_state_clean();
}

static void test_run_error_stop_and_stats(void)
{
    port_fixture_t fixture;
    sh_t shell;
    static const uint8_t byte = 'x';
    sh_freertos_stats_t stats;
    BaseType_t woken = pdFALSE;

    fake_freertos_reset();
    reset_shell_spy();
    fixture_init(&fixture, 1u);
    memset(&shell, 0, sizeof(shell));
    shell.cfg.transport = sh_freertos_transport(&fixture.port);

    input_status = SH_ERR_LINE_TOO_LONG;
    ASSERT_INT(pdPASS, sh_freertos_rx_from_isr(&fixture.port,
                                               &byte,
                                               1u,
                                               &woken));
    ASSERT_INT(SH_ERR_LINE_TOO_LONG,
               sh_freertos_run(&fixture.port, &shell));
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(1u, stats.rx_errors);
    ASSERT_INT(pdFALSE, sh_freertos_is_running(&fixture.port));

    sh_freertos_request_stop(&fixture.port);
    reset_shell_spy();
    ASSERT_INT(SH_OK, sh_freertos_run(&fixture.port, &shell));
    ASSERT_SIZE(0u, prompt_calls);
    ASSERT_SIZE(0u, input_calls);
    ASSERT_INT(SH_OK, sh_freertos_clear_stop(&fixture.port));

    fixture.port.running = pdTRUE;
    ASSERT_INT(SH_ERR_BUSY, sh_freertos_clear_stop(&fixture.port));
    ASSERT_INT(SH_ERR_BUSY, sh_freertos_reset_stats(&fixture.port));
    ASSERT_INT(pdTRUE, sh_freertos_is_running(&fixture.port));
    fixture.port.running = pdFALSE;
    ASSERT_INT(SH_OK, sh_freertos_reset_stats(&fixture.port));
    ASSERT_INT(SH_OK, sh_freertos_get_stats(&fixture.port, &stats));
    ASSERT_SIZE(0u, stats.rx_bytes_enqueued);
    ASSERT_SIZE(0u, stats.rx_bytes_delivered);
    ASSERT_SIZE(0u, stats.rx_overflow_bytes);
    ASSERT_SIZE(0u, stats.rx_errors);
    ASSERT_SIZE(0u, stats.tx_bytes_enqueued);
    ASSERT_SIZE(0u, stats.tx_overflow_bytes);
    ASSERT_SIZE(0u, stats.tx_errors);

    shell.cfg.transport = NULL;
    ASSERT_INT(SH_ERR_INVALID_CONFIG,
               sh_freertos_run(&fixture.port, &shell));
    ASSERT_INT(SH_ERR_INVALID_ARG, sh_freertos_get_stats(NULL, &stats));
    ASSERT_INT(SH_ERR_INVALID_ARG,
               sh_freertos_get_stats(&fixture.port, NULL));
    assert_critical_state_clean();
}

int main(void)
{
    test_init_and_transport();
    test_isr_rx_partial_and_overflow();
    test_tx_partial_and_backpressure();
    test_run_loop();
    test_run_error_stop_and_stats();

    puts("test_freertos_port: all tests passed");
    return 0;
}
