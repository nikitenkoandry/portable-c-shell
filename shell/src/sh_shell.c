#include "sh_shell.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int sh_command_dispatch_internal(sh_t *shell, int argc, char **argv);
void sh_history_add_internal(sh_t *shell, const char *line);
int sh_complete_line_internal(sh_t *shell);
const sh_cmd_t *sh_resolve_command_internal(sh_t *shell, int argc, char **argv, int *consumed, char *path, size_t path_size);

/** @brief Write internal shell text while intentionally ignoring output status. */
void sh_write_internal(sh_t *shell, const char *text)
{
    (void)sh_puts(shell, text);
}

/** @brief Format internal shell text while intentionally ignoring output status. */
void sh_writef_internal(sh_t *shell, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)sh_vprintf(shell, fmt, ap);
    va_end(ap);
}

/** @brief Clear a memory range through volatile writes. */
static void secure_zero(void *data, size_t len)
{
    volatile unsigned char *bytes = (volatile unsigned char *)data;

    while (bytes && len != 0u) {
        *bytes++ = 0u;
        len--;
    }
}

/** @brief Write an exact byte range through the configured shell backend. */
int sh_write(sh_t *shell, const void *data, size_t len)
{
    if (!shell || (!data && len != 0u)) {
        return SH_ERR_INVALID_ARG;
    }
    if (shell->cfg.transport) {
        size_t written = 0u;
        sh_transport_status_t status = sh_transport_write_all(
            shell->cfg.transport, (const uint8_t *)data, len,
            shell->cfg.transport_write_attempts, &written);

        if (status == SH_TRANSPORT_OK && written == len) {
            return SH_OK;
        }
        if (status == SH_TRANSPORT_WOULD_BLOCK ||
            status == SH_TRANSPORT_ERR_ATTEMPTS_EXHAUSTED) {
            return SH_ERR_WOULD_BLOCK;
        }
        return SH_ERR_TRANSPORT;
    }
    if (!shell->cfg.write) {
        return SH_ERR_INVALID_CONFIG;
    }
    if (len != 0u) {
        shell->cfg.write((const char *)data, len, shell->cfg.write_ctx);
    }
    return SH_OK;
}

/** @brief Write a NUL-terminated string without adding a line ending. */
int sh_puts(sh_t *shell, const char *text)
{
    if (!text) {
        return SH_ERR_INVALID_ARG;
    }
    return sh_write(shell, text, strlen(text));
}

/** @brief Format and write text from an existing variable argument list. */
int sh_vprintf(sh_t *shell, const char *fmt, va_list ap)
{
    char buf[256];
    int count;

    if (!shell || !fmt) {
        return SH_ERR_INVALID_ARG;
    }
    count = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (count < 0) {
        return SH_ERR_INVALID_ARG;
    }
    if ((size_t)count >= sizeof(buf)) {
        (void)sh_write(shell, buf, sizeof(buf) - 1u);
        return SH_ERR_TRUNCATED;
    }
    return sh_write(shell, buf, (size_t)count);
}

/** @brief Format and write text using printf-style arguments. */
int sh_printf(sh_t *shell, const char *fmt, ...)
{
    int status;
    va_list ap;

    va_start(ap, fmt);
    status = sh_vprintf(shell, fmt, ap);
    va_end(ap);
    return status;
}

/** @brief Flush the configured status-capable transport. */
int sh_flush(sh_t *shell)
{
    sh_transport_status_t status;

    if (!shell) {
        return SH_ERR_INVALID_ARG;
    }
    if (!shell->cfg.transport) {
        return SH_OK;
    }
    status = sh_transport_flush(shell->cfg.transport);
    if (status == SH_TRANSPORT_OK) {
        return SH_OK;
    }
    if (status == SH_TRANSPORT_WOULD_BLOCK) {
        return SH_ERR_WOULD_BLOCK;
    }
    return SH_ERR_TRANSPORT;
}

