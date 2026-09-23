#include "test_common.h"

typedef struct {
    int calls;
    int argc;
    char argv[SH_MAX_ARGC][SH_MAX_LINE_LEN + 1u];
} editor_capture_t;

static editor_capture_t capture;

static void feed_text(sh_t *shell, const char *text)
{
    while (*text != '\0') {
        sh_input_byte(shell, (unsigned char)*text++);
    }
}

static void feed_ansi_key(sh_t *shell, const char *sequence)
{
    sh_input_byte(shell, 0x1bu);
    feed_text(shell, sequence);
}

static void clear_output(test_shell_t *ts)
{
    ts->out.len = 0u;
    ts->out.data[0] = '\0';
}

static size_t count_occurrences(const char *text, const char *needle)
{
    size_t count = 0u;
    size_t needle_len = strlen(needle);

    ASSERT_TRUE(needle_len != 0u);
    while ((text = strstr(text, needle)) != NULL) {
        count++;
        text += needle_len;
    }
    return count;
}

static int cmd_capture(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    int i;

    (void)shell;
    (void)user_ctx;
    capture.calls++;
    capture.argc = argc;
    for (i = 0; i < argc && i < (int)SH_MAX_ARGC; i++) {
        size_t len = strlen(argv[i]);
        if (len > SH_MAX_LINE_LEN) {
            len = SH_MAX_LINE_LEN;
        }
        memcpy(capture.argv[i], argv[i], len);
        capture.argv[i][len] = '\0';
    }
    return SH_OK;
}

static int cmd_emit(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return sh_puts(shell, "RESULT\r\n");
}

static int cmd_sensitive(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return SH_OK;
}

static const sh_cmd_t commands[] = {
    SH_CMD("run", "[args...]", "Capture editor result", cmd_capture),
    SH_CMD("ok", "", "Short recovery command", cmd_capture),
    SH_CMD("emit", "", "Emit command output", cmd_emit),
    SH_CMD_SENSITIVE("secret", "<value>", "Sensitive input", cmd_sensitive),
};

static void init_editor(test_shell_t *ts)
{
    memset(&capture, 0, sizeof(capture));
    test_shell_init(ts);
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&ts->shell, commands,
                                       SH_ARRAY_SIZE(commands)));
}

static void init_editor_with_line_max(test_shell_t *ts, size_t line_max)
{
    memset(&capture, 0, sizeof(capture));
    memset(ts, 0, sizeof(*ts));
    sh_default_config(&ts->cfg);
    ts->cfg.write = test_write;
    ts->cfg.write_ctx = &ts->out;
    ts->cfg.line_max = line_max;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&ts->shell, &ts->cfg,
                          ts->line, sizeof(ts->line),
                          ts->argv, SH_ARRAY_SIZE(ts->argv),
                          &ts->history[0][0], SH_ARRAY_SIZE(ts->history),
                          sizeof(ts->history[0]),
                          ts->draft, sizeof(ts->draft)));
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&ts->shell, commands,
                                       SH_ARRAY_SIZE(commands)));
}

static void assert_line_state(const test_shell_t *ts, const char *line,
                              size_t cursor)
{
    ASSERT_STREQ(line, ts->shell.line);
    ASSERT_EQ_INT((int)strlen(line), (int)ts->shell.line_len);
    ASSERT_EQ_INT((int)cursor, (int)ts->shell.cursor);
    ASSERT_TRUE(ts->shell.cursor <= ts->shell.line_len);
    ASSERT_TRUE(ts->shell.line_len < ts->shell.line_capacity);
    ASSERT_EQ_INT(0, (unsigned char)ts->shell.line[ts->shell.line_len]);
}

static void test_backspace_at_middle_and_boundary(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "abcd");
    feed_ansi_key(&ts.shell, "[D");
    feed_ansi_key(&ts.shell, "[D");
    sh_input_byte(&ts.shell, 0x7fu);
    assert_line_state(&ts, "acd", 1u);

    feed_ansi_key(&ts.shell, "[H");
    sh_input_byte(&ts.shell, '\b');
    assert_line_state(&ts, "acd", 0u);
}

