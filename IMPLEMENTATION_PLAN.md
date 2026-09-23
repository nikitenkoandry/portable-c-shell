# Portable C Shell Parser Implementation Plan

## Goal

Build an independent shell parser module in C with Zephyr-like shell ergonomics:

- interactive command input;
- command history with capacity/count diagnostics;
- Up/Down navigation through history;
- Tab completion for root and nested commands;
- convenient static command registration for embedded projects;
- optional dynamic command registration for host/testing use;
- configurable large argument limits;
- no mandatory heap allocation;
- host terminal runner for final manual verification;
- full unit and integration test coverage;
- adapters for host, FreeRTOS, and bare-metal style loops.

The module must remain portable. The shell core must not depend directly on
FreeRTOS, ESP-IDF, Zephyr, POSIX terminals, or a specific UART driver.

## Agent Work Split

### Agent 1: Architecture and API

Output file:

- `docs/architecture_api.md`

Scope:

- module directory layout;
- public headers and opaque shell context;
- configuration model;
- static memory model;
- input/output callbacks;
- FreeRTOS, bare-metal, and host adapters;
- API boundaries between shell core and application code.

### Agent 2: Line Editor, History, and Completion UX

Output file:

- `docs/line_editor_history_completion.md`

Scope:

- Enter, Backspace, Delete, Left, Right, Home, End;
- Arrow Up/Down history navigation;
- draft line restoration after history navigation;
- ANSI escape sequence decoding;
- Tab completion behavior;
- multiple completion matches;
- nested command completion;
- echo controls;
- sensitive argument masking.

### Agent 3: Testing and Acceptance

Output file:

- `docs/testing_acceptance.md`

Scope:

- unit test matrix;
- integration tests using byte-by-byte input;
- host terminal manual checklist;
- local compiler verification;
- error cases;
- acceptance criteria.

### Agent 4: Command Model and Examples

Output file:

- `docs/command_model_examples.md`

Scope:

- static embedded-friendly command tree;
- optional dynamic registration;
- nested commands;
- usage/help metadata;
- argument validation helpers;
- examples from one command to many nested commands;
- FreeRTOS UART and host terminal examples.

## Main Integration Work

After the agent outputs are ready:

1. Review all agent documents.
2. Merge the useful parts into one final implementation specification.
3. Create the initial project skeleton.
4. Implement the shell core in small slices:
   - tokenizer;
   - command registry and dispatcher;
   - built-in help;
   - history ring buffer;
   - line editor;
   - ANSI key decoder;
   - completion engine;
   - built-in history/echo/clear;
   - masking support;
   - host terminal runner.
5. Add tests alongside each implemented slice.
6. Build locally with the available C compiler.
7. Run all automated tests.
8. Launch the host terminal runner for final manual UX verification.

## Proposed Repository Layout

```text
shell/
  include/
    sh_config.h
    sh_shell.h
    sh_command.h
    sh_history.h
    sh_completion.h
    sh_transport.h
  src/
    sh_shell.c
    sh_line_editor.c
    sh_tokenizer.c
    sh_command.c
    sh_history.c
    sh_completion.c
    sh_ansi.c
  ports/
    sh_port_host.c
    sh_port_freertos.c
    sh_port_baremetal.c
  examples/
    host_terminal_shell.c
    simple_shell.c
    nested_commands.c
    freertos_uart_shell.c
  tests/
    test_tokenizer.c
    test_history.c
    test_completion.c
    test_command_dispatch.c
    test_line_editor.c
    test_integration.c
```

## Mandatory Features

### Command Parsing

- Parse commands into `argc`/`argv`.
- Support repeated whitespace.
- Support quoted arguments.
- Support escaped quotes and backslashes.
- Report clear parse errors:
  - line too long;
  - too many arguments;
  - unterminated quote.

### Command Registration

- Support a static command tree as the primary embedded-friendly model.
- Allow nested commands:
  - `wifi scan`;
  - `wifi set ssid <ssid>`;
  - `tag config set interval <ms>`;
  - `cloud set bearer <token>`.
- Bare command entry must show contextual help:
  - `wifi`;
  - `wifi help`;
  - `help wifi`.

### History

- Configurable history depth.
- Default history depth from config.
- Ring-buffer behavior.
- Show capacity and filled command count.
- Navigate with Arrow Up and Arrow Down.
- Restore unsent draft input after returning below newest history entry.

### Completion

- Complete root commands.
- Complete nested commands at the current command-tree level.
- Single match inserts the remaining text.
- Multiple matches display candidates.
- Common prefix may be inserted when applicable.

### Terminal Runner

Provide a host executable that starts an interactive shell:

```text
sh> help
sh> wifi scan
sh> wifi set ssid "My Home WiFi"
sh> tag config set interval 1000
sh> history status
```

The user must be able to verify manually:

- normal command execution;
- history Up/Down;
- Tab completion;
- command help;
- nested command dispatch;
- large argument lists;
- quotes;
- unknown commands;
- line-too-long errors;
- too-many-arguments errors.

## Default Limits

Initial defaults:

```c
#define SH_MAX_LINE_LEN             256
#define SH_MAX_ARGC                 32
#define SH_MAX_CMD_DEPTH            8
#define SH_HISTORY_DEFAULT_DEPTH    16
```

All limits should be configurable through `sh_config_t` and/or compile-time
overrides.

## Acceptance Criteria

The implementation is complete only when:

- the module builds locally;
- all automated tests pass;
- `host_terminal_shell` runs interactively;
- history capacity/count are visible;
- Arrow Up/Down history navigation works;
- Tab completion works for root and nested commands;
- at least 32 input arguments are supported;
- commands are easy to add without editing shell internals;
- sensitive values can be masked from echo/history/output;
- FreeRTOS and bare-metal integration examples are present;
- all shell-owned code lives inside the module directory.
