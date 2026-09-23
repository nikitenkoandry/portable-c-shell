#include "sh_shell.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void sh_write_internal(sh_t *shell, const char *text);
void sh_writef_internal(sh_t *shell, const char *fmt, ...);
void sh_redraw_line_internal(sh_t *shell);

typedef struct {
    const sh_cmd_t *primary;
    size_t primary_count;
    const sh_cmd_t *secondary;
    size_t secondary_count;
    const sh_cmd_t *provider_command;
    size_t provider_arg_index;
    bool registered_root;
} sh_completion_level_t;

static const sh_cmd_t history_children[] = {
    SH_CMD("clear", "", "Clear command history", NULL),
    SH_CMD("status", "", "Show history usage", NULL),
};

static const sh_cmd_t echo_children[] = {
    SH_CMD("off", "", "Disable local echo", NULL),
    SH_CMD("on", "", "Enable local echo", NULL),
    SH_CMD("status", "", "Show echo state", NULL),
};

static const sh_cmd_t builtin_commands[] = {
    SH_CMD("clear", "", "Clear terminal", NULL),
    SH_CMD("help", "[command]", "Show help", NULL),
    SH_CMD_SUB("history", "[clear|status]", "Show command history", history_children),
    SH_CMD_SUB("echo", "on|off|status", "Control local echo", echo_children),
};

