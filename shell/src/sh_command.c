#include "sh_shell.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void sh_write_internal(sh_t *shell, const char *text);
void sh_writef_internal(sh_t *shell, const char *fmt, ...);
void sh_history_add_internal(sh_t *shell, const char *line);
int sh_complete_line_internal(sh_t *shell);

static const sh_cmd_t *find_child(const sh_cmd_t *commands, size_t count, const char *name)
{
    size_t i;
    for (i = 0u; i < count; i++) {
        if (commands[i].name && strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

static void print_command_list(sh_t *shell, const sh_cmd_t *commands, size_t count)
{
    size_t i;
    for (i = 0u; i < count; i++) {
        if (commands[i].flags & SH_CMD_FLAG_HIDDEN) {
            continue;
        }
        sh_writef_internal(shell, "  %-14s %s\r\n",
                           commands[i].name ? commands[i].name : "",
                           commands[i].help ? commands[i].help : "");
    }
}

static void print_root_command_list(sh_t *shell)
{
    size_t i;

    if (shell->command_sets) {
        for (i = 0u; i < shell->command_set_count; i++) {
            print_command_list(shell, shell->command_sets[i].commands,
                               shell->command_sets[i].command_count);
        }
        return;
    }
    print_command_list(shell, shell->commands, shell->command_count);
}

static void print_help_for(sh_t *shell, const sh_cmd_t *cmd, const char *path)
{
    if (!cmd) {
        sh_write_internal(shell, "Commands:\r\n");
        print_root_command_list(shell);
        if (shell->cfg.builtins_enabled) {
            sh_write_internal(shell, "Built-ins:\r\n  help           Show help\r\n  history        Show command history\r\n  echo           Echo controls\r\n  clear          Clear terminal\r\n");
        }
        return;
    }

    sh_writef_internal(shell, "%s\r\n", path);
    if (cmd->usage && *cmd->usage) {
        sh_writef_internal(shell, "usage: %s %s\r\n", path, cmd->usage);
    }
    if (cmd->help && *cmd->help) {
        sh_writef_internal(shell, "%s\r\n", cmd->help);
    }
    if (cmd->handler) {
        if (cmd->max_args == SH_ARGS_ANY) {
            sh_writef_internal(shell, "args: min=%u, max=any (including command)\r\n",
                               (unsigned)cmd->min_args);
        } else {
            sh_writef_internal(shell, "args: min=%u, max=%u (including command)\r\n",
                               (unsigned)cmd->min_args,
                               (unsigned)cmd->max_args);
        }
    }
    if (cmd->subcommands && cmd->subcommand_count) {
        sh_write_internal(shell, "Subcommands:\r\n");
        print_command_list(shell, cmd->subcommands, cmd->subcommand_count);
    }
}

const sh_cmd_t *sh_resolve_command_internal(sh_t *shell, int argc, char **argv, int *consumed, char *path, size_t path_size)
{
    const sh_cmd_t *cmd = NULL;
    const sh_cmd_t *level = NULL;
    size_t level_count = 0u;
    int i;

    if (path_size) {
        path[0] = '\0';
    }

    for (i = 0; i < argc; i++) {
        const sh_cmd_t *next = NULL;

        if (i == 0 && shell->command_sets) {
            size_t set_index;
            for (set_index = 0u; set_index < shell->command_set_count; set_index++) {
                next = find_child(shell->command_sets[set_index].commands,
                                  shell->command_sets[set_index].command_count,
                                  argv[i]);
                if (next) {
                    break;
                }
            }
        } else if (i == 0) {
            next = find_child(shell->commands, shell->command_count, argv[i]);
        } else {
            next = find_child(level, level_count, argv[i]);
        }
        if (!next) {
            break;
        }
        cmd = next;
        if (path_size) {
            size_t used = strlen(path);
            snprintf(path + used, path_size - used, "%s%s", used ? " " : "", argv[i]);
        }
        level = next->subcommands;
        level_count = next->subcommand_count;
    }

    *consumed = i;
    return cmd;
}

static int builtin_history(sh_t *shell, int argc, char **argv)
{
    size_t i;
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        sh_history_clear(shell);
        sh_write_internal(shell, "history cleared\r\n");
        return SH_OK;
    }

    sh_writef_internal(shell, "history: %u/%u commands\r\n",
                       (unsigned)sh_history_count(shell),
                       (unsigned)sh_history_capacity(shell));

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return SH_OK;
    }

    if (argc != 1) {
        sh_write_internal(shell, "usage: history [clear|status]\r\n");
        return SH_ERR_INVALID_ARG;
    }

    for (i = 0u; i < sh_history_count(shell); i++) {
        const char *entry = sh_history_get_newest(shell, sh_history_count(shell) - 1u - i);
        sh_writef_internal(shell, "%u: %s\r\n", (unsigned)i, entry ? entry : "");
    }
    return SH_OK;
}

static int builtin_echo(sh_t *shell, int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0)) {
        sh_writef_internal(shell, "echo: %s\r\n", shell->echo_enabled ? "on" : "off");
        return SH_OK;
    }
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
        sh_set_echo_enabled(shell, true);
        sh_write_internal(shell, "echo: on\r\n");
        return SH_OK;
    }
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        sh_set_echo_enabled(shell, false);
        sh_write_internal(shell, "echo: off\r\n");
        return SH_OK;
    }
    sh_write_internal(shell, "usage: echo on|off|status\r\n");
    return SH_ERR_INVALID_ARG;
}