static void test_delete_home_end_left_right(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "abcd");
    feed_ansi_key(&ts.shell, "[D");
    feed_ansi_key(&ts.shell, "[D");
    assert_line_state(&ts, "abcd", 2u);

    feed_ansi_key(&ts.shell, "[3~");
    assert_line_state(&ts, "abd", 2u);
    feed_ansi_key(&ts.shell, "[H");
    assert_line_state(&ts, "abd", 0u);
    feed_ansi_key(&ts.shell, "[C");
    feed_text(&ts.shell, "X");
    assert_line_state(&ts, "aXbd", 2u);
    feed_ansi_key(&ts.shell, "[F");
    assert_line_state(&ts, "aXbd", 4u);
    feed_ansi_key(&ts.shell, "[C");
    assert_line_state(&ts, "aXbd", 4u);
}

static void test_ctrl_c_cancels_and_recovers(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "run cancelled");
    clear_output(&ts);
    sh_input_byte(&ts.shell, 0x03u);

    assert_line_state(&ts, "", 0u);
    ASSERT_EQ_INT(-1, ts.shell.history_view);
    ASSERT_STREQ("", ts.shell.draft);
    ASSERT_CONTAINS(ts.out.data, "^C\r\n");
    ASSERT_CONTAINS(ts.out.data, ts.cfg.prompt);

    feed_text(&ts.shell, "ok\r");
    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_STREQ("ok", capture.argv[0]);
}

static void test_ctrl_u_clears_entire_line(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "run keep-right");
    feed_ansi_key(&ts.shell, "[D");
    feed_ansi_key(&ts.shell, "[D");
    sh_input_byte(&ts.shell, 0x15u);
    assert_line_state(&ts, "", 0u);
}

static void test_ctrl_w_deletes_words_left(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "one two   three");
    sh_input_byte(&ts.shell, 0x17u);
    assert_line_state(&ts, "one two   ", 10u);
    sh_input_byte(&ts.shell, 0x17u);
    assert_line_state(&ts, "one ", 4u);
    sh_input_byte(&ts.shell, 0x17u);
    assert_line_state(&ts, "", 0u);
}

static void test_ctrl_l_clears_screen_and_redraws(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "run pending");
    feed_ansi_key(&ts.shell, "[D");
    clear_output(&ts);

    sh_input_byte(&ts.shell, 0x0cu);

    assert_line_state(&ts, "run pending", strlen("run pending") - 1u);
    ASSERT_CONTAINS(ts.out.data, "\x1b[2J\x1b[H");
    ASSERT_CONTAINS(ts.out.data, ts.cfg.prompt);
    ASSERT_CONTAINS(ts.out.data, "run pending");
}

static void run_terminator_case(const unsigned char *terminator,
                                size_t terminator_len)
{
    test_shell_t ts;

    init_editor(&ts);
    sh_set_echo_enabled(&ts.shell, false);
    feed_text(&ts.shell, "ok");
    clear_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, terminator, terminator_len));

    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_EQ_INT(1, (int)sh_history_count(&ts.shell));
    ASSERT_EQ_INT(1, (int)count_occurrences(ts.out.data, ts.cfg.prompt));
    ASSERT_STREQ("", ts.shell.line);
}

static void test_cr_lf_and_crlf_execute_once(void)
{
    static const unsigned char cr[] = { '\r' };
    static const unsigned char lf[] = { '\n' };
    static const unsigned char crlf[] = { '\r', '\n' };

    run_terminator_case(cr, sizeof(cr));
    run_terminator_case(lf, sizeof(lf));
    run_terminator_case(crlf, sizeof(crlf));
}

static void test_split_ansi_sequences_across_input_calls(void)
{
    static const unsigned char esc[] = { 0x1bu };
    static const unsigned char bracket_one[] = { '[', '1' };
    static const unsigned char home_final[] = { '~' };
    static const unsigned char bracket_four[] = { '[', '4' };
    static const unsigned char end_final[] = { '~' };
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "abc");

    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, esc, sizeof(esc)));
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, bracket_one, sizeof(bracket_one)));
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, home_final, sizeof(home_final)));
    assert_line_state(&ts, "abc", 0u);

    feed_text(&ts.shell, "X");
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, esc, sizeof(esc)));
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, bracket_four, sizeof(bracket_four)));
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, end_final, sizeof(end_final)));
    assert_line_state(&ts, "Xabc", 4u);
}

