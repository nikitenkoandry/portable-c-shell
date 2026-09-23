#include "sh_shell.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_LINE_MAX 96u
#define FUZZ_ARGC_MAX 12u
#define FUZZ_HISTORY_DEPTH 8u
#define FUZZ_STREAM_COUNT 4096u
#define FUZZ_STREAM_MAX 384u
#define CANARY_SIZE 16u
#define CANARY_VALUE 0xa5u

typedef struct {
    unsigned char before[CANARY_SIZE];
    sh_t value;
    unsigned char after[CANARY_SIZE];
} guarded_shell_t;

typedef struct {
    unsigned char before[CANARY_SIZE];
    char value[FUZZ_LINE_MAX + 1u];
    unsigned char after[CANARY_SIZE];
} guarded_line_t;

typedef struct {
    unsigned char before[CANARY_SIZE];
    char *value[FUZZ_ARGC_MAX];
    unsigned char after[CANARY_SIZE];
} guarded_argv_t;

typedef struct {
    unsigned char before[CANARY_SIZE];
    char value[FUZZ_HISTORY_DEPTH * (FUZZ_LINE_MAX + 1u)];
    unsigned char after[CANARY_SIZE];
} guarded_history_t;

typedef struct {
    guarded_shell_t shell;
    guarded_line_t line;
    guarded_argv_t argv;
    guarded_history_t history;
    guarded_line_t draft;
} fuzz_arena_t;

static void discard_output(const char *data, size_t len, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
}

static uint32_t prng_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static unsigned char random_input_byte(uint32_t *state)
{
    uint32_t value = prng_next(state);

    switch (value & 15u) {
    case 0u: return '\r';
    case 1u: return '\n';
    case 2u: return '\t';
    case 3u: return 0x1bu;
    case 4u: return '[';
    case 5u: return (unsigned char)('A' + (value % 4u));
    case 6u: return (unsigned char)('0' + (value % 10u));
    case 7u: return '\b';
    case 8u: return 0x7fu;
    case 9u: return 0x03u;
    case 10u: return 0x0cu;
    case 11u: return 0x15u;
    case 12u: return 0x17u;
    case 13u: return (unsigned char)(32u + (value % 95u));
    default: return (unsigned char)(value >> 8);
    }
}

static void set_canary(unsigned char *bytes)
{
    memset(bytes, CANARY_VALUE, CANARY_SIZE);
}

static int canary_is_valid(const unsigned char *bytes)
{
    size_t i;

    for (i = 0u; i < CANARY_SIZE; i++) {
        if (bytes[i] != CANARY_VALUE) {
            return 0;
        }
    }
    return 1;
}

static int guarded_region_is_valid(const unsigned char *before,
                                   const unsigned char *after)
{
    return canary_is_valid(before) && canary_is_valid(after);
}

static int bounded_string_is_valid(const char *text, size_t capacity)
{
    return memchr(text, '\0', capacity) != NULL;
}

static int fail_invariant(const char *name, size_t stream_index,
                          size_t processed)
{
    fprintf(stderr,
            "fuzz invariant failed: %s (stream=%lu, processed=%lu)\n",
            name, (unsigned long)stream_index, (unsigned long)processed);
    return 0;
}

#define CHECK_INVARIANT(expr, name_)                                      \
    do {                                                                  \
        if (!(expr)) {                                                    \
            return fail_invariant((name_), stream_index, processed);      \
        }                                                                 \
    } while (0)

