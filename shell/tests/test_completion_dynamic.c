#include "test_common.h"

static size_t observed_arg_index;
static void *observed_context;

static const char *mode_completion(sh_t *shell, size_t arg_index,
                                   size_t candidate_index, void *ctx)
{
    static const char *const first_arg[] = {
        "fast", "station", "static"
    };
    (void)shell;
    observed_arg_index = arg_index;
    observed_context = ctx;
    if (arg_index != 1u || candidate_index >= SH_ARRAY_SIZE(first_arg)) {
        return NULL;
    }
    return first_arg[candidate_index];
}

static int noop(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    return SH_OK;
}

static void feed(sh_t *shell, const char *text)
{
    while (*text) {
        sh_input_byte(shell, (unsigned char)*text++);
    }
}

int main(void)
{
    static int context;
    static const sh_cmd_t commands[] = {
        { "mode", "<name>", "Select mode", noop,
          NULL, 0u, SH_CMD_FLAG_NONE, 2u, 2u, &context, mode_completion },
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));

    feed(&ts.shell, "mode f");
    sh_input_byte(&ts.shell, '\t');
    ASSERT_STREQ("mode fast ", ts.shell.line);
    ASSERT_EQ_INT(1, (int)observed_arg_index);
    ASSERT_TRUE(observed_context == &context);

    ts.shell.line_len = 0u;
    ts.shell.cursor = 0u;
    ts.shell.line[0] = '\0';
    feed(&ts.shell, "mode st");
    sh_input_byte(&ts.shell, '\t');
    ASSERT_STREQ("mode stati", ts.shell.line);
    sh_input_byte(&ts.shell, '\t');
    ASSERT_CONTAINS(ts.out.data, "station");
    ASSERT_CONTAINS(ts.out.data, "static");
    return 0;
}
