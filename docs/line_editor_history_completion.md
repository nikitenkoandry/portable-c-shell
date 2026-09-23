# Line Editor, History, and Completion UX Specification

> Historical UX design note. Current behavior and API are documented in
> `docs/porting.md` and verified by the tests listed in `docs/testing.md`.

## Scope

This document describes the UX and terminal behavior for the C shell parser module line editor. The line editor owns interactive input before a command is submitted to the parser:

- printable character insertion;
- cursor movement inside the current line;
- Enter submission;
- Backspace deletion;
- command history navigation;
- draft line restore while browsing history;
- Tab completion;
- terminal echo and redrawing;
- masking of sensitive arguments.

The parser remains responsible for tokenization, command lookup, validation, and execution. The line editor may call parser-provided metadata APIs to resolve completions, nested commands, argument kinds, and sensitive fields.

## Terminology

- Input buffer: editable command line bytes collected before Enter.
- Cursor: byte offset inside the input buffer, from `0` to `len`.
- Prompt: shell prompt printed before the editable line.
- Draft line: user-entered line that has not yet been submitted while the user starts browsing history.
- History entry: previously submitted non-empty command accepted for history storage.
- Completion candidate: command, subcommand, option, enum value, or argument value proposed for the current cursor context.
- Display buffer: text rendered to the terminal. It may differ from the input buffer when sensitive values are masked.

## Input Model

The line editor must process input byte-by-byte from the terminal or UART stream. It must classify bytes into:

- printable ASCII characters;
- control characters such as CR, LF, Backspace, DEL, Tab, Ctrl-C, Ctrl-D;
- ANSI escape sequences for arrows and navigation keys;
- unsupported bytes.

UTF-8 support is optional for the first implementation. If unsupported, non-ASCII bytes must be ignored or replaced by a configurable placeholder. They must not corrupt the input buffer or cursor accounting.

The editor should expose a small state machine:

- `NORMAL`: ordinary editable line input;
- `ESC_SEEN`: received `ESC` and waiting for the next byte;
- `CSI_SEEN`: received `ESC [` and waiting for the final byte or parameters;
- `HISTORY_BROWSING`: user is viewing a history entry instead of the draft line.

`HISTORY_BROWSING` may be represented as a flag over `NORMAL`; it does not need a separate parser state.

## Enter Handling

The editor must treat both `\r`, `\n`, and the sequence `\r\n` as Enter. For CRLF streams, submitting twice for a single physical Enter is forbidden.

On Enter:

1. Print a line break if echo is enabled.
2. Finalize the current input buffer exactly as bytes, not the masked display text.
3. Leave history browsing mode.
4. Clear the draft line.
5. Reset cursor to `0` and buffer length to `0`.
6. Pass the finalized command string to the shell parser.
7. After command execution, print the next prompt.

Empty or whitespace-only lines must not be submitted as executable commands unless the shell explicitly supports an empty command handler. They also must not be added to history. The default behavior is:

- Enter on empty line: print a new prompt;
- Enter on whitespace-only line: discard line, print a new prompt.

History storage rules:

- Store only successfully submitted non-empty logical command lines.
- Do not store duplicate consecutive entries.
- Store the real command line in memory only if policy allows it.
- If the line contains sensitive values, store either a redacted version or skip history storage according to the security policy described below.

## Backspace Handling

Backspace must support both ASCII Backspace (`0x08`) and DEL (`0x7f`).

Behavior:

- If cursor is at `0`, do nothing and do not redraw.
- If cursor is greater than `0`, delete the byte immediately before cursor.
- Move cursor left by one byte.
- Redraw from the cursor position to the end of the line.
- Preserve all bytes after the deleted byte.

Example:

```text
Input:  abc|def
Key:    Backspace
Result: ab|def
```

When echo is enabled and cursor is at the end of the line, a minimal terminal sequence may be used:

```text
\b \b
```

When cursor is in the middle of the line, the editor must redraw the complete editable line or redraw the suffix and reposition the cursor. The displayed line must never leave stale characters on screen.

When echo is disabled, Backspace still updates the internal input buffer and cursor, but prints nothing.

When sensitive masking is active, Backspace must delete the real byte from the input buffer while redrawing only the masked display representation.

