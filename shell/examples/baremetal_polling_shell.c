#include "sh_port_baremetal.h"

#include <string.h>

typedef struct {
    sh_t shell;
    sh_port_baremetal_t port;
    char line[SH_MAX_LINE_LEN + 1u];
    char *argv[SH_MAX_ARGC];
    char history[SH_HISTORY_DEFAULT_DEPTH][SH_MAX_LINE_LEN + 1u];
    char draft[SH_MAX_LINE_LEN + 1u];
} bm_shell_t;

/** @brief Initializes a statically allocated polling shell instance. */
int bm_shell_init(bm_shell_t *instance,
                  const sh_cmd_t *commands,
                  size_t command_count,
                  sh_port_baremetal_read_fn_t read,
                  void *read_ctx,
                  sh_port_baremetal_write_fn_t write,
                  void *write_ctx)
{
    sh_config_t config;
    int status;

    if (instance == NULL || read == NULL || write == NULL ||
        (commands == NULL && command_count != 0u)) {
        return SH_ERR_INVALID_ARG;
    }

    memset(instance, 0, sizeof(*instance));
    if (sh_port_baremetal_init(&instance->port, &instance->shell,
                               read, read_ctx, write, write_ctx) !=
        SH_PORT_BAREMETAL_OK) {
        return SH_ERR_INVALID_CONFIG;
    }

    sh_default_config(&config);
    config.prompt = "bm> ";
    config.transport = sh_port_baremetal_transport(&instance->port);
    status = sh_init(&instance->shell, &config,
                     instance->line, sizeof(instance->line),
                     instance->argv, SH_ARRAY_SIZE(instance->argv),
                     &instance->history[0][0],
                     SH_ARRAY_SIZE(instance->history),
                     sizeof(instance->history[0]),
                     instance->draft, sizeof(instance->draft));
    if (status != SH_OK) {
        return status;
    }
    return sh_register_commands(&instance->shell, commands, command_count);
}

/** @brief Starts the bare-metal shell and emits its prompt. */
int bm_shell_start(bm_shell_t *instance)
{
    return instance != NULL ? sh_start(&instance->shell) : SH_ERR_INVALID_ARG;
}

/** @brief Processes up to the requested number of transport input bytes. */
sh_port_baremetal_poll_result_t bm_shell_poll(bm_shell_t *instance,
                                              size_t byte_budget)
{
    sh_port_baremetal_poll_result_t result;

    if (instance == NULL) {
        result.status = SH_PORT_BAREMETAL_ERR_INVALID_ARGUMENT;
        result.bytes_processed = 0u;
        return result;
    }
    return sh_port_baremetal_poll(&instance->port, byte_budget);
}
