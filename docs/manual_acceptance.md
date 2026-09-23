# Manual Acceptance Checklist

This checklist validates the visible terminal behavior of the current shell.
It is a procedure, not a record of a completed run. Mark an item PASS only
after observing it on the named build and transport.

Use `docs/testing.md` for automated tests and coverage. A host-terminal pass
does not replace UART, FreeRTOS, TCP, or product-hardware acceptance.

## Test Record

Fill this before testing:

```text
Date/time:
Operator:
Commit/revision:
Compiler/version:
Build command:
Executable/firmware hash:
Host OS and terminal:
Target board and firmware revision:
Transport: host / UART / USB CDC / raw TCP / Telnet
UART settings or network endpoint:
Compile-time SH_MAX_LINE_LEN:
Compile-time SH_MAX_ARGC:
Runtime line_max/argc_max/history_depth:
Result: PASS / FAIL / BLOCKED
Evidence location:
```

Do not store real passwords, bearer tokens, private keys, or customer
credentials in the evidence. Use a unique synthetic marker.

## Build And Start The Host Terminal

On Windows from the repository root:

```powershell
.\scripts\build_host.ps1
.\build_host_artifacts\host_terminal_shell.exe
```

Expected first-screen evidence:

- the process prints its short usage banner;
- one `demo> ` prompt is visible;
- typed characters appear once, not doubled;
- Enter moves command output to a new line;
- the process remains responsive after an invalid command.

The current example has an `exit` command that prints `bye` and closes the host
session. Ctrl+C is intentionally consumed by the line editor and cancels only
the current input line.

Useful non-interactive host options are:

```powershell
.\build_host_artifacts\host_terminal_shell.exe --limits
.\build_host_artifacts\host_terminal_shell.exe --no-ansi
.\build_host_artifacts\host_terminal_shell.exe --script path\to\commands.txt
```

`--limits` prints active compile-time limits and storage sizes. `--no-ansi`
checks append-only output for a dumb terminal. Script mode feeds bounded chunks
through `sh_feed()` and is also used by the current host CTest fixture.

## 1. Help And Nested Commands

Run:

```text
help
help wifi
wifi
help tag config
tag config help
```

PASS criteria:

- root help contains `ping`, `args`, `interface`, `exit`, `wifi`, and `tag`;
- built-ins include `help`, `history`, `echo`, and `clear`;
- `help wifi` and `wifi` show Wi-Fi subcommands;
- both help forms for `tag config` show its nested commands;
- no hidden command is printed if the product table contains hidden entries.

Run an unknown command:

```text
not-a-command
```

PASS criteria: an unknown-command error is printed and a fresh prompt follows.

## 2. Arguments, Quotes, And Nesting

Run:

```text
ping
wifi scan
wifi set ssid "Lab Network"
tag config set interval 2500
tag config show
```

PASS criteria for the current host example:

- `ping` prints `pong`;
- the quoted SSID reaches the handler as one payload argument;
- `tag config set interval 2500` reaches the leaf handler with payload values
  `interval` and `2500`;
- the prompt returns after every command.

Check quoted and escaped inputs with a command that prints arguments:

```text
wifi set ssid "two words"
wifi set ssid escaped\ space
wifi set ssid pre"mid"post
wifi set ssid "quote\"inside"
```

Expected payloads are respectively:

```text
two words
escaped space
premidpost
quote"inside
```

Then submit an unterminated quote:

```text
wifi set ssid "unfinished
```

PASS criteria: the shell reports `unterminated quote`, does not call the
handler, and accepts the next valid command.

## 3. Tab Completion

`<Tab>` below means press the Tab key; do not type the text.

1. Type `pi<Tab>`.
   Expected line: `ping ` with a trailing space.
2. Press Enter.
   Expected output: `pong`.
3. Type `wifi sc<Tab>`.
   Expected line: `wifi scan `.
4. Clear the line with Ctrl+U.
5. Type `wifi s<Tab>`.
   Expected output lists both `scan` and `set`, followed by a redrawn prompt
   and unchanged `wifi s` input.
6. Type `e<Tab>` at the root.
   Expected line: `echo `.
7. Enter `echo s<Tab>`.
   Expected line: `echo status `.
8. Enter `interface et<Tab>`.
   Expected line: `interface eth0 ` from the dynamic argument provider.

PASS criteria:

- exact/unique matches add a space;
- ambiguous matches do not select an arbitrary command;
- the input line is redrawn after alternatives;
- repeated Tab does not corrupt cursor position or duplicate text;
- built-ins participate at root and their subcommands complete;
- hidden commands are not offered.

For an application command with a dynamic completion provider, repeat with:

- one matching candidate;
- two candidates with a longer common prefix;
- two candidates with no additional common prefix;
- no candidate;
- first and later payload argument positions.

Verify that the provider stops returning candidates with NULL and never relies
on more than `SH_COMPLETION_PROVIDER_MAX_ITEMS` calls.

