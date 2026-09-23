# Architecture and Public API Specification

> Historical design note. Some names and signatures below predate the current
> implementation. The normative API is in `shell/include/*.h` and
> `docs/porting.md`.

## Purpose

The module shall provide a portable interactive shell parser in C with a user
experience close to Zephyr shell, while remaining independent from any
operating system, scheduler, UART driver, heap allocator, or logging framework.

The module owns command-line editing, tokenization, command lookup, contextual
help, dispatch, prompt rendering, and transport-neutral I/O glue. The
application owns command handlers, command tables, transport drivers, optional
task/thread creation, persistent settings, and security policy.

## Design Goals

- Build as a reusable C module for embedded and host targets.
- Require no mandatory `malloc`; support fully static configuration.
- Keep all OS-specific code behind small adapter layers.
- Allow byte-stream transports such as UART, USB CDC, TCP, stdio, and test
  fixtures.
- Support a Zephyr-like command tree: root commands, subcommands, help text,
  usage text, and contextual help when a base command is entered alone.
- Keep the default `help` compact and provide focused help through
  `help <topic>`, `<topic>`, and `<topic> help`.
- Provide optional echo control and masking hooks for sensitive arguments.
- Be deterministic: no hidden global driver dependencies, no unbounded parsing,
  and no transport blocking inside parser code.

## Module Layers

### 1. Core Parser Layer

Responsible for:

- input byte normalization;
- line editing state;
- command buffer management;
- tokenization into `argc` and `argv`;
- quote and escape handling;
- command lookup;
- built-in command dispatch;
- application command dispatch;
- error code generation.

This layer shall not call OS APIs, sleep functions, UART drivers, printf-like
global streams, or heap allocators directly.

Expected files:

- `serial_shell.h`
- `serial_shell.c`

### 2. Command Registry Layer

Responsible for:

- storing root command descriptors;
- storing child command descriptors;
- longest-prefix matching for command topics;
- command visibility/filtering flags;
- command metadata used by help output;
- optional dynamic registration if enabled by configuration.

The default registry shall be static: the application passes arrays of command
descriptors at initialization time. Dynamic registration is optional and must be
compiled out unless explicitly enabled.

Expected files:

- `serial_shell_cmd.h`
- `serial_shell_cmd.c`

### 3. Built-In Commands Layer

Responsible for standard shell commands:

- `help`
- `echo on`
- `echo off`
- `echo status`
- optional `clear`
- optional `history`
- optional `version`

Built-ins shall be regular commands in the same registry model as application
commands. An application may disable individual built-ins at compile time or
replace them with application-owned handlers.

Expected files:

- `serial_shell_builtin.h`
- `serial_shell_builtin.c`

### 4. Transport Boundary

Responsible for sending and receiving bytes through caller-provided callbacks.
The shell core shall only know about the callback interface. It shall not know
whether bytes come from UART ISR queues, USB CDC, TCP sockets, stdin, or test
buffers.

Expected files:

- `serial_shell_transport.h`

Transport implementations belong to adapters, board support packages, or tests,
not to the core parser.

### 5. Platform Adapter Layer

Responsible for optional integration with a platform runtime:

- mutex/lock wrappers;
- critical sections;
- task creation;
- tick/time source;
- stdio bridge for host tests;
- RTOS queue bridge;
- bare-metal polling loop.

Adapters shall be thin and replaceable. The core API must remain usable without
linking any adapter.

Expected adapter directories:

- `adapters/freertos/`
- `adapters/bare_metal/`
- `adapters/host/`

## Public Types

### Status Codes

```c
typedef enum {
    SERIAL_SHELL_OK = 0,
    SERIAL_SHELL_EINVAL,
    SERIAL_SHELL_ENOMEM,
    SERIAL_SHELL_EOVERFLOW,
    SERIAL_SHELL_ENOTFOUND,
    SERIAL_SHELL_EAMBIGUOUS,
    SERIAL_SHELL_EIO,
    SERIAL_SHELL_EBUSY,
} serial_shell_status_t;
```

