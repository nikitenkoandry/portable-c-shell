#include "sh_port_host.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200u
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004u
#endif

#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008u
#endif

static int sh_host_queue_sequence(sh_host_terminal_t *terminal,
                                  const unsigned char *sequence,
                                  size_t length,
                                  WORD repeat_count)
{
    if (length == 0u || length > sizeof(terminal->byte_queue)) {
        return SH_HOST_TERMINAL_ERROR;
    }

    memcpy(terminal->byte_queue, sequence, length);
    terminal->byte_queue_length = length;
    terminal->byte_queue_position = 0u;
    terminal->repeat_remaining = repeat_count == 0u ? 1u : repeat_count;
    return SH_HOST_TERMINAL_SUCCESS;
}

static int sh_host_take_queued_byte(sh_host_terminal_t *terminal,
                                    unsigned char *byte_out)
{
    if (terminal->byte_queue_position >= terminal->byte_queue_length) {
        return SH_HOST_TERMINAL_READ_EOF;
    }

    *byte_out = terminal->byte_queue[terminal->byte_queue_position++];
    if (terminal->byte_queue_position == terminal->byte_queue_length) {
        if (terminal->repeat_remaining > 1u) {
            --terminal->repeat_remaining;
            terminal->byte_queue_position = 0u;
        } else {
            terminal->byte_queue_length = 0u;
            terminal->byte_queue_position = 0u;
            terminal->repeat_remaining = 0u;
        }
    }

    return SH_HOST_TERMINAL_READ_BYTE;
}

static int sh_host_queue_special_key(sh_host_terminal_t *terminal,
                                     WORD virtual_key,
                                     WORD repeat_count)
{
    const unsigned char *sequence = NULL;
    size_t length = 0u;
    static const unsigned char up[] = "\x1b[A";
    static const unsigned char down[] = "\x1b[B";
    static const unsigned char right[] = "\x1b[C";
    static const unsigned char left[] = "\x1b[D";
    static const unsigned char home[] = "\x1b[H";
    static const unsigned char end[] = "\x1b[F";
    static const unsigned char delete_key[] = "\x1b[3~";

    switch (virtual_key) {
    case VK_UP:
        sequence = up;
        length = sizeof(up) - 1u;
        break;
    case VK_DOWN:
        sequence = down;
        length = sizeof(down) - 1u;
        break;
    case VK_RIGHT:
        sequence = right;
        length = sizeof(right) - 1u;
        break;
    case VK_LEFT:
        sequence = left;
        length = sizeof(left) - 1u;
        break;
    case VK_HOME:
        sequence = home;
        length = sizeof(home) - 1u;
        break;
    case VK_END:
        sequence = end;
        length = sizeof(end) - 1u;
        break;
    case VK_DELETE:
        sequence = delete_key;
        length = sizeof(delete_key) - 1u;
        break;
    default:
        return SH_HOST_TERMINAL_READ_EOF;
    }

    return sh_host_queue_sequence(terminal, sequence, length, repeat_count) ==
                   SH_HOST_TERMINAL_SUCCESS
               ? SH_HOST_TERMINAL_READ_BYTE
               : SH_HOST_TERMINAL_ERROR;
}

static int sh_host_queue_unicode(sh_host_terminal_t *terminal,
                                 WCHAR character,
                                 WORD repeat_count)
{
    WCHAR utf16[2];
    int utf16_length = 1;
    char utf8[sizeof(terminal->byte_queue)];
    int utf8_length;

    if (character >= 0xd800u && character <= 0xdbffu) {
        terminal->pending_high_surrogate = (unsigned short)character;
        return SH_HOST_TERMINAL_READ_EOF;
    }

    if (character >= 0xdc00u && character <= 0xdfffu) {
        if (terminal->pending_high_surrogate == 0u) {
            return SH_HOST_TERMINAL_READ_EOF;
        }
        utf16[0] = (WCHAR)terminal->pending_high_surrogate;
        utf16[1] = character;
        utf16_length = 2;
        terminal->pending_high_surrogate = 0u;
    } else {
        terminal->pending_high_surrogate = 0u;
        if (character == 0u) {
            return SH_HOST_TERMINAL_READ_EOF;
        }
        utf16[0] = character;
    }

    utf8_length = WideCharToMultiByte(CP_UTF8,
                                      WC_ERR_INVALID_CHARS,
                                      utf16,
                                      utf16_length,
                                      utf8,
                                      (int)sizeof(utf8),
                                      NULL,
                                      NULL);
    if (utf8_length <= 0) {
        return SH_HOST_TERMINAL_READ_EOF;
    }

    return sh_host_queue_sequence(terminal,
                                  (const unsigned char *)utf8,
                                  (size_t)utf8_length,
                                  repeat_count) == SH_HOST_TERMINAL_SUCCESS
               ? SH_HOST_TERMINAL_READ_BYTE
               : SH_HOST_TERMINAL_ERROR;
}

