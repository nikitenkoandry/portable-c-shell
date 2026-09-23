#include "sh_port_host.h"

#ifndef _WIN32

#include <errno.h>
#include <string.h>
#include <unistd.h>

/** @brief Convert saved POSIX termios flags to raw byte-oriented input. */
static void sh_host_make_raw(struct termios *mode)
{
    mode->c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    mode->c_oflag &= (tcflag_t)~OPOST;
    mode->c_cflag |= (tcflag_t)CS8;
    mode->c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    mode->c_cc[VMIN] = 1;
    mode->c_cc[VTIME] = 0;
}

/** @brief Open standard input and enable raw POSIX terminal mode when possible. */
int sh_host_terminal_open(sh_host_terminal_t *terminal)
{
    struct termios raw_mode;

    if (terminal == NULL) {
        return SH_HOST_TERMINAL_ERROR;
    }

    memset(terminal, 0, sizeof(*terminal));
    terminal->input_fd = STDIN_FILENO;

    if (tcgetattr(terminal->input_fd, &terminal->original_input_mode) == 0) {
        terminal->input_mode_saved = 1u;
        raw_mode = terminal->original_input_mode;
        sh_host_make_raw(&raw_mode);
        if (tcsetattr(terminal->input_fd, TCSAFLUSH, &raw_mode) != 0) {
            memset(terminal, 0, sizeof(*terminal));
            return SH_HOST_TERMINAL_ERROR;
        }
    } else if (errno != ENOTTY) {
        return SH_HOST_TERMINAL_ERROR;
    }

    terminal->opened = 1u;
    return SH_HOST_TERMINAL_SUCCESS;
}

/** @brief Read one blocking byte or EOF from POSIX standard input. */
int sh_host_terminal_read_byte(sh_host_terminal_t *terminal,
                               unsigned char *byte_out)
{
    ssize_t read_count;

    if (terminal == NULL || byte_out == NULL || terminal->opened == 0u) {
        return SH_HOST_TERMINAL_ERROR;
    }

    do {
        read_count = read(terminal->input_fd, byte_out, 1u);
    } while (read_count < 0 && errno == EINTR);

    if (read_count > 0) {
        return SH_HOST_TERMINAL_READ_BYTE;
    }
    if (read_count == 0) {
        return SH_HOST_TERMINAL_READ_EOF;
    }
    return SH_HOST_TERMINAL_ERROR;
}

/** @brief Restore the saved POSIX terminal mode. */
int sh_host_terminal_close(sh_host_terminal_t *terminal)
{
    int result = SH_HOST_TERMINAL_SUCCESS;

    if (terminal == NULL) {
        return SH_HOST_TERMINAL_ERROR;
    }
    if (terminal->opened == 0u) {
        return SH_HOST_TERMINAL_SUCCESS;
    }

    if (terminal->input_mode_saved != 0u &&
        tcsetattr(terminal->input_fd,
                  TCSAFLUSH,
                  &terminal->original_input_mode) != 0) {
        result = SH_HOST_TERMINAL_ERROR;
    }

    terminal->opened = 0u;
    terminal->input_mode_saved = 0u;
    return result;
}

#else

typedef int sh_port_posix_translation_unit_is_disabled;

#endif