/** @brief Populate a shell configuration with supported defaults. */
void sh_default_config(sh_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->prompt = "sh> ";
    cfg->line_max = SH_MAX_LINE_LEN;
    cfg->argc_max = SH_MAX_ARGC;
    cfg->history_depth = SH_HISTORY_DEFAULT_DEPTH;
    cfg->echo_enabled = true;
    cfg->ansi_enabled = true;
    cfg->builtins_enabled = SH_ENABLE_BUILTINS != 0;
    cfg->secure_clear_enabled = true;
    cfg->transport_write_attempts = 8u;
}

/** @brief Initialize a shell with explicit caller-owned storage buffers. */
int sh_init(sh_t *shell,
            const sh_config_t *cfg,
            char *line_buffer,
            size_t line_buffer_size,
            char **argv_buffer,
            size_t argv_capacity,
            char *history_buffer,
            size_t history_depth,
            size_t history_slot_size,
            char *draft_buffer,
            size_t draft_buffer_size)
{
    sh_config_t local_cfg;

    if (!shell || !line_buffer || line_buffer_size == 0u || !argv_buffer || argv_capacity == 0u) {
        return SH_ERR_INVALID_ARG;
    }

    sh_default_config(&local_cfg);
    if (cfg) {
        local_cfg = *cfg;
        if (!local_cfg.prompt) {
            local_cfg.prompt = "sh> ";
        }
        if (local_cfg.line_max == 0u) {
            local_cfg.line_max = line_buffer_size - 1u;
        }
        if (local_cfg.argc_max == 0u) {
            local_cfg.argc_max = argv_capacity;
        }
        if (local_cfg.transport_write_attempts == 0u) {
            local_cfg.transport_write_attempts = 8u;
        }
    }

    if (local_cfg.line_max == 0u || local_cfg.line_max > SH_MAX_LINE_LEN ||
        line_buffer_size <= local_cfg.line_max ||
        local_cfg.argc_max == 0u || local_cfg.argc_max > SH_MAX_ARGC ||
        local_cfg.argc_max > argv_capacity) {
        return SH_ERR_INVALID_CONFIG;
    }
    if (local_cfg.history_depth > history_depth) {
        return SH_ERR_INVALID_CONFIG;
    }
    if (local_cfg.history_depth &&
        (!history_buffer || history_slot_size <= local_cfg.line_max ||
         !draft_buffer || draft_buffer_size <= local_cfg.line_max)) {
        return SH_ERR_INVALID_CONFIG;
    }

    memset(shell, 0, sizeof(*shell));
    shell->cfg = local_cfg;
    shell->line = line_buffer;
    shell->line_capacity = local_cfg.line_max + 1u;
    shell->argv = argv_buffer;
    shell->argv_capacity = local_cfg.argc_max;
    shell->history_storage = history_buffer;
    shell->history_depth = local_cfg.history_depth;
    shell->history_slot_size = history_slot_size;
    shell->draft = draft_buffer;
    shell->echo_enabled = local_cfg.echo_enabled;
    shell->history_view = -1;
    sh_ansi_init(&shell->ansi);
    shell->line[0] = '\0';
    if (shell->draft) {
        shell->draft[0] = '\0';
    }
    return SH_OK;
}

/** @brief Initialize a shell from a compact static storage descriptor. */
int sh_init_static(sh_t *shell, const sh_config_t *cfg,
                   const sh_storage_t *storage)
{
    if (!storage) {
        return SH_ERR_INVALID_ARG;
    }
    return sh_init(shell, cfg,
                   storage->line_buffer, storage->line_buffer_size,
                   storage->argv_buffer, storage->argv_capacity,
                   storage->history_buffer, storage->history_capacity,
                   storage->history_slot_size,
                   storage->draft_buffer, storage->draft_buffer_size);
}

/** @brief Emit the configured interactive prompt using best-effort output. */
void sh_prompt(sh_t *shell)
{
    sh_write_internal(shell, shell && shell->cfg.prompt ? shell->cfg.prompt : "sh> ");
}