static void test_malformed_escape_recovery(void)
{
    test_shell_t ts;

    init_editor(&ts);
    feed_text(&ts.shell, "ab");
    feed_ansi_key(&ts.shell, "[1234567890~");
    ASSERT_TRUE(!sh_ansi_is_active(&ts.shell.ansi));
    feed_text(&ts.shell, "c");
    assert_line_state(&ts, "abc", 3u);

    sh_input_byte(&ts.shell, 0x1bu);
    sh_input_byte(&ts.shell, 'X');
    ASSERT_TRUE(!sh_ansi_is_active(&ts.shell.ansi));
    assert_line_state(&ts, "abcX", 4u);

    feed_ansi_key(&ts.shell, "[9~");
    feed_text(&ts.shell, "d");
    assert_line_state(&ts, "abcXd", 5u);
}

static void test_line_limit_error_recovers_for_next_command(void)
{
    test_shell_t ts;

    init_editor_with_line_max(&ts, 4u);
    feed_text(&ts.shell, "abcd");
    clear_output(&ts);
    sh_input_byte(&ts.shell, 'e');

    assert_line_state(&ts, "abcd", 4u);
    ASSERT_CONTAINS(ts.out.data, "error: line too long");
    ASSERT_TRUE(ts.shell.line[ts.shell.line_capacity - 1u] == '\0');

    sh_input_byte(&ts.shell, 0x15u);
    feed_text(&ts.shell, "ok\r");
    ASSERT_EQ_INT(1, capture.calls);
    ASSERT_STREQ("ok", capture.argv[0]);
    assert_line_state(&ts, "", 0u);
}

static void test_echo_off_command_output_starts_on_new_line(void)
{
    test_shell_t ts;

    init_editor(&ts);
    sh_set_echo_enabled(&ts.shell, false);
    clear_output(&ts);

    feed_text(&ts.shell, "emit");
    ASSERT_STREQ("", ts.out.data);
    sh_input_byte(&ts.shell, '\r');

    ASSERT_TRUE(strncmp(ts.out.data, "\r\nRESULT\r\n",
                        strlen("\r\nRESULT\r\n")) == 0);
}

static void test_sensitive_typed_value_never_reaches_output(void)
{
    static const char secret_value[] = "S3cr3t-Token-9Z";
    test_shell_t ts;

    init_editor(&ts);
    clear_output(&ts);
    feed_text(&ts.shell, "secret ");
    feed_text(&ts.shell, secret_value);

    ASSERT_TRUE(strstr(ts.out.data, secret_value) == NULL);
    ASSERT_CONTAINS(ts.out.data, "***************");

    sh_input_byte(&ts.shell, '\r');
    ASSERT_TRUE(strstr(ts.out.data, secret_value) == NULL);
    ASSERT_TRUE(strstr(sh_history_get_newest(&ts.shell, 0u), secret_value) == NULL);
    ASSERT_CONTAINS(sh_history_get_newest(&ts.shell, 0u), "<hidden>");
}

typedef void (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t fn;
} named_test_t;

static const named_test_t tests[] = {
    { "backspace", test_backspace_at_middle_and_boundary },
    { "navigation-delete", test_delete_home_end_left_right },
    { "ctrl-c", test_ctrl_c_cancels_and_recovers },
    { "ctrl-u", test_ctrl_u_clears_entire_line },
    { "ctrl-w", test_ctrl_w_deletes_words_left },
    { "ctrl-l", test_ctrl_l_clears_screen_and_redraws },
    { "line-endings", test_cr_lf_and_crlf_execute_once },
    { "split-ansi", test_split_ansi_sequences_across_input_calls },
    { "malformed-ansi", test_malformed_escape_recovery },
    { "line-limit", test_line_limit_error_recovers_for_next_command },
    { "echo-off", test_echo_off_command_output_starts_on_new_line },
    { "sensitive", test_sensitive_typed_value_never_reaches_output },
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
        fprintf(stderr, "unknown line editor test: %s\n", argv[1]);
        return 2;
    }

    for (i = 0u; i < SH_ARRAY_SIZE(tests); i++) {
        tests[i].fn();
    }
    return 0;
}