## Left and Right Arrow Handling

Left and Right arrows are received as ANSI escape sequences, usually:

```text
ESC [ D    Left
ESC [ C    Right
```

Left behavior:

- If cursor is greater than `0`, decrement cursor by one byte.
- If echo is enabled, move the terminal cursor left one cell.
- If cursor is already `0`, do nothing.

Right behavior:

- If cursor is less than input length, increment cursor by one byte.
- If echo is enabled, move the terminal cursor right one cell.
- If cursor is already at the end, do nothing.

If future UTF-8 editing is implemented, Left/Right must move by Unicode code point or grapheme cluster, not raw byte. Until then, the module should document byte-based movement.

Left/Right must not exit history browsing mode by themselves. Editing while viewing a history entry has separate rules below.

## Up and Down History Navigation

Up and Down arrows are received as ANSI escape sequences, usually:

```text
ESC [ A    Up
ESC [ B    Down
```

History navigation is active only during interactive input before Enter.

### Entering History Browsing

When the user presses Up while editing a draft line:

1. Save the current input buffer as the draft line.
2. Save the current cursor position as the draft cursor.
3. Set history index to the most recent history entry.
4. Replace the editable line with that history entry.
5. Move cursor to the end of the loaded entry.
6. Redraw the line.

If history is empty, Up does nothing.

### Continuing Up

When already browsing history and the user presses Up:

- Move to the previous older history entry if one exists.
- Replace the current editable line.
- Move cursor to the end.
- Redraw.

If the oldest entry is already selected, keep it selected and do nothing.

### Down

When browsing history and the user presses Down:

- If a newer history entry exists, load it.
- If the current entry is the newest entry, restore the draft line.
- Restore the saved draft cursor when returning to the draft.
- Leave history browsing mode after restoring the draft.
- Redraw the line.

When not browsing history, Down does nothing.

### Editing a History Entry

If the user edits a loaded history entry by inserting printable text, deleting text, or accepting a completion:

- Copy the visible history entry into the active input buffer.
- Leave history browsing mode.
- Keep the history list unchanged.
- Do not overwrite the saved draft line unless the edit happens after the draft was restored.

This makes history recall non-destructive and avoids surprising loss of partially typed commands.

## Draft Line Restoration

The draft line is the unsent input present when the user first pressed Up.

Required behavior:

- `wifi set ss` typed by user is saved as draft.
- User presses Up and sees `status`.
- User presses Up and sees `wifi scan`.
- User presses Down and sees `status`.
- User presses Down again and sees `wifi set ss`.
- Cursor is restored to the position it had before history browsing started.

The draft line must be discarded after:

- Enter submits any command;
- Ctrl-C cancels the current line;
- a history-loaded line is edited;
- a completion is accepted while browsing history.

## Tab Completion

Tab completion is context-sensitive and must use parser metadata. It should complete only the token at the current cursor, not always the last byte of the input line.

Completion must consider:

- command names at token index `0`;
- nested subcommands after a command token;
- options or flags when the current token begins with `-`;
- enum arguments when metadata provides allowed values;
- dynamic argument providers when registered by a command;
- filesystem paths only if a command argument explicitly requests path completion.

Completion must preserve text before and after the cursor. If the cursor is in the middle of a token, only the token prefix before cursor participates in matching, and the token suffix after cursor remains unless the completion policy replaces the whole token.

### Token Context

Before completing, the line editor asks the parser for completion context:

```text
input buffer
cursor offset
parsed tokens before cursor
current token prefix
expected node: command, subcommand, option, argument, value
```

Quoted strings and escaped spaces must be handled by the parser metadata layer. The line editor must not guess complex shell syntax by string splitting.

### Single Match

If exactly one candidate matches:

- Replace or extend the current token with the candidate.
- Append a trailing space when the candidate is complete and expects another token.
- Do not append a trailing space when completing a partial path segment, quoted string that remains open, or value that expects more characters.
- Redraw the line.
- Keep cursor after the inserted completion text.

Example:

```text
Input:  wi<Tab>
Output: wifi<space>
```

### Multiple Matches With Shared Prefix

If multiple candidates match and they share a longer common prefix than the current token:

- Extend the current token to the common prefix.
- Do not print the candidate list yet on the first Tab.
- Redraw the line.

