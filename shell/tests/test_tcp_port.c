#include "sh_port_tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "assert failed: %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                               \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define ASSERT_INT(expected, actual)                                          \
    ASSERT_TRUE((int)(expected) == (int)(actual))
#define ASSERT_SIZE(expected, actual)                                         \
    ASSERT_TRUE((size_t)(expected) == (size_t)(actual))

typedef struct {
    const uint8_t *rx;
    size_t rx_len;
    size_t rx_pos;
    size_t read_chunk;
    sh_tcp_io_status_t rx_empty_status;
    size_t read_calls;
    uint8_t tx[4096];
    size_t tx_len;
    size_t write_chunk;
    sh_tcp_io_status_t write_status;
    size_t write_calls;
    size_t close_calls;
} fake_io_t;

typedef struct {
    const char *name;
    size_t calls;
    char last_value[32];
} auth_ctx_t;

SH_STORAGE_DEFINE(storage_a, 96u, 12u, 6u);
SH_STORAGE_DEFINE(storage_b, 96u, 12u, 6u);
SH_STORAGE_DEFINE(storage_c, 96u, 12u, 6u);
SH_STORAGE_DEFINE(storage_d, 96u, 12u, 6u);

static sh_tcp_io_status_t fake_read(void *ctx, uint8_t *data,
                                    size_t capacity, size_t *read_count)
{
    fake_io_t *io = (fake_io_t *)ctx;
    size_t amount;

    io->read_calls++;
    *read_count = 0u;
    if (io->rx_pos == io->rx_len) {
        return io->rx_empty_status;
    }

    amount = io->rx_len - io->rx_pos;
    if (amount > capacity) {
        amount = capacity;
    }
    if (io->read_chunk != 0u && amount > io->read_chunk) {
        amount = io->read_chunk;
    }
    memcpy(data, io->rx + io->rx_pos, amount);
    io->rx_pos += amount;
    *read_count = amount;
    return SH_TCP_IO_OK;
}

static sh_tcp_io_status_t fake_write(void *ctx, const uint8_t *data,
                                     size_t len, size_t *written)
{
    fake_io_t *io = (fake_io_t *)ctx;
    size_t amount = len;

    io->write_calls++;
    *written = 0u;
    if (io->write_status == SH_TCP_IO_CLOSED ||
        io->write_status == SH_TCP_IO_ERROR) {
        return io->write_status;
    }
    if (io->write_status == SH_TCP_IO_WOULD_BLOCK &&
        io->write_chunk == 0u) {
        return SH_TCP_IO_WOULD_BLOCK;
    }
    if (io->write_chunk != 0u && amount > io->write_chunk) {
        amount = io->write_chunk;
    }
    if (amount > sizeof(io->tx) - io->tx_len) {
        amount = sizeof(io->tx) - io->tx_len;
    }
    memcpy(io->tx + io->tx_len, data, amount);
    io->tx_len += amount;
    *written = amount;
    return io->write_status;
}

static void fake_close(void *ctx)
{
    fake_io_t *io = (fake_io_t *)ctx;
    io->close_calls++;
}

static int cmd_record(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    auth_ctx_t *auth = (auth_ctx_t *)user_ctx;

    ASSERT_TRUE(auth != NULL);
    auth->calls++;
    auth->last_value[0] = '\0';
    if (argc > 1) {
        size_t len = strlen(argv[1]);
        if (len >= sizeof(auth->last_value)) {
            len = sizeof(auth->last_value) - 1u;
        }
        memcpy(auth->last_value, argv[1], len);
        auth->last_value[len] = '\0';
    }
    return sh_printf(shell, "%s:%s\r\n", auth->name, auth->last_value);
}

static const sh_cmd_t commands[] = {
    SH_CMD_ARG("record", "<value>", "Record a value", cmd_record, 2u, 2u)
};

static void init_session(sh_tcp_session_t *session,
                         fake_io_t *io,
                         auth_ctx_t *auth,
                         const sh_storage_t *storage,
                         size_t write_attempts)
{
    sh_config_t shell_config;
    sh_tcp_session_config_t config;

    memset(session, 0, sizeof(*session));
    sh_default_config(&shell_config);
    shell_config.prompt = "tcp> ";
    shell_config.line_max = 96u;
    shell_config.argc_max = 12u;
    shell_config.history_depth = 6u;
    shell_config.echo_enabled = false;
    shell_config.transport_write_attempts = write_attempts;

    memset(&config, 0, sizeof(config));
    config.shell_config = &shell_config;
    config.storage = storage;
    config.commands = commands;
    config.command_count = SH_ARRAY_SIZE(commands);
    config.read = fake_read;
    config.write = fake_write;
    config.close = fake_close;
    config.io_ctx = io;
    config.auth_user_ctx = auth;

    ASSERT_INT(SH_TCP_OK, sh_tcp_session_init(session, &config));
    ASSERT_TRUE(sh_tcp_session_shell(session) == &session->shell);
    ASSERT_TRUE(sh_tcp_session_transport(session) == &session->transport);
    ASSERT_TRUE(sh_tcp_session_auth_user_ctx(session) == auth);
}

