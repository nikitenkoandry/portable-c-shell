#include "sh_shell.h"
#include <string.h>

static char *slot_at(const sh_t *shell, size_t physical)
{
    return shell->history_storage + physical * shell->history_slot_size;
}

size_t sh_history_capacity(const sh_t *shell)
{
    return shell ? shell->history_depth : 0u;
}

size_t sh_history_count(const sh_t *shell)
{
    return shell ? shell->history_count : 0u;
}

void sh_history_clear(sh_t *shell)
{
    if (!shell) {
        return;
    }
    shell->history_count = 0u;
    shell->history_start = 0u;
    shell->history_view = -1;
    if (shell->draft) {
        memset(shell->draft, 0, shell->line_capacity);
    }
    if (shell->history_storage && shell->history_depth && shell->history_slot_size) {
        memset(shell->history_storage, 0, shell->history_depth * shell->history_slot_size);
    }
}

const char *sh_history_get_newest(const sh_t *shell, size_t newest_index)
{
    size_t logical;
    size_t physical;

    if (!shell || newest_index >= shell->history_count || shell->history_depth == 0u) {
        return NULL;
    }

    logical = shell->history_count - 1u - newest_index;
    physical = (shell->history_start + logical) % shell->history_depth;
    return slot_at(shell, physical);
}

void sh_history_add_internal(sh_t *shell, const char *line)
{
    size_t len;
    size_t physical;
    const char *latest;

    if (!shell || !line || !*line || !shell->history_storage || shell->history_depth == 0u) {
        return;
    }

    latest = sh_history_get_newest(shell, 0u);
    if (latest && strcmp(latest, line) == 0) {
        return;
    }

    len = strlen(line);
    if (len >= shell->history_slot_size) {
        len = shell->history_slot_size - 1u;
    }

    if (shell->history_count < shell->history_depth) {
        physical = (shell->history_start + shell->history_count) % shell->history_depth;
        shell->history_count++;
    } else {
        physical = shell->history_start;
        shell->history_start = (shell->history_start + 1u) % shell->history_depth;
    }

    memcpy(slot_at(shell, physical), line, len);
    slot_at(shell, physical)[len] = '\0';
}
