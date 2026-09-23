#include "test_common.h"

static int calls;

static int cmd_count(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    calls++;
    return 37;
}

static int cmd_help_override(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    return sh_puts(shell, "application help\r\n");
}

static const sh_cmd_t commands[] = {
    SH_CMD("count", "", "Count calls", cmd_count),
};

static const sh_cmd_t help_command[] = {
    SH_CMD("help", "", "Application help", cmd_help_override),
};

static void test_lifecycle_and_feed_status(void)
{
    test_shell_t ts;
    static const unsigned char input[] = "count\rcount\n";

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_OK, sh_start(&ts.shell));
    ASSERT_EQ_INT(SH_ERR_BUSY, sh_start(&ts.shell));
    ASSERT_EQ_INT(37, sh_feed(&ts.shell, input, sizeof(input) - 1u));
    ASSERT_EQ_INT(2, calls);
    ASSERT_EQ_INT(SH_OK, sh_stop(&ts.shell));
}

static void test_execute_argv_and_validation(void)
{
    test_shell_t ts;
    char *argv[] = { "count" };

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(37, sh_execute_argv(&ts.shell, 1u, argv));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_execute_argv(&ts.shell, ts.shell.argv_capacity + 1u, argv));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_feed_byte(NULL, 'x'));
}

static void test_line_limit_status_and_recovery(void)
{
    test_shell_t ts;
    size_t i;

    test_shell_init(&ts);
    ts.shell.echo_enabled = false;
    for (i = 0u; i < ts.shell.cfg.line_max; i++) {
        ASSERT_EQ_INT(SH_OK, sh_feed_byte(&ts.shell, 'x'));
    }
    ASSERT_EQ_INT(SH_ERR_LINE_TOO_LONG, sh_feed_byte(&ts.shell, 'y'));
    ASSERT_EQ_INT((int)ts.shell.cfg.line_max, (int)ts.shell.line_len);
    ASSERT_EQ_INT(SH_OK, sh_feed_byte(&ts.shell, 0x15u));
    ASSERT_EQ_INT(0, (int)ts.shell.line_len);
}

static void test_dumb_terminal_and_linear_append_output(void)
{
    test_shell_t ts;
    size_t before;
    size_t i;

    test_shell_init(&ts);
    ts.shell.cfg.ansi_enabled = false;
    before = ts.out.len;
    for (i = 0u; i < 64u; i++) {
        ASSERT_EQ_INT(SH_OK, sh_feed_byte(&ts.shell, 'a'));
    }
    ASSERT_EQ_INT(64, (int)(ts.out.len - before));
    (void)sh_feed_byte(&ts.shell, 0x0cu);
    ASSERT_TRUE(strstr(ts.out.data, "\x1b") == NULL);
}

static void test_builtins_can_be_disabled(void)
{
    test_shell_t ts;

    test_shell_init(&ts);
    ts.shell.cfg.builtins_enabled = false;
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, help_command,
                                               SH_ARRAY_SIZE(help_command)));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "help"));
    ASSERT_CONTAINS(ts.out.data, "application help");
}

int main(void)
{
    test_lifecycle_and_feed_status();
    test_execute_argv_and_validation();
    test_line_limit_status_and_recovery();
    test_dumb_terminal_and_linear_append_output();
    test_builtins_can_be_disabled();
    return 0;
}