/** @brief Start an interactive shell session and emit its first prompt. */
int sh_start(sh_t *shell)
{
    int status;

    if (!shell) {
        return SH_ERR_INVALID_ARG;
    }
    if (shell->started) {
        return SH_ERR_BUSY;
    }
    shell->started = true;
    status = sh_puts(shell, shell->cfg.prompt ? shell->cfg.prompt : "sh> ");
    if (status != SH_OK) {
        shell->started = false;
    }
    return status;
}

/** @brief Stop a shell session, reset ANSI state, and flush output. */
int sh_stop(sh_t *shell)
{
    if (!shell) {
        return SH_ERR_INVALID_ARG;
    }
    shell->started = false;
    sh_ansi_reset(&shell->ansi);
    return sh_flush(shell);
}

/** @brief Change the runtime input-echo state. */
void sh_set_echo_enabled(sh_t *shell, bool enabled)
{
    if (shell) {
        shell->echo_enabled = enabled;
    }
}

/** @brief Report whether interactive input echo is enabled. */
bool sh_echo_is_enabled(const sh_t *shell)
{
    return shell ? shell->echo_enabled : false;
}

/** @brief Replace the editable line and place the cursor at its end. */
static void set_line(sh_t *shell, const char *line)
{
    size_t len = strlen(line);
    if (len >= shell->line_capacity) {
        len = shell->line_capacity - 1u;
    }
    memcpy(shell->line, line, len);
    shell->line[len] = '\0';
    shell->line_len = len;
    shell->cursor = len;
}

/** @brief Locate one token's byte span in the original unparsed line. */
static bool raw_token_span(const char *line, size_t token_index,
                           size_t *start_out, size_t *end_out)
{
    size_t index = 0u;
    size_t token = 0u;
    char quote = '\0';
    bool escaped = false;

    while (line[index] != '\0') {
        while (isspace((unsigned char)line[index])) {
            index++;
        }
        if (line[index] == '\0') {
            return false;
        }
        if (token == token_index) {
            *start_out = index;
        }
        while (line[index] != '\0') {
            char c = line[index];
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (isspace((unsigned char)c)) {
                break;
            }
            index++;
        }
        if (token == token_index) {
            *end_out = index;
            return true;
        }
        token++;
    }
    return false;
}

/** @brief Return the quote character left open at the end of a line. */
static char unmatched_quote(const char *line)
{
    char quote = '\0';
    bool escaped = false;

    while (*line) {
        char c = *line++;
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        }
    }
    return quote;
}

/** @brief Build an echo-safe copy of the current line with secrets masked. */
static void build_display_line(sh_t *shell, char *display, size_t display_size)
{
    char parse[SH_MAX_LINE_LEN + 2u];
    char *argv[SH_MAX_ARGC];
    char path[128];
    const sh_cmd_t *cmd;
    size_t len = shell->line_len;
    int argc = 0;
    int consumed = 0;
    int status;
    int i;

    if (len >= display_size) {
        len = display_size - 1u;
    }
    memcpy(display, shell->line, len);
    display[len] = '\0';
    memcpy(parse, shell->line, shell->line_len);
    parse[shell->line_len] = '\0';

    status = sh_tokenize(parse, argv, shell->argv_capacity, &argc);
    if (status == SH_ERR_UNTERMINATED_QUOTE && shell->line_len + 1u < sizeof(parse)) {
        char quote = unmatched_quote(shell->line);
        if (quote != '\0') {
            memcpy(parse, shell->line, shell->line_len);
            parse[shell->line_len] = quote;
            parse[shell->line_len + 1u] = '\0';
            status = sh_tokenize(parse, argv, shell->argv_capacity, &argc);
        }
    }
    if (status != SH_OK || argc == 0) {
        return;
    }

    cmd = sh_resolve_command_internal(shell, argc, argv, &consumed,
                                      path, sizeof(path));
    for (i = 0; i < argc; i++) {
        bool mask = cmd && (cmd->flags & SH_CMD_FLAG_SENSITIVE) && i >= consumed;
        size_t start;
        size_t end;
        size_t pos;

        if (shell->cfg.should_mask_arg &&
            shell->cfg.should_mask_arg(path, i, argv[i], shell->cfg.mask_ctx)) {
            mask = true;
        }
        if (!mask || !raw_token_span(shell->line, (size_t)i, &start, &end)) {
            continue;
        }
        for (pos = start; pos < end && pos < len; pos++) {
            if (display[pos] != '"' && display[pos] != '\'') {
                display[pos] = '*';
            }
        }
    }
}

