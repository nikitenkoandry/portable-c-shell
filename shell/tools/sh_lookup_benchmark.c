#include "sh_shell.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define BENCHMARK_MAX_COMMANDS 256u
#define BENCHMARK_ITERATIONS 10000u

SH_STORAGE_DEFINE(benchmark_storage, 64u, 2u, 0u);

static int command_handler(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)shell;
    (void)argc;
    (void)argv;
    (void)ctx;
    return SH_OK;
}

static void discard_output(const char *data, size_t len, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
}

static void initialize_commands(sh_cmd_t *commands,
                                char names[BENCHMARK_MAX_COMMANDS][12])
{
    size_t i;

    memset(commands, 0, sizeof(*commands) * BENCHMARK_MAX_COMMANDS);
    for (i = 0u; i < BENCHMARK_MAX_COMMANDS; i++) {
        (void)snprintf(names[i], sizeof(names[i]), "command%03u",
                       (unsigned)i);
        commands[i].name = names[i];
        commands[i].handler = command_handler;
        commands[i].min_args = 1u;
        commands[i].max_args = 1u;
    }
}

int main(void)
{
    static const size_t sizes[] = { 16u, 64u, 128u, 256u };
    sh_cmd_t commands[BENCHMARK_MAX_COMMANDS];
    char names[BENCHMARK_MAX_COMMANDS][12];
    sh_config_t config;
    sh_t shell;
    size_t size_index;

    initialize_commands(commands, names);
    sh_default_config(&config);
    config.line_max = 64u;
    config.argc_max = 2u;
    config.history_depth = 0u;
    config.echo_enabled = false;
    config.write = discard_output;
    if (sh_init_static(&shell, &config, &benchmark_storage) != SH_OK) {
        return 1;
    }

    puts("Linear command lookup benchmark (host clock time)");
    for (size_index = 0u; size_index < SH_ARRAY_SIZE(sizes); size_index++) {
        size_t count = sizes[size_index];
        char *argv[] = { names[count - 1u] };
        clock_t start;
        clock_t elapsed;
        size_t iteration;

        if (sh_register_commands(&shell, commands, count) != SH_OK) {
            return 2;
        }
        start = clock();
        for (iteration = 0u; iteration < BENCHMARK_ITERATIONS; iteration++) {
            if (sh_execute_argv(&shell, 1u, argv) != SH_OK) {
                return 3;
            }
        }
        elapsed = clock() - start;
        printf("%3u commands: %.3f us/lookup (%u iterations)\n",
               (unsigned)count,
               1000000.0 * (double)elapsed /
                   ((double)CLOCKS_PER_SEC * (double)BENCHMARK_ITERATIONS),
               (unsigned)BENCHMARK_ITERATIONS);
    }
    return 0;
}
