#include "sh_port_tcp.h"

#include <string.h>

/** @brief Notify the connection owner of closure at most once. */
static void notify_close(sh_tcp_session_t *session)
{
    if (!session->close_notified) {
        session->close_notified = true;
        session->close(session->io_ctx);
    }
}

/** @brief Move an open session to a terminal state and notify its owner. */
static void set_terminal_state(sh_tcp_session_t *session,
                               sh_tcp_session_state_t state)
{
    if (session->state == SH_TCP_SESSION_OPEN) {
        session->state = state;
        notify_close(session);
    }
}

/** @brief Construct a complete TCP polling result value. */
static sh_tcp_poll_result_t make_poll_result(sh_tcp_status_t status,
                                             size_t bytes_received,
                                             int shell_status)
{
    sh_tcp_poll_result_t result;

    result.status = status;
    result.bytes_received = bytes_received;
    result.shell_status = shell_status;
    return result;
}

/** @brief Initialize one isolated raw-TCP shell session without performing I/O. */
sh_tcp_status_t sh_tcp_session_init(
    sh_tcp_session_t *session,
    const sh_tcp_session_config_t *config)
{
    sh_config_t shell_config;
    int status;

    if (session == NULL || config == NULL || config->storage == NULL ||
        config->read == NULL || config->write == NULL ||
        config->close == NULL ||
        (config->commands == NULL && config->command_count != 0u)) {
        return SH_TCP_ERR_INVALID_ARGUMENT;
    }

    memset(session, 0, sizeof(*session));
    session->storage = *config->storage;
    session->read = config->read;
    session->write = config->write;
    session->close = config->close;
    session->io_ctx = config->io_ctx;
    session->auth_user_ctx = config->auth_user_ctx;
    session->state = SH_TCP_SESSION_OPEN;
    session->last_shell_status = SH_OK;
    session->transport.write = sh_tcp_session_transport_write;
    session->transport.flush = NULL;
    session->transport.ctx = session;

    if (config->shell_config != NULL) {
        shell_config = *config->shell_config;
    } else {
        sh_default_config(&shell_config);
    }
    shell_config.write = NULL;
    shell_config.write_ctx = NULL;
    shell_config.transport = &session->transport;
    shell_config.user_ctx = config->auth_user_ctx;

    status = sh_init_static(&session->shell, &shell_config,
                            &session->storage);
    if (status != SH_OK) {
        session->last_shell_status = status;
        session->state = SH_TCP_SESSION_UNINITIALIZED;
        return SH_TCP_ERR_SHELL;
    }

    status = sh_register_commands(&session->shell,
                                  config->commands,
                                  config->command_count);
    if (status != SH_OK) {
        session->last_shell_status = status;
        session->state = SH_TCP_SESSION_UNINITIALIZED;
        return SH_TCP_ERR_SHELL;
    }

    return SH_TCP_OK;
}

/** @brief Write the configured prompt to an open TCP session. */
int sh_tcp_session_prompt(sh_tcp_session_t *session)
{
    if (session == NULL) {
        return SH_ERR_INVALID_ARG;
    }
    if (session->state != SH_TCP_SESSION_OPEN) {
        return SH_ERR_TRANSPORT;
    }
    return sh_puts(&session->shell,
                   session->shell.cfg.prompt != NULL
                       ? session->shell.cfg.prompt
                       : "sh> ");
}

/** @brief Perform one receive callback and feed confirmed bytes to the shell. */
sh_tcp_poll_result_t sh_tcp_session_poll(sh_tcp_session_t *session)
{
    sh_tcp_io_status_t io_status;
    size_t read_count = 0u;
    int shell_status = SH_OK;

    if (session == NULL || session->read == NULL) {
        return make_poll_result(SH_TCP_ERR_INVALID_ARGUMENT, 0u,
                                SH_ERR_INVALID_ARG);
    }
    if (session->state != SH_TCP_SESSION_OPEN) {
        return make_poll_result(SH_TCP_ERR_CLOSED, 0u,
                                session->last_shell_status);
    }

    io_status = session->read(session->io_ctx,
                              session->rx_buffer,
                              sizeof(session->rx_buffer),
                              &read_count);
    if (read_count > sizeof(session->rx_buffer)) {
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return make_poll_result(SH_TCP_ERR_PROTOCOL, 0u,
                                session->last_shell_status);
    }

    if (read_count != 0u) {
        shell_status = sh_input(&session->shell,
                                session->rx_buffer,
                                read_count);
        session->last_shell_status = shell_status;
        if (shell_status != SH_OK) {
            return make_poll_result(SH_TCP_ERR_SHELL, read_count,
                                    shell_status);
        }
        if (session->state != SH_TCP_SESSION_OPEN) {
            return make_poll_result(SH_TCP_ERR_IO, read_count,
                                    shell_status);
        }
    }

    switch (io_status) {
    case SH_TCP_IO_OK:
        if (read_count == 0u) {
            set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
            return make_poll_result(SH_TCP_ERR_PROTOCOL, 0u,
                                    shell_status);
        }
        return make_poll_result(SH_TCP_RX_DATA, read_count, shell_status);

    case SH_TCP_IO_WOULD_BLOCK:
        if (read_count == 0u) {
            return make_poll_result(SH_TCP_IDLE, 0u, shell_status);
        }
        return make_poll_result(SH_TCP_RX_DATA, read_count, shell_status);

    case SH_TCP_IO_CLOSED:
        set_terminal_state(session, SH_TCP_SESSION_PEER_CLOSED);
        return make_poll_result(SH_TCP_DISCONNECTED, read_count,
                                shell_status);

    case SH_TCP_IO_ERROR:
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return make_poll_result(SH_TCP_ERR_IO, read_count, shell_status);

    default:
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return make_poll_result(SH_TCP_ERR_PROTOCOL, read_count,
                                shell_status);
    }
}

