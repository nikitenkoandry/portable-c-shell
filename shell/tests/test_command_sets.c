#include "test_common.h"

static int alpha_calls;
static int scan_calls;

static int cmd_alpha(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    alpha_calls++;
    return SH_OK;
}

static int cmd_scan(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    scan_calls++;
    return SH_OK;
}

static const sh_cmd_t system_commands[] = {
    SH_CMD_ARG("alpha", "", "Alpha command", cmd_alpha, 1u, 1u),
};

static const sh_cmd_t wifi_children[] = {
    SH_CMD_ARG("scan", "", "Scan networks", cmd_scan, 1u, 1u),
};

static const sh_cmd_t network_commands[] = {
    SH_CMD_SUB("wifi", "", "Wi-Fi commands", wifi_children),
};

static const sh_command_set_t command_sets[] = {
    SH_COMMAND_SET(system_commands),
    SH_COMMAND_SET(network_commands),
};

static const sh_cmd_t duplicate_commands[] = {
    SH_CMD("alpha", "", "Duplicate", cmd_alpha),
};

static const sh_command_set_t duplicate_sets[] = {
    SH_COMMAND_SET(system_commands),
    SH_COMMAND_SET(duplicate_commands),
};

int main(void)
{
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK,
                  sh_register_command_sets(&ts.shell, command_sets,
                                           SH_ARRAY_SIZE(command_sets)));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "alpha"));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "wifi scan"));
    ASSERT_EQ_INT(1, alpha_calls);
    ASSERT_EQ_INT(1, scan_calls);

    ts.shell.line[0] = 'w';
    ts.shell.line[1] = '\0';
    ts.shell.line_len = 1u;
    ts.shell.cursor = 1u;
    ASSERT_EQ_INT(SH_OK, sh_feed_byte(&ts.shell, '\t'));
    ASSERT_STREQ("wifi ", ts.shell.line);

    ts.out.len = 0u;
    ts.out.data[0] = '\0';
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "help"));
    ASSERT_CONTAINS(ts.out.data, "alpha");
    ASSERT_CONTAINS(ts.out.data, "wifi");

    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_command_sets(&ts.shell, duplicate_sets,
                                           SH_ARRAY_SIZE(duplicate_sets)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_register_command_sets(&ts.shell, NULL, 1u));
    return 0;
}