static int check_invariants(fuzz_arena_t *arena, size_t stream_index,
                            size_t processed)
{
    sh_t *shell = &arena->shell.value;
    guarded_line_t parse;
    guarded_argv_t parsed_argv;
    int argc = -1;
    int status;
    int i;
    size_t slot;

    CHECK_INVARIANT(guarded_region_is_valid(arena->shell.before,
                                             arena->shell.after),
                    "shell canary");
    CHECK_INVARIANT(guarded_region_is_valid(arena->line.before,
                                             arena->line.after),
                    "line canary");
    CHECK_INVARIANT(guarded_region_is_valid(arena->argv.before,
                                             arena->argv.after),
                    "argv canary");
    CHECK_INVARIANT(guarded_region_is_valid(arena->history.before,
                                             arena->history.after),
                    "history canary");
    CHECK_INVARIANT(guarded_region_is_valid(arena->draft.before,
                                             arena->draft.after),
                    "draft canary");

    CHECK_INVARIANT(shell->line == arena->line.value, "line pointer");
    CHECK_INVARIANT(shell->argv == arena->argv.value, "argv pointer");
    CHECK_INVARIANT(shell->history_storage == arena->history.value,
                    "history pointer");
    CHECK_INVARIANT(shell->draft == arena->draft.value, "draft pointer");
    CHECK_INVARIANT(shell->line_capacity == FUZZ_LINE_MAX + 1u,
                    "line capacity");
    CHECK_INVARIANT(shell->line_len <= FUZZ_LINE_MAX, "line length");
    CHECK_INVARIANT(shell->cursor <= shell->line_len, "cursor");
    CHECK_INVARIANT(shell->line[shell->line_len] == '\0',
                    "line terminator");
    CHECK_INVARIANT(strlen(shell->line) == shell->line_len,
                    "line length consistency");
    CHECK_INVARIANT(shell->argv_capacity == FUZZ_ARGC_MAX,
                    "argv capacity");

    CHECK_INVARIANT(shell->history_depth == FUZZ_HISTORY_DEPTH,
                    "history depth");
    CHECK_INVARIANT(shell->history_count <= shell->history_depth,
                    "history count");
    CHECK_INVARIANT(shell->history_start < shell->history_depth,
                    "history start");
    CHECK_INVARIANT(shell->history_view >= -1, "history view lower bound");
    CHECK_INVARIANT(shell->history_view < 0 ||
                        (size_t)shell->history_view < shell->history_count,
                    "history view upper bound");
    CHECK_INVARIANT(bounded_string_is_valid(arena->draft.value,
                                             FUZZ_LINE_MAX + 1u),
                    "draft terminator");

    for (slot = 0u; slot < FUZZ_HISTORY_DEPTH; slot++) {
        const char *entry = arena->history.value +
                            slot * (FUZZ_LINE_MAX + 1u);
        CHECK_INVARIANT(bounded_string_is_valid(entry,
                                                 FUZZ_LINE_MAX + 1u),
                        "history terminator");
    }

    memset(&parse, 0, sizeof(parse));
    memset(&parsed_argv, 0, sizeof(parsed_argv));
    set_canary(parse.before);
    set_canary(parse.after);
    set_canary(parsed_argv.before);
    set_canary(parsed_argv.after);
    memcpy(parse.value, shell->line, shell->line_len + 1u);

    status = sh_tokenize(parse.value, parsed_argv.value,
                         FUZZ_ARGC_MAX, &argc);
    CHECK_INVARIANT(status == SH_OK || status == SH_ERR_INVALID_ARG ||
                        status == SH_ERR_TOO_MANY_ARGS ||
                        status == SH_ERR_UNTERMINATED_QUOTE,
                    "tokenizer status");
    CHECK_INVARIANT(argc >= 0 && (size_t)argc <= FUZZ_ARGC_MAX,
                    "argc range");
    if (status != SH_OK) {
        CHECK_INVARIANT(argc == 0, "argc reset on tokenizer error");
    }
    for (i = 0; i < argc; i++) {
        size_t remaining;

        CHECK_INVARIANT(parsed_argv.value[i] >= parse.value &&
                            parsed_argv.value[i] <=
                                parse.value + FUZZ_LINE_MAX,
                        "argv token pointer");
        remaining = (size_t)((parse.value + FUZZ_LINE_MAX + 1u) -
                             parsed_argv.value[i]);
        CHECK_INVARIANT(bounded_string_is_valid(parsed_argv.value[i],
                                                 remaining),
                        "argv token terminator");
    }
    CHECK_INVARIANT(guarded_region_is_valid(parse.before, parse.after),
                    "tokenizer line canary");
    CHECK_INVARIANT(guarded_region_is_valid(parsed_argv.before,
                                             parsed_argv.after),
                    "tokenizer argv canary");
    return 1;
}

static int init_arena(fuzz_arena_t *arena)
{
    sh_config_t config;

    memset(arena, 0, sizeof(*arena));
    set_canary(arena->shell.before);
    set_canary(arena->shell.after);
    set_canary(arena->line.before);
    set_canary(arena->line.after);
    set_canary(arena->argv.before);
    set_canary(arena->argv.after);
    set_canary(arena->history.before);
    set_canary(arena->history.after);
    set_canary(arena->draft.before);
    set_canary(arena->draft.after);

    sh_default_config(&config);
    config.line_max = FUZZ_LINE_MAX;
    config.argc_max = FUZZ_ARGC_MAX;
    config.history_depth = FUZZ_HISTORY_DEPTH;
    config.echo_enabled = false;
    config.write = discard_output;

    return sh_init(&arena->shell.value, &config,
                   arena->line.value, sizeof(arena->line.value),
                   arena->argv.value, FUZZ_ARGC_MAX,
                   arena->history.value, FUZZ_HISTORY_DEPTH,
                   FUZZ_LINE_MAX + 1u,
                   arena->draft.value, sizeof(arena->draft.value));
}

int main(void)
{
    const uint32_t initial_seed = UINT32_C(0x6d2b79f5);
    uint32_t state = initial_seed;
    uint64_t total_bytes = 0u;
    uint64_t total_chunks = 0u;
    unsigned char stream[FUZZ_STREAM_MAX];
    fuzz_arena_t arena;
    size_t stream_index;

    for (stream_index = 0u; stream_index < FUZZ_STREAM_COUNT;
         stream_index++) {
        size_t stream_size = 1u +
                             (size_t)(prng_next(&state) % FUZZ_STREAM_MAX);
        size_t offset = 0u;
        size_t i;

        if (init_arena(&arena) != SH_OK) {
            fprintf(stderr, "shell initialization failed\n");
            return EXIT_FAILURE;
        }
        for (i = 0u; i < stream_size; i++) {
            stream[i] = random_input_byte(&state);
        }

        while (offset < stream_size) {
            size_t chunk = 1u + (size_t)(prng_next(&state) % 31u);
            int input_status;

            if (chunk > stream_size - offset) {
                chunk = stream_size - offset;
            }
            input_status = sh_input(&arena.shell.value,
                                    stream + offset, chunk);
            if (input_status > SH_OK ||
                input_status < SH_ERR_INCOMPLETE_COMMAND) {
                fprintf(stderr,
                        "sh_input returned invalid status %d "
                        "(stream=%lu, offset=%lu)\n",
                        input_status,
                        (unsigned long)stream_index, (unsigned long)offset);
                return EXIT_FAILURE;
            }
            offset += chunk;
            total_chunks++;
            if (!check_invariants(&arena, stream_index, offset)) {
                return EXIT_FAILURE;
            }
        }
        total_bytes += (uint64_t)stream_size;
    }

    printf("deterministic stream fuzz passed: seed=0x%08" PRIx32
           ", streams=%u, chunks=%" PRIu64 ", bytes=%" PRIu64 "\n",
           initial_seed, (unsigned)FUZZ_STREAM_COUNT,
           total_chunks, total_bytes);
    return EXIT_SUCCESS;
}
