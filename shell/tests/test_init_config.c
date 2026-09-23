#include "test_common.h"

SH_STORAGE_DEFINE(small_storage, 64u, 8u, 4u);

static void test_static_storage_wrapper(void)
{
    sh_t shell;
    sh_config_t cfg;

    sh_default_config(&cfg);
    cfg.line_max = 64u;
    cfg.argc_max = 8u;
    cfg.history_depth = 4u;
    ASSERT_EQ_INT(SH_OK, sh_init_static(&shell, &cfg, &small_storage));
    ASSERT_EQ_INT(64, (int)shell.cfg.line_max);
    ASSERT_EQ_INT(8, (int)shell.argv_capacity);
    ASSERT_EQ_INT(4, (int)sh_history_capacity(&shell));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_init_static(&shell, &cfg, NULL));
}

static void test_history_can_be_disabled(void)
{
    sh_t shell;
    sh_config_t cfg;
    char line[33];
    char *argv[4];

    sh_default_config(&cfg);
    cfg.line_max = 32u;
    cfg.argc_max = 4u;
    cfg.history_depth = 0u;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&shell, &cfg, line, sizeof(line),
                          argv, SH_ARRAY_SIZE(argv),
                          NULL, 0u, 0u, NULL, 0u));
    ASSERT_EQ_INT(0, (int)sh_history_capacity(&shell));
}

static void test_invalid_storage_limits(void)
{
    sh_t shell;
    sh_config_t cfg;

    sh_default_config(&cfg);
    cfg.line_max = 64u;
    cfg.argc_max = 8u;
    cfg.history_depth = 5u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init_static(&shell, &cfg, &small_storage));

    cfg.history_depth = 4u;
    cfg.line_max = SH_MAX_LINE_LEN + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init_static(&shell, &cfg, &small_storage));
}

int main(void)
{
    test_static_storage_wrapper();
    test_history_can_be_disabled();
    test_invalid_storage_limits();
    return 0;
}
