# Raw TCP And Telnet Integration

The current repository provides a platform-neutral raw TCP session adapter:

- `shell/ports/tcp/sh_port_tcp.h`
- `shell/ports/tcp/sh_port_tcp.c`
- `shell/examples/tcp_loopback_shell.c`
- `shell/tests/test_tcp_port.c`

It does not call BSD sockets, lwIP, FreeRTOS+TCP, or another network stack.
Instead, the application maps its stack to read/write/close callbacks. The
adapter owns shell-session state transitions, transport mapping, one bounded RX
chunk, and close-once notification.

The adapter is raw TCP. It does not implement Telnet negotiation, TLS,
authentication, authorization, listening sockets, reconnects, or idle timeout.

## Build Targets

`SH_BUILD_TCP_PORT` defaults to `ON` and creates `sh_port_tcp`. With examples
enabled, it also creates `tcp_loopback_shell`, which exercises a scripted
in-process byte stream; it is not a listening network server.

```sh
cmake -S . -B build -DSH_BUILD_TCP_PORT=ON
cmake --build build --target sh_port_tcp tcp_loopback_shell test_tcp_port
ctest --test-dir build --output-on-failure -R '^test_tcp_port$'
```

`SH_TCP_RX_CHUNK_SIZE` defaults to 128 bytes and is embedded in every
`sh_tcp_session_t`. Override it consistently for the TCP adapter and all users
through a target-wide compiler definition.

## Raw TCP Versus Telnet

| Property | Raw TCP | Telnet |
|---|---|---|
| Payload | Application bytes only | Data plus IAC protocol commands |
| Shell RX | Feed directly | Filter Telnet commands first |
| Echo | Application/client convention | Negotiated option |
| Line endings | Client-defined | NVT CR/LF rules |
| `0xff` | Ordinary byte | IAC and must be escaped |
| Terminal keys | ANSI bytes if client sends them | ANSI data after Telnet decoding |

For a controlled device tool, raw TCP is simpler. Do not label a raw TCP port
as Telnet: unfiltered negotiation bytes can corrupt commands.

Neither mode is encrypted. `SH_CMD_SENSITIVE` protects editor echo and history,
not network traffic. Put the service behind product authentication and an
encrypted management boundary when the network is not fully trusted.

## Session Model And Ownership

One `sh_tcp_session_t` represents exactly one connection. It contains:

- its own `sh_t`, transport, connection state, and RX chunk;
- a copied `sh_storage_t` descriptor;
- connection callbacks and I/O context;
- an independent authentication context;
- the last shell status and close-notified flag.

The buffers referenced by `sh_storage_t` remain caller-owned and must be unique
to that connection for its entire lifetime. Never reuse line/history buffers
across simultaneous sessions.

`cfg.secure_clear_enabled` is enabled by default, but closing a TCP session does
not wipe every caller-owned history slot or transport buffer. After the close
callback has stopped all users, securely clear the complete storage and I/O
queues before assigning them to another authenticated client.

The connection task or event-loop slot is the sole owner of the session. It
calls `sh_tcp_session_poll()` serially. Command handlers execute synchronously
inside that call. The adapter contains no mutex and performs no allocation.

`auth_user_ctx` is installed as `shell.cfg.user_ctx`; handlers without a
per-command context receive it. Retrieve it directly with
`sh_tcp_session_auth_user_ctx()` when connection-level code needs it. Complete
authentication and authorization before enabling privileged command tables.

## Session Initialization

Fill `sh_tcp_session_config_t` with:

- optional `shell_config` (`sh_default_config()` is used when NULL);
- one unique storage descriptor;
- commands and explicit command count;
- non-NULL read, write, and close callbacks;
- stack-specific `io_ctx`;
- optional `auth_user_ctx`.

`sh_tcp_session_init()` forces shell output through the session transport,
clears legacy `cfg.write`, and installs `auth_user_ctx` as shell-wide context.
It initializes the shell and validates command registration without performing
I/O. On failure it returns `SH_TCP_ERR_SHELL`, stores the core status in
`session.last_shell_status`, and leaves the session uninitialized. The caller
must close a socket itself when initialization fails because close notification
has not started.

After success, call `sh_tcp_session_prompt()` once. Do not also call
`sh_start()` for the same connection; the TCP adapter has its own lifecycle.