### Shell Instance

```c
typedef struct serial_shell serial_shell_t;
```

The instance is opaque to users of the public API. For no-malloc builds the
caller provides storage through `serial_shell_storage_t`.

### Static Storage

```c
typedef struct {
    char *line_buf;
    size_t line_buf_size;
    char **argv_buf;
    size_t argv_buf_count;
    char *history_buf;
    size_t history_buf_size;
    void *user_area;
    size_t user_area_size;
} serial_shell_storage_t;
```

`history_buf` and `user_area` are optional. If history is disabled,
`history_buf_size` may be zero.

### Configuration

```c
typedef struct {
    const char *prompt;
    const char *newline;
    bool echo_default;
    bool insert_mode_default;
    bool enable_builtin_help;
    bool enable_builtin_echo;
    bool enable_history;
    bool enable_colors;
    size_t max_line_len;
    size_t max_args;
    void *user_ctx;
} serial_shell_config_t;
```

Rules:

- `prompt` defaults to `"> "` if `NULL`.
- `newline` defaults to `"\r\n"` for embedded targets.
- `max_line_len` and `max_args` must fit the buffers provided in
  `serial_shell_storage_t`.
- `user_ctx` is passed unchanged to command handlers and callbacks.

### Transport Callbacks

```c
typedef struct {
    serial_shell_status_t (*write)(
        void *ctx,
        const uint8_t *data,
        size_t len,
        size_t *written);

    serial_shell_status_t (*flush)(void *ctx);

    void *ctx;
} serial_shell_transport_t;
```

The core receives input through explicit feed APIs, so a read callback is not
required. This keeps the parser usable from ISR-drained queues, polling loops,
unit tests, and host stdio.

Transport requirements:

- `write` may write fewer bytes than requested and report the count in
  `written`.
- The core shall not spin forever on partial writes.
- `flush` is optional and may be `NULL`.
- Transport callbacks must never call back into the same shell instance unless
  the adapter documents that it is reentrant-safe.

### Command Handler

```c
typedef int (*serial_shell_cmd_handler_t)(
    serial_shell_t *shell,
    int argc,
    char **argv,
    void *user_ctx);
```

Handlers return zero on success and a negative application-defined error code
on failure. The shell shall print a generic error only when the handler returns
failure without producing its own diagnostic.

### Command Descriptor

```c
typedef struct serial_shell_cmd {
    const char *name;
    const char *summary;
    const char *usage;
    const char *help;
    serial_shell_cmd_handler_t handler;
    const struct serial_shell_cmd *children;
    size_t child_count;
    uint32_t flags;
} serial_shell_cmd_t;
```

Command descriptor rules:

- `name` is required.
- `summary` is one short line used by compact help.
- `usage` is shown in contextual help.
- `help` may contain longer plain text.
- `handler` may be `NULL` if the command is only a parent topic.
- `children` may be `NULL` when `child_count` is zero.
- Command names are case-sensitive by default.

Suggested flags:

```c
#define SERIAL_SHELL_CMD_HIDDEN        (1u << 0)
#define SERIAL_SHELL_CMD_ADMIN         (1u << 1)
#define SERIAL_SHELL_CMD_SENSITIVE_ARG (1u << 2)
#define SERIAL_SHELL_CMD_NO_HISTORY    (1u << 3)
```

### Command Registry

```c
typedef struct {
    const serial_shell_cmd_t *commands;
    size_t command_count;
} serial_shell_registry_t;
```

The registry may be replaced as a whole while the shell is stopped. Runtime
mutation requires `SERIAL_SHELL_ENABLE_DYNAMIC_REGISTRY`.

### Masking Callback

```c
typedef bool (*serial_shell_mask_arg_fn)(
    void *user_ctx,
    int argc,
    char **argv,
    int arg_index);
```

When echo or history is enabled, the module calls this hook before displaying
or storing each argument. It allows application rules such as masking passwords,
bearer tokens, keys, or provisioning secrets as `<hidden>`.

