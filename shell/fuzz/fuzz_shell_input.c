#include "sh_shell.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_LINE_MAX 128u
#define HARNESS_ARGC_MAX 16u
#define HARNESS_HISTORY_DEPTH 8u
#define HARNESS_CANARY_SIZE 16u
#define HARNESS_CANARY_VALUE 0x5au
#define HARNESS_INPUT_LIMIT (1024u * 1024u)

typedef struct {
    unsigned char before[HARNESS_CANARY_SIZE];
    char value[HARNESS_LINE_MAX + 1u];
    unsigned char after[HARNESS_CANARY_SIZE];
} guarded_line_t;

typedef struct {
    unsigned char before[HARNESS_CANARY_SIZE];
    char *value[HARNESS_ARGC_MAX];
    unsigned char after[HARNESS_CANARY_SIZE];
} guarded_argv_t;

typedef struct {
    unsigned char before[HARNESS_CANARY_SIZE];
    char value[HARNESS_HISTORY_DEPTH * (HARNESS_LINE_MAX + 1u)];
    unsigned char after[HARNESS_CANARY_SIZE];
} guarded_history_t;

static void discard_output(const char *data, size_t len, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
}

static void set_guard(unsigned char *guard)
{
    memset(guard, HARNESS_CANARY_VALUE, HARNESS_CANARY_SIZE);
}

static int guard_is_valid(const unsigned char *guard)
{
    size_t i;

    for (i = 0u; i < HARNESS_CANARY_SIZE; i++) {
        if (guard[i] != HARNESS_CANARY_VALUE) {
            return 0;
        }
    }
    return 1;
}

static void verify_state(const sh_t *shell,
                         const guarded_line_t *line,
                         const guarded_argv_t *argv,
                         const guarded_history_t *history,
                         const guarded_line_t *draft)
{
    if (!guard_is_valid(line->before) || !guard_is_valid(line->after) ||
        !guard_is_valid(argv->before) || !guard_is_valid(argv->after) ||
        !guard_is_valid(history->before) ||
        !guard_is_valid(history->after) ||
        !guard_is_valid(draft->before) || !guard_is_valid(draft->after) ||
        shell->line_len > HARNESS_LINE_MAX ||
        shell->cursor > shell->line_len ||
        shell->line[shell->line_len] != '\0' ||
        shell->history_count > HARNESS_HISTORY_DEPTH ||
        shell->history_start >= HARNESS_HISTORY_DEPTH ||
        shell->history_view < -1 ||
        (shell->history_view >= 0 &&
         (size_t)shell->history_view >= shell->history_count)) {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    sh_t shell;
    sh_config_t config;
    guarded_line_t line;
    guarded_argv_t argv;
    guarded_history_t history;
    guarded_line_t draft;
    size_t offset = 0u;

    if (size > HARNESS_INPUT_LIMIT || (data == NULL && size != 0u)) {
        return 0;
    }

    memset(&line, 0, sizeof(line));
    memset(&argv, 0, sizeof(argv));
    memset(&history, 0, sizeof(history));
    memset(&draft, 0, sizeof(draft));
    set_guard(line.before);
    set_guard(line.after);
    set_guard(argv.before);
    set_guard(argv.after);
    set_guard(history.before);
    set_guard(history.after);
    set_guard(draft.before);
    set_guard(draft.after);

    sh_default_config(&config);
    config.line_max = HARNESS_LINE_MAX;
    config.argc_max = HARNESS_ARGC_MAX;
    config.history_depth = HARNESS_HISTORY_DEPTH;
    config.echo_enabled = false;
    config.write = discard_output;

    if (sh_init(&shell, &config,
                line.value, sizeof(line.value),
                argv.value, HARNESS_ARGC_MAX,
                history.value, HARNESS_HISTORY_DEPTH,
                HARNESS_LINE_MAX + 1u,
                draft.value, sizeof(draft.value)) != SH_OK) {
        abort();
    }

    while (offset < size) {
        size_t chunk = 1u + (size_t)(data[offset] & 31u);
        int input_status;

        if (chunk > size - offset) {
            chunk = size - offset;
        }
        input_status = sh_input(&shell, data + offset, chunk);
        if (input_status > SH_OK ||
            input_status < SH_ERR_INCOMPLETE_COMMAND) {
            abort();
        }
        offset += chunk;
        verify_state(&shell, &line, &argv, &history, &draft);
    }
    verify_state(&shell, &line, &argv, &history, &draft);
    return 0;
}
