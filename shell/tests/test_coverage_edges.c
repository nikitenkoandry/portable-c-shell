#include "test_common.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

int sh_complete_line_internal(sh_t *shell);

SH_STORAGE_DEFINE(edge_storage, 16u, 4u, 2u);

typedef struct {
    sh_transport_status_t write_status;
    sh_transport_status_t flush_status;
    size_t write_amount;
    size_t write_calls;
    size_t flush_calls;
} edge_transport_t;

typedef struct {
    size_t calls;
    bool saw_context;
    bool saw_login_path;
} mask_probe_t;

static int noop_handler(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    return SH_OK;
}

static sh_transport_status_t edge_transport_write(void *ctx,
                                                  const uint8_t *data,
                                                  size_t len,
                                                  size_t *written)
{
    edge_transport_t *probe = (edge_transport_t *)ctx;
    size_t amount = probe->write_amount;

    (void)data;
    probe->write_calls++;
    if (amount > len && probe->write_status != SH_TRANSPORT_OK) {
        amount = len;
    }
    *written = amount;
    return probe->write_status;
}

static sh_transport_status_t edge_transport_flush(void *ctx)
{
    edge_transport_t *probe = (edge_transport_t *)ctx;

    probe->flush_calls++;
    return probe->flush_status;
}

static void init_transport_shell(test_shell_t *ts,
                                 sh_transport_t *transport,
                                 edge_transport_t *probe)
{
    memset(ts, 0, sizeof(*ts));
    memset(probe, 0, sizeof(*probe));
    probe->write_status = SH_TRANSPORT_OK;
    probe->flush_status = SH_TRANSPORT_OK;
    transport->write = edge_transport_write;
    transport->flush = edge_transport_flush;
    transport->ctx = probe;

    sh_default_config(&ts->cfg);
    ts->cfg.transport = transport;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&ts->shell, &ts->cfg,
                          ts->line, sizeof(ts->line),
                          ts->argv, SH_ARRAY_SIZE(ts->argv),
                          &ts->history[0][0], SH_ARRAY_SIZE(ts->history),
                          sizeof(ts->history[0]),
                          ts->draft, sizeof(ts->draft)));
}

static void reset_output(test_shell_t *ts)
{
    ts->out.len = 0u;
    ts->out.data[0] = '\0';
}

static void set_editor_line(sh_t *shell, const char *text, size_t cursor)
{
    size_t len = strlen(text);

    ASSERT_TRUE(len < shell->line_capacity);
    memcpy(shell->line, text, len + 1u);
    shell->line_len = len;
    shell->cursor = cursor;
}

static void feed_text(sh_t *shell, const char *text)
{
    while (*text != '\0') {
        ASSERT_EQ_INT(SH_OK, sh_input_byte(shell, (unsigned char)*text));
        text++;
    }
}

static void feed_csi(sh_t *shell, const char *sequence)
{
    while (*sequence != '\0') {
        ASSERT_EQ_INT(SH_OK, sh_input_byte(shell, (unsigned char)*sequence));
        sequence++;
    }
}

static int call_vprintf(sh_t *shell, const char *fmt, ...)
{
    int status;
    va_list ap;

    va_start(ap, fmt);
    status = sh_vprintf(shell, fmt, ap);
    va_end(ap);
    return status;
}

static bool mask_second_argument(const char *path, int arg_index,
                                 const char *arg, void *ctx)
{
    mask_probe_t *probe = (mask_probe_t *)ctx;

    probe->calls++;
    probe->saw_context = ctx == probe;
    if (strcmp(path, "login") == 0) {
        probe->saw_login_path = true;
    }
    return arg != NULL && arg_index == 1;
}

static const char *short_provider(sh_t *shell, size_t arg_index,
                                  size_t candidate_index, void *ctx)
{
    size_t *termination_calls = (size_t *)ctx;
    static const char *const values[] = { "alpha", "alpine" };

    (void)shell;
    ASSERT_EQ_INT(1, (int)arg_index);
    if (candidate_index >= SH_ARRAY_SIZE(values)) {
        (*termination_calls)++;
        return NULL;
    }
    return values[candidate_index];
}