int sh_command_dispatch_internal(sh_t *shell, int argc, char **argv)
{
    const sh_cmd_t *cmd;
    int consumed = 0;
    char path[128];

    if (argc == 0) {
        return SH_OK;
    }

    if (shell->cfg.builtins_enabled && strcmp(argv[0], "help") == 0) {
        if (argc == 1) {
            print_help_for(shell, NULL, "");
            return SH_OK;
        }
        cmd = sh_resolve_command_internal(shell, argc - 1, &argv[1], &consumed, path, sizeof(path));
        if (cmd && consumed == argc - 1) {
            print_help_for(shell, cmd, path);
            return SH_OK;
        }
        sh_write_internal(shell, "help: unknown topic\r\n");
        return SH_ERR_UNKNOWN_COMMAND;
    }

    if (shell->cfg.builtins_enabled && strcmp(argv[0], "history") == 0) {
        return builtin_history(shell, argc, argv);
    }

    if (shell->cfg.builtins_enabled && strcmp(argv[0], "echo") == 0) {
        return builtin_echo(shell, argc, argv);
    }

    if (shell->cfg.builtins_enabled && strcmp(argv[0], "clear") == 0) {
        if (shell->cfg.ansi_enabled) {
            sh_write_internal(shell, "\x1b[2J\x1b[H");
        } else {
            sh_write_internal(shell, "\r\n");
        }
        return SH_OK;
    }

    cmd = sh_resolve_command_internal(shell, argc, argv, &consumed, path, sizeof(path));
    if (!cmd) {
        sh_writef_internal(shell, "error: unknown command: %s\r\n", argv[0]);
        return SH_ERR_UNKNOWN_COMMAND;
    }

    if (consumed < argc &&
        (strcmp(argv[consumed], "help") == 0 ||
         strcmp(argv[consumed], "--help") == 0 ||
         strcmp(argv[consumed], "-h") == 0)) {
        print_help_for(shell, cmd, path);
        return SH_OK;
    }

    if (consumed < argc && cmd->subcommands && cmd->subcommand_count) {
        sh_writef_internal(shell, "error: unknown subcommand: %s\r\n",
                           argv[consumed]);
        return SH_ERR_UNKNOWN_COMMAND;
    }

    if (cmd->subcommands && cmd->subcommand_count && consumed == argc) {
        print_help_for(shell, cmd, path);
        return SH_OK;
    }

    if (!cmd->handler) {
        sh_writef_internal(shell, "error: incomplete command: %s\r\n", path);
        return SH_ERR_INCOMPLETE_COMMAND;
    }

    {
        int handler_argc = argc - consumed + 1;
        char **handler_argv = &argv[consumed - 1];
        void *handler_ctx = cmd->user_ctx ? cmd->user_ctx : shell->cfg.user_ctx;

        if ((size_t)handler_argc < cmd->min_args ||
            (cmd->max_args != SH_ARGS_ANY && (size_t)handler_argc > cmd->max_args)) {
            sh_writef_internal(shell, "error: invalid argument count for %s\r\n", path);
            if (cmd->usage && *cmd->usage) {
                sh_writef_internal(shell, "usage: %s %s\r\n", path, cmd->usage);
            }
            return SH_ERR_INVALID_ARG;
        }
        return cmd->handler(shell, handler_argc, handler_argv, handler_ctx);
    }
}