`sh_tcp_session_config_t` accepts one flat command table. To use modular
command sets, initialize the session with `commands = NULL` and
`command_count = 0`, then replace the empty registration before the prompt:

```c
sh_t *shell = sh_tcp_session_shell(&session);
int status = sh_register_command_sets(shell, command_sets,
                                      SH_ARRAY_SIZE(command_sets));
if (status != SH_OK) {
    (void)sh_tcp_session_close(&session);
}
```

## I/O Callback Contract

Both read and write callbacks must always set their byte count to `0..capacity`
or `0..len`. Partial progress may accompany every I/O status.

Read mappings:

| Stack result | Callback result |
|---|---|
| Bytes received | `SH_TCP_IO_OK`, `read_count > 0` |
| No byte ready yet | `SH_TCP_IO_WOULD_BLOCK`, count 0 |
| Peer orderly close | `SH_TCP_IO_CLOSED` |
| Reset/permanent error | `SH_TCP_IO_ERROR` |

`SH_TCP_IO_OK` with zero bytes is a protocol error. A poll invokes read exactly
once. It feeds every confirmed byte before applying CLOSED/ERROR state, so a
callback may return final bytes together with disconnect.

Write mappings are analogous. `SH_TCP_IO_OK` with zero progress becomes
`SH_TRANSPORT_ERR_NO_PROGRESS`; a count larger than requested is a protocol
error. CLOSED and ERROR move the session to a terminal state and invoke close
notification once. WOULD_BLOCK keeps it open and maps to shell backpressure.

The close callback is mandatory and is invoked at most once. Explicit
`sh_tcp_session_close()` is idempotent after a successful initialization.

## Poll Results

`sh_tcp_session_poll()` returns `sh_tcp_poll_result_t`:

| Status | Meaning |
|---|---|
| `SH_TCP_RX_DATA` | One read produced shell bytes |
| `SH_TCP_IDLE` | Read would block with no bytes |
| `SH_TCP_DISCONNECTED` | Peer closed; close callback was notified |
| `SH_TCP_ERR_IO` | Permanent I/O error |
| `SH_TCP_ERR_PROTOCOL` | Callback violated byte/status contract |
| `SH_TCP_ERR_SHELL` | Editor/parser/handler returned non-OK |
| `SH_TCP_ERR_CLOSED` | Poll requested after terminal state |

`bytes_received` reports confirmed RX progress. `shell_status` contains the
core status. A user typo, unknown command, invalid count, or ambiguous Tab can
produce `SH_TCP_ERR_SHELL` while the session remains open. The owner may log
the synthetic status and continue polling when
`sh_tcp_session_state(session) == SH_TCP_SESSION_OPEN`; it should not treat
every command error as a network disconnect.

## Complete POSIX Socket Binding

The following is a complete C99 single-client-at-a-time server built on the
current `sh_port_tcp` API. It listens only on loopback. One static storage set
is reused after each client closes; a concurrent server must allocate one set
per active session.

`tcp_shell_posix.c`:

```c
#define _POSIX_C_SOURCE 200809L

#include "sh_port_tcp.h"

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

enum { SERVER_PORT = 2323 };

typedef struct {
    int fd;
} socket_io_t;

typedef struct {
    const char *name;
} auth_context_t;

SH_STORAGE_DEFINE(g_tcp_storage, 256u, 32u, 16u);

static sh_tcp_io_status_t socket_read(void *ctx, uint8_t *data,
                                      size_t capacity, size_t *read_count)
{
    socket_io_t *io = (socket_io_t *)ctx;
    ssize_t result;

    *read_count = 0u;
    result = recv(io->fd, data, capacity, 0);
    if (result > 0) {
        *read_count = (size_t)result;
        return SH_TCP_IO_OK;
    }
    if (result == 0) {
        return SH_TCP_IO_CLOSED;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return SH_TCP_IO_WOULD_BLOCK;
    }
    return SH_TCP_IO_ERROR;
}

static sh_tcp_io_status_t socket_write(void *ctx, const uint8_t *data,
                                       size_t len, size_t *written)
{
    socket_io_t *io = (socket_io_t *)ctx;
    ssize_t result;

    *written = 0u;
    result = send(io->fd, data, len, 0);
    if (result > 0) {
        *written = (size_t)result;
        return SH_TCP_IO_OK;
    }
    if (result < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return SH_TCP_IO_WOULD_BLOCK;
    }
    return result == 0 ? SH_TCP_IO_CLOSED : SH_TCP_IO_ERROR;
}

static void socket_close(void *ctx)
{
    socket_io_t *io = (socket_io_t *)ctx;

    if (io->fd >= 0) {
        (void)shutdown(io->fd, SHUT_RDWR);
        (void)close(io->fd);
        io->fd = -1;
    }
}

static int cmd_whoami(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    const auth_context_t *auth = (const auth_context_t *)user_ctx;
    (void)argc;
    (void)argv;
    return sh_printf(shell, "user=%s\r\n", auth->name);
}

static int cmd_args(sh_t *shell, int argc, char **argv, void *user_ctx)
{
    int i;
    (void)user_ctx;

    for (i = 1; i < argc; i++) {
        int status = sh_printf(shell, "arg[%d]=%s\r\n", i - 1, argv[i]);
        if (status != SH_OK) {
            return status;
        }
    }
    return SH_OK;
}

static const sh_cmd_t commands[] = {
    SH_CMD_ARG("whoami", "", "Show session identity",
               cmd_whoami, 1u, 1u),
    SH_CMD_ARG("args", "<values...>", "Print arguments",
               cmd_args, 2u, SH_ARGS_ANY),
};

static void serve_client(int client_fd)
{
    socket_io_t io;
    auth_context_t auth;
    sh_config_t shell_config;
    sh_tcp_session_config_t session_config;
    sh_tcp_session_t session;
    sh_tcp_status_t init_status;

    io.fd = client_fd;
    auth.name = "loopback-admin"; /* Replace after real authentication. */

    sh_default_config(&shell_config);
    shell_config.prompt = "tcp> ";
    shell_config.line_max = 256u;
    shell_config.argc_max = 32u;
    shell_config.history_depth = 16u;
    shell_config.transport_write_attempts = 8u;

    memset(&session_config, 0, sizeof(session_config));
    session_config.shell_config = &shell_config;
    session_config.storage = &g_tcp_storage;
    session_config.commands = commands;
    session_config.command_count = SH_ARRAY_SIZE(commands);
    session_config.read = socket_read;
    session_config.write = socket_write;
    session_config.close = socket_close;
    session_config.io_ctx = &io;
    session_config.auth_user_ctx = &auth;

    init_status = sh_tcp_session_init(&session, &session_config);
    if (init_status != SH_TCP_OK) {
        socket_close(&io);
        return;
    }
    if (sh_tcp_session_prompt(&session) != SH_OK) {
        (void)sh_tcp_session_close(&session);
        return;
    }

    while (sh_tcp_session_state(&session) == SH_TCP_SESSION_OPEN) {
        sh_tcp_poll_result_t result = sh_tcp_session_poll(&session);

        if (result.status == SH_TCP_RX_DATA ||
            result.status == SH_TCP_IDLE) {
            continue;
        }
        if (result.status == SH_TCP_ERR_SHELL &&
            sh_tcp_session_state(&session) == SH_TCP_SESSION_OPEN) {
            fprintf(stderr, "shell status: %s\n",
                    sh_status_string(result.shell_status));
            continue;
        }
        break;
    }

    (void)sh_tcp_session_close(&session);
}

int main(void)
{
    int server_fd;
    int reuse = 1;
    struct sockaddr_in address;

    (void)signal(SIGPIPE, SIG_IGN);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) != 0) {
        perror("setsockopt");
        (void)close(server_fd);
        return 2;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)SERVER_PORT);
    if (bind(server_fd, (const struct sockaddr *)&address,
             sizeof(address)) != 0 || listen(server_fd, 1) != 0) {
        perror("bind/listen");
        (void)close(server_fd);
        return 3;
    }

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        struct timeval timeout;

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        (void)setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));
        serve_client(client_fd);
    }

    (void)close(server_fd);
    return 4;
}
```

Build from the repository root on a POSIX host:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -Werror \
  -Ishell/include -Ishell/ports/tcp \
  tcp_shell_posix.c \
  shell/src/sh_shell.c shell/src/sh_tokenizer.c \
  shell/src/sh_command.c shell/src/sh_history.c \
  shell/src/sh_completion.c shell/src/sh_ansi.c \
  shell/src/sh_transport.c shell/ports/tcp/sh_port_tcp.c \
  -o tcp_shell_posix
