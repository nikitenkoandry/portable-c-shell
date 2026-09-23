#include "test_common.h"

static int calls;

static int cmd_leaf(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)ctx;
    calls++;
    ASSERT_EQ_INT(2, argc);
    ASSERT_TRUE(strcmp(argv[0], "ssid") == 0 || strcmp(argv[0], "pass") == 0);
    ASSERT_STREQ("value", argv[1]);
    return SH_OK;
}

static const sh_cmd_t set_cmds[] = {
    SH_CMD("ssid", "<ssid>", "Set SSID", cmd_leaf),
    SH_CMD_SENSITIVE("pass", "<password>", "Set password", cmd_leaf),
};

static const sh_cmd_t wifi_cmds[] = {
    SH_CMD_SUB("set", "", "Settings", set_cmds),
};

static const sh_cmd_t root_cmds[] = {
    SH_CMD_SUB("wifi", "", "Wi-Fi", wifi_cmds),
};

int main(void)
{
    test_shell_t ts;
    test_shell_init(&ts);
    sh_register_commands(&ts.shell, root_cmds, SH_ARRAY_SIZE(root_cmds));

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "wifi set ssid value"));
    ASSERT_EQ_INT(1, calls);

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "wifi"));
    ASSERT_CONTAINS(ts.out.data, "Subcommands:");

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "help wifi"));
    ASSERT_CONTAINS(ts.out.data, "Wi-Fi");

    calls = 0;
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "wifi set pass value"));
    ASSERT_STREQ("wifi set pass <hidden>", sh_history_get_newest(&ts.shell, 0));
    return 0;
}