static const char *empty_provider(sh_t *shell, size_t arg_index,
                                  size_t candidate_index, void *ctx)
{
    size_t *calls = (size_t *)ctx;

    (void)shell;
    (void)arg_index;
    (void)candidate_index;
    (*calls)++;
    return NULL;
}

static const char *overflow_provider(sh_t *shell, size_t arg_index,
                                     size_t candidate_index, void *ctx)
{
    static char value[16];
    size_t *calls = (size_t *)ctx;

    (void)shell;
    ASSERT_EQ_INT(1, (int)arg_index);
    (*calls)++;
    (void)snprintf(value, sizeof(value), "item%03u",
                   (unsigned)candidate_index);
    return value;
}

static void test_invalid_and_storage_config(void)
{
    sh_t shell;
    sh_config_t cfg;
    sh_storage_t storage;
    char line[17];
    char *argv[4];
    char history[2][17];
    char draft[17];

    sh_default_config(NULL);
    sh_default_config(&cfg);
    cfg.line_max = 16u;
    cfg.argc_max = 4u;
    cfg.history_depth = 2u;

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init(NULL, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init(&shell, &cfg, NULL, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init(&shell, &cfg, line, 0u, argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init(&shell, &cfg, line, sizeof(line), NULL,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv, 0u,
                          &history[0][0], SH_ARRAY_SIZE(history),
                          sizeof(history[0]), draft, sizeof(draft)));

    cfg.line_max = SH_MAX_LINE_LEN + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    cfg.line_max = 16u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, 16u, argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    cfg.argc_max = SH_MAX_ARGC + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    cfg.argc_max = SH_ARRAY_SIZE(argv) + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    cfg.argc_max = SH_ARRAY_SIZE(argv);
    cfg.history_depth = 3u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    cfg.history_depth = 2u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), NULL,
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), 16u,
                          draft, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          NULL, sizeof(draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), &history[0][0],
                          SH_ARRAY_SIZE(history), sizeof(history[0]),
                          draft, 16u));

    memset(&cfg, 0, sizeof(cfg));
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), NULL, 0u, 0u, NULL, 0u));
    ASSERT_EQ_INT(16, (int)shell.cfg.line_max);
    ASSERT_EQ_INT(4, (int)shell.cfg.argc_max);
    ASSERT_EQ_INT(8, (int)shell.cfg.transport_write_attempts);
    ASSERT_STREQ("sh> ", shell.cfg.prompt);

    sh_default_config(&cfg);
    cfg.line_max = 16u;
    cfg.argc_max = 4u;
    cfg.history_depth = 2u;
    ASSERT_EQ_INT(SH_OK, sh_init_static(&shell, &cfg, &edge_storage));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_init_static(&shell, &cfg, NULL));

    storage = edge_storage;
    storage.line_buffer = NULL;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init_static(&shell, &cfg, &storage));
    storage = edge_storage;
    storage.argv_buffer = NULL;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_init_static(&shell, &cfg, &storage));
    storage = edge_storage;
    storage.history_capacity = 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init_static(&shell, &cfg, &storage));
    storage = edge_storage;
    storage.history_slot_size = 16u;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init_static(&shell, &cfg, &storage));
    storage = edge_storage;
    storage.draft_buffer = NULL;
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_init_static(&shell, &cfg, &storage));
}

static void test_status_strings(void)
{
    static const struct {
        int status;
        const char *text;
    } cases[] = {
        { SH_OK, "ok" },
        { SH_ERR_INVALID_ARG, "invalid argument" },
        { SH_ERR_LINE_TOO_LONG, "line too long" },
        { SH_ERR_TOO_MANY_ARGS, "too many arguments" },
        { SH_ERR_UNTERMINATED_QUOTE, "unterminated quote" },
        { SH_ERR_UNKNOWN_COMMAND, "unknown command" },
        { SH_ERR_AMBIGUOUS, "ambiguous command" },
        { SH_ERR_NO_MEMORY, "not enough memory" },
        { SH_ERR_INVALID_CONFIG, "invalid configuration" },
        { SH_ERR_TRANSPORT, "transport error" },
        { SH_ERR_WOULD_BLOCK, "operation would block" },
        { SH_ERR_TRUNCATED, "result truncated" },
        { SH_ERR_BUSY, "busy" },
        { SH_ERR_INCOMPLETE_COMMAND, "incomplete command" },
        { 12345, "unknown error" },
    };
    size_t i;

    for (i = 0u; i < SH_ARRAY_SIZE(cases); i++) {
        ASSERT_STREQ(cases[i].text, sh_status_string(cases[i].status));
    }
}

