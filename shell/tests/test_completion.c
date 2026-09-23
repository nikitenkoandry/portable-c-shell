#include "test_common.h"

static int noop(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell; (void)argc; (void)argv; (void)ctx;
    return SH_OK;
}

static const sh_cmd_t wifi_cmds[] = {
    SH_CMD("scan", "", "Scan", noop),
    SH_CMD("set", "", "Set", noop),
};

static const sh_cmd_t root_cmds[] = {
    SH_CMD_SUB("wifi", "", "Wi-Fi", wifi_cmds),
};

int main(void)
{
    test_shell_t ts;
    test_shell_init(&ts);
    sh_register_commands(&ts.shell, root_cmds, SH_ARRAY_SIZE(root_cmds));

    sh_input_byte(&ts.shell, 'w');
    sh_input_byte(&ts.shell, '\t');
    ASSERT_STREQ("wifi ", ts.shell.line);

    ts.shell.line_len = ts.shell.cursor = 0;
    ts.shell.line[0] = '\0';
    sh_input_byte(&ts.shell, 'w');
    sh_input_byte(&ts.shell, 'i');
    sh_input_byte(&ts.shell, 'f');
    sh_input_byte(&ts.shell, 'i');
    sh_input_byte(&ts.shell, ' ');
    sh_input_byte(&ts.shell, 's');
    sh_input_byte(&ts.shell, 'c');
    sh_input_byte(&ts.shell, '\t');
    ASSERT_STREQ("wifi scan ", ts.shell.line);
    return 0;
}
