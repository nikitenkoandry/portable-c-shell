#include "sh_shell.h"
#include <ctype.h>
#include <limits.h>

/** @brief Split a writable command line into quoted and escaped tokens. */
int sh_tokenize(char *line, char **argv, size_t argv_capacity, int *argc_out)
{
    size_t argc = 0u;
    char *read = line;
    char *write = line;

    if (argc_out == NULL) {
        return SH_ERR_INVALID_ARG;
    }
    *argc_out = 0;

    if (line == NULL || argv == NULL || argv_capacity == 0u) {
        return SH_ERR_INVALID_ARG;
    }

    while (*read != '\0') {
        while (isspace((unsigned char)*read)) {
            read++;
        }
        if (*read == '\0') {
            break;
        }
        if (argc >= argv_capacity || argc >= (size_t)INT_MAX) {
            return SH_ERR_TOO_MANY_ARGS;
        }

        argv[argc++] = write;
        char quote = '\0';

        while (*read != '\0') {
            char c = *read++;

            if (quote == '\0' && isspace((unsigned char)c)) {
                break;
            }

            if (c == '\\' && quote != '\'') {
                if (*read == '\0') {
                    return SH_ERR_INVALID_ARG;
                }
                *write++ = *read++;
                continue;
            }

            if (quote == '\0') {
                if (c == '"' || c == '\'') {
                    quote = c;
                    continue;
                }
            } else if (c == quote) {
                quote = '\0';
                continue;
            }

            *write++ = c;
        }

        if (quote != '\0') {
            return SH_ERR_UNTERMINATED_QUOTE;
        }
        *write++ = '\0';
    }

    *argc_out = (int)argc;
    return SH_OK;
}

/** @brief Map a shell status value to a stable diagnostic string. */
const char *sh_status_string(int status)
{
    switch (status) {
    case SH_OK: return "ok";
    case SH_ERR_INVALID_ARG: return "invalid argument";
    case SH_ERR_LINE_TOO_LONG: return "line too long";
    case SH_ERR_TOO_MANY_ARGS: return "too many arguments";
    case SH_ERR_UNTERMINATED_QUOTE: return "unterminated quote";
    case SH_ERR_UNKNOWN_COMMAND: return "unknown command";
    case SH_ERR_AMBIGUOUS: return "ambiguous command";
    case SH_ERR_NO_MEMORY: return "not enough memory";
    case SH_ERR_INVALID_CONFIG: return "invalid configuration";
    case SH_ERR_TRANSPORT: return "transport error";
    case SH_ERR_WOULD_BLOCK: return "operation would block";
    case SH_ERR_TRUNCATED: return "result truncated";
    case SH_ERR_BUSY: return "busy";
    case SH_ERR_INCOMPLETE_COMMAND: return "incomplete command";
    default: return "unknown error";
    }
}
