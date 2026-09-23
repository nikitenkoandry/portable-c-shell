#include "test_common.h"

#define SCALE_COMMAND_COUNT 128u

static int noop_handler(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    return SH_OK;
}

static void initialize_commands(sh_cmd_t *commands,
                                char names[SCALE_COMMAND_COUNT][8])
{
    size_t i;

    memset(commands, 0, sizeof(*commands) * SCALE_COMMAND_COUNT);
    for (i = 0u; i < SCALE_COMMAND_COUNT; i++) {
        (void)snprintf(names[i], sizeof(names[i]), "cmd%03u", (unsigned)i);
        commands[i].name = names[i];
        commands[i].usage = "";
        commands[i].help = "Scale command";
        commands[i].handler = noop_handler;
        commands[i].min_args = 1u;
        commands[i].max_args = 1u;
    }
}

int main(void)
{
    test_shell_t ts;
    sh_cmd_t commands[SCALE_COMMAND_COUNT];
    char names[SCALE_COMMAND_COUNT][8];

    initialize_commands(commands, names);
    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&ts.shell, commands,
                                       SCALE_COMMAND_COUNT));

    memcpy(ts.shell.line, "cmd", 4u);
    ts.shell.line_len = 3u;
    ts.shell.cursor = 3u;
    ASSERT_EQ_INT(SH_ERR_AMBIGUOUS, sh_feed_byte(&ts.shell, '\t'));
    ASSERT_STREQ("cmd", ts.shell.line);
    ASSERT_CONTAINS(ts.out.data, "cmd000");
    ASSERT_CONTAINS(ts.out.data, "cmd031");
    ASSERT_CONTAINS(ts.out.data, "... 96 more");

    memcpy(ts.shell.line, "cmd127", 7u);
    ts.shell.line_len = 6u;
    ts.shell.cursor = 6u;
    ASSERT_EQ_INT(SH_OK, sh_feed_byte(&ts.shell, '\t'));
    ASSERT_STREQ("cmd127 ", ts.shell.line);
    return 0;
}