int sh_host_terminal_open(sh_host_terminal_t *terminal)
{
    HANDLE input_handle;
    HANDLE output_handle;
    DWORD input_mode;
    DWORD output_mode;
    DWORD requested_mode;

    if (terminal == NULL) {
        return SH_HOST_TERMINAL_ERROR;
    }

    memset(terminal, 0, sizeof(*terminal));
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input_handle == NULL || input_handle == INVALID_HANDLE_VALUE) {
        return SH_HOST_TERMINAL_ERROR;
    }

    terminal->input_handle = input_handle;
    if (output_handle != INVALID_HANDLE_VALUE) {
        terminal->output_handle = output_handle;
    }

    if (GetConsoleMode(input_handle, &input_mode) != 0) {
        terminal->input_is_console = 1u;
        terminal->input_mode_saved = 1u;
        terminal->original_input_mode = input_mode;

        requested_mode = input_mode;
        requested_mode &= ~(ENABLE_LINE_INPUT |
                            ENABLE_ECHO_INPUT |
                            ENABLE_PROCESSED_INPUT |
                            ENABLE_MOUSE_INPUT |
                            ENABLE_WINDOW_INPUT |
                            ENABLE_QUICK_EDIT_MODE |
                            ENABLE_VIRTUAL_TERMINAL_INPUT);
        requested_mode |= ENABLE_EXTENDED_FLAGS;

        if (SetConsoleMode(input_handle, requested_mode) == 0) {
            memset(terminal, 0, sizeof(*terminal));
            return SH_HOST_TERMINAL_ERROR;
        }
    }

    if (terminal->output_handle != NULL &&
        GetConsoleMode((HANDLE)terminal->output_handle, &output_mode) != 0) {
        terminal->output_mode_saved = 1u;
        terminal->original_output_mode = output_mode;
        requested_mode = output_mode |
                         ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                         DISABLE_NEWLINE_AUTO_RETURN;
        if (SetConsoleMode((HANDLE)terminal->output_handle, requested_mode) == 0) {
            requested_mode = output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            (void)SetConsoleMode((HANDLE)terminal->output_handle, requested_mode);
        }
    }

    terminal->opened = 1u;
    return SH_HOST_TERMINAL_SUCCESS;
}

int sh_host_terminal_read_byte(sh_host_terminal_t *terminal,
                               unsigned char *byte_out)
{
    int queue_status;

    if (terminal == NULL || byte_out == NULL || terminal->opened == 0u) {
        return SH_HOST_TERMINAL_ERROR;
    }

    queue_status = sh_host_take_queued_byte(terminal, byte_out);
    if (queue_status == SH_HOST_TERMINAL_READ_BYTE) {
        return queue_status;
    }

    if (terminal->input_is_console == 0u) {
        DWORD bytes_read = 0u;
        if (ReadFile((HANDLE)terminal->input_handle,
                     byte_out,
                     1u,
                     &bytes_read,
                     NULL) == 0) {
            return SH_HOST_TERMINAL_ERROR;
        }
        return bytes_read == 0u ? SH_HOST_TERMINAL_READ_EOF
                                : SH_HOST_TERMINAL_READ_BYTE;
    }

    for (;;) {
        INPUT_RECORD record;
        DWORD records_read = 0u;
        KEY_EVENT_RECORD *key;
        int event_status;

        if (ReadConsoleInputW((HANDLE)terminal->input_handle,
                              &record,
                              1u,
                              &records_read) == 0) {
            return SH_HOST_TERMINAL_ERROR;
        }
        if (records_read == 0u || record.EventType != KEY_EVENT) {
            continue;
        }

        key = &record.Event.KeyEvent;
        if (key->bKeyDown == 0) {
            continue;
        }

        event_status = sh_host_queue_special_key(terminal,
                                                 key->wVirtualKeyCode,
                                                 key->wRepeatCount);
        if (event_status == SH_HOST_TERMINAL_ERROR) {
            return event_status;
        }
        if (event_status != SH_HOST_TERMINAL_READ_BYTE) {
            event_status = sh_host_queue_unicode(terminal,
                                                 key->uChar.UnicodeChar,
                                                 key->wRepeatCount);
            if (event_status == SH_HOST_TERMINAL_ERROR) {
                return event_status;
            }
        }

        if (event_status == SH_HOST_TERMINAL_READ_BYTE) {
            return sh_host_take_queued_byte(terminal, byte_out);
        }
    }
}

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
        SetConsoleMode((HANDLE)terminal->input_handle,
                       (DWORD)terminal->original_input_mode) == 0) {
        result = SH_HOST_TERMINAL_ERROR;
    }
    if (terminal->output_mode_saved != 0u &&
        SetConsoleMode((HANDLE)terminal->output_handle,
                       (DWORD)terminal->original_output_mode) == 0) {
        result = SH_HOST_TERMINAL_ERROR;
    }

    terminal->opened = 0u;
    terminal->byte_queue_length = 0u;
    terminal->byte_queue_position = 0u;
    terminal->repeat_remaining = 0u;
    terminal->pending_high_surrogate = 0u;
    return result;
}

#else

typedef int sh_port_windows_translation_unit_is_disabled;

#endif
