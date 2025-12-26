/**
 * Etherlink - Implementation
 *
 * MIT License - https://github.com/user/etherlink
 */

#include "etherlink.h"
#include <string.h>

/*******************************************************************************
 * CRC-8/CCITT Lookup Table
 * Polynomial: 0x07, Init: 0x00
 ******************************************************************************/

static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

/*******************************************************************************
 * CRC Functions
 ******************************************************************************/

uint8_t el_crc8_update(uint8_t crc, uint8_t byte) {
    return crc8_table[crc ^ byte];
}

uint8_t el_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

/*******************************************************************************
 * Core API Implementation
 ******************************************************************************/

bool el_init(el_ctx_t *ctx, const el_config_t *config) {
    if (!ctx || !config || !config->on_message || !config->send_bytes) {
        return false;
    }

    memset(ctx, 0, sizeof(el_ctx_t));
    ctx->on_message = config->on_message;
    ctx->send_bytes = config->send_bytes;
    ctx->state = EL_STATE_IDLE;

    return true;
}

void el_reset(el_ctx_t *ctx) {
    if (ctx) {
        ctx->state = EL_STATE_IDLE;
        ctx->payload_idx = 0;
        ctx->running_crc = 0;
    }
}

void el_process_byte(el_ctx_t *ctx, uint8_t byte) {
    if (!ctx) return;

    switch (ctx->state) {
        case EL_STATE_IDLE:
            if (byte == EL_SYNC_BYTE) {
                ctx->state = EL_STATE_GOT_SYNC;
                ctx->running_crc = 0;
            }
            break;

        case EL_STATE_GOT_SYNC:
            ctx->msg_id = byte;
            ctx->running_crc = el_crc8_update(ctx->running_crc, byte);
            ctx->state = EL_STATE_GOT_ID;
            break;

        case EL_STATE_GOT_ID:
            ctx->payload_len = byte;
            ctx->running_crc = el_crc8_update(ctx->running_crc, byte);

            if (ctx->payload_len > EL_MAX_PAYLOAD) {
                // Invalid length, reset
                ctx->rx_errors++;
                ctx->state = EL_STATE_IDLE;
            } else if (ctx->payload_len == 0) {
                // No payload, go straight to CRC
                ctx->state = EL_STATE_GOT_PAYLOAD;
            } else {
                ctx->payload_idx = 0;
                ctx->state = EL_STATE_GOT_LEN;
            }
            break;

        case EL_STATE_GOT_LEN:
            ctx->rx_buffer[ctx->payload_idx++] = byte;
            ctx->running_crc = el_crc8_update(ctx->running_crc, byte);

            if (ctx->payload_idx >= ctx->payload_len) {
                ctx->state = EL_STATE_GOT_PAYLOAD;
            }
            break;

        case EL_STATE_GOT_PAYLOAD:
            // Validate CRC
            if (byte == ctx->running_crc) {
                // Valid frame!
                ctx->rx_frames++;
                if (ctx->on_message) {
                    ctx->on_message(ctx->msg_id, ctx->rx_buffer, ctx->payload_len);
                }
            } else {
                // CRC mismatch
                ctx->rx_errors++;
            }
            ctx->state = EL_STATE_IDLE;
            break;
    }
}

void el_process_bytes(el_ctx_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        el_process_byte(ctx, data[i]);
    }
}

bool el_send(el_ctx_t *ctx, uint8_t msg_id, const void *payload, uint8_t len) {
    if (!ctx || !ctx->send_bytes) return false;
    if (len > EL_MAX_PAYLOAD) return false;
    if (len > 0 && !payload) return false;

    // Build frame in stack buffer
    uint8_t frame[EL_FRAME_OVERHEAD + EL_MAX_PAYLOAD];
    uint8_t frame_len = 0;

    // Header
    frame[frame_len++] = EL_SYNC_BYTE;
    frame[frame_len++] = msg_id;
    frame[frame_len++] = len;

    // Payload
    if (len > 0) {
        memcpy(&frame[frame_len], payload, len);
        frame_len += len;
    }

    // CRC (over msg_id + len + payload)
    uint8_t crc = el_crc8(&frame[1], 2 + len);
    frame[frame_len++] = crc;

    // Transmit
    ctx->send_bytes(frame, frame_len);
    ctx->tx_frames++;

    return true;
}
