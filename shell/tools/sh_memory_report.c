#include "sh_shell.h"

#include <stdio.h>

typedef struct {
    const char *name;
    size_t line_max;
    size_t argc_max;
    size_t history_depth;
} memory_profile_t;

/** @brief Prints the static memory cost of one shell configuration profile. */
static void print_profile(const memory_profile_t *profile)
{
    size_t line_bytes = profile->line_max + 1u;
    size_t argv_bytes = profile->argc_max * sizeof(char *);
    size_t history_bytes = profile->history_depth * line_bytes;
    size_t draft_bytes = profile->history_depth == 0u ? 0u : line_bytes;
    size_t buffers_total = line_bytes + argv_bytes + history_bytes +
                           draft_bytes;

    printf("\n[%s]\n", profile->name);
    printf("  limits:          line=%lu, argc=%lu, history=%lu\n",
           (unsigned long)profile->line_max,
           (unsigned long)profile->argc_max,
           (unsigned long)profile->history_depth);
    printf("  line buffer:     %lu bytes\n", (unsigned long)line_bytes);
    printf("  argv pointers:   %lu bytes\n", (unsigned long)argv_bytes);
    printf("  history slots:   %lu bytes\n", (unsigned long)history_bytes);
    printf("  history draft:   %lu bytes\n", (unsigned long)draft_bytes);
    printf("  buffers total:   %lu bytes\n", (unsigned long)buffers_total);
    printf("  instance total:  %lu bytes\n",
           (unsigned long)(sizeof(sh_t) + buffers_total));
}

/** @brief Reports shell object and buffer sizes for representative profiles. */
int main(void)
{
    static const memory_profile_t profiles[] = {
        { "minimal-no-history", 64u, 8u, 0u },
        { "small", 96u, 12u, 4u },
        { "freertos", 128u, 16u, 8u },
        { "compiled-default", SH_MAX_LINE_LEN, SH_MAX_ARGC,
          SH_HISTORY_DEFAULT_DEPTH }
    };
    size_t i;

    printf("Portable shell memory report\n");
    printf("  sizeof(void *):       %lu bytes\n",
           (unsigned long)sizeof(void *));
    printf("  sizeof(sh_config_t):  %lu bytes\n",
           (unsigned long)sizeof(sh_config_t));
    printf("  sizeof(sh_t):         %lu bytes\n",
           (unsigned long)sizeof(sh_t));
    printf("  sizeof(sh_cmd_t):     %lu bytes\n",
           (unsigned long)sizeof(sh_cmd_t));

    for (i = 0u; i < SH_ARRAY_SIZE(profiles); i++) {
        print_profile(&profiles[i]);
    }

    printf("\nTotals exclude command tables, application context, transport queues, "
           "and allocator overhead.\n");
    return 0;
}