static void test_output_lifecycle_and_transport_errors(void)
{
    test_shell_t ts;
    sh_transport_t transport;
    edge_transport_t probe;
    char long_text[320];

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_start(NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_stop(NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_flush(NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_write(NULL, "x", 1u));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_puts(NULL, "x"));

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_write(&ts.shell, NULL, 1u));
    ASSERT_EQ_INT(SH_OK, sh_write(&ts.shell, NULL, 0u));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_puts(&ts.shell, NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_printf(NULL, "%s", "x"));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  call_vprintf(&ts.shell, NULL, "ignored"));
    ASSERT_EQ_INT(SH_OK, sh_flush(&ts.shell));
    ASSERT_EQ_INT(SH_OK, sh_start(&ts.shell));
    ASSERT_EQ_INT(SH_ERR_BUSY, sh_start(&ts.shell));
    ASSERT_TRUE(ts.shell.started);
    ASSERT_EQ_INT(SH_OK, sh_stop(&ts.shell));
    ASSERT_TRUE(!ts.shell.started);

    memset(long_text, 'x', sizeof(long_text) - 1u);
    long_text[sizeof(long_text) - 1u] = '\0';
    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_TRUNCATED,
                  sh_printf(&ts.shell, "%s", long_text));
    ASSERT_EQ_INT(255, (int)ts.out.len);

    memset(&ts, 0, sizeof(ts));
    sh_default_config(&ts.cfg);
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&ts.shell, &ts.cfg, ts.line, sizeof(ts.line),
                          ts.argv, SH_ARRAY_SIZE(ts.argv),
                          &ts.history[0][0], SH_ARRAY_SIZE(ts.history),
                          sizeof(ts.history[0]), ts.draft, sizeof(ts.draft)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG, sh_puts(&ts.shell, "x"));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG, sh_start(&ts.shell));
    ASSERT_TRUE(!ts.shell.started);
    sh_prompt(NULL);

    init_transport_shell(&ts, &transport, &probe);
    probe.write_amount = 3u;
    ASSERT_EQ_INT(SH_OK, sh_write(&ts.shell, "abc", 3u));

    probe.write_status = SH_TRANSPORT_WOULD_BLOCK;
    probe.write_amount = 0u;
    ts.shell.cfg.transport_write_attempts = 1u;
    ASSERT_EQ_INT(SH_ERR_WOULD_BLOCK, sh_write(&ts.shell, "x", 1u));
    ASSERT_EQ_INT(SH_ERR_WOULD_BLOCK, sh_start(&ts.shell));
    ASSERT_TRUE(!ts.shell.started);

    probe.write_status = SH_TRANSPORT_OK;
    probe.write_amount = 1u;
    ASSERT_EQ_INT(SH_ERR_WOULD_BLOCK, sh_write(&ts.shell, "xy", 2u));

    probe.write_status = SH_TRANSPORT_OK;
    probe.write_amount = 0u;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_write(&ts.shell, "x", 1u));

    probe.write_status = SH_TRANSPORT_ERR_IO;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_write(&ts.shell, "x", 1u));

    probe.write_status = (sh_transport_status_t)77;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_write(&ts.shell, "x", 1u));

    probe.write_status = SH_TRANSPORT_OK;
    probe.write_amount = 2u;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_write(&ts.shell, "x", 1u));

    probe.flush_status = SH_TRANSPORT_OK;
    ASSERT_EQ_INT(SH_OK, sh_flush(&ts.shell));
    probe.flush_status = SH_TRANSPORT_WOULD_BLOCK;
    ASSERT_EQ_INT(SH_ERR_WOULD_BLOCK, sh_flush(&ts.shell));
    probe.flush_status = SH_TRANSPORT_ERR_IO;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_flush(&ts.shell));
    probe.flush_status = (sh_transport_status_t)77;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_flush(&ts.shell));

    ts.shell.started = true;
    (void)sh_ansi_feed(&ts.shell.ansi, 0x1bu);
    probe.flush_status = SH_TRANSPORT_ERR_IO;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_stop(&ts.shell));
    ASSERT_TRUE(!ts.shell.started);
    ASSERT_TRUE(!sh_ansi_is_active(&ts.shell.ansi));

    transport.write = NULL;
    ASSERT_EQ_INT(SH_ERR_TRANSPORT, sh_write(&ts.shell, NULL, 0u));
}

