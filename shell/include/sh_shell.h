#ifndef SH_SHELL_H
#define SH_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include "sh_ansi.h"
#include "sh_command.h"
#include "sh_config.h"
#include "sh_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Status values returned by the shell core and command handlers. */
typedef enum {
    SH_OK = 0,
    SH_ERR_INVALID_ARG = -1,
    SH_ERR_LINE_TOO_LONG = -2,
    SH_ERR_TOO_MANY_ARGS = -3,
    SH_ERR_UNTERMINATED_QUOTE = -4,
    SH_ERR_UNKNOWN_COMMAND = -5,
    SH_ERR_AMBIGUOUS = -6,
    SH_ERR_NO_MEMORY = -7,
    SH_ERR_INVALID_CONFIG = -8,
    SH_ERR_TRANSPORT = -9,
    SH_ERR_WOULD_BLOCK = -10,
    SH_ERR_TRUNCATED = -11,
    SH_ERR_BUSY = -12,
    SH_ERR_INCOMPLETE_COMMAND = -13
} sh_status_t;

/**
 * @brief Legacy best-effort output callback.
 *
 * The callback must consume the complete buffer before returning. New ports
 * should normally use @ref sh_transport_t so partial writes and failures can
 * be reported.
 *
 * @param data Bytes to write. The buffer is valid only for this call.
 * @param len Number of bytes in @p data.
 * @param ctx Application-owned context from sh_config_t::write_ctx.
 */
typedef void (*sh_output_fn_t)(const char *data, size_t len, void *ctx);

/**
 * @brief Decide whether one command argument must be hidden while echoing.
 *
 * @param cmd_path Space-separated path of the resolved command.
 * @param arg_index Zero-based payload argument index; argv[0] is not counted.
 * @param arg Argument text. It is valid only for this call.
 * @param ctx Application-owned context from sh_config_t::mask_ctx.
 * @return true to replace the argument with the configured mask, otherwise
 *         false.
 */
typedef bool (*sh_mask_arg_fn_t)(const char *cmd_path, int arg_index,
                                 const char *arg, void *ctx);

/** Runtime configuration copied into a shell instance by sh_init(). */
typedef struct {
    const char *prompt;
    size_t line_max;
    size_t argc_max;
    size_t history_depth;
    bool echo_enabled;
    bool ansi_enabled;
    bool builtins_enabled;
    bool secure_clear_enabled;
    sh_output_fn_t write;
    void *write_ctx;
    const sh_transport_t *transport;
    size_t transport_write_attempts;
    void *user_ctx;
    sh_mask_arg_fn_t should_mask_arg;
    void *mask_ctx;
} sh_config_t;

/**
 * Caller-owned storage used by sh_init_static(). All buffers must remain
 * valid and exclusively owned by the shell for its complete lifetime.
 */
typedef struct {
    char *line_buffer;
    size_t line_buffer_size;
    char **argv_buffer;
    size_t argv_capacity;
    char *history_buffer;
    size_t history_capacity;
    size_t history_slot_size;
    char *draft_buffer;
    size_t draft_buffer_size;
} sh_storage_t;

/**
 * @brief Define static storage suitable for sh_init_static().
 *
 * @param name_ Name of the generated sh_storage_t object.
 * @param line_max_ Maximum editable line length, excluding the terminating NUL.
 * @param argc_max_ Maximum number of tokens produced by the parser.
 * @param history_depth_ Number of history entries; zero disables history.
 */
#define SH_STORAGE_DEFINE(name_, line_max_, argc_max_, history_depth_)          \
    static char name_##_line[(line_max_) + 1u];                                \
    static char *name_##_argv[(argc_max_)];                                    \
    static char name_##_history[((history_depth_) ? (history_depth_) : 1u) *   \
                                ((line_max_) + 1u)];                            \
    static char name_##_draft[(line_max_) + 1u];                               \
    static const sh_storage_t name_ = {                                        \
        name_##_line, sizeof(name_##_line),                                    \
        name_##_argv, SH_ARRAY_SIZE(name_##_argv),                             \
        (history_depth_) ? name_##_history : NULL, (history_depth_),            \
        (line_max_) + 1u,                                                      \
        (history_depth_) ? name_##_draft : NULL, sizeof(name_##_draft)          \
    }

/**
 * Mutable shell instance.
 *
 * A single execution context must own an instance at a time. Ports may queue
 * bytes from interrupts or other tasks, but parsing, editing, completion and
 * command execution must be serialized before calling the core API.
 */
typedef struct sh {
    sh_config_t cfg;
    const sh_cmd_t *commands;
    size_t command_count;
    const sh_command_set_t *command_sets;
    size_t command_set_count;

    char *line;
    size_t line_capacity;
    size_t line_len;
    size_t cursor;

    char **argv;
    size_t argv_capacity;

    char *history_storage;
    size_t history_depth;
    size_t history_slot_size;
    size_t history_count;
    size_t history_start;
    int history_view;
    char *draft;

    bool echo_enabled;
    bool started;
    sh_ansi_decoder_t ansi;
    bool last_input_was_cr;
} sh_t;

