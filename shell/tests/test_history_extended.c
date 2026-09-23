#include "test_common.h"

typedef struct {
    int calls;
    int argc[2];
    char argv[2][SH_MAX_ARGC][SH_MAX_LINE_LEN + 1u];
} history_capture_t;

static history_capture_t capture;

static void feed_text(sh_t *shell, const char *text)
{
    while (*text != '\0') {
        sh_input_byte(shell, (unsigned char)*text++);
    }
}

static void feed_ansi_key(sh_t *shell, unsigned char final_byte)
{
    sh_input_byte(shell, 0x1bu);
    sh_input_byte(shell, '[');
    sh_input_byte(shell, final_byte);
}

static void init_with_history_depth(test_shell_t *ts, size_t depth)
{
    memset(ts, 0, sizeof(*ts));
    sh_default_config(&ts->cfg);
    ts->cfg.write = test_write;
    ts->cfg.write_ctx = &ts->out;
    ts->cfg.history_depth = depth;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&ts->shell, &ts->cfg,
                          ts->line, sizeof(ts->line),
                          ts->argv, SH_ARRAY_SIZE(ts->argv),
                          &ts->history[0][0], SH_ARRAY_SIZE(ts->history),
                          sizeof(ts->history[0]),
                          ts->draft, sizeof(ts->draft)));
}

static int cmd_noop(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return SH_OK;
}

static int cmd_capture(sh_t *shell, int argc, char **argv, void *user_ctx)
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

static const sh_cmd_t commands[] = {
    SH_CMD("record", "<value>", "Store a history entry", cmd_noop),
    SH_CMD("capture", "<args...>", "Capture parsed arguments", cmd_capture),
};

static void register_commands(test_shell_t *ts)
{
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&ts->shell, commands,
                                       SH_ARRAY_SIZE(commands)));
}

static void execute_ok(sh_t *shell, const char *line)
{
    ASSERT_EQ_INT(SH_OK, sh_execute_line(shell, line));
}

static void test_capacity_and_filled_count(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 3u);
    register_commands(&ts);

    ASSERT_EQ_INT(3, (int)sh_history_capacity(&ts.shell));
    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));
    ASSERT_TRUE(sh_history_get_newest(&ts.shell, 0u) == NULL);

    execute_ok(&ts.shell, "record one");
    ASSERT_EQ_INT(1, (int)sh_history_count(&ts.shell));
    execute_ok(&ts.shell, "record two");
    ASSERT_EQ_INT(2, (int)sh_history_count(&ts.shell));
    execute_ok(&ts.shell, "record three");
    ASSERT_EQ_INT(3, (int)sh_history_count(&ts.shell));
    execute_ok(&ts.shell, "record four");
    ASSERT_EQ_INT(3, (int)sh_history_count(&ts.shell));
}

static void test_wrap_around_preserves_newest_order(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 3u);
    register_commands(&ts);

    execute_ok(&ts.shell, "record one");
    execute_ok(&ts.shell, "record two");
    execute_ok(&ts.shell, "record three");
    execute_ok(&ts.shell, "record four");
    execute_ok(&ts.shell, "record five");

    ASSERT_EQ_INT(3, (int)sh_history_count(&ts.shell));
    ASSERT_STREQ("record five", sh_history_get_newest(&ts.shell, 0u));
    ASSERT_STREQ("record four", sh_history_get_newest(&ts.shell, 1u));
    ASSERT_STREQ("record three", sh_history_get_newest(&ts.shell, 2u));
    ASSERT_TRUE(sh_history_get_newest(&ts.shell, 3u) == NULL);
}

static void test_duplicate_latest_policy(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 4u);
    register_commands(&ts);

    execute_ok(&ts.shell, "record same");
    execute_ok(&ts.shell, "record same");
    ASSERT_EQ_INT(1, (int)sh_history_count(&ts.shell));

    execute_ok(&ts.shell, "record other");
    execute_ok(&ts.shell, "record same");
    ASSERT_EQ_INT(3, (int)sh_history_count(&ts.shell));
    ASSERT_STREQ("record same", sh_history_get_newest(&ts.shell, 0u));
    ASSERT_STREQ("record other", sh_history_get_newest(&ts.shell, 1u));
    ASSERT_STREQ("record same", sh_history_get_newest(&ts.shell, 2u));
}