static void test_builtins_help_and_incomplete_commands(void)
{
    static const sh_cmd_t child_commands[] = {
        SH_CMD("leaf", "<value>", "Leaf help text", noop_handler),
    };
    static const sh_cmd_t commands[] = {
        SH_CMD("plain", "<value>", "Plain help text", noop_handler),
        SH_CMD_HIDDEN("hidden", "", "Must stay hidden", noop_handler),
        SH_CMD_SUB("group", "<subcommand>", "Group help", child_commands),
        { "broken", "", "No handler", NULL, NULL, 0u,
          SH_CMD_FLAG_NONE, 1u, 1u, NULL, NULL },
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "help"));
    ASSERT_CONTAINS(ts.out.data, "plain");
    ASSERT_TRUE(strstr(ts.out.data, "Must stay hidden") == NULL);
    ASSERT_CONTAINS(ts.out.data, "Built-ins:");

    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "plain help"));
    ASSERT_CONTAINS(ts.out.data, "Plain help text");
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "plain --help"));
    ASSERT_CONTAINS(ts.out.data, "usage: plain <value>");
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "plain -h"));
    ASSERT_CONTAINS(ts.out.data, "Plain help text");
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "help group leaf"));
    ASSERT_CONTAINS(ts.out.data, "group leaf");

    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_UNKNOWN_COMMAND,
                  sh_execute_line(&ts.shell, "help missing"));
    ASSERT_CONTAINS(ts.out.data, "unknown topic");
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_execute_line(&ts.shell, "echo invalid"));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "echo off"));
    ASSERT_TRUE(!sh_echo_is_enabled(&ts.shell));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "echo"));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "echo status"));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "echo on"));
    ASSERT_TRUE(sh_echo_is_enabled(&ts.shell));

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "history status"));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_execute_line(&ts.shell, "history extra"));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "history clear"));
    ASSERT_EQ_INT(0, (int)sh_history_count(&ts.shell));

    ts.shell.cfg.ansi_enabled = true;
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "clear"));
    ASSERT_CONTAINS(ts.out.data, "\x1b[2J\x1b[H");
    ts.shell.cfg.ansi_enabled = false;
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "clear"));
    ASSERT_STREQ("\r\n", ts.out.data);

    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_INCOMPLETE_COMMAND,
                  sh_execute_line(&ts.shell, "broken"));
    ASSERT_CONTAINS(ts.out.data, "incomplete command");
    reset_output(&ts);
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "group"));
    ASSERT_CONTAINS(ts.out.data, "Subcommands:");
    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_UNKNOWN_COMMAND,
                  sh_execute_line(&ts.shell, "group missing"));
    ASSERT_CONTAINS(ts.out.data, "unknown subcommand");
}