/**
 * @brief Fill a configuration object with the library defaults.
 *
 * The caller may override individual fields before initialization. Passing
 * NULL is allowed and has no effect.
 *
 * @param[out] cfg Configuration object to initialize.
 */
void sh_default_config(sh_config_t *cfg);

/**
 * @brief Initialize a shell with explicitly supplied static storage.
 *
 * The shell does not allocate memory and does not copy the supplied buffers.
 * Every buffer, callback context, transport and command table registered later
 * must remain valid while the shell is in use. When @p cfg is NULL, default
 * configuration is used.
 *
 * @param[out] shell Shell instance to initialize.
 * @param[in] cfg Configuration to copy, or NULL for defaults.
 * @param line_buffer Editable line buffer of at least line_max + 1 bytes.
 * @param line_buffer_size Size of @p line_buffer in bytes.
 * @param argv_buffer Parser pointer array.
 * @param argv_capacity Number of entries in @p argv_buffer.
 * @param history_buffer Contiguous history storage, or NULL when history is
 *        disabled.
 * @param history_depth Number of slots physically available in history storage.
 * @param history_slot_size Size of each history slot in bytes.
 * @param draft_buffer Buffer used to restore an unfinished line after history
 *        navigation, or NULL when history is disabled.
 * @param draft_buffer_size Size of @p draft_buffer in bytes.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_INVALID_CONFIG.
 */
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
            size_t draft_buffer_size);

/**
 * @brief Initialize a shell from a sh_storage_t descriptor.
 *
 * This is the compact form of sh_init() intended for storage created with
 * SH_STORAGE_DEFINE().
 *
 * @param[out] shell Shell instance to initialize.
 * @param[in] cfg Configuration to copy, or NULL for defaults.
 * @param[in] storage Caller-owned storage descriptor and buffers.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_INVALID_CONFIG.
 */
int sh_init_static(sh_t *shell, const sh_config_t *cfg,
                   const sh_storage_t *storage);

/**
 * @brief Replace the active registry with one flat command table.
 *
 * The table and all referenced strings, child arrays and contexts are borrowed
 * and must remain valid until another registry is installed or the shell is no
 * longer used. The complete tree is validated before it becomes active.
 *
 * @param shell Initialized shell instance.
 * @param commands Root command array, or NULL only when @p count is zero.
 * @param count Number of root commands.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_INVALID_CONFIG.
 */
int sh_register_commands(sh_t *shell, const sh_cmd_t *commands, size_t count);

/**
 * @brief Replace the active registry with multiple command sets.
 *
 * Command names must remain unique across all sets. Descriptors and referenced
 * storage are borrowed for the lifetime of the registration.
 *
 * @param shell Initialized shell instance.
 * @param sets Command-set array, or NULL only when @p set_count is zero.
 * @param set_count Number of command sets.
 * @return SH_OK, SH_ERR_INVALID_ARG, or SH_ERR_INVALID_CONFIG.
 */
int sh_register_command_sets(sh_t *shell, const sh_command_set_t *sets,
                             size_t set_count);

/**
 * @brief Start an interactive session and emit the initial prompt.
 * @param shell Initialized shell instance.
 * @return SH_OK, SH_ERR_BUSY if already started, or an output error.
 */
int sh_start(sh_t *shell);

/**
 * @brief Stop an interactive session, reset input state and flush output.
 * @param shell Initialized shell instance.
 * @return SH_OK or a transport flush error.
 */
int sh_stop(sh_t *shell);

/**
 * @brief Emit the configured prompt using best-effort compatibility semantics.
 *
 * Use sh_start() or sh_write() when the caller needs an output status.
 *
 * @param shell Initialized shell instance. NULL is ignored.
 */
void sh_prompt(sh_t *shell);

/**
 * @brief Feed one raw input byte into the interactive line editor.
 *
 * The byte may complete an ANSI key sequence, edit the current line, invoke
 * completion, or execute a line on CR/LF. Command handlers run synchronously
 * in the caller's context.
 *
 * @param shell Started shell instance.
 * @param byte Raw byte received from the input transport.
 * @return SH_OK or a parser, command, completion, configuration, or transport
 *         status.
 */
int sh_input_byte(sh_t *shell, unsigned char byte);

/**
 * @brief Feed a byte block into the interactive line editor.
 *
 * Every byte is processed. If multiple calls fail, the first non-SH_OK status
 * is returned after the complete block has been consumed.
 *
 * @param shell Started shell instance.
 * @param data Input byte block; may be NULL only when @p len is zero.
 * @param len Number of bytes to process.
 * @return SH_OK or the first non-SH_OK status produced while processing.
 */
int sh_input(sh_t *shell, const unsigned char *data, size_t len);

/** @brief Status-preserving alias of sh_input_byte(). */
int sh_feed_byte(sh_t *shell, unsigned char byte);

/** @brief Status-preserving alias of sh_input(). */
int sh_feed(sh_t *shell, const unsigned char *data, size_t len);