```

Run and connect from another terminal:

```sh
./tcp_shell_posix
nc 127.0.0.1 2323
```

This binding uses a blocking `recv()` and a two-second send timeout. One shell
write may call the socket callback up to `transport_write_attempts` times, so
tune timeout and attempts together. An event-driven server should use a
nonblocking read callback and call poll only when the socket is readable.

## lwIP Or FreeRTOS+TCP Mapping

Use the same adapter without changing shell core:

1. Allocate one session/storage set per accepted socket.
2. Map positive receive length to `SH_TCP_IO_OK`.
3. Map no-data-yet to `SH_TCP_IO_WOULD_BLOCK`.
4. Map orderly peer close to `SH_TCP_IO_CLOSED`.
5. Map reset/permanent failure to `SH_TCP_IO_ERROR`.
6. Report partial send progress exactly through `*written`.
7. Let the connection owner schedule retries and idle/auth timeouts.

With an asynchronous raw TCP API, callbacks should enqueue received bytes for
the session owner and drain a per-session TX ring on sent/poll callbacks. Never
retain a pointer supplied to the write callback: it can point to a temporary
`sh_printf()` buffer and is valid only during the call.

## Telnet Filtering

The current TCP adapter is raw and feeds callback output directly to the shell.
A Telnet integration must decode IAC in its application read callback before
reporting bytes to `sh_tcp_session_poll()`.

A decoder needs a per-session state machine with at least:

```c
typedef enum {
    TELNET_DATA = 0,
    TELNET_IAC,
    TELNET_OPTION,
    TELNET_SUBNEGOTIATION,
    TELNET_SUBNEGOTIATION_IAC
} telnet_state_t;

typedef struct {
    telnet_state_t state;
} telnet_decoder_t;
```

It must consume `IAC WILL/WONT/DO/DONT option`, consume subnegotiation through
`IAC SE`, and preserve state across receive calls. Decoded data length can be
zero even when raw negotiation bytes were consumed. In that case the read
callback must return `SH_TCP_IO_WOULD_BLOCK` with `read_count == 0`; returning
`SH_TCP_IO_OK` with zero is intentionally a TCP-adapter protocol error.

A production Telnet endpoint must also:

- implement deterministic option acceptance/refusal;
- negotiate server echo and suppress-go-ahead consistently;
- escape outbound data byte `0xff` as `0xff 0xff`;
- report `*written` in original shell-byte units despite escaping;
- test CR LF and CR NUL with every supported client;
- bound malformed/subnegotiation input;
- keep Telnet control bytes out of history and masking.

Use a maintained Telnet library when broad client compatibility is required.

## History, Tab, And Terminal Keys

Interactive editing works only when the client sends bytes immediately:

- Up/Down: `ESC [ A/B`;
- Left/Right: `ESC [ D/C`;
- Tab: `0x09`;
- Backspace: `0x08` or `0x7f`;
- Home/End/Delete: encodings listed in `docs/porting.md`.

Line-buffered clients cannot provide live Tab or cursor editing. `nc` behavior
varies by implementation. Use a character-at-a-time client for acceptance.

Each session has independent volatile history. Disconnecting destroys that
session state. Persistence, if required, belongs outside this module and must
exclude recoverable secrets.

## Network Production Checklist

- Unique `sh_tcp_session_t` and storage per active client.
- Authentication completes before privileged commands are available.
- Maximum clients, idle time, command time, and output volume are bounded.
- Callback counts are correct for partial OK/WOULD_BLOCK/CLOSED/ERROR results.
- `SH_TCP_ERR_SHELL` policy distinguishes user errors from connection errors.
- Close callback is idempotent and releases all stack resources once.
- Caller-owned line/history/I/O storage is wiped before authenticated reuse.
- Slow readers cannot cause unbounded memory use or unbounded shell blocking.
- Raw TCP and Telnet endpoints are not confused.
- TLS or another encrypted boundary protects untrusted networks.
- Secrets are absent from echo, history, logs, traces, and crash data.
- Fragmentation, half-close, reset, reconnect, and multi-session isolation are
  tested on the target network stack.