The current scale test registers 128 root commands. On a product build with a
large table, manually confirm that the first
`SH_COMPLETION_MAX_MATCHES` alternatives are shown and the remaining count is
reported without truncating the internal match calculation.

## 4. Cursor Editing

Use a line that is not immediately executed, for example:

```text
wifi set ssid LabX
```

Before pressing Enter:

1. Press Left several times and insert a character in the middle.
2. Press Backspace and verify it removes the byte before the cursor.
3. Press Delete and verify it removes the byte at the cursor.
4. Press Home, type one character, then remove it.
5. Press End and verify the cursor returns to line end.
6. Press Ctrl+W and verify the word left of the cursor is removed.
7. Press Ctrl+U and verify the entire line is cleared.
8. Type another partial line and press Ctrl+L.
9. Press Ctrl+C.

PASS criteria:

- text remains NUL-safe and visually aligned after every operation;
- Home/End and Left/Right do not cross line boundaries;
- Ctrl+L clears the screen and redraws the unchanged line;
- Ctrl+C prints `^C`, discards the line, and produces one new prompt;
- a valid command still works immediately afterward.

Repeat Delete/Home/End with both terminal encodings if the client can select
them: `ESC [ H/F` and `ESC [ 1~/3~/4~`.

## 5. History Capacity, Up, Down, And Draft

Start with:

```text
history clear
ping
wifi scan
tag list
history status
history
```

PASS criteria:

- `history clear` prints confirmation and leaves history empty;
- status uses `filled/capacity` form;
- host default capacity is 16 unless the build configuration changed it;
- entries are listed oldest to newest;
- filled count never exceeds capacity.

Check navigation:

1. At an empty prompt, type `unfinished-draft` without Enter.
2. Press Up once: the newest stored command replaces the draft.
3. Continue pressing Up: commands move toward older entries.
4. Press Up at the oldest entry: it remains at the oldest entry.
5. Press Down toward the newest entry.
6. Press Down once more: `unfinished-draft` is restored exactly.
7. Press Down again: the draft remains unchanged.

Edit a recalled command, then navigate away and back. PASS criteria: the stored
history entry is unchanged; only the editable copy changes.

Run the same valid command twice consecutively. PASS criteria: it occupies one
new history slot. Run a different command and then the first one again. PASS
criteria: the later non-consecutive occurrence is stored.

On a build with small history depth, execute more commands than capacity and
verify wrap-around keeps the newest entries in the correct order.

## 6. Sensitive Input And No-History Policy

Use a unique synthetic marker:

```text
history clear
wifi set pass ACCEPTANCE_SECRET_9371
history
```

PASS criteria:

- `ACCEPTANCE_SECRET_9371` never appears while typing;
- the line editor shows mask characters for the sensitive payload;
- command output does not contain the marker;
- history contains `wifi set pass <hidden>`;
- pressing Up recalls `<hidden>`, not the original marker;
- captured terminal logs do not contain the marker.

For a command registered with `SH_CMD_NO_HISTORY`, execute it and compare
`history status` before and after. PASS criteria: the command itself adds no
entry. Remember that running `history status` is itself a separate command and
can change the count.

For selective `cfg.should_mask_arg`, verify each configured token index and
command path. The callback receives cleartext; instrument it only with
non-sensitive test markers.

An optional non-interactive Windows leak check for the host example is:

```powershell
$secret = 'ACCEPTANCE_SECRET_9371'
$inputText = "wifi set pass $secret`rhistory`r"
$outputText = $inputText | .\build_host_artifacts\host_terminal_shell.exe |
    Out-String
if ($outputText.Contains($secret)) {
    throw 'Sensitive value leaked to shell output'
}
if (-not $outputText.Contains('<hidden>')) {
    throw 'Masked history marker was not observed'
}
```

This checks process output only. It does not prove transport encryption,
secure clearing, log policy, or crash-dump safety.

With a debugger or target-side memory inspection harness, repeat Enter, Ctrl+C,
and Ctrl+U while `cfg.secure_clear_enabled = true`. PASS criteria: the full
editable line and draft are zeroed at the documented cleanup points, parser
temporaries are not retained after `sh_execute_line()`, and the handler still
receives the live cleartext only during execution. Then destroy/reuse the
session and verify the application separately wipes history slots, RX/TX
queues, DMA buffers, and any handler-owned credential copy. `sh_stop()` alone
is not a complete storage wipe.

## 7. Echo Control

Run:

```text
echo status
echo off
```

Then type `ping` blindly and press Enter. Finally type `echo on` blindly and
press Enter.

PASS criteria:

- status initially reports `on` for the default host configuration;
- after `echo off`, input and redraws are invisible;
- Enter still emits CRLF before `pong`, so command output starts on a new line;
- `echo on` restores visible editing;
- history and command parsing continue while echo is off.

## 8. Limits And Recovery

### Line Limit

Paste more than the configured `line_max` without pressing Enter.

PASS criteria:

- the shell prints `error: line too long` for rejected extra input;
- the accepted prefix remains valid and bounded;
- Ctrl+U clears it;
- `ping` works immediately after recovery.