/**
 * @brief Parse and execute one NUL-terminated command line.
 *
 * The caller's string is not modified. The function uses the shell's internal
 * line and argv storage, applies history policy, resolves the command tree and
 * calls the handler synchronously.
 *
 * @param shell Initialized shell instance.
 * @param line NUL-terminated command line.
 * @return SH_OK, a tokenization/lookup error, or the handler's return value.
 */
int sh_execute_line(sh_t *shell, const char *line);

/**
 * @brief Resolve and execute a command from an existing argv array.
 *
 * This entry point does not tokenize input and does not add an entry to
 * history. Argument strings must remain valid until the handler returns.
 *
 * @param shell Initialized shell instance.
 * @param argc Number of entries in @p argv; must be greater than zero.
 * @param argv Mutable argument pointer array with the root command at index 0.
 * @return SH_OK, a lookup/arity error, or the handler's return value.
 */
int sh_execute_argv(sh_t *shell, size_t argc, char **argv);

/**
 * @brief Write an exact byte block through the configured output backend.
 *
 * A status-capable transport is preferred over the legacy write callback when
 * both are configured.
 *
 * @param shell Initialized shell instance.
 * @param data Byte block; may be NULL only when @p len is zero.
 * @param len Number of bytes to write.
 * @return SH_OK, SH_ERR_INVALID_ARG, SH_ERR_TRANSPORT, or SH_ERR_WOULD_BLOCK.
 */
int sh_write(sh_t *shell, const void *data, size_t len);

/**
 * @brief Write a NUL-terminated string without appending a newline.
 * @param shell Initialized shell instance.
 * @param text String to write.
 * @return Status from sh_write().
 */
int sh_puts(sh_t *shell, const char *text);

/**
 * @brief Format and write text using a va_list.
 *
 * Formatting uses a fixed 256-byte local buffer. A truncated prefix is written
 * and SH_ERR_TRUNCATED is returned when the formatted result does not fit.
 *
 * @param shell Initialized shell instance.
 * @param fmt printf-compatible format string.
 * @param ap Variable argument list.
 * @return SH_OK, SH_ERR_TRUNCATED, SH_ERR_INVALID_ARG, or an output error.
 */
int sh_vprintf(sh_t *shell, const char *fmt, va_list ap);

/**
 * @brief Format and write text using printf-compatible arguments.
 * @return Status from sh_vprintf().
 */
int sh_printf(sh_t *shell, const char *fmt, ...);

/**
 * @brief Flush pending output when the configured transport supports it.
 * @param shell Initialized shell instance.
 * @return SH_OK when no flush callback is required, otherwise the mapped
 *         transport status.
 */
int sh_flush(sh_t *shell);

/**
 * @brief Enable or disable interactive input echo at runtime.
 * @param shell Initialized shell instance. NULL is ignored.
 * @param enabled New echo state.
 */
void sh_set_echo_enabled(sh_t *shell, bool enabled);

/**
 * @brief Read the current interactive echo state.
 * @param shell Shell instance.
 * @return true when echo is enabled; false for NULL or disabled instances.
 */
bool sh_echo_is_enabled(const sh_t *shell);

/**
 * @brief Return the configured number of history slots.
 * @return History capacity, or zero for NULL/disabled instances.
 */
size_t sh_history_capacity(const sh_t *shell);

/**
 * @brief Return the number of currently populated history entries.
 * @return Populated entry count, or zero for a NULL instance.
 */
size_t sh_history_count(const sh_t *shell);

/**
 * @brief Remove every stored history entry and securely clear its storage.
 * @param shell Shell instance. NULL is ignored.
 */
void sh_history_clear(sh_t *shell);

/**
 * @brief Read a history entry by reverse chronological index.
 *
 * Index zero selects the newest entry. The returned pointer is owned by the
 * shell and remains valid only until that slot is overwritten, history is
 * cleared, or the shell is reinitialized.
 *
 * @param shell Shell instance.
 * @param newest_index Zero-based index relative to the newest entry.
 * @return NUL-terminated entry, or NULL when the index is unavailable.
 */
const char *sh_history_get_newest(const sh_t *shell, size_t newest_index);

/**
 * @brief Tokenize a command line in place.
 *
 * Whitespace separates tokens; single and double quotes group text; backslash
 * escapes the following character except inside single quotes. Token text is
 * compacted into @p line and @p argv points into that same buffer.
 *
 * @param[in,out] line Writable NUL-terminated command line.
 * @param[out] argv Array receiving token pointers.
 * @param argv_capacity Number of entries available in @p argv.
 * @param[out] argc_out Number of produced tokens; set to zero before parsing.
 * @return SH_OK, SH_ERR_INVALID_ARG, SH_ERR_TOO_MANY_ARGS, or
 *         SH_ERR_UNTERMINATED_QUOTE.
 */
int sh_tokenize(char *line, char **argv, size_t argv_capacity, int *argc_out);

/**
 * @brief Convert a shell status code to a stable diagnostic string.
 * @param status Shell status value.
 * @return Pointer to a static string literal; never NULL.
 */
const char *sh_status_string(int status);

#ifdef __cplusplus
}
#endif

#endif