static void test_registry_and_command_set_validation(void)
{
    static const sh_cmd_t valid_child[] = {
        SH_CMD("child", "", "Child", noop_handler),
    };
    static const sh_cmd_t null_name[] = {
        { NULL, "", "", noop_handler, NULL, 0u, SH_CMD_FLAG_NONE,
          1u, 1u, NULL, NULL },
    };
    static const sh_cmd_t empty_name[] = {
        SH_CMD("", "", "", noop_handler),
    };
    static const sh_cmd_t whitespace_name[] = {
        SH_CMD("bad name", "", "", noop_handler),
    };
    static const sh_cmd_t null_children[] = {
        { "bad", "", "", NULL, NULL, 1u, SH_CMD_FLAG_NONE,
          1u, 1u, NULL, NULL },
    };
    static const sh_cmd_t zero_children[] = {
        { "bad", "", "", NULL, valid_child, 0u, SH_CMD_FLAG_NONE,
          1u, 1u, NULL, NULL },
    };
    static const sh_cmd_t zero_min[] = {
        { "bad", "", "", noop_handler, NULL, 0u, SH_CMD_FLAG_NONE,
          0u, 1u, NULL, NULL },
    };
    static const sh_cmd_t reversed_args[] = {
        { "bad", "", "", noop_handler, NULL, 0u, SH_CMD_FLAG_NONE,
          3u, 2u, NULL, NULL },
    };
    static const sh_cmd_t duplicates[] = {
        SH_CMD("same", "", "", noop_handler),
        SH_CMD("same", "", "", noop_handler),
    };
    static const sh_cmd_t reserved[] = {
        SH_CMD("help", "", "", noop_handler),
    };
    static const sh_cmd_t level9[] = {
        SH_CMD("nine", "", "", noop_handler),
    };
    static const sh_cmd_t level8[] = {
        SH_CMD_SUB("eight", "", "", level9),
    };
    static const sh_cmd_t level7[] = {
        SH_CMD_SUB("seven", "", "", level8),
    };
    static const sh_cmd_t level6[] = {
        SH_CMD_SUB("six", "", "", level7),
    };
    static const sh_cmd_t level5[] = {
        SH_CMD_SUB("five", "", "", level6),
    };
    static const sh_cmd_t level4[] = {
        SH_CMD_SUB("four", "", "", level5),
    };
    static const sh_cmd_t level3[] = {
        SH_CMD_SUB("three", "", "", level4),
    };
    static const sh_cmd_t level2[] = {
        SH_CMD_SUB("two", "", "", level3),
    };
    static const sh_cmd_t too_deep[] = {
        SH_CMD_SUB("one", "", "", level2),
    };
    static const sh_cmd_t set_a_commands[] = {
        SH_CMD("alpha", "", "", noop_handler),
    };
    static const sh_cmd_t set_b_commands[] = {
        SH_CMD("beta", "", "", noop_handler),
    };
    static const sh_cmd_t set_duplicate_commands[] = {
        SH_CMD("alpha", "", "", noop_handler),
    };
    static const sh_command_set_t valid_sets[] = {
        SH_COMMAND_SET(set_a_commands),
        SH_COMMAND_SET(set_b_commands),
    };
    static const sh_command_set_t duplicate_sets[] = {
        SH_COMMAND_SET(set_a_commands),
        SH_COMMAND_SET(set_duplicate_commands),
    };
    static const sh_command_set_t malformed_set[] = {
        { NULL, 1u },
    };
    test_shell_t ts;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_register_commands(NULL, NULL, 0u));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_register_commands(&ts.shell, NULL, 1u));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, null_name,
                                       SH_ARRAY_SIZE(null_name)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, empty_name,
                                       SH_ARRAY_SIZE(empty_name)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, whitespace_name,
                                       SH_ARRAY_SIZE(whitespace_name)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, null_children,
                                       SH_ARRAY_SIZE(null_children)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, zero_children,
                                       SH_ARRAY_SIZE(zero_children)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, zero_min,
                                       SH_ARRAY_SIZE(zero_min)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, reversed_args,
                                       SH_ARRAY_SIZE(reversed_args)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, duplicates,
                                       SH_ARRAY_SIZE(duplicates)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, reserved,
                                       SH_ARRAY_SIZE(reserved)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_commands(&ts.shell, too_deep,
                                       SH_ARRAY_SIZE(too_deep)));

    ts.shell.cfg.builtins_enabled = false;
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, reserved,
                                               SH_ARRAY_SIZE(reserved)));
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, NULL, 0u));

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_register_command_sets(NULL, valid_sets,
                                           SH_ARRAY_SIZE(valid_sets)));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_register_command_sets(&ts.shell, NULL, 1u));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_command_sets(&ts.shell, malformed_set,
                                           SH_ARRAY_SIZE(malformed_set)));
    ASSERT_EQ_INT(SH_ERR_INVALID_CONFIG,
                  sh_register_command_sets(&ts.shell, duplicate_sets,
                                           SH_ARRAY_SIZE(duplicate_sets)));
    ASSERT_EQ_INT(SH_OK,
                  sh_register_command_sets(&ts.shell, valid_sets,
                                           SH_ARRAY_SIZE(valid_sets)));
    ASSERT_EQ_INT(SH_OK, sh_register_command_sets(&ts.shell, NULL, 0u));
}

