# Testing and Acceptance Strategy

> Historical test-plan draft. The executable test inventory, commands, and
> verified results are maintained in `docs/testing.md`.

This document defines the test strategy and acceptance scenarios for the
portable C shell parser module. The module is expected to be host-independent:
core tokenizer, history, completion, and command dispatch logic must be testable
without UART, RTOS, or platform terminal dependencies.

## Scope

The test suite covers:

- tokenizer behavior for whitespace, quoting, escaping, empty arguments, and
  parser errors;
- command history navigation and storage limits;
- completion matching and formatting;
- command dispatch, argument passing, return codes, and unknown-command errors;
- byte-by-byte input integration, including editing keys and line termination;
- terminal-facing behavior verified manually on a host serial/console adapter;
- boundary limits for line length, argument count, history depth, and completion
  result count.

The test suite does not validate board-specific UART drivers, shell transport
interrupt handlers, or persistent storage unless those adapters are explicitly
provided by the host application.

## Test Layers

### Unit Tests

Unit tests should link directly against the portable parser module and use fake
callbacks for output, command handlers, history storage, and completion sources.
Each unit test must be deterministic and must not require a real terminal.

Required tokenizer tests:

- `""` returns zero arguments and no dispatch request.
- `"help"` returns one argument: `help`.
- `"  wifi   scan  "` returns `wifi`, `scan`.
- `"wifi set ssid \"Office AP\""` returns `wifi`, `set`, `ssid`,
  `Office AP`.
- `"cmd 'single quoted value'"` returns `cmd`, `single quoted value` if single
  quotes are supported; otherwise it must return a documented unsupported-quote
  error.
- `"cmd \"\""` returns an empty second argument.
- `"cmd a\\ b"` returns `cmd`, `a b` if backslash escaping is supported.
- `"cmd \"unterminated"` returns `SHELL_ERR_UNTERMINATED_QUOTE`.
- a line longer than `SHELL_MAX_LINE_LEN` returns
  `SHELL_ERR_LINE_TOO_LONG`.
- a token count greater than `SHELL_MAX_ARGS` returns
  `SHELL_ERR_TOO_MANY_ARGS`.
- tokenizer output never writes past the argv array or line buffer.

Required history tests:

- pushing commands stores them in insertion order;
- duplicate policy is explicit and tested, either preserving duplicates or
  replacing the newest duplicate according to the module contract;
- history capacity overflow drops the oldest entry;
- empty lines and whitespace-only lines are not stored;
- Up/Down navigation clamps or wraps exactly as documented;
- editing a recalled line does not mutate the stored history entry until the
  edited line is submitted;
- history reset clears all entries and navigation state.

Required completion tests:

- prefix completion returns all matching commands for `he` when `help` and
  `hello` are registered;
- an exact command followed by a space switches to subcommand or argument
  completion when provided by the command handler;
- no-match completion prints no candidates and leaves input unchanged;
- one-match completion inserts the missing suffix;
- many-match completion respects `SHELL_MAX_COMPLETIONS` and returns a
  documented truncation status if more candidates exist;
- completion never emits hidden commands unless explicitly configured to do so;
- completion formatting is stable: candidates are separated by spaces or lines
  according to the public contract.

Required command dispatch tests:

- a registered command receives `argc`, `argv`, user context, and output
  callback unchanged from parsed input;
- unknown commands return `SHELL_ERR_UNKNOWN_COMMAND` and print the configured
  unknown-command message;
- handler return codes propagate to the caller;
- a command entered with no arguments calls the handler with `argc == 1`;
- a base command entered alone can trigger contextual help if the command table
  marks it as a help topic;
- command aliases, if supported, resolve to the same handler and context;
- command table lookup is case-sensitive or case-insensitive according to the
  module contract and has tests for both matching and non-matching cases;
- dispatch rejects `argc > SHELL_MAX_ARGS` before invoking any handler.

### Integration Tests

Integration tests should feed bytes through the same public input function used
by the host adapter. They verify parser state across multiple bytes, not just
whole-line parsing.

Required byte-input scenarios:

- bytes `h`, `e`, `l`, `p`, `\r` dispatch `help`;
- `\n` and `\r\n` are handled according to the documented newline policy and do
  not dispatch the same line twice;
- Backspace deletes the previous character and updates the line buffer;
- Backspace on an empty line is ignored and does not underflow;
- Tab invokes completion without dispatching;
- Up recalls the previous command and Enter dispatches the recalled line;
- Down after Up restores the edited or empty current line according to the
  documented behavior;
- Ctrl+C cancels the current line, clears parser state, and prints a fresh
  prompt if prompts are enabled;
- Ctrl+U or the configured kill-line key clears the current line;
- partial quoted input keeps accumulating until Enter, then reports
  `SHELL_ERR_UNTERMINATED_QUOTE`;
- a line exceeding `SHELL_MAX_LINE_LEN` reports `SHELL_ERR_LINE_TOO_LONG`,
  clears or locks the input state according to the public contract, and can
  accept the next valid command;
- entering more than `SHELL_MAX_ARGS` arguments reports
  `SHELL_ERR_TOO_MANY_ARGS` and does not call the command handler.

Integration tests should capture output into a memory buffer and compare it with
expected prompts, echoes, completions, and error messages. If ANSI escape output
is configurable, run the same scenario with ANSI enabled and disabled.

## Boundary and Limit Tests