/** @brief Redraw the prompt, masked line, and cursor position. */
void sh_redraw_line_internal(sh_t *shell)
{
    char display[SH_MAX_LINE_LEN + 1u];

    if (!shell || !shell->echo_enabled) {
        return;
    }
    build_display_line(shell, display, sizeof(display));
    sh_write_internal(shell, shell->cfg.ansi_enabled ? "\r\x1b[2K" : "\r\n");
    sh_prompt(shell);
    sh_write_internal(shell, display);
    if (shell->cfg.ansi_enabled && shell->cursor < shell->line_len) {
        sh_writef_internal(shell, "\x1b[%uD", (unsigned)(shell->line_len - shell->cursor));
    }
}

/** @brief Move through history while preserving and restoring the draft line. */
static void move_history(sh_t *shell, int direction)
{
    const char *entry;

    if (!shell || shell->history_count == 0u) {
        return;
    }

    if (shell->history_view < 0) {
        size_t len = shell->line_len;
        if (len >= shell->line_capacity) {
            len = shell->line_capacity - 1u;
        }
        memcpy(shell->draft, shell->line, len);
        shell->draft[len] = '\0';
    }

    if (direction < 0) {
        if (shell->history_view < (int)shell->history_count - 1) {
            shell->history_view++;
        }
    } else {
        if (shell->history_view >= 0) {
            shell->history_view--;
        }
    }

    if (shell->history_view < 0) {
        set_line(shell, shell->draft ? shell->draft : "");
    } else {
        entry = sh_history_get_newest(shell, (size_t)shell->history_view);
        set_line(shell, entry ? entry : "");
    }
    sh_redraw_line_internal(shell);
}

/** @brief Insert one printable character at the current cursor position. */
static int insert_char(sh_t *shell, char c)
{
    bool append_at_end;

    if (shell->line_len + 1u >= shell->line_capacity) {
        sh_write_internal(shell, "\r\nerror: line too long\r\n");
        sh_prompt(shell);
        return SH_ERR_LINE_TOO_LONG;
    }
    append_at_end = shell->cursor == shell->line_len;
    memmove(shell->line + shell->cursor + 1u, shell->line + shell->cursor,
            shell->line_len - shell->cursor + 1u);
    shell->line[shell->cursor] = c;
    shell->cursor++;
    shell->line_len++;
    shell->history_view = -1;
    if (shell->echo_enabled && append_at_end) {
        char display[SH_MAX_LINE_LEN + 1u];
        build_display_line(shell, display, sizeof(display));
        (void)sh_write(shell, &display[shell->cursor - 1u], 1u);
    } else {
        sh_redraw_line_internal(shell);
    }
    return SH_OK;
}

/** @brief Delete the character immediately left of the cursor. */
static void backspace(sh_t *shell)
{
    if (shell->cursor == 0u) {
        return;
    }
    memmove(shell->line + shell->cursor - 1u, shell->line + shell->cursor,
            shell->line_len - shell->cursor + 1u);
    shell->cursor--;
    shell->line_len--;
    shell->history_view = -1;
    sh_redraw_line_internal(shell);
}