static void test_history_flags_and_callback_masking(void)
{
    static const sh_cmd_t commands[] = {
        SH_CMD("login", "<secret> [label]", "Login", noop_handler),
        SH_CMD_NO_HISTORY("volatile", "", "No history", noop_handler),
        SH_CMD_SENSITIVE("password", "<secret>", "Sensitive", noop_handler),
    };
    test_shell_t ts;
    mask_probe_t mask_probe;
    const char *saved;
    char saved_copy[SH_MAX_LINE_LEN + 1u];

    memset(&mask_probe, 0, sizeof(mask_probe));
    test_shell_init(&ts);
    ts.shell.cfg.should_mask_arg = mask_second_argument;
    ts.shell.cfg.mask_ctx = &mask_probe;
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));

    ASSERT_EQ_INT(SH_OK,
                  sh_execute_line(&ts.shell,
                                  "login \"top secret\" \"quote\\\"inside\""));
    saved = sh_history_get_newest(&ts.shell, 0u);
    ASSERT_STREQ("login <hidden> \"quote\\\"inside\"", saved);
    memcpy(saved_copy, saved, strlen(saved) + 1u);
    ASSERT_TRUE(mask_probe.saw_context);
    ASSERT_TRUE(mask_probe.saw_login_path);

    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "volatile transient"));
    ASSERT_STREQ(saved_copy, sh_history_get_newest(&ts.shell, 0u));

    ASSERT_EQ_INT(SH_OK,
                  sh_execute_line(&ts.shell, "password keep-this-private"));
    ASSERT_STREQ("password <hidden>", sh_history_get_newest(&ts.shell, 0u));

    reset_output(&ts);
    feed_text(&ts.shell, "login \"top secret\"");
    ASSERT_TRUE(strstr(ts.out.data, "top secret") == NULL);
    ASSERT_CONTAINS(ts.out.data, "*");
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\r'));
    ASSERT_TRUE(strstr(ts.out.data, "top secret") == NULL);
}

