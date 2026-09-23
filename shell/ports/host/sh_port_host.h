#ifndef SH_PORT_HOST_H
#define SH_PORT_HOST_H

#include <stddef.h>

#ifndef _WIN32
#include <termios.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * open() and close() return SH_HOST_TERMINAL_SUCCESS or
 * SH_HOST_TERMINAL_ERROR. read_byte() returns SH_HOST_TERMINAL_READ_BYTE,
 * SH_HOST_TERMINAL_READ_EOF, or SH_HOST_TERMINAL_ERROR.
 */
enum {
    SH_HOST_TERMINAL_ERROR = -1,
    SH_HOST_TERMINAL_SUCCESS = 0,
    SH_HOST_TERMINAL_READ_EOF = 0,
    SH_HOST_TERMINAL_READ_BYTE = 1
};

/**
 * Caller-owned native terminal state. Initialize with SH_HOST_TERMINAL_INIT and
 * keep one context per active console input stream.
 */
typedef struct {
#ifdef _WIN32
    void *input_handle;
    void *output_handle;
    unsigned long original_input_mode;
    unsigned long original_output_mode;
    unsigned char input_mode_saved;
    unsigned char output_mode_saved;
    unsigned char input_is_console;

    unsigned char byte_queue[8];
    size_t byte_queue_length;
    size_t byte_queue_position;
    unsigned short repeat_remaining;
    unsigned short pending_high_surrogate;
#else
    int input_fd;
    struct termios original_input_mode;
    unsigned char input_mode_saved;
#endif
    unsigned char opened;
} sh_host_terminal_t;

/** Static initializer for sh_host_terminal_t. */
#define SH_HOST_TERMINAL_INIT {0}

/**
 * @brief Open standard input as a blocking raw terminal byte stream.
 *
 * On Windows, console key events are translated to UTF-8 or ANSI key
 * sequences. On POSIX, terminal modes are adjusted with termios. Redirected
 * standard input remains supported as an ordinary byte stream. The caller
 * owns @p terminal and must eventually call sh_host_terminal_close().
 *
 * @param[out] terminal Terminal context to initialize.
 * @return SH_HOST_TERMINAL_SUCCESS or SH_HOST_TERMINAL_ERROR.
 */
int sh_host_terminal_open(sh_host_terminal_t *terminal);

/**
 * @brief Read one normalized terminal byte.
 *
 * The call blocks until a byte, EOF, or error. Windows arrow, Home, End and
 * Delete events are emitted as the same ANSI sequences used by POSIX terminals,
 * allowing the shell core to remain platform-independent.
 *
 * @param terminal Open terminal context.
 * @param[out] byte_out Destination used only when READ_BYTE is returned.
 * @return SH_HOST_TERMINAL_READ_BYTE, SH_HOST_TERMINAL_READ_EOF, or
 *         SH_HOST_TERMINAL_ERROR.
 */
int sh_host_terminal_read_byte(sh_host_terminal_t *terminal,
                               unsigned char *byte_out);

/**
 * @brief Restore every console or terminal mode saved by open().
 *
 * Closing an already closed initialized context is a successful no-op.
 *
 * @param terminal Terminal context.
 * @return SH_HOST_TERMINAL_SUCCESS or SH_HOST_TERMINAL_ERROR if restoration
 *         failed.
 */
int sh_host_terminal_close(sh_host_terminal_t *terminal);

#ifdef __cplusplus
}
#endif

#endif