static void test_independent_sessions_and_multiple_commands(void)
{
    static const uint8_t input_a[] = "record one\rrecord two\r";
    static const uint8_t input_b[] = "record beta\r";
    fake_io_t io_a;
    fake_io_t io_b;
    auth_ctx_t auth_a = { "alice", 0u, { 0 } };
    auth_ctx_t auth_b = { "bob", 0u, { 0 } };
    sh_tcp_session_t session_a;
    sh_tcp_session_t session_b;
    sh_tcp_poll_result_t result;

    memset(&io_a, 0, sizeof(io_a));
    memset(&io_b, 0, sizeof(io_b));
    io_a.rx = input_a;
    io_a.rx_len = sizeof(input_a) - 1u;
    io_a.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io_a.write_status = SH_TCP_IO_OK;
    io_b.rx = input_b;
    io_b.rx_len = sizeof(input_b) - 1u;
    io_b.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io_b.write_status = SH_TCP_IO_OK;

    init_session(&session_a, &io_a, &auth_a, &storage_a, 8u);
    init_session(&session_b, &io_b, &auth_b, &storage_b, 8u);

    result = sh_tcp_session_poll(&session_a);
    ASSERT_INT(SH_TCP_RX_DATA, result.status);
    ASSERT_SIZE(sizeof(input_a) - 1u, result.bytes_received);
    ASSERT_SIZE(2u, auth_a.calls);
    ASSERT_TRUE(strcmp(auth_a.last_value, "two") == 0);
    ASSERT_SIZE(2u, sh_history_count(&session_a.shell));
    ASSERT_SIZE(0u, auth_b.calls);
    ASSERT_SIZE(0u, sh_history_count(&session_b.shell));

    result = sh_tcp_session_poll(&session_b);
    ASSERT_INT(SH_TCP_RX_DATA, result.status);
    ASSERT_SIZE(1u, auth_b.calls);
    ASSERT_TRUE(strcmp(auth_b.last_value, "beta") == 0);
    ASSERT_SIZE(1u, sh_history_count(&session_b.shell));
    ASSERT_TRUE(strstr((const char *)io_a.tx, "alice:one") != NULL);
    ASSERT_TRUE(strstr((const char *)io_b.tx, "bob:beta") != NULL);
}

static void test_fragmented_rx_and_idle(void)
{
    static const uint8_t input[] = "record fragmented\r";
    fake_io_t io;
    auth_ctx_t auth = { "frag", 0u, { 0 } };
    sh_tcp_session_t session;
    sh_tcp_poll_result_t result;
    size_t polls = 0u;

    memset(&io, 0, sizeof(io));
    io.rx = input;
    io.rx_len = sizeof(input) - 1u;
    io.read_chunk = 2u;
    io.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io.write_status = SH_TCP_IO_OK;
    init_session(&session, &io, &auth, &storage_c, 8u);

    while (io.rx_pos < io.rx_len) {
        result = sh_tcp_session_poll(&session);
        ASSERT_INT(SH_TCP_RX_DATA, result.status);
        ASSERT_TRUE(result.bytes_received <= 2u);
        polls++;
    }
    ASSERT_TRUE(polls > 1u);
    ASSERT_SIZE(1u, auth.calls);
    ASSERT_TRUE(strcmp(auth.last_value, "fragmented") == 0);

    result = sh_tcp_session_poll(&session);
    ASSERT_INT(SH_TCP_IDLE, result.status);
    ASSERT_SIZE(0u, result.bytes_received);
}

static void test_partial_and_backpressure_output(void)
{
    fake_io_t io;
    auth_ctx_t auth = { "tx", 0u, { 0 } };
    sh_tcp_session_t session;

    memset(&io, 0, sizeof(io));
    io.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io.write_status = SH_TCP_IO_OK;
    io.write_chunk = 2u;
    init_session(&session, &io, &auth, &storage_d, 8u);

    ASSERT_INT(SH_OK, sh_puts(&session.shell, "abcdef"));
    ASSERT_SIZE(6u, io.tx_len);
    ASSERT_SIZE(3u, io.write_calls);
    ASSERT_TRUE(memcmp(io.tx, "abcdef", 6u) == 0);

    io.write_status = SH_TCP_IO_WOULD_BLOCK;
    io.write_chunk = 2u;
    session.shell.cfg.transport_write_attempts = 3u;
    ASSERT_INT(SH_OK, sh_puts(&session.shell, "ghijkl"));
    ASSERT_SIZE(12u, io.tx_len);
    ASSERT_SIZE(6u, io.write_calls);
    ASSERT_TRUE(memcmp(io.tx + 6u, "ghijkl", 6u) == 0);

    io.write_chunk = 0u;
    session.shell.cfg.transport_write_attempts = 2u;
    ASSERT_INT(SH_ERR_WOULD_BLOCK, sh_puts(&session.shell, "blocked"));
    ASSERT_INT(SH_TCP_SESSION_OPEN, sh_tcp_session_state(&session));
    ASSERT_SIZE(0u, io.close_calls);
}

