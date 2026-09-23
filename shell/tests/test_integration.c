#include "test_common.h"

static int called;

static int cmd_ping(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)argc; (void)argv; (void)ctx;
    called++;
    shell->cfg.write("pong\r\n", 6, shell->cfg.write_ctx);
    return SH_OK;
}

static const sh_cmd_t root_cmds[] = {
    SH_CMD("ping", "", "Print pong", cmd_ping),
};

int main(void)
{
    test_shell_t ts;
    const char *input = "ping\r";
    const char *p;

    test_shell_init(&ts);
    sh_register_commands(&ts.shell, root_cmds, SH_ARRAY_SIZE(root_cmds));

    for (p = input; *p; p++) {
        sh_input_byte(&ts.shell, (unsigned char)*p);
    }

    ASSERT_EQ_INT(1, called);
    ASSERT_CONTAINS(ts.out.data, "pong");
    ASSERT_EQ_INT(1, (int)sh_history_count(&ts.shell));
    return 0;
}
