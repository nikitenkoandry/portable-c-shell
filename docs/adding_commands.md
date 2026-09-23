# Adding Commands

Commands are static `sh_cmd_t` descriptors. The shell borrows command tables
and their strings; it does not allocate or copy them. Keep descriptors in
`static const` storage for at least as long as they are registered.

## Handler Contract

Every leaf handler has the same signature:

```c
static int cmd_status(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    (void)argc;
    (void)argv;
    (void)user_ctx;
    return sh_puts(shell, "device ready\r\n");
}
```

The argument contract is:

```text
argv[0]                  resolved leaf command name
argv[1]..argv[argc - 1] payload arguments
```

Parent names are not included. For `wifi set channel 6`, the handler attached
to `channel` receives `argv[0] == "channel"` and `argv[1] == "6"`.

Handlers run synchronously in the shell owner's execution context. Keep them
bounded; enqueue long operations to an application worker and return. Use
`sh_write()`, `sh_puts()`, or `sh_printf()` for output so the same handler works
with UART, TCP, FreeRTOS, bare-metal, and host transports.

## Add One Command

Use `SH_CMD_ARG` when argument count matters. Its minimum and maximum include
`argv[0]`, so a command with no payload requires exactly one argument:

```c
static const sh_cmd_t app_commands[] = {
    SH_CMD_ARG("status", "", "Show device status",
               cmd_status, 1u, 1u),
};
```

Register the table after shell initialization:

```c
int status = sh_register_commands(shell,
                                  app_commands,
                                  SH_ARRAY_SIZE(app_commands));
if (status != SH_OK) {
    return status;
}
```

`SH_CMD` accepts any payload count. Prefer `SH_CMD_ARG` for commands with a
fixed contract so invalid input is rejected before the handler runs.

## Parameters And Validation

The shell validates counts; handlers validate values. This command requires
one integer payload:

```c
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int cmd_interval(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    char *end;
    long value;

    (void)argc;
    (void)user_ctx;

    errno = 0;
    value = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || value < 100 || value > 60000) {
        return sh_puts(shell, "interval must be 100..60000 ms\r\n");
    }

    return sh_printf(shell, "interval=%ld ms\r\n", value);
}

static const sh_cmd_t app_commands[] = {
    SH_CMD_ARG("interval", "<100..60000>", "Set sample interval",
               cmd_interval, 2u, 2u),
};
```

For a variable number of payload values, use `SH_ARGS_ANY`:

```c
static int cmd_echo(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    int i;
    int status = SH_OK;

    (void)user_ctx;
    for (i = 1; i < argc && status == SH_OK; ++i) {
        status = sh_printf(shell, "%s%s", i == 1 ? "" : " ", argv[i]);
    }
    if (status == SH_OK) {
        status = sh_puts(shell, "\r\n");
    }
    return status;
}

static const sh_cmd_t app_commands[] = {
    SH_CMD_ARG("echo", "<text...>", "Print arguments",
               cmd_echo, 2u, SH_ARGS_ANY),
};
```

Quoted values and escaped spaces are already tokenized before the handler:

```text
echo "two words" three\ words
```

Both payload values arrive as `two words` and `three words`.

## Nested Commands

Build nested commands from child arrays. There is no parser change when a new
group or leaf is added:

```c
static int cmd_wifi_scan(sh_t *shell, int argc, char **argv, void *user_ctx);
static int cmd_wifi_set_ssid(sh_t *shell, int argc, char **argv,
                             void *user_ctx);
static int cmd_wifi_set_pass(sh_t *shell, int argc, char **argv,
                             void *user_ctx);

static const sh_cmd_t wifi_set_commands[] = {
    SH_CMD_ARG("ssid", "<name>", "Set Wi-Fi SSID",
               cmd_wifi_set_ssid, 2u, 2u),
    SH_CMD_SENSITIVE_ARG("pass", "<password>", "Set Wi-Fi password",
                         cmd_wifi_set_pass, 2u, 2u),
};

static const sh_cmd_t wifi_commands[] = {
    SH_CMD_ARG("scan", "", "Scan access points",
               cmd_wifi_scan, 1u, 1u),
    SH_CMD_SUB("set", "<ssid|pass>", "Set Wi-Fi configuration",
               wifi_set_commands),
};

static const sh_cmd_t root_commands[] = {
    SH_CMD_SUB("wifi", "<scan|set>", "Wi-Fi operations", wifi_commands),
};
```

The resulting command paths are:

```text
wifi scan
wifi set ssid "Lab Network"
wifi set pass secret
```

The password handler receives the original value. Interactive echo and history
store `<hidden>` instead of the sensitive payload.