## Public API

### Initialization

```c
serial_shell_status_t serial_shell_init_static(
    serial_shell_t *shell,
    const serial_shell_config_t *config,
    const serial_shell_storage_t *storage,
    const serial_shell_transport_t *transport,
    const serial_shell_registry_t *registry);
```

Initializes a caller-owned shell instance and caller-owned buffers. This is the
primary API and must be available in all builds.

```c
serial_shell_t *serial_shell_create(
    const serial_shell_config_t *config,
    const serial_shell_transport_t *transport,
    const serial_shell_registry_t *registry);

void serial_shell_destroy(serial_shell_t *shell);
```

These APIs are optional and only available when `SERIAL_SHELL_ENABLE_MALLOC` is
enabled.

### Input Feeding

```c
serial_shell_status_t serial_shell_feed_byte(
    serial_shell_t *shell,
    uint8_t byte);

serial_shell_status_t serial_shell_feed(
    serial_shell_t *shell,
    const uint8_t *data,
    size_t len);
```

Adapters call these functions when received bytes are available. The functions
may dispatch a command when a full line is received.

### Polling and Prompt

```c
serial_shell_status_t serial_shell_start(serial_shell_t *shell);
serial_shell_status_t serial_shell_stop(serial_shell_t *shell);
serial_shell_status_t serial_shell_print_prompt(serial_shell_t *shell);
bool serial_shell_is_running(const serial_shell_t *shell);
```

`start` prints the initial prompt and enables input processing. It shall not
create a thread or task by itself.

### Output Helpers

```c
serial_shell_status_t serial_shell_write(
    serial_shell_t *shell,
    const char *text);

serial_shell_status_t serial_shell_printf(
    serial_shell_t *shell,
    const char *fmt,
    ...);

serial_shell_status_t serial_shell_vprintf(
    serial_shell_t *shell,
    const char *fmt,
    va_list ap);
```

`printf` support may be disabled with `SERIAL_SHELL_ENABLE_PRINTF=0`. When
disabled, handlers use `serial_shell_write`.

### Command Execution

```c
serial_shell_status_t serial_shell_execute_line(
    serial_shell_t *shell,
    const char *line);

serial_shell_status_t serial_shell_execute_argv(
    serial_shell_t *shell,
    int argc,
    char **argv);
```

These APIs support tests, scripted host execution, and application-driven
command invocation.

### Registry Operations

```c
serial_shell_status_t serial_shell_set_registry(
    serial_shell_t *shell,
    const serial_shell_registry_t *registry);

const serial_shell_registry_t *serial_shell_get_registry(
    const serial_shell_t *shell);
```

Optional dynamic APIs:

```c
serial_shell_status_t serial_shell_register_command(
    serial_shell_t *shell,
    const serial_shell_cmd_t *command);

serial_shell_status_t serial_shell_unregister_command(
    serial_shell_t *shell,
    const char *name);
```

### Echo and Sensitive Data

```c
void serial_shell_set_echo_enabled(serial_shell_t *shell, bool enabled);
bool serial_shell_echo_is_enabled(const serial_shell_t *shell);

void serial_shell_set_mask_arg_callback(
    serial_shell_t *shell,
    serial_shell_mask_arg_fn callback);
```

The default echo state comes from `serial_shell_config_t.echo_default`.
Applications that accept credentials must install a masking callback or mark
commands with `SERIAL_SHELL_CMD_SENSITIVE_ARG`.

### Help

```c
serial_shell_status_t serial_shell_print_help(
    serial_shell_t *shell,
    const char *topic);
```

Behavior:

- `help` prints compact root command help.
- `help <topic>` prints contextual help for the best matching command topic.
- `<topic>` prints contextual help when the topic has children but no direct
  action.
- `<topic> help` prints the same contextual help.
- Unknown topics return `SERIAL_SHELL_ENOTFOUND` and print a short diagnostic.
- Ambiguous topics return `SERIAL_SHELL_EAMBIGUOUS` and print matching choices.