/** @brief Delete the character currently under the cursor. */
static void delete_at_cursor(sh_t *shell)
{
    if (shell->cursor >= shell->line_len) {
        return;
    }
    memmove(shell->line + shell->cursor, shell->line + shell->cursor + 1u,
            shell->line_len - shell->cursor);
    shell->line_len--;
    shell->history_view = -1;
    sh_redraw_line_internal(shell);
}

/** @brief Clear the editable line and its saved history draft. */
static void clear_input_line(sh_t *shell)
{
    shell->line_len = 0u;
    shell->cursor = 0u;
    if (shell->cfg.secure_clear_enabled) {
        secure_zero(shell->line, shell->line_capacity);
    } else {
        shell->line[0] = '\0';
    }
    shell->history_view = -1;
    if (shell->draft && shell->cfg.secure_clear_enabled) {
        secure_zero(shell->draft, shell->line_capacity);
    }
    sh_redraw_line_internal(shell);
}

/** @brief Delete whitespace and the preceding word left of the cursor. */
static void delete_word_left(sh_t *shell)
{
    size_t start = shell->cursor;

    while (start > 0u && isspace((unsigned char)shell->line[start - 1u])) {
        start--;
    }
    while (start > 0u && !isspace((unsigned char)shell->line[start - 1u])) {
        start--;
    }
    if (start == shell->cursor) {
        return;
    }
    memmove(shell->line + start, shell->line + shell->cursor,
            shell->line_len - shell->cursor + 1u);
    shell->line_len -= shell->cursor - start;
    shell->cursor = start;
    shell->history_view = -1;
    sh_redraw_line_internal(shell);
}

/** @brief Cancel current input, reset decoder state, and print a new prompt. */
static void cancel_input_line(sh_t *shell)
{
    sh_write_internal(shell, "^C\r\n");
    shell->line_len = 0u;
    shell->cursor = 0u;
    if (shell->cfg.secure_clear_enabled) {
        secure_zero(shell->line, shell->line_capacity);
    } else {
        shell->line[0] = '\0';
    }
    shell->history_view = -1;
    if (shell->draft) {
        if (shell->cfg.secure_clear_enabled) {
            secure_zero(shell->draft, shell->line_capacity);
        } else {
            shell->draft[0] = '\0';
        }
    }
    sh_ansi_reset(&shell->ansi);
    sh_prompt(shell);
}

/** @brief Execute the current line and reset editor state for the next prompt. */
static int handle_enter(sh_t *shell)
{
    char exec_buf[SH_MAX_LINE_LEN + 1u];
    int status = SH_OK;

    sh_write_internal(shell, "\r\n");
    if (shell->line_len < sizeof(exec_buf)) {
        memcpy(exec_buf, shell->line, shell->line_len);
        exec_buf[shell->line_len] = '\0';
        status = sh_execute_line(shell, exec_buf);
    }
    shell->line_len = 0u;
    shell->cursor = 0u;
    if (shell->cfg.secure_clear_enabled) {
        secure_zero(shell->line, shell->line_capacity);
        secure_zero(exec_buf, sizeof(exec_buf));
    } else {
        shell->line[0] = '\0';
    }
    shell->history_view = -1;
    if (shell->draft) {
        if (shell->cfg.secure_clear_enabled) {
            secure_zero(shell->draft, shell->line_capacity);
        } else {
            shell->draft[0] = '\0';
        }
    }
    sh_prompt(shell);
    return status;
}

/** @brief Determine whether a token needs quoting in reconstructed history. */
static bool token_needs_quotes(const char *token)
{
    const unsigned char *p = (const unsigned char *)token;

    if (!token || *token == '\0') {
        return true;
    }
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\\' || *p == '\'') {
            return true;
        }
        p++;
    }
    return false;
}

/** @brief Append one character to a bounded NUL-terminated string. */
static void append_char(char *dst, size_t dst_size, char value)
{
    size_t used = strlen(dst);
    if (used + 1u < dst_size) {
        dst[used] = value;
        dst[used + 1u] = '\0';
    }
}

