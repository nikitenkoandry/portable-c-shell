#include "sh_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "assert failed: %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #expr);                               \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define ASSERT_STATUS(expected, actual)                                       \
    ASSERT_TRUE((sh_transport_status_t)(expected) ==                          \
                (sh_transport_status_t)(actual))
#define ASSERT_SIZE(expected, actual)                                         \
    ASSERT_TRUE((size_t)(expected) == (size_t)(actual))

typedef struct {
    sh_transport_status_t status;
    size_t written;
} fake_write_step_t;

typedef struct {
    const fake_write_step_t *steps;
    size_t step_count;
    size_t next_step;
    uint8_t sink[64];
    size_t sink_len;
    sh_transport_status_t flush_status;
    size_t flush_calls;
} fake_transport_t;

static sh_transport_status_t fake_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written)
{
    fake_transport_t *fake = (fake_transport_t *)ctx;
    fake_write_step_t step;

    if (fake->next_step >= fake->step_count) {
        *written = 0u;
        return SH_TRANSPORT_ERR_IO;
    }

    step = fake->steps[fake->next_step++];
    *written = step.written;

    if (step.written <= len &&
        step.written <= sizeof(fake->sink) - fake->sink_len) {
        memcpy(fake->sink + fake->sink_len, data, step.written);
        fake->sink_len += step.written;
    }

    return step.status;
}

static sh_transport_status_t fake_flush(void *ctx)
{
    fake_transport_t *fake = (fake_transport_t *)ctx;
    fake->flush_calls++;
    return fake->flush_status;
}

static sh_transport_t make_transport(fake_transport_t *fake)
{
    sh_transport_t transport;
    transport.write = fake_write;
    transport.flush = fake_flush;
    transport.ctx = fake;
    return transport;
}

static fake_transport_t make_fake(
    const fake_write_step_t *steps,
    size_t step_count)
{
    fake_transport_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.steps = steps;
    fake.step_count = step_count;
    fake.flush_status = SH_TRANSPORT_OK;
    return fake;
}

static void test_full_write(void)
{
    static const uint8_t data[] = "abcdef";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 6u }
    };
    fake_transport_t fake = make_fake(steps, 1u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 99u;

    ASSERT_STATUS(SH_TRANSPORT_OK,
                  sh_transport_write_all(&transport, data, 6u, 1u, &written));
    ASSERT_SIZE(6u, written);
    ASSERT_SIZE(1u, fake.next_step);
    ASSERT_TRUE(memcmp(fake.sink, data, 6u) == 0);
}

static void test_partial_writes_preserve_order(void)
{
    static const uint8_t data[] = "abcdef";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 2u },
        { SH_TRANSPORT_OK, 1u },
        { SH_TRANSPORT_OK, 3u }
    };
    fake_transport_t fake = make_fake(steps, 3u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 0u;

    ASSERT_STATUS(SH_TRANSPORT_OK,
                  sh_transport_write_all(&transport, data, 6u, 3u, &written));
    ASSERT_SIZE(6u, written);
    ASSERT_SIZE(3u, fake.next_step);
    ASSERT_TRUE(memcmp(fake.sink, data, 6u) == 0);
}

static void test_zero_progress_is_rejected(void)
{
    static const uint8_t data[] = "abc";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 0u },
        { SH_TRANSPORT_OK, 3u }
    };
    fake_transport_t fake = make_fake(steps, 2u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 99u;

    ASSERT_STATUS(SH_TRANSPORT_ERR_NO_PROGRESS,
                  sh_transport_write_all(&transport, data, 3u, 8u, &written));
    ASSERT_SIZE(0u, written);
    ASSERT_SIZE(1u, fake.next_step);
}

static void test_would_block_then_success(void)
{
    static const uint8_t data[] = "abc";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_WOULD_BLOCK, 0u },
        { SH_TRANSPORT_OK, 3u }
    };
    fake_transport_t fake = make_fake(steps, 2u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 0u;

    ASSERT_STATUS(SH_TRANSPORT_OK,
                  sh_transport_write_all(&transport, data, 3u, 2u, &written));
    ASSERT_SIZE(3u, written);
    ASSERT_SIZE(2u, fake.next_step);
}

static void test_would_block_is_bounded(void)
{
    static const uint8_t data[] = "abc";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_WOULD_BLOCK, 0u },
        { SH_TRANSPORT_WOULD_BLOCK, 0u },
        { SH_TRANSPORT_OK, 3u }
    };
    fake_transport_t fake = make_fake(steps, 3u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 99u;

    ASSERT_STATUS(SH_TRANSPORT_WOULD_BLOCK,
                  sh_transport_write_all(&transport, data, 3u, 2u, &written));
    ASSERT_SIZE(0u, written);
    ASSERT_SIZE(2u, fake.next_step);
}

static void test_partial_would_block_counts_progress(void)
{
    static const uint8_t data[] = "abcdef";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_WOULD_BLOCK, 2u },
        { SH_TRANSPORT_OK, 4u }
    };
    fake_transport_t fake = make_fake(steps, 2u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 0u;

    ASSERT_STATUS(SH_TRANSPORT_OK,
                  sh_transport_write_all(&transport, data, 6u, 2u, &written));
    ASSERT_SIZE(6u, written);
    ASSERT_TRUE(memcmp(fake.sink, data, 6u) == 0);
}

