#ifndef SH_COMMAND_H
#define SH_COMMAND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sh;

/**
 * @brief Execute one resolved leaf command.
 *
 * argv[0] is the leaf command name and payload arguments start at index 1.
 * All argument strings are borrowed from parser or caller storage and remain
 * valid only for the duration of the call. The handler runs synchronously in
 * the same execution context that called the shell.
 *
 * @param shell Shell executing the command.
 * @param argc Number of entries in @p argv, including argv[0].
 * @param argv Command argument array.
 * @param user_ctx Command-specific context from sh_cmd_t::user_ctx, falling
 *        back to the shell context when the command context is NULL.
 * @return SH_OK or an application-defined/shell status propagated to caller.
 */
typedef int (*sh_cmd_handler_t)(struct sh *shell, int argc, char **argv, void *user_ctx);

/**
 * @brief Enumerate completion candidates for one command argument.
 *
 * The shell calls the provider with increasing @p candidate_index values until
 * it returns NULL or the configured provider scan limit is reached. Returned
 * strings are borrowed and must remain valid until the callback returns.
 *
 * @param shell Shell requesting completion.
 * @param arg_index argv-style index being completed; payload starts at 1.
 * @param candidate_index Zero-based candidate number to query.
 * @param user_ctx Command or shell context, selected like handler context.
 * @return Candidate text, or NULL when no more candidates are available.
 */
typedef const char *(*sh_completion_provider_t)(struct sh *shell,
                                                size_t arg_index,
                                                size_t candidate_index,
                                                void *user_ctx);

/** Behavioral flags attached to a command descriptor. */
typedef enum {
    SH_CMD_FLAG_NONE = 0,
    /** Redact all payload arguments in echo and omit the original line from history. */
    SH_CMD_FLAG_SENSITIVE = 1u << 0,
    /** Omit the command from help and completion while keeping it executable. */
    SH_CMD_FLAG_HIDDEN = 1u << 1,
    /** Execute normally but do not store the line in command history. */
    SH_CMD_FLAG_NO_HISTORY = 1u << 2
} sh_cmd_flags_t;

/** Sentinel value for an unbounded sh_cmd_t::max_args field. */
#define SH_ARGS_ANY ((size_t)-1)

/**
 * Static command descriptor. Descriptors are borrowed by the registry, so all
 * referenced strings, arrays and contexts must outlive the registration.
 */
typedef struct sh_cmd {
    const char *name;                   /**< One token; must be unique among siblings. */
    const char *usage;                  /**< Optional usage suffix shown by help. */
    const char *help;                   /**< Optional short help text. */
    sh_cmd_handler_t handler;           /**< Leaf handler, or NULL for a pure parent. */
    const struct sh_cmd *subcommands;   /**< Child descriptor array. */
    size_t subcommand_count;            /**< Number of entries in @ref subcommands. */
    unsigned flags;                     /**< Bitwise combination of sh_cmd_flags_t. */
    size_t min_args;                    /**< Minimum argc, including argv[0]. */
    size_t max_args;                    /**< Maximum argc, including argv[0]. */
    void *user_ctx;                     /**< Optional context for handler/completion. */
    sh_completion_provider_t completion; /**< Optional payload completion provider. */
} sh_cmd_t;

/** One independently declared root command table. */
typedef struct {
    const sh_cmd_t *commands; /**< Root command descriptors. */
    size_t command_count;     /**< Number of descriptors in @ref commands. */
} sh_command_set_t;

/** Return the compile-time element count of an array. */
#define SH_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/** Build a sh_command_set_t from a statically sized command array. */
#define SH_COMMAND_SET(commands_) { (commands_), SH_ARRAY_SIZE(commands_) }

/*
 * Command initializer helpers. Arity values include argv[0], so a command
 * requiring two payload arguments uses min=3 and max=3. SH_CMD_SENSITIVE masks
 * every payload argument and suppresses the original line from history.
 */
#define SH_CMD(name_, usage_, help_, handler_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_NONE, 1u, SH_ARGS_ANY, NULL, NULL }
#define SH_CMD_ARG(name_, usage_, help_, handler_, min_, max_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_NONE, (min_), (max_), NULL, NULL }
#define SH_CMD_CTX(name_, usage_, help_, handler_, ctx_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_NONE, 1u, SH_ARGS_ANY, (ctx_), NULL }
#define SH_CMD_COMPLETE(name_, usage_, help_, handler_, completion_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_NONE, 1u, SH_ARGS_ANY, NULL, (completion_) }
#define SH_CMD_HIDDEN(name_, usage_, help_, handler_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_HIDDEN, 1u, SH_ARGS_ANY, NULL, NULL }
#define SH_CMD_NO_HISTORY(name_, usage_, help_, handler_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_NO_HISTORY, 1u, SH_ARGS_ANY, NULL, NULL }
#define SH_CMD_SENSITIVE(name_, usage_, help_, handler_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_SENSITIVE, 1u, SH_ARGS_ANY, NULL, NULL }
#define SH_CMD_SENSITIVE_ARG(name_, usage_, help_, handler_, min_, max_) \
    { (name_), (usage_), (help_), (handler_), NULL, 0u, SH_CMD_FLAG_SENSITIVE, (min_), (max_), NULL, NULL }
#define SH_CMD_SUB(name_, usage_, help_, subs_) \
    { (name_), (usage_), (help_), NULL, (subs_), SH_ARRAY_SIZE(subs_), SH_CMD_FLAG_NONE, 1u, 1u, NULL, NULL }

/* Sentinel for external table iteration; exclude it from explicit registry counts. */
#define SH_CMD_END \
    { NULL, NULL, NULL, NULL, NULL, 0u, SH_CMD_FLAG_NONE, 0u, 0u, NULL, NULL }

#ifdef __cplusplus
}
#endif

#endif