static bool is_valid_command_name(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;

    if (!name || *name == '\0') {
        return false;
    }
    while (*p) {
        if (isspace(*p) || *p < 33u || *p == 127u) {
            return false;
        }
        p++;
    }
    return true;
}

static bool is_reserved_root_name(const char *name)
{
    static const char *const reserved[] = { "clear", "echo", "help", "history" };
    size_t i;

    for (i = 0u; i < SH_ARRAY_SIZE(reserved); i++) {
        if (strcmp(name, reserved[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int validate_command_level(const sh_cmd_t *commands, size_t count,
                                  size_t depth, bool root,
                                  bool builtins_enabled)
{
    size_t i;
    size_t j;

    if (count != 0u && !commands) {
        return SH_ERR_INVALID_CONFIG;
    }
    if (depth > SH_MAX_CMD_DEPTH) {
        return SH_ERR_INVALID_CONFIG;
    }

    for (i = 0u; i < count; i++) {
        const sh_cmd_t *cmd = &commands[i];

        if (!is_valid_command_name(cmd->name) ||
            (root && builtins_enabled && is_reserved_root_name(cmd->name)) ||
            (!cmd->subcommands && cmd->subcommand_count != 0u) ||
            (cmd->subcommands && cmd->subcommand_count == 0u) ||
            (cmd->handler && (cmd->min_args == 0u ||
             (cmd->max_args != SH_ARGS_ANY && cmd->min_args > cmd->max_args)))) {
            return SH_ERR_INVALID_CONFIG;
        }
        for (j = i + 1u; j < count; j++) {
            if (commands[j].name && strcmp(cmd->name, commands[j].name) == 0) {
                return SH_ERR_INVALID_CONFIG;
            }
        }
        if (cmd->subcommand_count != 0u) {
            int status = validate_command_level(cmd->subcommands,
                                                cmd->subcommand_count,
                                                 depth + 1u, false,
                                                 builtins_enabled);
            if (status != SH_OK) {
                return status;
            }
        }
    }
    return SH_OK;
}

int sh_register_commands(sh_t *shell, const sh_cmd_t *commands, size_t count)
{
    int status;

    if (!shell || (!commands && count != 0u)) {
        return SH_ERR_INVALID_ARG;
    }
    status = validate_command_level(commands, count, 1u, true,
                                    shell->cfg.builtins_enabled);
    if (status != SH_OK) {
        return status;
    }
    shell->commands = commands;
    shell->command_count = count;
    shell->command_sets = NULL;
    shell->command_set_count = 0u;
    return SH_OK;
}

int sh_register_command_sets(sh_t *shell, const sh_command_set_t *sets,
                             size_t set_count)
{
    size_t i;
    size_t j;
    size_t a;
    size_t b;

    if (!shell || (!sets && set_count != 0u)) {
        return SH_ERR_INVALID_ARG;
    }
    for (i = 0u; i < set_count; i++) {
        int status = validate_command_level(sets[i].commands,
                                            sets[i].command_count,
                                            1u, true,
                                            shell->cfg.builtins_enabled);
        if (status != SH_OK) {
            return status;
        }
    }
    for (i = 0u; i < set_count; i++) {
        for (j = i + 1u; j < set_count; j++) {
            for (a = 0u; a < sets[i].command_count; a++) {
                for (b = 0u; b < sets[j].command_count; b++) {
                    if (strcmp(sets[i].commands[a].name,
                               sets[j].commands[b].name) == 0) {
                        return SH_ERR_INVALID_CONFIG;
                    }
                }
            }
        }
    }

    shell->commands = NULL;
    shell->command_count = 0u;
    shell->command_sets = sets;
    shell->command_set_count = set_count;
    return SH_OK;
}