/** @brief Append one escaped token to a reconstructed command line. */
static void append_token(char *dst, size_t dst_size, const char *token)
{
    size_t used = strlen(dst);
    bool quoted = token_needs_quotes(token);
    const char *p;

    if (used != 0u && used + 1u < dst_size) {
        dst[used++] = ' ';
        dst[used] = '\0';
    }
    if (quoted) {
        append_char(dst, dst_size, '"');
    }
    for (p = token; p && *p; p++) {
        if (*p == '"' || *p == '\\') {
            append_char(dst, dst_size, '\\');
        }
        append_char(dst, dst_size, *p);
    }
    if (quoted) {
        append_char(dst, dst_size, '"');
    }
}

/** @brief Build the history entry while applying command masking policy. */
static bool build_history_line(sh_t *shell, const char *original,
                               int argc, char **argv, char *out, size_t out_size)
{
    int consumed = 0;
    int i;
    char path[128];
    const sh_cmd_t *cmd;
    bool must_rebuild = false;

    out[0] = '\0';
    cmd = sh_resolve_command_internal(shell, argc, argv, &consumed, path, sizeof(path));
    if (cmd && (cmd->flags & SH_CMD_FLAG_NO_HISTORY)) {
        return false;
    }

    for (i = 0; i < argc; i++) {
        if ((cmd && (cmd->flags & SH_CMD_FLAG_SENSITIVE) && i >= consumed) ||
            (shell->cfg.should_mask_arg &&
             shell->cfg.should_mask_arg(path, i, argv[i], shell->cfg.mask_ctx))) {
            must_rebuild = true;
            break;
        }
    }

    if (!must_rebuild) {
        size_t len = strlen(original);
        if (len >= out_size) {
            len = out_size - 1u;
        }
        memcpy(out, original, len);
        out[len] = '\0';
        return true;
    }

    for (i = 0; i < argc; i++) {
        bool mask = false;
        if (cmd && (cmd->flags & SH_CMD_FLAG_SENSITIVE) && i >= consumed) {
            mask = true;
        }
        if (shell->cfg.should_mask_arg && shell->cfg.should_mask_arg(path, i, argv[i], shell->cfg.mask_ctx)) {
            mask = true;
        }
        append_token(out, out_size, mask ? "<hidden>" : argv[i]);
    }
    return true;
}

/** @brief Apply one decoded ANSI navigation or editing event. */
static void handle_escape_key(sh_t *shell, sh_ansi_event_t key)
{
    switch (key) {
    case SH_ANSI_EVENT_UP: move_history(shell, -1); break;
    case SH_ANSI_EVENT_DOWN: move_history(shell, 1); break;
    case SH_ANSI_EVENT_RIGHT:
        if (shell->cursor < shell->line_len) {
            shell->cursor++;
            sh_redraw_line_internal(shell);
        }
        break;
    case SH_ANSI_EVENT_LEFT:
        if (shell->cursor > 0u) {
            shell->cursor--;
            sh_redraw_line_internal(shell);
        }
        break;
    case SH_ANSI_EVENT_HOME:
        if (shell->cursor != 0u) {
            shell->cursor = 0u;
            sh_redraw_line_internal(shell);
        }
        break;
    case SH_ANSI_EVENT_END:
        if (shell->cursor != shell->line_len) {
            shell->cursor = shell->line_len;
            sh_redraw_line_internal(shell);
        }
        break;
    case SH_ANSI_EVENT_DELETE:
        delete_at_cursor(shell);
        break;
    default:
        break;
    }
}