static void test_completion_edges(void)
{
    static size_t termination_calls;
    static size_t empty_calls;
    static size_t overflow_calls;
    static const sh_cmd_t commands[] = {
        SH_CMD_HIDDEN("hidden", "", "Hidden", noop_handler),
        SH_CMD("hive", "", "Visible", noop_handler),
        { "choose", "<value>", "Provider", noop_handler, NULL, 0u,
          SH_CMD_FLAG_NONE, 2u, 2u, &termination_calls, short_provider },
        { "empty", "<value>", "Empty provider", noop_handler, NULL, 0u,
          SH_CMD_FLAG_NONE, 2u, 2u, &empty_calls, empty_provider },
        { "many", "<value>", "Overflow provider", noop_handler, NULL, 0u,
          SH_CMD_FLAG_NONE, 2u, 2u, &overflow_calls, overflow_provider },
    };
    static const sh_cmd_t set_one_commands[] = {
        SH_CMD("alpha", "", "Alpha", noop_handler),
    };
    static const sh_cmd_t set_two_commands[] = {
        SH_CMD("beta", "", "Beta", noop_handler),
    };
    static const sh_command_set_t sets[] = {
        SH_COMMAND_SET(set_one_commands),
        SH_COMMAND_SET(set_two_commands),
    };
    static const sh_cmd_t long_command[] = {
        SH_CMD("abcdefgh", "", "Long", noop_handler),
    };
    test_shell_t ts;
    sh_t small_shell;
    sh_config_t cfg;
    char line[9];
    char *argv[4];

    termination_calls = 0u;
    empty_calls = 0u;
    overflow_calls = 0u;
    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&ts.shell, commands,
                                               SH_ARRAY_SIZE(commands)));

    set_editor_line(&ts.shell, "zzz", 3u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("zzz", ts.shell.line);

    set_editor_line(&ts.shell, "hid", 3u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("hid", ts.shell.line);

    set_editor_line(&ts.shell, "hiv", 3u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("hive ", ts.shell.line);

    set_editor_line(&ts.shell, "choose a", strlen("choose a"));
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("choose alp", ts.shell.line);
    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_AMBIGUOUS, sh_input_byte(&ts.shell, '\t'));
    ASSERT_CONTAINS(ts.out.data, "alpha");
    ASSERT_CONTAINS(ts.out.data, "alpine");
    ASSERT_TRUE(termination_calls >= 3u);

    set_editor_line(&ts.shell, "empty ", strlen("empty "));
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_EQ_INT(1, (int)empty_calls);

    set_editor_line(&ts.shell, "many item", strlen("many item"));
    reset_output(&ts);
    ASSERT_EQ_INT(SH_ERR_AMBIGUOUS, sh_input_byte(&ts.shell, '\t'));
    ASSERT_CONTAINS(ts.out.data, "... 224 more");
    ASSERT_EQ_INT((int)(SH_COMPLETION_PROVIDER_MAX_ITEMS * 2u),
                  (int)overflow_calls);

    ts.shell.cursor = ts.shell.line_len + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_complete_line_internal(&ts.shell));
    ts.shell.line_len = SH_MAX_LINE_LEN + 1u;
    ts.shell.cursor = SH_MAX_LINE_LEN + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_complete_line_internal(&ts.shell));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_complete_line_internal(NULL));

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_OK, sh_register_command_sets(&ts.shell, sets,
                                                  SH_ARRAY_SIZE(sets)));
    set_editor_line(&ts.shell, "be", 2u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("beta ", ts.shell.line);
    set_editor_line(&ts.shell, "ec", 2u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\t'));
    ASSERT_STREQ("echo ", ts.shell.line);

    sh_default_config(&cfg);
    cfg.line_max = 8u;
    cfg.argc_max = SH_ARRAY_SIZE(argv);
    cfg.history_depth = 0u;
    cfg.echo_enabled = false;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&small_shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), NULL, 0u, 0u, NULL, 0u));
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&small_shell, long_command,
                                       SH_ARRAY_SIZE(long_command)));
    set_editor_line(&small_shell, "a", 1u);
    ASSERT_EQ_INT(SH_ERR_LINE_TOO_LONG,
                  sh_input_byte(&small_shell, '\t'));
}