### Argument Limit

For the default `SH_MAX_ARGC == 32`, generate more than 32 total tokens,
including the command name. The exact command can be produced in PowerShell:

```powershell
$line = 'wifi scan ' + ((1..40 | ForEach-Object { "a$_" }) -join ' ')
$line
```

Submit the line in a terminal or through the transport test harness.

PASS criteria: the shell reports `too many arguments`, does not invoke the
handler, and accepts the next valid command.

### Argument Count Contract

For product commands declared with `SH_CMD_ARG`, test exactly one below minimum,
minimum, maximum, and one above maximum. PASS criteria: invalid counts print the
command path and usage; valid counts call the leaf handler with `argv[0]` equal
to the leaf name.

## 9. CR, LF, CRLF, And Fragmentation

Use a byte-level UART/network harness, not manual typing, to send these as
separate cases:

```text
"ping\r"
"ping\n"
"ping\r\n"
```

For CRLF, split CR and LF across two RX callbacks.

PASS criteria: each case executes exactly once and prints one following prompt.

Split `ESC [ A` and `ESC [ 3 ~` at every possible callback boundary. PASS
criteria: history-up and Delete still execute once. Send malformed and
overlong CSI input, then send printable text. PASS criteria: malformed input is
ignored and the next text is accepted without resetting the session.

## 10. Bare-Metal UART Acceptance

Use the adapter pattern in `docs/porting.md`.

- Confirm configured baud, data bits, parity, stop bits, and flow control.
- Paste a line near `line_max` at the maximum supported rate.
- Hold TX blocked temporarily and observe the defined backpressure policy.
- Force one RX driver error and verify `sh_port_baremetal_read_error_count()`.
- Clear the error counter and verify it resets.
- Limit each poll and prove the main loop still services other work.
- Test partial TX accepts and confirm byte order is unchanged.
- Reset during a partial ANSI sequence and verify initialization clears state.

PASS requires zero unexplained RX loss. If loss is intentional under overload,
it must be counted, reported, and followed by a defined session resynchronizing
policy.

## 11. FreeRTOS UART Acceptance

Use the ownership model in `docs/freertos.md`.

- Verify only one ISR producer writes RX StreamBuffer.
- Verify only shell task consumes RX and produces TX.
- Verify only one TX task/driver consumes TX StreamBuffer.
- Force RX StreamBuffer overflow and check `rx_overflow_bytes`.
- Force TX saturation and check return/overflow behavior.
- Request stop with no input and measure latency against `read_timeout_ticks`.
- Attempt a second `sh_freertos_run()` and expect `SH_ERR_BUSY`.
- Restart only after `sh_freertos_clear_stop()` returns `SH_OK`.
- Measure shell and TX task stack high-water marks under long help/completion,
  maximum arguments, and sustained paste.
- Run a deliberately slow command and confirm the product's worker-delegation
  policy prevents unacceptable input starvation.

Record measured task latency, stack margin, dropped-byte counters, and queue
high-water behavior. A compile-only result is not a FreeRTOS acceptance pass.

## 12. Raw TCP Acceptance

For each client session:

- verify a fresh prompt and empty private history;
- send one command byte-by-byte and in large receive chunks;
- split ANSI sequences across TCP reads;
- test partial socket writes and send timeout;
- disconnect normally, half-close, and reset the peer;
- reconnect and verify no prior line, ANSI state, or history remains;
- connect the maximum supported clients and verify isolation;
- hold one client from reading while another remains responsive;
- verify authentication/authorization before exposing privileged commands.

PASS criteria: no client receives another client's output or history, slow
clients cannot exhaust unbounded memory, and disconnect frees all per-session
resources.

## 13. Telnet Acceptance

Run this only when a Telnet adapter is present. Raw TCP alone is not a Telnet
implementation.

- Verify IAC WILL/WONT/DO/DONT bytes never enter the command line.
- Split every negotiation sequence across receive callbacks.
- Exercise subnegotiation and malformed IAC recovery.
- Confirm one visible echo, not duplicate local/server echo.
- Verify CR LF and CR NUL each execute once.
- Verify outbound `0xff` data is doubled.
- Test Up/Down/Tab with each supported Telnet client.
- Repeat secret-leak checks on captured network traffic; expect cleartext unless
  a separate encrypted tunnel is in use.

## 14. Final Evidence And Sign-Off

Attach or record:

```text
Automated CTest result:
Coverage line/branch report:
Sanitizer result, or reason not run:
Host manual checklist result:
Target UART checklist result:
FreeRTOS checklist result:
Raw TCP/Telnet result, if applicable:
Measured stack margins:
Measured RX/TX overflow counts:
Security review for sensitive commands:
Known deviations and owner:
Release decision and approver:
```

Reject the release for any cleartext secret in shell output/history, cross-client
state leak, unbounded blocking in the shell owner, silent RX loss, corrupted
partial TX, disabled/unverified secure-clear policy, or inability to recover
after malformed input.