The suite must include explicit large-limit cases:

- exactly `SHELL_MAX_LINE_LEN - 1` input bytes plus newline succeeds;
- exactly `SHELL_MAX_LINE_LEN` input bytes follows the documented limit rule;
- `SHELL_MAX_LINE_LEN + 1` input bytes fails with
  `SHELL_ERR_LINE_TOO_LONG`;
- exactly `SHELL_MAX_ARGS` arguments dispatch successfully;
- `SHELL_MAX_ARGS + 1` arguments fail with `SHELL_ERR_TOO_MANY_ARGS`;
- exactly `SHELL_HISTORY_DEPTH` accepted commands are retained;
- `SHELL_HISTORY_DEPTH + 1` commands retain the newest configured number;
- exactly `SHELL_MAX_COMPLETIONS` matches are displayed;
- `SHELL_MAX_COMPLETIONS + 1` matches return the documented truncation status.

Every boundary test must verify both the returned status and the absence of
buffer overwrite. In CI this should be paired with AddressSanitizer or an
equivalent memory checker when the compiler supports it.

## Error Acceptance Criteria

For parser errors, acceptance requires:

- `line too long`: returns `SHELL_ERR_LINE_TOO_LONG`, prints a clear error,
  does not dispatch, and accepts the next valid line;
- `too many args`: returns `SHELL_ERR_TOO_MANY_ARGS`, prints a clear error,
  does not dispatch, and leaves registered command handlers untouched;
- `unterminated quote`: returns `SHELL_ERR_UNTERMINATED_QUOTE`, prints a clear
  error, does not dispatch, and resets quote state after the failed line;
- unknown command: returns `SHELL_ERR_UNKNOWN_COMMAND`, prints the unknown token,
  and leaves history behavior consistent with the module contract;
- completion overflow: returns the documented completion overflow status and
  does not write beyond the completion result buffer.

Error messages should be stable enough for automated tests. If exact text is not
part of the API, tests should assert the error code and check for a stable error
category token such as `line too long`.

## Host Terminal Manual Checklist

Run this checklist against a small host demo program that links the portable
module and registers at least `help`, `echo`, `history`, and `wifi scan` test
commands.

- Start the demo in a terminal and confirm the prompt appears once.
- Type `help` and press Enter; confirm the command dispatches and the prompt
  returns.
- Type text slowly byte-by-byte; confirm local echo matches what the user types
  when echo is enabled.
- Disable echo if supported; confirm typed characters are not printed, but
  command output still appears.
- Type `echo one two "three four"`; confirm the command receives three payload
  arguments: `one`, `two`, `three four`.
- Press Backspace in the middle of input; confirm the displayed line and
  dispatched command both reflect the edit.
- Press Up and Down through at least three history entries; confirm ordering and
  boundary behavior.
- Press Tab on a partial command; confirm one-match and many-match completion
  behavior.
- Enter a command longer than the configured line limit; confirm the terminal
  shows the line-too-long error and the next normal command still works.
- Enter more than the configured argument limit; confirm the too-many-args error
  and no handler side effect.
- Enter an unterminated quote; confirm the quote error and recovery on the next
  prompt.
- Send Ctrl+C during partial input; confirm the line is canceled.
- Paste a long multiword command; confirm byte-input behavior matches manual
  typing.
- On Windows terminal, PowerShell, Linux terminal, and serial terminal emulator,
  confirm newline handling does not double-dispatch for CRLF.

## Local C Compiler Commands

The exact file names may differ by repository layout. The commands below assume
tests are plain C files under `tests/` and module sources are under `src/`.

Build and run with GCC or Clang:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc \
  src/shell_tokenizer.c src/shell_history.c src/shell_completion.c \
  src/shell_dispatch.c tests/test_shell.c -o build/test_shell
./build/test_shell
```

Build with AddressSanitizer when supported:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Isrc \
  src/shell_tokenizer.c src/shell_history.c src/shell_completion.c \
  src/shell_dispatch.c tests/test_shell.c -o build/test_shell_asan
./build/test_shell_asan
```

PowerShell example:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
cc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc `
  src/shell_tokenizer.c src/shell_history.c src/shell_completion.c `
  src/shell_dispatch.c tests/test_shell.c -o build/test_shell.exe
.\build\test_shell.exe
```

If the project uses CMake, local validation should be:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSHELL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI Commands

CI should run at least one warning-clean compiler job and one sanitizer job.

Minimal POSIX CI script:

```sh
set -eu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSHELL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Recommended sanitizer job:

```sh
set -eu
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSHELL_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Recommended matrix:

- Linux GCC, warnings as errors;
- Linux Clang, warnings as errors;
- Linux Clang with AddressSanitizer and UndefinedBehaviorSanitizer;
- Windows MSVC or MinGW build if Windows hosts are supported;
- embedded cross-compile smoke test if the parser module is consumed by firmware.

## Definition of Done

The shell parser module is accepted when:

- all tokenizer, history, completion, and dispatch unit tests pass locally and
  in CI;
- byte-input integration tests pass with CR, LF, and CRLF newline cases;
- boundary tests cover line length, argument count, history depth, and completion
  count limits;
- error scenarios for line too long, too many args, and unterminated quote have
  stable return codes and recovery behavior;
- host terminal manual checklist passes on at least one desktop terminal and one
  serial-style terminal path;
- CI publishes failing test output clearly enough to identify the scenario and
  input bytes that failed.
