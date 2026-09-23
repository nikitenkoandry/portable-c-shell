#include "sh_shell.h"
#include "sh_port_host.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int exit_requested;
} host_app_t;

static void host_write(const char *data, size_t len, void *ctx)
{
    (void)ctx;
    (void)fwrite(data, 1u, len, stdout);
    (void)fflush(stdout);
}

static int cmd_ping(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    return sh_puts(shell, "pong\r\n");
}

static int cmd_print_args(sh_t *shell, int argc, char **argv, void *ctx)
{
    int i;
    int result = SH_OK;

    (void)ctx;
    for (i = 1; i < argc; i++) {
        int status = sh_printf(shell, "arg[%d]=%s\r\n", i - 1, argv[i]);
        if (result == SH_OK && status != SH_OK) {
            result = status;
        }
    }
    return result;
}

static int cmd_secret_arg(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    return sh_puts(shell, "stored <hidden>\r\n");
}

static int cmd_wifi_status(sh_t *shell, int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    return sh_puts(shell, "wifi: disconnected\r\n");
}

static int cmd_exit(sh_t *shell, int argc, char **argv, void *ctx)
{
    host_app_t *app = (host_app_t *)ctx;

    (void)argc;
    (void)argv;
    app->exit_requested = 1;
    return sh_puts(shell, "bye\r\n");
}

static const char *interface_completion(sh_t *shell, size_t arg_index,
                                        size_t candidate_index, void *ctx)
{
    static const char *const names[] = { "eth0", "uart0", "usb_cdc0", "wifi0" };

    (void)shell;
    (void)ctx;
    if (arg_index != 1u || candidate_index >= SH_ARRAY_SIZE(names)) {
        return NULL;
    }
    return names[candidate_index];
}

static const sh_cmd_t wifi_set_cmds[] = {
    SH_CMD_ARG("ssid", "<ssid>", "Set Wi-Fi SSID", cmd_print_args, 2u, 2u),
    SH_CMD_SENSITIVE_ARG("pass", "<password>", "Set Wi-Fi password",
                         cmd_secret_arg, 2u, 2u),
};

static const sh_cmd_t wifi_cmds[] = {
    SH_CMD_ARG("scan", "", "Scan Wi-Fi networks", cmd_print_args, 1u, 1u),
    SH_CMD_SUB("set", "", "Wi-Fi settings", wifi_set_cmds),
    SH_CMD_ARG("status", "", "Show Wi-Fi status", cmd_wifi_status, 1u, 1u),
};

static const sh_cmd_t tag_config_cmds[] = {
    SH_CMD_ARG("set", "interval <ms>", "Set tag config value",
               cmd_print_args, 3u, 3u),
    SH_CMD_ARG("show", "", "Show tag config", cmd_print_args, 1u, 1u),
};

static const sh_cmd_t tag_cmds[] = {
    SH_CMD("list", "", "List tags", cmd_print_args),
    SH_CMD_SUB("config", "", "Tag configuration", tag_config_cmds),
};

static const sh_cmd_t root_cmds[] = {
    SH_CMD_ARG("ping", "", "Print pong", cmd_ping, 1u, 1u),
    SH_CMD("args", "[arg ...]", "Print many arguments", cmd_print_args),
    SH_CMD_COMPLETE("interface", "<name>", "Select an interface",
                    cmd_print_args, interface_completion),
    SH_CMD_CTX("exit", "", "Exit the host terminal", cmd_exit, NULL),
    SH_CMD_SUB("wifi", "", "Wi-Fi commands", wifi_cmds),
    SH_CMD_SUB("tag", "", "Tag commands", tag_cmds),
};

SH_STORAGE_DEFINE(host_storage, SH_MAX_LINE_LEN, SH_MAX_ARGC,
                  SH_HISTORY_DEFAULT_DEPTH);