/** @brief Close a TCP shell session locally using idempotent notification. */
sh_tcp_status_t sh_tcp_session_close(sh_tcp_session_t *session)
{
    if (session == NULL || session->close == NULL) {
        return SH_TCP_ERR_INVALID_ARGUMENT;
    }
    if (session->state == SH_TCP_SESSION_UNINITIALIZED) {
        return SH_TCP_ERR_CLOSED;
    }
    if (session->state == SH_TCP_SESSION_OPEN) {
        session->state = SH_TCP_SESSION_LOCAL_CLOSED;
    }
    notify_close(session);
    return SH_TCP_OK;
}

/** @brief Return the current TCP session state. */
sh_tcp_session_state_t sh_tcp_session_state(
    const sh_tcp_session_t *session)
{
    if (session == NULL) {
        return SH_TCP_SESSION_UNINITIALIZED;
    }
    return session->state;
}

/** @brief Return the shell embedded in an initialized TCP session. */
sh_t *sh_tcp_session_shell(sh_tcp_session_t *session)
{
    if (session == NULL ||
        session->state == SH_TCP_SESSION_UNINITIALIZED) {
        return NULL;
    }
    return &session->shell;
}

/** @brief Return the per-client authentication/application context. */
void *sh_tcp_session_auth_user_ctx(const sh_tcp_session_t *session)
{
    if (session == NULL) {
        return NULL;
    }
    return session->auth_user_ctx;
}

/** @brief Return the output transport embedded in a TCP session. */
const sh_transport_t *sh_tcp_session_transport(
    const sh_tcp_session_t *session)
{
    if (session == NULL ||
        session->state == SH_TCP_SESSION_UNINITIALIZED) {
        return NULL;
    }
    return &session->transport;
}

/** @brief Map one raw-TCP send attempt to shell transport semantics. */
sh_transport_status_t sh_tcp_session_transport_write(
    void *ctx,
    const uint8_t *data,
    size_t len,
    size_t *written)
{
    sh_tcp_session_t *session = (sh_tcp_session_t *)ctx;
    sh_tcp_io_status_t status;

    if (written != NULL) {
        *written = 0u;
    }
    if (session == NULL || written == NULL || session->write == NULL ||
        (data == NULL && len != 0u)) {
        return SH_TRANSPORT_ERR_INVALID_ARGUMENT;
    }
    if (session->state != SH_TCP_SESSION_OPEN) {
        return SH_TRANSPORT_ERR_IO;
    }
    if (len == 0u) {
        return SH_TRANSPORT_OK;
    }

    status = session->write(session->io_ctx, data, len, written);
    if (*written > len) {
        *written = 0u;
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return SH_TRANSPORT_ERR_PROTOCOL;
    }

    switch (status) {
    case SH_TCP_IO_OK:
        if (*written == 0u) {
            set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
            return SH_TRANSPORT_ERR_NO_PROGRESS;
        }
        return SH_TRANSPORT_OK;

    case SH_TCP_IO_WOULD_BLOCK:
        return SH_TRANSPORT_WOULD_BLOCK;

    case SH_TCP_IO_CLOSED:
        set_terminal_state(session, SH_TCP_SESSION_PEER_CLOSED);
        return SH_TRANSPORT_ERR_IO;

    case SH_TCP_IO_ERROR:
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return SH_TRANSPORT_ERR_IO;

    default:
        set_terminal_state(session, SH_TCP_SESSION_IO_ERROR);
        return SH_TRANSPORT_ERR_PROTOCOL;
    }
}
