#include "test_common.h"

typedef struct {
    int calls;
    int argc[2];
    char argv[2][SH_MAX_ARGC][SH_MAX_LINE_LEN + 1u];
} command_capture_t;

static command_capture_t capture;

static void feed_text(sh_t *shell, const char *text)
{
    while (*text) {
        sh_input_byte(shell, (unsigned char)*text++);
    }
}

static void feed_key(sh_t *shell, char key)
{
    sh_input_byte(shell, '\x1b');
    sh_input_byte(shell, '[');
    sh_input_byte(shell, (unsigned char)key);
}

static size_t count_occurrences(const char *text, const char *needle)
{
    size_t count = 0u;
    size_t needle_len = strlen(needle);

    ASSERT_TRUE(needle_len > 0u);
    while ((text = strstr(text, needle)) != NULL) {
        count++;
        text += needle_len;
    }
    return count;
}

static int cmd_count(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    int call = capture.calls;
    int i;

    (void)shell;
    (void)user_ctx;
    if (call < (int)SH_ARRAY_SIZE(capture.argc)) {
        capture.argc[call] = argc;
        for (i = 0; i < argc && i < (int)SH_MAX_ARGC; i++) {
            size_t len = strlen(argv[i]);
            if (len > SH_MAX_LINE_LEN) {
                len = SH_MAX_LINE_LEN;
            }
            memcpy(capture.argv[call][i], argv[i], len);
            capture.argv[call][i][len] = '\0';
        }
    }
    capture.calls++;
    return SH_OK;
}

static int cmd_noop(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return SH_OK;
}

static const sh_cmd_t crlf_commands[] = {
    SH_CMD("ping", "", "Count executions", cmd_count),
};

static const sh_cmd_t history_commands[] = {
    SH_CMD("say", "<text>", "Capture a quoted argument", cmd_count),
};

static const sh_cmd_t completion_commands[] = {
    SH_CMD("wifi", "", "Wi-Fi commands", cmd_noop),
};

static void test_crlf_executes_once_and_prints_one_prompt(void)
{
    test_shell_t ts;

    memset(&capture, 0, sizeof(capture));
    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, crlf_commands,
                                               SH_ARRAY_SIZE(crlf_commands)));
    sh_set_echo_enabled(&ts.shell, false);

    feed_text(&ts.shell, "ping\r\n");

    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_EQ_INT(1, (int)count_occurrences(ts.out.data, ts.cfg.prompt));
}

static void test_quoted_argument_survives_history_recall(void)
{
    test_shell_t ts;

    memset(&capture, 0, sizeof(capture));
    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, history_commands,
                                               SH_ARRAY_SIZE(history_commands)));
    sh_set_echo_enabled(&ts.shell, false);

    feed_text(&ts.shell, "say \"hello world\"\r");
    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_EQ_INT(2, capture.argc[0]);
    ASSERT_STREQ("say", capture.argv[0][0]);
    ASSERT_STREQ("hello world", capture.argv[0][1]);

    feed_key(&ts.shell, 'A');
    sh_input_byte(&ts.shell, '\r');

    ASSERT_EQ_INT(2, capture.calls);
    ASSERT_EQ_INT(capture.argc[0], capture.argc[1]);
    ASSERT_STREQ(capture.argv[0][0], capture.argv[1][0]);
    ASSERT_STREQ(capture.argv[0][1], capture.argv[1][1]);
}

static void test_exact_completion_adds_space(void)
{
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, completion_commands,
                                               SH_ARRAY_SIZE(completion_commands)));

    feed_text(&ts.shell, "wifi");
    sh_input_byte(&ts.shell, '\t');

    ASSERT_STREQ("wifi ", ts.shell.line);
    ASSERT_EQ_INT(5, (int)ts.shell.cursor);
}

static void test_completion_in_middle_preserves_right_side(void)
{
    test_shell_t ts;
    int i;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, completion_commands,
                                               SH_ARRAY_SIZE(completion_commands)));

    feed_text(&ts.shell, "wi tail");
    for (i = 0; i < 5; i++) {
        feed_key(&ts.shell, 'D');
    }
    ASSERT_EQ_INT(2, (int)ts.shell.cursor);

    sh_input_byte(&ts.shell, '\t');

    ASSERT_STREQ("wifi tail", ts.shell.line);
    ASSERT_EQ_INT(4, (int)ts.shell.cursor);
}

static void test_empty_completion_lists_builtins(void)
{
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, completion_commands,
                                               SH_ARRAY_SIZE(completion_commands)));

    sh_input_byte(&ts.shell, '\t');

    ASSERT_CONTAINS(ts.out.data, "help");
    ASSERT_CONTAINS(ts.out.data, "history");
    ASSERT_CONTAINS(ts.out.data, "echo");
    ASSERT_CONTAINS(ts.out.data, "clear");
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        if (strcmp(argv[1], "crlf") == 0) {
            test_crlf_executes_once_and_prints_one_prompt();
        } else if (strcmp(argv[1], "quoted-history") == 0) {
            test_quoted_argument_survives_history_recall();
        } else if (strcmp(argv[1], "exact-tab") == 0) {
            test_exact_completion_adds_space();
        } else if (strcmp(argv[1], "middle-tab") == 0) {
            test_completion_in_middle_preserves_right_side();
        } else if (strcmp(argv[1], "builtins-tab") == 0) {
            test_empty_completion_lists_builtins();
        } else {
            fprintf(stderr, "unknown regression test: %s\n", argv[1]);
            return 2;
        }
        return 0;
    }

    test_crlf_executes_once_and_prints_one_prompt();
    test_quoted_argument_survives_history_recall();
    test_exact_completion_adds_space();
    test_completion_in_middle_preserves_right_side();
    test_empty_completion_lists_builtins();
    return 0;
}