## Tokenization Requirements

The parser shall support:

- spaces and tabs as separators;
- double quotes for arguments containing spaces;
- backslash escape inside quoted strings;
- empty quoted arguments;
- CR, LF, and CRLF line endings;
- configurable maximum line length;
- configurable maximum argument count.

The parser may reject unsupported shell grammar such as pipes, redirects,
environment expansion, globbing, command substitution, and background jobs.
These features are out of scope for the portable embedded module.

## Memory Model

The module must support these modes:

### Static-Only Mode

Compile-time setting:

```c
#define SERIAL_SHELL_ENABLE_MALLOC 0
```

Requirements:

- all buffers are supplied by `serial_shell_storage_t`;
- no calls to `malloc`, `calloc`, `realloc`, or `free`;
- command registry is caller-owned and read-only;
- history uses caller-owned fixed storage or is disabled;
- line overflow returns `SERIAL_SHELL_EOVERFLOW` and recovers cleanly.

### Optional Heap Mode

Compile-time setting:

```c
#define SERIAL_SHELL_ENABLE_MALLOC 1
```

Requirements:

- heap APIs are isolated in one allocation wrapper;
- allocation failure returns `SERIAL_SHELL_ENOMEM`;
- static initialization remains available and preferred for firmware.

## Transport Callback Contract

The shell core emits output only through `serial_shell_transport_t.write`.

Adapter examples:

- UART adapter: `write` queues or blocks according to board policy.
- USB CDC adapter: `write` submits bytes to CDC class driver.
- TCP adapter: `write` sends to socket with partial-write handling.
- Host adapter: `write` forwards to stdout or a test capture buffer.

Input flow:

1. Driver receives bytes.
2. Adapter forwards bytes to `serial_shell_feed` or `serial_shell_feed_byte`.
3. Core updates edit state.
4. Core dispatches command when a full line is accepted.
5. Handler writes responses through shell output helpers.
6. Core prints the next prompt if running.

## Adapter Requirements

### FreeRTOS Adapter

The FreeRTOS adapter may provide:

- a UART RX queue consumer task;
- optional TX mutex;
- optional shell task creation helper;
- `serial_shell_freertos_start_task`;
- stack size and priority configuration;
- queue-backed ISR-safe RX submission.

The adapter shall not be required by the core library. Applications may also
call the core API directly from their own FreeRTOS task.

Suggested API:

```c
typedef struct {
    const char *task_name;
    uint32_t stack_words;
    UBaseType_t priority;
    QueueHandle_t rx_queue;
} serial_shell_freertos_config_t;

serial_shell_status_t serial_shell_freertos_start_task(
    serial_shell_t *shell,
    const serial_shell_freertos_config_t *config);
```

### Bare-Metal Adapter

The bare-metal adapter may provide:

- polling helper for RX-ready drivers;
- ring buffer helper;
- non-blocking TX shim;
- periodic `serial_shell_bare_metal_poll` entry point.

Suggested API:

```c
typedef int (*serial_shell_bare_read_byte_fn)(void *ctx);

typedef struct {
    serial_shell_bare_read_byte_fn read_byte;
    void *ctx;
} serial_shell_bare_metal_config_t;

serial_shell_status_t serial_shell_bare_metal_poll(
    serial_shell_t *shell,
    const serial_shell_bare_metal_config_t *config);
```

### Host Adapter

The host adapter may provide:

- stdin/stdout bridge;
- script execution from files;
- test capture transport;
- fuzz or parser test harness support.

Suggested API:

```c
int serial_shell_host_run_stdio(
    serial_shell_t *shell);

int serial_shell_host_run_script(
    serial_shell_t *shell,
    const char *path);
```

## Configuration Surface

Required compile-time options:

```c
#define SERIAL_SHELL_ENABLE_MALLOC            0
#define SERIAL_SHELL_ENABLE_PRINTF            1
#define SERIAL_SHELL_ENABLE_HISTORY           0
#define SERIAL_SHELL_ENABLE_COLORS            0
#define SERIAL_SHELL_ENABLE_DYNAMIC_REGISTRY  0
#define SERIAL_SHELL_MAX_LINE_LEN             128
#define SERIAL_SHELL_MAX_ARGS                 16
```

Required runtime configuration:

- prompt string;
- newline sequence;
- default echo state;
- built-in command enablement;
- storage buffers;
- transport callbacks;
- command registry;
- user context pointer.

The module shall compile with only C99 and standard headers unless an adapter
explicitly documents additional platform dependencies.

## Command Registry Example

```c
static int cmd_wifi_status(serial_shell_t *shell, int argc, char **argv, void *ctx);
static int cmd_wifi_connect(serial_shell_t *shell, int argc, char **argv, void *ctx);

static const serial_shell_cmd_t wifi_children[] = {
    {
        .name = "status",
        .summary = "Show Wi-Fi state",
        .usage = "wifi status",
        .handler = cmd_wifi_status,
    },
    {
        .name = "connect",
        .summary = "Connect to an access point",
        .usage = "wifi connect <ssid> <password>",
        .handler = cmd_wifi_connect,
        .flags = SERIAL_SHELL_CMD_SENSITIVE_ARG,
    },
};

static const serial_shell_cmd_t root_commands[] = {
    {
        .name = "wifi",
        .summary = "Wi-Fi commands",
        .usage = "wifi <command>",
        .help = "Use `wifi status` or `wifi connect <ssid> <password>`.",
        .children = wifi_children,
        .child_count = sizeof(wifi_children) / sizeof(wifi_children[0]),
    },
};

static const serial_shell_registry_t app_registry = {
    .commands = root_commands,
    .command_count = sizeof(root_commands) / sizeof(root_commands[0]),
};
```

## Initialization Example

```c
static char line_buf[128];
static char *argv_buf[16];

static serial_shell_t shell;

static const serial_shell_storage_t shell_storage = {
    .line_buf = line_buf,
    .line_buf_size = sizeof(line_buf),
    .argv_buf = argv_buf,
    .argv_buf_count = sizeof(argv_buf) / sizeof(argv_buf[0]),
};

static const serial_shell_config_t shell_config = {
    .prompt = "dev> ",
    .newline = "\r\n",
    .echo_default = true,
    .enable_builtin_help = true,
    .enable_builtin_echo = true,
    .max_line_len = sizeof(line_buf),
    .max_args = sizeof(argv_buf) / sizeof(argv_buf[0]),
};

void app_shell_init(void)
{
    serial_shell_init_static(
        &shell,
        &shell_config,
        &shell_storage,
        &uart_transport,
        &app_registry);

    serial_shell_start(&shell);
}
```

## Error Handling and Diagnostics

Required user-facing diagnostics:

- unknown command;
- ambiguous command;
- missing argument;
- too many arguments;
- line too long;
- unterminated quote;
- transport write failure.

Diagnostics shall be concise by default. Detailed parser traces may be compiled
in for tests or debug builds.

## Non-Goals

- POSIX shell compatibility.
- Job control.
- Pipes and redirection.
- Environment variable expansion.
- Mandatory heap allocation.
- Mandatory RTOS integration.
- Mandatory UART ownership.
- Application-specific command implementations.

## Acceptance Criteria

- Core parser builds without FreeRTOS, Zephyr, ESP-IDF, or POSIX headers.
- Static-only initialization works without any heap allocator.
- Unit tests can feed bytes and capture output without hardware.
- A FreeRTOS adapter can run the shell from an RX queue task.
- A bare-metal adapter can run the shell from a polling loop.
- A host adapter can run scripts and stdio interaction.
- `help`, `help <topic>`, `<topic>`, and `<topic> help` produce consistent
  contextual output.
- Sensitive arguments can be masked in echo and history.
- Transport partial writes and failures are handled deterministically.
