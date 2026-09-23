#include "sh_port_tcp.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *input;
    size_t input_len;
    size_t input_pos;
    size_t close_calls;
} loopback_io_t;

typedef struct {
    const char *user_name;
} loopback_auth_t;

SH_STORAGE_DEFINE(loopback_storage, 96u, 12u, 8u);

/** @brief Reads the next chunk from the in-memory loopback input. */
static sh_tcp_io_status_t loopback_read(void *ctx, uint8_t *data,
                                        size_t capacity, size_t *read_count)
{
    loopback_io_t *io = (loopback_io_t *)ctx;
    size_t amount;

    *read_count = 0u;
    if (io->input_pos == io->input_len) {
        return SH_TCP_IO_CLOSED;
    }

    amount = io->input_len - io->input_pos;
    if (amount > capacity) {
        amount = capacity;
    }
    memcpy(data, io->input + io->input_pos, amount);
    io->input_pos += amount;
    *read_count = amount;
    return SH_TCP_IO_OK;
}

/** @brief Writes a deliberately partial loopback chunk to standard output. */
static sh_tcp_io_status_t loopback_write(void *ctx, const uint8_t *data,
                                         size_t len, size_t *written)
{
    loopback_io_t *io = (loopback_io_t *)ctx;
    size_t amount = len;

    (void)io;
    if (amount > 5u) {
        amount = 5u;
    }
    *written = fwrite(data, 1u, amount, stdout);
    if (*written != amount) {
        return SH_TCP_IO_ERROR;
    }
    return SH_TCP_IO_OK;
}

/** @brief Records closure of the simulated TCP connection. */
static void loopback_close(void *ctx)
{
    loopback_io_t *io = (loopback_io_t *)ctx;
    io->close_calls++;
}

/** @brief Prints the identity attached to the current TCP session. */
static int cmd_whoami(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    const loopback_auth_t *auth = (const loopback_auth_t *)user_ctx;

    (void)argc;
    (void)argv;
    return sh_printf(shell, "authenticated user: %s\r\n", auth->user_name);
}

static const sh_cmd_t commands[] = {
    SH_CMD_ARG("whoami", "", "Show session identity", cmd_whoami, 1u, 1u)
};

/** @brief Runs a complete shell session over an in-memory TCP loopback. */
int main(void)
{
    static const uint8_t input[] = "whoami\rhistory status\r";
    loopback_io_t io;
    loopback_auth_t auth;
    sh_config_t shell_config;
    sh_tcp_session_config_t session_config;
    sh_tcp_session_t session;
    sh_tcp_poll_result_t result;

    memset(&io, 0, sizeof(io));
    io.input = input;
    io.input_len = sizeof(input) - 1u;
    auth.user_name = "loopback-admin";

    sh_default_config(&shell_config);
    shell_config.prompt = "tcp> ";
    shell_config.line_max = 96u;
    shell_config.argc_max = 12u;
    shell_config.history_depth = 8u;
    shell_config.echo_enabled = false;

    memset(&session_config, 0, sizeof(session_config));
    session_config.shell_config = &shell_config;
    session_config.storage = &loopback_storage;
    session_config.commands = commands;
    session_config.command_count = SH_ARRAY_SIZE(commands);
    session_config.read = loopback_read;
    session_config.write = loopback_write;
    session_config.close = loopback_close;
    session_config.io_ctx = &io;
    session_config.auth_user_ctx = &auth;

    if (sh_tcp_session_init(&session, &session_config) != SH_TCP_OK) {
        return 1;
    }
    if (sh_tcp_session_prompt(&session) != SH_OK) {
        return 1;
    }

    do {
        result = sh_tcp_session_poll(&session);
    } while (result.status == SH_TCP_RX_DATA);

    return result.status == SH_TCP_DISCONNECTED && io.close_calls == 1u
               ? 0
               : 1;
}