Example:

```text
Candidates: cloud, clear, close
Input:      cl<Tab>
Output:     cl
```

If the common prefix is `cl`, the line does not change. A second Tab may list matches.

### Multiple Matches Without Further Prefix

If multiple candidates match and no further unambiguous prefix exists:

- First Tab may either do nothing or emit a short bell (`BEL`, `0x07`) if enabled.
- Second consecutive Tab on the same input context prints the candidate list.
- After printing candidates, redraw prompt and current line.

Candidate list formatting:

- Sort lexicographically unless parser metadata defines command order.
- Prefer columns for short names if terminal width is known.
- Use one candidate per line if width is unknown.
- Include short descriptions only when explicitly requested by `help` or a completion detail mode; default Tab should stay compact.

### No Match

If no candidate matches:

- Do not alter the input buffer.
- Optionally emit `BEL` if enabled.
- Keep cursor position unchanged.

### Nested Commands

Nested commands must complete level by level.

Example command tree:

```text
wifi
  scan
  connect
  disconnect
  status
cloud
  set
    url
    bearer
  status
```

Expected behavior:

```text
w<Tab>              -> wifi<space>
wifi s<Tab>         -> wifi scan<space>
cloud se<Tab>       -> cloud set<space>
cloud set b<Tab>    -> cloud set bearer<space>
```

If a base command is complete and the next token is empty, Tab should list or complete available subcommands.

```text
wifi <Tab>
```

Candidates:

```text
scan
connect
disconnect
status
```

If the base command itself is executable and also has subcommands, metadata must tell the line editor whether to append space, show subcommands, or leave the line unchanged.

### Completion and Sensitive Fields

Completion must never reveal secret values. For sensitive argument positions:

- command names and sensitive argument names may be completed;
- actual secret values must not be suggested from history;
- dynamic providers must be disabled unless explicitly marked safe;
- display must use masking rules once a sensitive value token is being edited.

Examples of sensitive fields:

- password;
- pass;
- token;
- bearer;
- api_key;
- secret;
- private_key;
- psk.

Parser metadata should provide an explicit `sensitive` flag instead of relying only on name heuristics. Name heuristics are a fallback.

## ANSI Escape Sequences

The editor must recognize common terminal sequences:

```text
ESC [ A    Up
ESC [ B    Down
ESC [ C    Right
ESC [ D    Left
ESC [ H    Home, optional
ESC [ F    End, optional
ESC [ 1 ~  Home, optional
ESC [ 4 ~  End, optional
ESC [ 3 ~  Delete, optional
```

Required keys for the first implementation:

- Up;
- Down;
- Right;
- Left.

Optional keys:

- Home moves cursor to `0`;
- End moves cursor to input length;
- Delete removes byte at cursor.

Escape parser rules:

- `ESC` starts escape collection.
- If the sequence is recognized, handle it and return to normal input.
- If sequence is incomplete, wait for more bytes up to a small timeout or max length.
- If sequence is unknown, ignore the sequence and return to normal input.
- Unknown escape sequences must not inject raw bytes into the command buffer.

Recommended maximum escape sequence length: 8 bytes for the first implementation.

## Redraw Behavior

The editor should provide a single `redraw_line()` operation used after history load, completion, middle-line edit, and masking state changes.

Redraw sequence when echo is enabled:

1. Move to beginning of current terminal line.
2. Print prompt.
3. Print display buffer.
4. Clear from cursor to end of line.
5. Move cursor back from display end to desired display cursor position.

Typical ANSI sequence:

```text
\r
<prompt>
<display buffer>
ESC [ K
ESC [ <n> D
```

If ANSI output is disabled or unsupported, the fallback is to print a new line with prompt and current display buffer. This is less elegant but must remain correct.

When echo is disabled, `redraw_line()` must not print the editable content. It may print only command output and fresh prompts.

## Echo On and Off

Echo controls whether user keystrokes and redraws are printed back to the terminal.

Echo on:

- Printable characters appear as the user types.
- Backspace visibly removes characters.
- Arrow navigation visibly moves or redraws the line.
- History and completion redraw the current line.
- Sensitive tokens are displayed with masking, not raw secret text.

Echo off:

