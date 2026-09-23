#ifndef SH_ANSI_H
#define SH_ANSI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SH_ANSI_CSI_MAX
#define SH_ANSI_CSI_MAX 8u
#endif

/** Decoded terminal key events emitted from ANSI escape sequences. */
typedef enum {
    SH_ANSI_EVENT_NONE = 0,
    SH_ANSI_EVENT_UP,
    SH_ANSI_EVENT_DOWN,
    SH_ANSI_EVENT_LEFT,
    SH_ANSI_EVENT_RIGHT,
    SH_ANSI_EVENT_HOME,
    SH_ANSI_EVENT_END,
    SH_ANSI_EVENT_DELETE
} sh_ansi_event_t;

/** Caller-owned incremental ANSI decoder state. */
typedef struct {
    unsigned char state;
    size_t csi_length;
    bool csi_overflow;
    bool last_byte_consumed;
    char csi[SH_ANSI_CSI_MAX];
} sh_ansi_decoder_t;

/**
 * @brief Initialize an ANSI decoder to its idle state.
 * @param[out] decoder Decoder instance. NULL is ignored.
 */
void sh_ansi_init(sh_ansi_decoder_t *decoder);

/**
 * @brief Discard an incomplete sequence and return the decoder to idle.
 * @param decoder Decoder instance. NULL is ignored.
 */
void sh_ansi_reset(sh_ansi_decoder_t *decoder);

/**
 * @brief Feed one byte into an incremental ANSI escape-sequence decoder.
 *
 * Incomplete sequences are retained across calls. When ESC is followed by a
 * byte that is not part of a supported sequence, that byte is left unconsumed
 * so the line editor can process it as normal input.
 *
 * @param decoder Initialized decoder instance.
 * @param byte Next raw terminal byte.
 * @return A decoded key event, or SH_ANSI_EVENT_NONE.
 */
sh_ansi_event_t sh_ansi_feed(sh_ansi_decoder_t *decoder, unsigned char byte);

/**
 * @brief Check whether the most recently fed byte belonged to ANSI input.
 * @return true when consumed by the decoder; false otherwise or for NULL.
 */
bool sh_ansi_last_byte_consumed(const sh_ansi_decoder_t *decoder);

/**
 * @brief Check whether an ESC or CSI sequence is currently incomplete.
 * @return true while decoding a sequence; false when idle or for NULL.
 */
bool sh_ansi_is_active(const sh_ansi_decoder_t *decoder);

#ifdef __cplusplus
}
#endif

#endif