static void test_disconnect_and_error_close_once(void)
{
    fake_io_t io;
    auth_ctx_t auth = { "close", 0u, { 0 } };
    sh_tcp_session_t session;
    sh_tcp_poll_result_t result;
    size_t read_calls;

    memset(&io, 0, sizeof(io));
    io.rx_empty_status = SH_TCP_IO_CLOSED;
    io.write_status = SH_TCP_IO_OK;
    init_session(&session, &io, &auth, &storage_c, 8u);

    result = sh_tcp_session_poll(&session);
    ASSERT_INT(SH_TCP_DISCONNECTED, result.status);
    ASSERT_INT(SH_TCP_SESSION_PEER_CLOSED,
               sh_tcp_session_state(&session));
    ASSERT_SIZE(1u, io.close_calls);
    read_calls = io.read_calls;

    result = sh_tcp_session_poll(&session);
    ASSERT_INT(SH_TCP_ERR_CLOSED, result.status);
    ASSERT_SIZE(read_calls, io.read_calls);
    ASSERT_INT(SH_TCP_OK, sh_tcp_session_close(&session));
    ASSERT_SIZE(1u, io.close_calls);

    memset(&io, 0, sizeof(io));
    io.rx_empty_status = SH_TCP_IO_ERROR;
    io.write_status = SH_TCP_IO_OK;
    init_session(&session, &io, &auth, &storage_c, 8u);
    result = sh_tcp_session_poll(&session);
    ASSERT_INT(SH_TCP_ERR_IO, result.status);
    ASSERT_INT(SH_TCP_SESSION_IO_ERROR,
               sh_tcp_session_state(&session));
    ASSERT_SIZE(1u, io.close_calls);
}

static void test_write_disconnect_and_local_close(void)
{
    fake_io_t io;
    auth_ctx_t auth = { "write", 0u, { 0 } };
    sh_tcp_session_t session;

    memset(&io, 0, sizeof(io));
    io.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io.write_status = SH_TCP_IO_CLOSED;
    init_session(&session, &io, &auth, &storage_d, 8u);

    ASSERT_INT(SH_ERR_TRANSPORT, sh_puts(&session.shell, "x"));
    ASSERT_INT(SH_TCP_SESSION_PEER_CLOSED,
               sh_tcp_session_state(&session));
    ASSERT_SIZE(1u, io.close_calls);
    ASSERT_INT(SH_TCP_OK, sh_tcp_session_close(&session));
    ASSERT_SIZE(1u, io.close_calls);

    memset(&io, 0, sizeof(io));
    io.rx_empty_status = SH_TCP_IO_WOULD_BLOCK;
    io.write_status = SH_TCP_IO_OK;
    init_session(&session, &io, &auth, &storage_d, 8u);
    ASSERT_INT(SH_TCP_OK, sh_tcp_session_close(&session));
    ASSERT_INT(SH_TCP_SESSION_LOCAL_CLOSED,
               sh_tcp_session_state(&session));
    ASSERT_SIZE(1u, io.close_calls);
    ASSERT_INT(SH_TCP_OK, sh_tcp_session_close(&session));
    ASSERT_SIZE(1u, io.close_calls);
}

static void test_invalid_arguments(void)
{
    sh_tcp_session_t session;
    sh_tcp_session_config_t config;
    sh_tcp_poll_result_t result;

    memset(&config, 0, sizeof(config));
    ASSERT_INT(SH_TCP_ERR_INVALID_ARGUMENT,
               sh_tcp_session_init(NULL, &config));
    ASSERT_INT(SH_TCP_ERR_INVALID_ARGUMENT,
               sh_tcp_session_init(&session, &config));
    result = sh_tcp_session_poll(NULL);
    ASSERT_INT(SH_TCP_ERR_INVALID_ARGUMENT, result.status);
    ASSERT_INT(SH_TCP_ERR_INVALID_ARGUMENT,
               sh_tcp_session_close(NULL));
}

int main(void)
{
    test_independent_sessions_and_multiple_commands();
    test_fragmented_rx_and_idle();
    test_partial_and_backpressure_output();
    test_disconnect_and_error_close_once();
    test_write_disconnect_and_local_close();
    test_invalid_arguments();

    puts("test_tcp_port: all tests passed");
    return 0;
}
