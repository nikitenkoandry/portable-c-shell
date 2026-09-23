#include "sh_ansi.h"

enum {
    SH_ANSI_STATE_IDLE = 0,
    SH_ANSI_STATE_ESCAPE,
    SH_ANSI_STATE_CSI
};

static void sh_ansi_finish_sequence(sh_ansi_decoder_t *decoder)
{
    decoder->state = SH_ANSI_STATE_IDLE;
    decoder->csi_length = 0u;
    decoder->csi_overflow = false;
}

static bool sh_ansi_is_final_byte(unsigned char byte)
{
    return byte >= 0x40u && byte <= 0x7eu;
}

static bool sh_ansi_is_parameter_or_intermediate(unsigned char byte)
{
    return byte >= 0x20u && byte <= 0x3fu;
}

static sh_ansi_event_t sh_ansi_decode_csi(const sh_ansi_decoder_t *decoder,
                                          unsigned char final_byte)
{
    if (decoder->csi_length == 0u) {
        switch (final_byte) {
        case 'A':
            return SH_ANSI_EVENT_UP;
        case 'B':
            return SH_ANSI_EVENT_DOWN;
        case 'C':
            return SH_ANSI_EVENT_RIGHT;
        case 'D':
            return SH_ANSI_EVENT_LEFT;
        case 'H':
            return SH_ANSI_EVENT_HOME;
        case 'F':
            return SH_ANSI_EVENT_END;
        default:
            return SH_ANSI_EVENT_NONE;
        }
    }

    if (final_byte != '~' || decoder->csi_length != 1u) {
        return SH_ANSI_EVENT_NONE;
    }

    switch (decoder->csi[0]) {
    case '1':
        return SH_ANSI_EVENT_HOME;
    case '3':
        return SH_ANSI_EVENT_DELETE;
    case '4':
        return SH_ANSI_EVENT_END;
    default:
        return SH_ANSI_EVENT_NONE;
    }
}

void sh_ansi_reset(sh_ansi_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }

    decoder->state = SH_ANSI_STATE_IDLE;
    decoder->csi_length = 0u;
    decoder->csi_overflow = false;
    decoder->last_byte_consumed = false;
}

void sh_ansi_init(sh_ansi_decoder_t *decoder)
{
    sh_ansi_reset(decoder);
}

sh_ansi_event_t sh_ansi_feed(sh_ansi_decoder_t *decoder, unsigned char byte)
{
    sh_ansi_event_t event;

    if (decoder == NULL) {
        return SH_ANSI_EVENT_NONE;
    }

    decoder->last_byte_consumed = false;

    if (decoder->state == SH_ANSI_STATE_IDLE) {
        if (byte == 0x1bu) {
            decoder->state = SH_ANSI_STATE_ESCAPE;
            decoder->last_byte_consumed = true;
        }
        return SH_ANSI_EVENT_NONE;
    }

    decoder->last_byte_consumed = true;

    if (decoder->state == SH_ANSI_STATE_ESCAPE) {
        if (byte == '[') {
            decoder->state = SH_ANSI_STATE_CSI;
            decoder->csi_length = 0u;
            decoder->csi_overflow = false;
        } else if (byte != 0x1bu) {
            sh_ansi_finish_sequence(decoder);
            decoder->last_byte_consumed = false;
        }
        return SH_ANSI_EVENT_NONE;
    }

    if (byte == 0x1bu) {
        decoder->state = SH_ANSI_STATE_ESCAPE;
        decoder->csi_length = 0u;
        decoder->csi_overflow = false;
        return SH_ANSI_EVENT_NONE;
    }

    if (sh_ansi_is_final_byte(byte)) {
        event = decoder->csi_overflow
                    ? SH_ANSI_EVENT_NONE
                    : sh_ansi_decode_csi(decoder, byte);
        sh_ansi_finish_sequence(decoder);
        return event;
    }

    if (!sh_ansi_is_parameter_or_intermediate(byte)) {
        sh_ansi_finish_sequence(decoder);
        return SH_ANSI_EVENT_NONE;
    }

    if (decoder->csi_length < SH_ANSI_CSI_MAX) {
        decoder->csi[decoder->csi_length] = (char)byte;
        decoder->csi_length++;
    } else {
        decoder->csi_overflow = true;
    }

    return SH_ANSI_EVENT_NONE;
}

bool sh_ansi_last_byte_consumed(const sh_ansi_decoder_t *decoder)
{
    return decoder != NULL && decoder->last_byte_consumed;
}

bool sh_ansi_is_active(const sh_ansi_decoder_t *decoder)
{
    return decoder != NULL && decoder->state != SH_ANSI_STATE_IDLE;
}
