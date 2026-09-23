#include "test_common.h"

static int handler_calls;
static void *last_context;

static int capture_handler(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    ASSERT_EQ_INT(2, argc);
    ASSERT_STREQ("set", argv[0]);
    ASSERT_STREQ("value", argv[1]);
    handler_calls++;
    last_context = ctx;
    return 17;
}

static int noop_handler(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    return SH_OK;
}

static void test_argument_contract_and_context(void)
{
    static int command_context;
    static const sh_cmd_t commands[] = {
        { "set", "<value>", "Set value", capture_handler,
          NULL, 0u, SH_CMD_FLAG_NONE, 2u, 2u, &command_context, NULL },
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_line(&ts.shell, "set"));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_line(&ts.shell, "set a b"));
    ASSERT_EQ_INT(0, handler_calls);

    ASSERT_EQ_INT(17, sh_execute_line(&ts.shell, "set value"));
    ASSERT_EQ_INT(1, handler_calls);
    ASSERT_TRUE(last_context == &command_context);
}

static void test_registry_validation(void)
{
    static const sh_cmd_t duplicate[] = {
        SH_CMD("one", "", "One", capture_handler),
        SH_CMD("one", "", "Duplicate", capture_handler),
    };
    static const sh_cmd_t reserved[] = {
        SH_CMD("help", "", "Conflicts with built-in", capture_handler),
    };
    static const sh_cmd_t bad_children[] = {
        { "bad", "", "Bad children", NULL,
          NULL, 1u, SH_CMD_FLAG_NONE, 1u, 1u, NULL, NULL },
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, duplicate,
                                       SH_ARRAY_SIZE(duplicate)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, reserved,
                                       SH_ARRAY_SIZE(reserved)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, bad_children,
                                       SH_ARRAY_SIZE(bad_children)));
}

static void test_no_history_flag(void)
{
    static const sh_cmd_t commands[] = {
        SH_CMD_NO_HISTORY("secret", "", "Do not store", noop_handler),
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    (void)sh_execute_line(&ts.shell, "secret value");
    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));
}

int main(void)
{
    test_argument_contract_and_context();
    test_registry_validation();
    test_no_history_flag();
    return 0;
}