/** @brief Process one raw byte through ANSI decoding and line editing. */
int sh_input_byte(sh_t *shell, unsigned char byte)
{
    sh_ansi_event_t ansi_event;

    if (!shell) {
        return SH_ERR_INVALID_ARG;
    }

    if (byte == '\n' && shell->last_input_was_cr) {
        shell->last_input_was_cr = false;
        return SH_OK;
    }
    shell->last_input_was_cr = (byte == '\r');

    ansi_event = sh_ansi_feed(&shell->ansi, byte);
    if (ansi_event != SH_ANSI_EVENT_NONE) {
        handle_escape_key(shell, ansi_event);
        return SH_OK;
    }
    if (sh_ansi_last_byte_consumed(&shell->ansi)) {
        return SH_OK;
    }

    switch (byte) {
    case '\r':
    case '\n': return handle_enter(shell);
    case '\b':
    case 0x7f: backspace(shell); break;
    case '\t': return sh_complete_line_internal(shell);
    case 0x03: cancel_input_line(shell); break;
    case 0x0c:
        sh_write_internal(shell, shell->cfg.ansi_enabled
                                    ? "\x1b[2J\x1b[H" : "\r\n");
        sh_redraw_line_internal(shell);
        break;
    case 0x15: clear_input_line(shell); break;
    case 0x17: delete_word_left(shell); break;
    default:
        if (byte >= 32u && byte < 127u) {
            return insert_char(shell, (char)byte);
        }
        break;
    }
    return SH_OK;
}

/** @brief Process a complete input block and preserve its first error status. */
int sh_input(sh_t *shell, const unsigned char *data, size_t len)
{
    size_t i;
    int result = SH_OK;

    if (!shell || (!data && len != 0u)) {
        return SH_ERR_INVALID_ARG;
    }
    for (i = 0u; i < len; i++) {
        int status = sh_input_byte(shell, data[i]);
        if (result == SH_OK && status != SH_OK) {
            result = status;
        }
    }
    return result;
}

/** @brief Forward one byte to the interactive input processor. */
int sh_feed_byte(sh_t *shell, unsigned char byte)
{
    return sh_input_byte(shell, byte);
}

/** @brief Forward a byte block to the interactive input processor. */
int sh_feed(sh_t *shell, const unsigned char *data, size_t len)
{
    return sh_input(shell, data, len);
}

/** @brief Dispatch an already-tokenized argument vector. */
int sh_execute_argv(sh_t *shell, size_t argc, char **argv)
{
    if (!shell || (argc != 0u && !argv) || argc > shell->argv_capacity ||
        argc > (size_t)INT_MAX) {
        return SH_ERR_INVALID_ARG;
    }
    return sh_command_dispatch_internal(shell, (int)argc, argv);
}

/** @brief Tokenize, record, and execute one complete command line. */
int sh_execute_line(sh_t *shell, const char *line)
{
    char parse_buf[SH_MAX_LINE_LEN + 1u];
    char history_line[SH_MAX_LINE_LEN + 1u];
    size_t len;
    int argc = 0;
    int status;

    if (!shell || !line) {
        return SH_ERR_INVALID_ARG;
    }
    history_line[0] = '\0';
    len = strlen(line);
    if (len >= sizeof(parse_buf) || len >= shell->line_capacity) {
        sh_writef_internal(shell, "error: line too long, max=%u\r\n", (unsigned)(shell->line_capacity - 1u));
        return SH_ERR_LINE_TOO_LONG;
    }
    memcpy(parse_buf, line, len + 1u);
    status = sh_tokenize(parse_buf, shell->argv, shell->argv_capacity, &argc);
    if (status != SH_OK) {
        sh_writef_internal(shell, "error: %s\r\n", sh_status_string(status));
        goto cleanup;
    }
    if (argc == 0) {
        status = SH_OK;
        goto cleanup;
    }
    if (build_history_line(shell, line, argc, shell->argv,
                           history_line, sizeof(history_line))) {
        sh_history_add_internal(shell, history_line);
    }
    status = sh_command_dispatch_internal(shell, argc, shell->argv);

cleanup:
    if (shell->cfg.secure_clear_enabled) {
        secure_zero(parse_buf, sizeof(parse_buf));
        secure_zero(history_line, sizeof(history_line));
        secure_zero(shell->argv,
                    shell->argv_capacity * sizeof(shell->argv[0]));
    }
    return status;
}
