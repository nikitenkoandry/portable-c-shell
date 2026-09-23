#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "sh_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define TEST_MAYBE_UNUSED __attribute__((unused))
#else
#define TEST_MAYBE_UNUSED
#endif

#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "assert failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define ASSERT_EQ_INT(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)
#define ASSERT_CONTAINS(haystack, needle) ASSERT_TRUE(strstr((haystack), (needle)) != NULL)

typedef struct {
    char data[8192];
    size_t len;
} test_output_t;

static void test_write(const char *data, size_t len, void *ctx)
{
    test_output_t *out = (test_output_t *)ctx;
    if (out->len + len >= sizeof(out->data)) {
        len = sizeof(out->data) - out->len - 1u;
    }
    memcpy(out->data + out->len, data, len);
    out->len += len;
    out->data[out->len] = '\0';
}

typedef struct {
    sh_t shell;
    sh_config_t cfg;
    char line[SH_MAX_LINE_LEN + 1u];
    char *argv[SH_MAX_ARGC];
    char history[SH_HISTORY_DEFAULT_DEPTH][SH_MAX_LINE_LEN + 1u];
    char draft[SH_MAX_LINE_LEN + 1u];
    test_output_t out;
} test_shell_t;

static void TEST_MAYBE_UNUSED test_shell_init(test_shell_t *ts)
{
    memset(ts, 0, sizeof(*ts));
    sh_default_config(&ts->cfg);
    ts->cfg.write = test_write;
    ts->cfg.write_ctx = &ts->out;
    ASSERT_EQ_INT(SH_OK, sh_init(&ts->shell, &ts->cfg, ts->line, sizeof(ts->line),
                                 ts->argv, SH_ARRAY_SIZE(ts->argv),
                                 &ts->history[0][0], SH_ARRAY_SIZE(ts->history), sizeof(ts->history[0]),
                                 ts->draft, sizeof(ts->draft)));
}

#endif