static void test_clear_resets_all_history_state(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 3u);
    register_commands(&ts);
    execute_ok(&ts.shell, "record one");
    execute_ok(&ts.shell, "record two");

    feed_text(&ts.shell, "unfinished draft");
    feed_ansi_key(&ts.shell, 'A');
    ASSERT_STREQ("unfinished draft", ts.shell.draft);
    ASSERT_TRUE(ts.shell.history_view >= 0);

    sh_history_clear(&ts.shell);

    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));
    ASSERT_EQ_INT(-1, ts.shell.history_view);
    ASSERT_TRUE(sh_history_get_newest(&ts.shell, 0u) == NULL);

    execute_ok(&ts.shell, "record after-clear");
    ASSERT_EQ_INT(1, (int)sh_history_count(&ts.shell));
    ASSERT_STREQ("record after-clear", sh_history_get_newest(&ts.shell, 0u));
    ASSERT_STREQ("", ts.shell.draft);
}

static void test_draft_restore_and_history_boundaries(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 4u);
    register_commands(&ts);
    execute_ok(&ts.shell, "record oldest");
    execute_ok(&ts.shell, "record newest");

    feed_text(&ts.shell, "draft text");
    feed_ansi_key(&ts.shell, 'A');
    ASSERT_STREQ("record newest", ts.shell.line);
    feed_ansi_key(&ts.shell, 'A');
    ASSERT_STREQ("record oldest", ts.shell.line);
    feed_ansi_key(&ts.shell, 'A');
    ASSERT_STREQ("record oldest", ts.shell.line);

    feed_ansi_key(&ts.shell, 'B');
    ASSERT_STREQ("record newest", ts.shell.line);
    feed_ansi_key(&ts.shell, 'B');
    ASSERT_STREQ("draft text", ts.shell.line);
    ASSERT_EQ_INT((int)strlen("draft text"), (int)ts.shell.cursor);
    feed_ansi_key(&ts.shell, 'B');
    ASSERT_STREQ("draft text", ts.shell.line);
}

static void test_editing_recalled_line_does_not_mutate_history(void)
{
    test_shell_t ts;

    init_with_history_depth(&ts, 3u);
    register_commands(&ts);
    execute_ok(&ts.shell, "record immutable");

    feed_ansi_key(&ts.shell, 'A');
    sh_input_byte(&ts.shell, 0x7fu);
    feed_text(&ts.shell, "X");

    ASSERT_STREQ("record immutablX", ts.shell.line);
    ASSERT_STREQ("record immutable", sh_history_get_newest(&ts.shell, 0u));
}

static void test_quoted_recall_preserves_argument_semantics(void)
{
    static const char input[] =
        "capture \"hello world\" \"\" pre\"mid\"post escaped\\ space \"quote\\\"inside\"";
    test_shell_t ts;
    int i;

    memset(&capture, 0, sizeof(capture));
    init_with_history_depth(&ts, 4u);
    register_commands(&ts);
    sh_set_echo_enabled(&ts.shell, false);

    feed_text(&ts.shell, input);
    sh_input_byte(&ts.shell, '\r');
    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_EQ_INT(6, capture.argc[0]);
    ASSERT_STREQ("capture", capture.argv[0][0]);
    ASSERT_STREQ("hello world", capture.argv[0][1]);
    ASSERT_STREQ("", capture.argv[0][2]);
    ASSERT_STREQ("premidpost", capture.argv[0][3]);
    ASSERT_STREQ("escaped space", capture.argv[0][4]);
    ASSERT_STREQ("quote\"inside", capture.argv[0][5]);
    ASSERT_STREQ(input, sh_history_get_newest(&ts.shell, 0u));

    feed_ansi_key(&ts.shell, 'A');
    ASSERT_STREQ(input, ts.shell.line);
    sh_input_byte(&ts.shell, '\r');

    ASSERT_EQ_INT(2, capture.calls);
    ASSERT_EQ_INT(capture.argc[0], capture.argc[1]);
    for (i = 0; i < capture.argc[0]; i++) {
        ASSERT_STREQ(capture.argv[0][i], capture.argv[1][i]);
    }
}

typedef void (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t fn;
} named_test_t;

static const named_test_t tests[] = {
    { "capacity", test_capacity_and_filled_count },
    { "wrap", test_wrap_around_preserves_newest_order },
    { "duplicate", test_duplicate_latest_policy },
    { "clear", test_clear_resets_all_history_state },
    { "draft", test_draft_restore_and_history_boundaries },
    { "recalled-edit", test_editing_recalled_line_does_not_mutate_history },
    { "quoted-recall", test_quoted_recall_preserves_argument_semantics },
};

int main(int argc, char **argv)
{
    size_t i;

    if (argc == 2) {
        for (i = 0u; i < SH_ARRAY_SIZE(tests); i++) {
            if (strcmp(argv[1], tests[i].name) == 0) {
                tests[i].fn();
                return 0;
            }
        }
        fprintf(stderr, "unknown history test: %s\n", argv[1]);
        return 2;
    }

    for (i = 0u; i < SH_ARRAY_SIZE(tests); i++) {
        tests[i].fn();
    }
    return 0;
}
