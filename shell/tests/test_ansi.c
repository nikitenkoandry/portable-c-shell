#include "sh_ansi.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                        \
        if (!(expr)) {                                                          \
            fprintf(stderr, "assert failed: %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #expr);                                 \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

#define ASSERT_EVENT(expected, actual) ASSERT_TRUE((expected) == (actual))

static sh_ansi_event_t feed_sequence(sh_ansi_decoder_t *decoder,
                                     const unsigned char *bytes,
                                     size_t length)
{
    sh_ansi_event_t event = SH_ANSI_EVENT_NONE;
    size_t i;

    for (i = 0u; i < length; ++i) {
        event = sh_ansi_feed(decoder, bytes[i]);
        if (i + 1u < length) {
            ASSERT_EVENT(SH_ANSI_EVENT_NONE, event);
        }
    }

    return event;
}

static void test_arrow_keys(void)
{
    static const unsigned char up[] = {0x1bu, '[', 'A'};
    static const unsigned char down[] = {0x1bu, '[', 'B'};
    static const unsigned char right[] = {0x1bu, '[', 'C'};
    static const unsigned char left[] = {0x1bu, '[', 'D'};
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_UP, feed_sequence(&decoder, up, sizeof(up)));
    ASSERT_EVENT(SH_ANSI_EVENT_DOWN, feed_sequence(&decoder, down, sizeof(down)));
    ASSERT_EVENT(SH_ANSI_EVENT_RIGHT, feed_sequence(&decoder, right, sizeof(right)));
    ASSERT_EVENT(SH_ANSI_EVENT_LEFT, feed_sequence(&decoder, left, sizeof(left)));
}

static void test_home_end_and_delete(void)
{
    static const unsigned char home_short[] = {0x1bu, '[', 'H'};
    static const unsigned char end_short[] = {0x1bu, '[', 'F'};
    static const unsigned char home_tilde[] = {0x1bu, '[', '1', '~'};
    static const unsigned char delete_tilde[] = {0x1bu, '[', '3', '~'};
    static const unsigned char end_tilde[] = {0x1bu, '[', '4', '~'};
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_HOME,
                 feed_sequence(&decoder, home_short, sizeof(home_short)));
    ASSERT_EVENT(SH_ANSI_EVENT_END,
                 feed_sequence(&decoder, end_short, sizeof(end_short)));
    ASSERT_EVENT(SH_ANSI_EVENT_HOME,
                 feed_sequence(&decoder, home_tilde, sizeof(home_tilde)));
    ASSERT_EVENT(SH_ANSI_EVENT_DELETE,
                 feed_sequence(&decoder, delete_tilde, sizeof(delete_tilde)));
    ASSERT_EVENT(SH_ANSI_EVENT_END,
                 feed_sequence(&decoder, end_tilde, sizeof(end_tilde)));
}

static void test_sequence_split_between_feed_calls(void)
{
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, 0x1bu));
    ASSERT_TRUE(sh_ansi_is_active(&decoder));
    ASSERT_TRUE(sh_ansi_last_byte_consumed(&decoder));
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '['));
    ASSERT_TRUE(sh_ansi_is_active(&decoder));
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '3'));
    ASSERT_TRUE(sh_ansi_is_active(&decoder));
    ASSERT_EVENT(SH_ANSI_EVENT_DELETE, sh_ansi_feed(&decoder, '~'));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
    ASSERT_TRUE(sh_ansi_last_byte_consumed(&decoder));
}

static void test_plain_byte_is_not_consumed(void)
{
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, 'x'));
    ASSERT_TRUE(!sh_ansi_last_byte_consumed(&decoder));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
}

static void test_unknown_sequence_recovery(void)
{
    static const unsigned char unknown[] = {0x1bu, '[', '9', '~'};
    static const unsigned char valid[] = {0x1bu, '[', 'A'};
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_NONE,
                 feed_sequence(&decoder, unknown, sizeof(unknown)));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
    ASSERT_EVENT(SH_ANSI_EVENT_UP,
                 feed_sequence(&decoder, valid, sizeof(valid)));
}

static void test_too_long_sequence_recovery(void)
{
    static const unsigned char valid[] = {0x1bu, '[', 'C'};
    sh_ansi_decoder_t decoder;
    size_t i;

    sh_ansi_init(&decoder);
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, 0x1bu));
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '['));

    for (i = 0u; i < SH_ANSI_CSI_MAX + 3u; ++i) {
        ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '1'));
    }

    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '~'));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
    ASSERT_EVENT(SH_ANSI_EVENT_RIGHT,
                 feed_sequence(&decoder, valid, sizeof(valid)));
}

static void test_escape_restarts_damaged_sequence(void)
{
    sh_ansi_decoder_t decoder;
    size_t i;

    sh_ansi_init(&decoder);
    sh_ansi_feed(&decoder, 0x1bu);
    sh_ansi_feed(&decoder, '[');
    for (i = 0u; i < SH_ANSI_CSI_MAX + 1u; ++i) {
        sh_ansi_feed(&decoder, ';');
    }

    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, 0x1bu));
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, '['));
    ASSERT_EVENT(SH_ANSI_EVENT_DOWN, sh_ansi_feed(&decoder, 'B'));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
}

static void test_malformed_escape_and_reset(void)
{
    sh_ansi_decoder_t decoder;

    sh_ansi_init(&decoder);
    sh_ansi_feed(&decoder, 0x1bu);
    ASSERT_EVENT(SH_ANSI_EVENT_NONE, sh_ansi_feed(&decoder, 'X'));
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));

    sh_ansi_feed(&decoder, 0x1bu);
    sh_ansi_feed(&decoder, '[');
    ASSERT_TRUE(sh_ansi_is_active(&decoder));
    sh_ansi_reset(&decoder);
    ASSERT_TRUE(!sh_ansi_is_active(&decoder));
    ASSERT_TRUE(!sh_ansi_last_byte_consumed(&decoder));
}

int main(void)
{
    test_arrow_keys();
    test_home_end_and_delete();
    test_sequence_split_between_feed_calls();
    test_plain_byte_is_not_consumed();
    test_unknown_sequence_recovery();
    test_too_long_sequence_recovery();
    test_escape_restarts_damaged_sequence();
    test_malformed_escape_and_reset();
    return 0;
}