static void print_limits(void)
{
    size_t storage_bytes = sizeof(host_storage_line) + sizeof(host_storage_argv) +
                           sizeof(host_storage_history) + sizeof(host_storage_draft);

    printf("SH_MAX_LINE_LEN=%u\n", (unsigned)SH_MAX_LINE_LEN);
    printf("SH_MAX_ARGC=%u\n", (unsigned)SH_MAX_ARGC);
    printf("SH_MAX_CMD_DEPTH=%u\n", (unsigned)SH_MAX_CMD_DEPTH);
    printf("SH_HISTORY_DEFAULT_DEPTH=%u\n",
           (unsigned)SH_HISTORY_DEFAULT_DEPTH);
    printf("sizeof(sh_t)=%u\n", (unsigned)sizeof(sh_t));
    printf("host_storage_bytes=%u\n", (unsigned)storage_bytes);
}

static int run_script(sh_t *shell, const char *path, host_app_t *app)
{
    unsigned char buffer[17];
    FILE *stream = fopen(path, "rb");

    if (!stream) {
        fprintf(stderr, "Cannot open script: %s\n", path);
        return 5;
    }
    while (!app->exit_requested) {
        size_t count = fread(buffer, 1u, sizeof(buffer), stream);
        int status;

        if (count == 0u) {
            break;
        }
        status = sh_feed(shell, buffer, count);
        if (status != SH_OK) {
            fprintf(stderr, "script status: %s\n", sh_status_string(status));
        }
    }
    if (ferror(stream)) {
        (void)fclose(stream);
        return 6;
    }
    if (shell->line_len != 0u && !app->exit_requested) {
        (void)sh_feed_byte(shell, '\n');
    }
    (void)fclose(stream);
    return 0;
}

static int parse_options(int argc, char **argv, const char **script_path,
                         int *ansi_enabled)
{
    int i;

    *script_path = NULL;
    *ansi_enabled = 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-ansi") == 0) {
            *ansi_enabled = 0;
        } else if (strcmp(argv[i], "--limits") == 0) {
            print_limits();
            return 1;
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            *script_path = argv[++i];
        } else {
            fprintf(stderr,
                    "usage: %s [--no-ansi] [--limits] [--script file]\n",
                    argv[0]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    static sh_t shell;
    sh_config_t cfg;
    sh_host_terminal_t terminal = SH_HOST_TERMINAL_INIT;
    host_app_t app = { 0 };
    const char *script_path;
    unsigned char byte;
    int ansi_enabled;
    int read_status = SH_HOST_TERMINAL_READ_EOF;
    int option_status = parse_options(argc, argv, &script_path, &ansi_enabled);

    if (option_status != 0) {
        return option_status > 0 ? 0 : 2;
    }

    sh_default_config(&cfg);
    cfg.prompt = "demo> ";
    cfg.write = host_write;
    cfg.user_ctx = &app;
    cfg.ansi_enabled = ansi_enabled != 0;

    if (sh_init_static(&shell, &cfg, &host_storage) != SH_OK) {
        return 1;
    }
    if (sh_register_commands(&shell, root_cmds, SH_ARRAY_SIZE(root_cmds)) != SH_OK) {
        return 2;
    }

    if (script_path) {
        int result;
        (void)sh_start(&shell);
        result = run_script(&shell, script_path, &app);
        (void)sh_stop(&shell);
        return result;
    }

    puts("Portable C shell. Try help, wifi s<Tab>, interface <Tab>, history, Up/Down.");
    if (sh_host_terminal_open(&terminal) != SH_HOST_TERMINAL_SUCCESS) {
        fputs("Failed to open host terminal.\n", stderr);
        return 3;
    }
    if (sh_start(&shell) != SH_OK) {
        (void)sh_host_terminal_close(&terminal);
        return 4;
    }
    while (!app.exit_requested &&
           (read_status = sh_host_terminal_read_byte(&terminal, &byte)) ==
               SH_HOST_TERMINAL_READ_BYTE) {
        (void)sh_feed_byte(&shell, byte);
    }
    (void)sh_stop(&shell);
    if (sh_host_terminal_close(&terminal) != SH_HOST_TERMINAL_SUCCESS) {
        return 7;
    }
    return app.exit_requested || read_status == SH_HOST_TERMINAL_READ_EOF ? 0 : 8;
}
