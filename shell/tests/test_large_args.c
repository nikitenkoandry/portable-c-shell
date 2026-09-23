#include "test_common.h"

static int handler_calls;
static int handler_argc;

static int cmd_many(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)ctx;
    handler_calls++;
    handler_argc = argc;
    ASSERT_STREQ("many", argv[0]);
    return SH_OK;
}

static const sh_cmd_t commands[] = {
    SH_CMD_ARG("many", "[arg ...]", "Accept many arguments",
               cmd_many, 1u, SH_MAX_ARGC),
};

static void build_line(char *line, size_t size, size_t argc)
{
    size_t used = 0u;
    size_t i;

    ASSERT_TRUE(size >= 5u);
    memcpy(line, "many", 5u);
    used = 4u;
    for (i = 1u; i < argc; i++) {
        ASSERT_TRUE(used + 2u < size);
        line[used++] = ' ';
        line[used++] = 'x';
        line[used] = '\0';
    }
}

static void test_tokenizer_boundaries(void)
{
    char line[SH_MAX_LINE_LEN + 1u];
    char *argv[SH_MAX_ARGC];
    int argc = 0;

    build_line(line, sizeof(line), SH_MAX_ARGC);
    ASSERT_EQ_INT(SH_OK, sh_tokenize(line, argv, SH_ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(SH_MAX_ARGC, argc);
    ASSERT_STREQ("many", argv[0]);
    ASSERT_STREQ("x", argv[SH_MAX_ARGC - 1u]);

    build_line(line, sizeof(line), SH_MAX_ARGC + 1u);
    ASSERT_EQ_INT(SH_ERR_TOO_MANY_ARGS,
                  sh_tokenize(line, argv, SH_ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
}

static void test_dispatch_boundaries_and_recovery(void)
{
    test_shell_t ts;
    char line[SH_MAX_LINE_LEN + 1u];

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));

    build_line(line, sizeof(line), SH_MAX_ARGC);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, line));
    ASSERT_EQ_INT(1, handler_calls);
    ASSERT_EQ_INT(SH_MAX_ARGC, handler_argc);

    build_line(line, sizeof(line), SH_MAX_ARGC + 1u);
    ASSERT_EQ_INT(SH_ERR_TOO_MANY_ARGS, sh_execute_line(&ts.shell, line));
    ASSERT_EQ_INT(1, handler_calls);

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "many recovered"));
    ASSERT_EQ_INT(2, handler_calls);
    ASSERT_EQ_INT(2, handler_argc);
}

int main(void)
{
    test_tokenizer_boundaries();
    test_dispatch_boundaries_and_recovery();
    return 0;
}