- Printable characters are accepted into the input buffer but not printed.
- Backspace, arrows, history, and completion still update internal state.
- Tab may still perform completion internally.
- Candidate lists should not be printed unless a shell policy explicitly allows completion output while echo is off.
- Enter may print a newline only if needed to keep prompt/output formatting readable.

Echo mode should be runtime configurable:

```text
echo on
echo off
echo status
```

The line editor should expose a configuration flag, while the shell command layer owns the user-facing command.

Default: echo on.

## Masking Sensitive Arguments

Sensitive arguments must be stored, parsed, and executed using their real bytes, but rendered as masked text whenever echoed.

Masking applies to:

- live typing;
- Backspace redraw;
- Left/Right movement;
- history recall display;
- completion redraw;
- command echo logs;
- debug traces unless explicitly compiled in secure diagnostic mode.

Recommended display mask:

```text
<hidden>
```

For live editing, either of these policies is acceptable:

- Fixed token mask: render the entire sensitive token as `<hidden>`.
- Character mask: render each typed byte as `*`.

The preferred policy is fixed token mask because it does not reveal secret length.

Examples:

```text
Input buffer:   wifi set pass myPassword123
Display buffer: wifi set pass <hidden>

Input buffer:   cloud set bearer eyJhbGciOi...
Display buffer: cloud set bearer <hidden>
```

Sensitive detection should use parser metadata:

```text
command: wifi set pass
argument: password
sensitive: true
```

Fallback heuristic:

- If current or previous token contains `pass`, `password`, `token`, `bearer`, `secret`, `key`, or `psk`, treat the following value token as sensitive.

History policy for sensitive lines:

- Default: do not store commands containing sensitive values.
- Alternative if required: store redacted command text, never raw secret text.
- Raw sensitive commands must not be persisted to non-volatile history.

Completion policy for sensitive lines:

- Complete command path and sensitive field name.
- Do not complete sensitive value.
- Do not list sensitive values from history.

## Parser Metadata Requirements

The parser module should expose metadata to the line editor:

```c
typedef enum {
    SHELL_COMPLETE_COMMAND,
    SHELL_COMPLETE_SUBCOMMAND,
    SHELL_COMPLETE_OPTION,
    SHELL_COMPLETE_ARGUMENT_VALUE,
} shell_completion_kind_t;

typedef struct {
    const char *text;
    const char *description;
    shell_completion_kind_t kind;
    bool append_space;
    bool sensitive;
} shell_completion_candidate_t;
```

The exact C API may differ, but it must provide:

- current parse context at cursor;
- candidate list;
- whether the current value is sensitive;
- whether selected completion should append a space;
- whether the current command path is executable;
- whether more nested subcommands are expected.

The line editor must avoid hard-coding command names except for global editor commands such as echo control if they are not parser-owned.

## Error and Edge Cases

- Buffer full: reject additional printable bytes, optionally emit `BEL`, keep line unchanged.
- Cursor movement beyond bounds: clamp and do nothing visible.
- History entry longer than buffer: skip it or truncate only with explicit policy; preferred behavior is skip and log diagnostic.
- Completion result longer than buffer: reject completion and optionally emit `BEL`.
- Unknown ANSI sequence: ignore safely.
- Interrupted escape sequence: return to normal input without modifying buffer.
- Echo mode changed while a line is active: redraw according to the new mode.
- Prompt changes while a line is active: redraw with the new prompt.
- Parser metadata unavailable: Tab does nothing or emits `BEL`.

## Acceptance Criteria

- Enter submits exactly one command for CR, LF, or CRLF.
- Backspace deletes the byte before cursor and correctly redraws middle-line edits.
- Left and Right move cursor without changing the buffer.
- Up loads older history entries and saves the current draft line.
- Down loads newer history entries and restores the original draft line at the newest boundary.
- Editing a recalled history line does not modify the stored history entry.
- Tab completes command names, nested subcommands, options, and enum values using parser metadata.
- Multiple matches are handled by common-prefix extension and candidate listing on repeated Tab.
- Unknown escape sequences are ignored and never enter the command buffer.
- Echo off prevents typed characters and redraw content from being printed.
- Sensitive values are never displayed raw, never suggested by completion, and never stored in persistent history.