/** @brief Test whether a candidate begins with the requested prefix. */
static bool starts_with(const char *text, const char *prefix)
{
    return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

/** @brief Find an exact name in one completion command table. */
static const sh_cmd_t *find_exact_in(const sh_cmd_t *commands, size_t count,
                                     const char *name)
{
    size_t i;

    for (i = 0u; i < count; i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

/** @brief Resolve an exact name across the active completion level. */
static const sh_cmd_t *find_exact_level(sh_t *shell,
                                        const sh_completion_level_t *level,
                                        const char *name)
{
    const sh_cmd_t *cmd;
    size_t i;

    if (level->registered_root && shell->command_sets) {
        for (i = 0u; i < shell->command_set_count; i++) {
            cmd = find_exact_in(shell->command_sets[i].commands,
                                shell->command_sets[i].command_count, name);
            if (cmd) {
                return cmd;
            }
        }
    }
    cmd = find_exact_in(level->primary, level->primary_count, name);
    if (cmd) {
        return cmd;
    }
    return find_exact_in(level->secondary, level->secondary_count, name);
}

/** @brief Return the maximum candidate count for one completion level. */
static size_t level_limit(const sh_t *shell,
                          const sh_completion_level_t *level)
{
    size_t count = 0u;
    size_t i;

    if (level->provider_command) {
        return SH_COMPLETION_PROVIDER_MAX_ITEMS;
    }
    if (level->registered_root && shell->command_sets) {
        for (i = 0u; i < shell->command_set_count; i++) {
            count += shell->command_sets[i].command_count;
        }
        return count + level->secondary_count;
    }
    return level->primary_count + level->secondary_count;
}

/** @brief Fetch one visible static or provider-backed completion candidate. */
static bool level_candidate(sh_t *shell, const sh_completion_level_t *level,
                            size_t index, const char **name)
{
    const sh_cmd_t *cmd;

    *name = NULL;
    if (level->provider_command) {
        void *ctx = level->provider_command->user_ctx
                        ? level->provider_command->user_ctx
                        : shell->cfg.user_ctx;
        *name = level->provider_command->completion(
            shell, level->provider_arg_index, index, ctx);
        return *name != NULL;
    }
    if (level->registered_root && shell->command_sets) {
        size_t set_index;

        for (set_index = 0u; set_index < shell->command_set_count; set_index++) {
            if (index < shell->command_sets[set_index].command_count) {
                cmd = &shell->command_sets[set_index].commands[index];
                if (!(cmd->flags & SH_CMD_FLAG_HIDDEN)) {
                    *name = cmd->name;
                }
                return true;
            }
            index -= shell->command_sets[set_index].command_count;
        }
        if (index >= level->secondary_count) {
            return false;
        }
        cmd = &level->secondary[index];
        if (!(cmd->flags & SH_CMD_FLAG_HIDDEN)) {
            *name = cmd->name;
        }
        return true;
    }
    if (index < level->primary_count) {
        cmd = &level->primary[index];
    } else {
        index -= level->primary_count;
        if (index >= level->secondary_count) {
            return false;
        }
        cmd = &level->secondary[index];
    }
    if (!(cmd->flags & SH_CMD_FLAG_HIDDEN)) {
        *name = cmd->name;
    }
    return true;
}

/** @brief Count the identical leading bytes shared by two candidates. */
static size_t common_prefix_len(const char *a, const char *b)
{
    size_t i = 0u;
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return i;
}

/** @brief Replace the token around the cursor with a completion result. */
static int replace_token(sh_t *shell, size_t start, size_t end,
                         const char *replacement, bool append_space)
{
    size_t replacement_len = strlen(replacement);
    size_t suffix_len;
    size_t space_len = 0u;
    size_t new_len;

    if (start > end || end > shell->line_len) {
        return SH_ERR_INVALID_ARG;
    }
    if (append_space && (end == shell->line_len ||
                         !isspace((unsigned char)shell->line[end]))) {
        space_len = 1u;
    }

    suffix_len = shell->line_len - end;
    new_len = start + replacement_len + space_len + suffix_len;
    if (new_len >= shell->line_capacity) {
        return SH_ERR_LINE_TOO_LONG;
    }

    memmove(shell->line + start + replacement_len + space_len,
            shell->line + end, suffix_len + 1u);
    memcpy(shell->line + start, replacement, replacement_len);
    if (space_len != 0u) {
        shell->line[start + replacement_len] = ' ';
    }

    shell->line_len = new_len;
    shell->cursor = start + replacement_len + space_len;
    shell->history_view = -1;
    sh_redraw_line_internal(shell);
    return SH_OK;
}

/** @brief Print bounded alternatives for an ambiguous completion. */
static void print_matches(sh_t *shell, const sh_completion_level_t *level,
                          const char *prefix, size_t match_count)
{
    size_t i;
    size_t displayed = 0u;

    sh_write_internal(shell, "\r\n");
    for (i = 0u; i < level_limit(shell, level); i++) {
        const char *name;
        if (!level_candidate(shell, level, i, &name)) {
            break;
        }
        if (name && starts_with(name, prefix)) {
            if (displayed < SH_COMPLETION_MAX_MATCHES) {
                sh_writef_internal(shell, "%s\r\n", name);
            }
            displayed++;
        }
    }
    if (match_count > SH_COMPLETION_MAX_MATCHES) {
        sh_writef_internal(shell, "... %u more\r\n",
                           (unsigned)(match_count - SH_COMPLETION_MAX_MATCHES));
    }
    sh_redraw_line_internal(shell);
}

/** @brief Complete the token at the cursor or display matching alternatives. */
int sh_complete_line_internal(sh_t *shell)
{
    char left[SH_MAX_LINE_LEN + 1u];
    char prefix[SH_MAX_LINE_LEN + 1u];
    char *argv[SH_MAX_ARGC];
    sh_completion_level_t level;
    int argc = 0;
    int status;
    size_t token_start;
    size_t token_end;
    size_t prefix_len;
    size_t tokens_before;
    size_t i;
    char first_match[SH_MAX_LINE_LEN + 1u];
    size_t common_len = 0u;
    size_t match_count = 0u;

    if (!shell || shell->cursor > shell->line_len ||
        shell->cursor > SH_MAX_LINE_LEN) {
        return SH_ERR_INVALID_ARG;
    }

    memcpy(left, shell->line, shell->cursor);
    left[shell->cursor] = '\0';
    status = sh_tokenize(left, argv, shell->argv_capacity, &argc);
    if (status != SH_OK) {
        return status;
    }

    token_start = shell->cursor;
    while (token_start > 0u &&
           !isspace((unsigned char)shell->line[token_start - 1u])) {
        token_start--;
    }
    token_end = shell->cursor;
    while (token_end < shell->line_len &&
           !isspace((unsigned char)shell->line[token_end])) {
        token_end++;
    }

    prefix_len = shell->cursor - token_start;
    memcpy(prefix, shell->line + token_start, prefix_len);
    prefix[prefix_len] = '\0';
    tokens_before = (size_t)argc;
    if (prefix_len != 0u && tokens_before != 0u) {
        tokens_before--;
    }

    level.primary = shell->command_sets ? NULL : shell->commands;
    level.primary_count = shell->command_sets ? 0u : shell->command_count;
    level.secondary = shell->cfg.builtins_enabled ? builtin_commands : NULL;
    level.secondary_count = shell->cfg.builtins_enabled
                                ? SH_ARRAY_SIZE(builtin_commands) : 0u;
    level.provider_command = NULL;
    level.provider_arg_index = 0u;
    level.registered_root = shell->command_sets != NULL;

    for (i = 0u; i < tokens_before; i++) {
        const sh_cmd_t *exact = find_exact_level(shell, &level, argv[i]);
        if (!exact) {
            return SH_OK;
        }
        if ((!exact->subcommands || exact->subcommand_count == 0u) &&
            exact->completion && !(exact->flags & SH_CMD_FLAG_SENSITIVE)) {
            level.primary = NULL;
            level.primary_count = 0u;
            level.secondary = NULL;
            level.secondary_count = 0u;
            level.provider_command = exact;
            level.provider_arg_index = tokens_before - i;
            level.registered_root = false;
            break;
        }
        if (!exact->subcommands || exact->subcommand_count == 0u) {
            return SH_OK;
        }
        level.primary = exact->subcommands;
        level.primary_count = exact->subcommand_count;
        level.secondary = NULL;
        level.secondary_count = 0u;
        level.provider_command = NULL;
        level.provider_arg_index = 0u;
        level.registered_root = false;
    }

    first_match[0] = '\0';
    for (i = 0u; i < level_limit(shell, &level); i++) {
        const char *name;
        if (!level_candidate(shell, &level, i, &name)) {
            break;
        }
        if (name && starts_with(name, prefix)) {
            if (match_count == 0u) {
                size_t name_len = strlen(name);
                if (name_len > SH_MAX_LINE_LEN) {
                    name_len = SH_MAX_LINE_LEN;
                }
                memcpy(first_match, name, name_len);
                first_match[name_len] = '\0';
                common_len = name_len;
            } else {
                size_t length = common_prefix_len(first_match, name);
                if (length < common_len) {
                    common_len = length;
                }
            }
            match_count++;
        }
    }

    if (match_count == 0u) {
        return SH_OK;
    }
    if (match_count == 1u) {
        return replace_token(shell, token_start, token_end, first_match, true);
    }
    if (common_len > prefix_len) {
        char common[SH_MAX_LINE_LEN + 1u];
        memcpy(common, first_match, common_len);
        common[common_len] = '\0';
        return replace_token(shell, token_start, token_end, common, false);
    }

    print_matches(shell, &level, prefix, match_count);
    return SH_ERR_AMBIGUOUS;
}