static void test_history_disabled_null_and_input_bounds(void)
{
    static const sh_cmd_t commands[] = {
        SH_CMD("run", "", "Run", noop_handler),
    };
    sh_t shell;
    sh_config_t cfg;
    char line[17];
    char *argv[4];
    test_shell_t ts;
    static const unsigned char overflow_then_clear[] = { 'a', 'b', 'c', 0x15u };

    ASSERT_EQ_INT(0, (int)sh_history_capacity(NULL));
    ASSERT_EQ_INT(0, (int)sh_history_count(NULL));
    ASSERT_TRUE(sh_history_get_newest(NULL, 0u) == NULL);
    sh_history_clear(NULL);

    sh_default_config(&cfg);
    cfg.line_max = 16u;
    cfg.argc_max = SH_ARRAY_SIZE(argv);
    cfg.history_depth = 0u;
    cfg.echo_enabled = false;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&shell, &cfg, line, sizeof(line), argv,
                          SH_ARRAY_SIZE(argv), NULL, 0u, 0u, NULL, 0u));
    ASSERT_EQ_INT(SH_OK, sh_register_commands(&shell, commands,
                                               SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&shell, "run"));
    ASSERT_EQ_INT(0, (int)sh_history_count(&shell));
    ASSERT_TRUE(sh_history_get_newest(&shell, 0u) == NULL);
    sh_history_clear(&shell);
    feed_csi(&shell, "\x1b[A");
    ASSERT_EQ_INT(0, (int)shell.line_len);

    test_shell_init(&ts);
    set_editor_line(&ts.shell, "", 0u);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, '\b'));
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, 0x7fu));
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, 0x17u));
    feed_csi(&ts.shell, "\x1b[D");
    feed_csi(&ts.shell, "\x1b[C");
    feed_csi(&ts.shell, "\x1b[H");
    feed_csi(&ts.shell, "\x1b[F");
    feed_csi(&ts.shell, "\x1b[3~");
    ASSERT_EQ_INT(0, (int)ts.shell.cursor);
    ASSERT_EQ_INT(0, (int)ts.shell.line_len);
    ASSERT_EQ_INT(SH_OK, sh_input_byte(&ts.shell, 0x01u));

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_input(NULL, NULL, 0u));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_input(&ts.shell, NULL, 1u));
    ASSERT_EQ_INT(SH_OK, sh_input(&ts.shell, NULL, 0u));

    sh_default_config(&cfg);
    cfg.line_max = 2u;
    cfg.argc_max = SH_ARRAY_SIZE(argv);
    cfg.history_depth = 0u;
    cfg.echo_enabled = false;
    ASSERT_EQ_INT(SH_OK,
                  sh_init(&shell, &cfg, line, 3u, argv,
                          SH_ARRAY_SIZE(argv), NULL, 0u, 0u, NULL, 0u));
    ASSERT_EQ_INT(SH_ERR_LINE_TOO_LONG,
                  sh_input(&shell, overflow_then_clear,
                           sizeof(overflow_then_clear)));
    ASSERT_EQ_INT(0, (int)shell.line_len);
}

static void test_execute_and_tokenizer_invalid_inputs(void)
{
    test_shell_t ts;
    char *one_argv[] = { "missing" };
    char parse[] = "value";
    char *argv[1];
    int argc = 99;

    test_shell_init(&ts);
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_argv(NULL, 0u, NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_argv(&ts.shell, 1u, NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_execute_argv(&ts.shell, ts.shell.argv_capacity + 1u,
                                  one_argv));
#if SIZE_MAX > INT_MAX
    ts.shell.argv_capacity = (size_t)INT_MAX + 1u;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_execute_argv(&ts.shell, (size_t)INT_MAX + 1u,
                                  one_argv));
    ts.shell.argv_capacity = ts.shell.cfg.argc_max;
#endif
    ASSERT_EQ_INT(SH_OK, sh_execute_argv(&ts.shell, 0u, NULL));
    ASSERT_EQ_INT(SH_ERR_UNKNOWN_COMMAND,
                  sh_execute_argv(&ts.shell, 1u, one_argv));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_line(NULL, "x"));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG, sh_execute_line(&ts.shell, NULL));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&ts.shell, "  \t  "));

    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(parse, argv, SH_ARRAY_SIZE(argv), NULL));
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(NULL, argv, SH_ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(parse, NULL, SH_ARRAY_SIZE(argv), &argc));
    ASSERT_EQ_INT(0, argc);
    argc = 99;
    ASSERT_EQ_INT(SH_ERR_INVALID_ARG,
                  sh_tokenize(parse, argv, 0u, &argc));
    ASSERT_EQ_INT(0, argc);
}

int main(void)
{
    test_invalid_and_storage_config();
    test_status_strings();
    test_output_lifecycle_and_transport_errors();
    test_builtins_help_and_incomplete_commands();
    test_registry_and_command_set_validation();
    test_history_flags_and_callback_masking();
    test_completion_edges();
    test_history_disabled_null_and_input_bounds();
    test_execute_and_tokenizer_invalid_inputs();

    puts("test_coverage_edges: all tests passed");
    return 0;
}