static void test_partial_progress_can_exhaust_attempts(void)
{
    static const uint8_t data[] = "abcd";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 1u },
        { SH_TRANSPORT_OK, 1u },
        { SH_TRANSPORT_OK, 2u }
    };
    fake_transport_t fake = make_fake(steps, 3u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 0u;

    ASSERT_STATUS(SH_TRANSPORT_ERR_ATTEMPTS_EXHAUSTED,
                  sh_transport_write_all(&transport, data, 4u, 2u, &written));
    ASSERT_SIZE(2u, written);
    ASSERT_SIZE(2u, fake.next_step);
    ASSERT_TRUE(memcmp(fake.sink, data, 2u) == 0);
}

static void test_io_error_preserves_reported_progress(void)
{
    static const uint8_t data[] = "abcdef";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 2u },
        { SH_TRANSPORT_ERR_IO, 1u }
    };
    fake_transport_t fake = make_fake(steps, 2u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 0u;

    ASSERT_STATUS(SH_TRANSPORT_ERR_IO,
                  sh_transport_write_all(&transport, data, 6u, 4u, &written));
    ASSERT_SIZE(3u, written);
    ASSERT_SIZE(2u, fake.next_step);
    ASSERT_TRUE(memcmp(fake.sink, data, 3u) == 0);
}

static void test_invalid_callback_reports_protocol_error(void)
{
    static const uint8_t data[] = "abc";
    static const fake_write_step_t too_many[] = {
        { SH_TRANSPORT_OK, 4u }
    };
    static const fake_write_step_t unknown_status[] = {
        { (sh_transport_status_t)77, 1u }
    };
    fake_transport_t fake = make_fake(too_many, 1u);
    sh_transport_t transport = make_transport(&fake);
    size_t written = 99u;

    ASSERT_STATUS(SH_TRANSPORT_ERR_PROTOCOL,
                  sh_transport_write_all(&transport, data, 3u, 1u, &written));
    ASSERT_SIZE(0u, written);

    fake = make_fake(unknown_status, 1u);
    transport = make_transport(&fake);
    ASSERT_STATUS(SH_TRANSPORT_ERR_PROTOCOL,
                  sh_transport_write_all(&transport, data, 3u, 1u, &written));
    ASSERT_SIZE(1u, written);
}

static void test_arguments_and_empty_write(void)
{
    static const uint8_t data[] = "x";
    static const fake_write_step_t steps[] = {
        { SH_TRANSPORT_OK, 1u }
    };
    fake_transport_t fake = make_fake(steps, 1u);
    sh_transport_t transport = make_transport(&fake);
    sh_transport_t no_writer = transport;
    size_t written = 99u;

    no_writer.write = NULL;
    ASSERT_STATUS(SH_TRANSPORT_ERR_INVALID_ARGUMENT,
                  sh_transport_write_all(NULL, data, 1u, 1u, &written));
    ASSERT_SIZE(0u, written);
    ASSERT_STATUS(SH_TRANSPORT_ERR_INVALID_ARGUMENT,
                  sh_transport_write_all(&no_writer, data, 1u, 1u, NULL));
    ASSERT_STATUS(SH_TRANSPORT_ERR_INVALID_ARGUMENT,
                  sh_transport_write_all(&transport, NULL, 1u, 1u, NULL));
    ASSERT_STATUS(SH_TRANSPORT_ERR_INVALID_ARGUMENT,
                  sh_transport_write_all(&transport, data, 1u, 0u, NULL));

    ASSERT_STATUS(SH_TRANSPORT_OK,
                  sh_transport_write_all(&transport, NULL, 0u, 0u, &written));
    ASSERT_SIZE(0u, written);
    ASSERT_SIZE(0u, fake.next_step);
}

static void test_optional_flush(void)
{
    fake_transport_t fake = make_fake(NULL, 0u);
    sh_transport_t transport = make_transport(&fake);

    ASSERT_STATUS(SH_TRANSPORT_ERR_INVALID_ARGUMENT, sh_transport_flush(NULL));

    transport.flush = NULL;
    ASSERT_STATUS(SH_TRANSPORT_OK, sh_transport_flush(&transport));
    ASSERT_SIZE(0u, fake.flush_calls);

    transport.flush = fake_flush;
    fake.flush_status = SH_TRANSPORT_WOULD_BLOCK;
    ASSERT_STATUS(SH_TRANSPORT_WOULD_BLOCK, sh_transport_flush(&transport));
    ASSERT_SIZE(1u, fake.flush_calls);

    fake.flush_status = SH_TRANSPORT_ERR_IO;
    ASSERT_STATUS(SH_TRANSPORT_ERR_IO, sh_transport_flush(&transport));
    ASSERT_SIZE(2u, fake.flush_calls);

    fake.flush_status = (sh_transport_status_t)77;
    ASSERT_STATUS(SH_TRANSPORT_ERR_PROTOCOL, sh_transport_flush(&transport));
    ASSERT_SIZE(3u, fake.flush_calls);
}

int main(void)
{
    test_full_write();
    test_partial_writes_preserve_order();
    test_zero_progress_is_rejected();
    test_would_block_then_success();
    test_would_block_is_bounded();
    test_partial_would_block_counts_progress();
    test_partial_progress_can_exhaust_attempts();
    test_io_error_preserves_reported_progress();
    test_invalid_callback_reports_protocol_error();
    test_arguments_and_empty_write();
    test_optional_flush();

    puts("test_transport: all tests passed");
    return 0;
}