## Commands From Multiple Modules

Modules may keep independent root tables and register them without copying:

```c
static const sh_command_set_t command_sets[] = {
    SH_COMMAND_SET(system_commands),
    SH_COMMAND_SET(network_commands),
    SH_COMMAND_SET(diagnostics_commands),
};

int status = sh_register_command_sets(shell,
                                      command_sets,
                                      SH_ARRAY_SIZE(command_sets));
```

Root names must be unique across all sets. A later call to
`sh_register_commands()` replaces command sets, and a later call to
`sh_register_command_sets()` replaces the flat table.

## Application Context

Set `sh_config_t.user_ctx` to provide one application context to every command:

```c
sh_config_t config;
sh_default_config(&config);
config.user_ctx = &app_state;
```

`SH_CMD_CTX` assigns a command-specific context. A non-NULL command context
overrides the shell-wide context for that handler and its completion provider.

## Argument Completion

A completion provider returns one candidate at a time and then NULL:

```c
static const char *complete_mode(sh_t *shell,
                                 size_t arg_index,
                                 size_t candidate_index,
                                 void *user_ctx)
{
    static const char *const modes[] = { "normal", "safe", "service" };

    (void)shell;
    (void)user_ctx;
    if (arg_index != 1u || candidate_index >= SH_ARRAY_SIZE(modes)) {
        return NULL;
    }
    return modes[candidate_index];
}

static const sh_cmd_t app_commands[] = {
    SH_CMD_COMPLETE("mode", "<name>", "Select operating mode",
                    cmd_mode, complete_mode),
};
```

`arg_index == 1` means the first payload argument. The shell bounds provider
enumeration with `SH_COMPLETION_PROVIDER_MAX_ITEMS` and bounds displayed
alternatives with `SH_COMPLETION_MAX_MATCHES`.

## Command Flags

Use the initializer matching the required behavior:

| Initializer | Behavior |
|---|---|
| `SH_CMD_ARG` | Enforces minimum and maximum total `argc` |
| `SH_CMD_SUB` | Declares a parent with a static child table |
| `SH_CMD_CTX` | Supplies command-specific `user_ctx` |
| `SH_CMD_COMPLETE` | Supplies dynamic argument completion |
| `SH_CMD_SENSITIVE_ARG` | Masks payload and redacts history |
| `SH_CMD_NO_HISTORY` | Executes without adding the line to history |
| `SH_CMD_HIDDEN` | Omits command from help/completion but keeps it callable |

`SH_CMD_END` is only a convenience sentinel for external code. Registration
uses explicit counts, so never include `SH_CMD_END` in the registered count.

## Large Argument Counts

Three limits must agree:

1. Compile-time `SH_MAX_ARGC`.
2. Runtime `config.argc_max`.
3. The pointer capacity in `sh_storage_t` or `SH_STORAGE_DEFINE`.

For 63 payload values, configure 64 total entries because `argv[0]` consumes
one slot:

```cmake
target_compile_definitions(my_firmware PRIVATE SH_MAX_ARGC=64)
```

```c
SH_STORAGE_DEFINE(shell_storage, 256u, 64u, 16u);

sh_default_config(&config);
config.argc_max = 64u;
```

Apply compile definitions consistently to the shell library and every target
that includes its headers.

## Test A New Command

Command tests do not need UART or an RTOS. Register the command in a test shell,
execute text, and assert status, output, and history:

```c
#include "test_common.h"

int main(void)
{
    static const sh_cmd_t commands[] = {
        SH_CMD_ARG("status", "", "Show status", cmd_status, 1u, 1u),
    };
    test_shell_t test;

    test_shell_init(&test);
    ASSERT_EQ_INT(SH_OK,
                  sh_register_commands(&test.shell, commands,
                                       SH_ARRAY_SIZE(commands)));
    ASSERT_EQ_INT(SH_OK, sh_execute_line(&test.shell, "status"));
    ASSERT_CONTAINS(test.out.data, "device ready");
    ASSERT_STREQ("status", sh_history_get_newest(&test.shell, 0u));
    return 0;
}
```

Add the source in the CMake test block with `add_shell_test(test_name)`, then
run `ctest --test-dir build --output-on-failure -R '^test_name$'`.

## Checklist

- Handler checks and propagates output errors.
- Arity counts include `argv[0]`.
- Values and ranges are validated by the handler.
- Secrets use a sensitive command or selective mask callback.
- Long work is delegated outside the shell execution context.
- Command strings, tables, contexts, and completion candidates remain alive.
- New paths are covered by success, invalid-argument, and boundary tests.
