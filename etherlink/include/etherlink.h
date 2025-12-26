/**
 * Etherlink - Lightweight Binary Protocol for Embedded Systems
 *
 * A minimal protocol for bidirectional communication over BLE/Serial.
 * Designed for low power/computation with user-definable message schemas.
 *
 * Frame format: [SYNC 0xA5] [MSG_ID] [LENGTH] [PAYLOAD...] [CRC8]
 *
 * MIT License - https://github.com/user/etherlink
 */

#ifndef ETHERLINK_H
#define ETHERLINK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Configuration
 ******************************************************************************/

#define EL_SYNC_BYTE        0xA5
#define EL_MAX_PAYLOAD      250     // Max payload size
#define EL_FRAME_OVERHEAD   4       // SYNC + MSG_ID + LEN + CRC

/*******************************************************************************
 * Message ID Conventions
 ******************************************************************************/

// Reserved system message IDs (0x00 - 0x0F)
#define EL_MSG_PING         0x00    // Heartbeat/ping request
#define EL_MSG_PONG         0x01    // Ping response
#define EL_MSG_VERSION      0x02    // Protocol version query/response
#define EL_MSG_ERROR        0x0F    // Error response

// User-defined ranges:
// 0x10 - 0x7F: Telemetry (device -> host)
// 0x80 - 0xFE: Commands (host -> device)
// 0xFF: Reserved

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * Callback when a complete message is received
 * @param msg_id Message type identifier
 * @param payload Pointer to payload data (valid only during callback)
 * @param len Payload length in bytes
 */
typedef void (*el_on_message_t)(uint8_t msg_id, const void *payload, uint8_t len);

/**
 * Callback to send raw bytes (implement for your transport)
 * @param data Pointer to data to send
 * @param len Number of bytes to send
 */
typedef void (*el_send_bytes_t)(const uint8_t *data, size_t len);

/**
 * Parser state machine states
 */
typedef enum {
    EL_STATE_IDLE,          // Waiting for sync byte
    EL_STATE_GOT_SYNC,      // Got sync, waiting for msg_id
    EL_STATE_GOT_ID,        // Got msg_id, waiting for length
    EL_STATE_GOT_LEN,       // Got length, receiving payload
    EL_STATE_GOT_PAYLOAD,   // Got payload, waiting for CRC
} el_state_t;

/**
 * Etherlink instance context
 */
typedef struct {
    // Configuration
    el_on_message_t on_message;     // Message received callback
    el_send_bytes_t send_bytes;     // Byte transmission callback

    // Parser state
    el_state_t state;
    uint8_t msg_id;
    uint8_t payload_len;
    uint8_t payload_idx;
    uint8_t rx_buffer[EL_MAX_PAYLOAD];
    uint8_t running_crc;

    // Statistics
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t tx_frames;
} el_ctx_t;

/**
 * Configuration for el_init
 */
typedef struct {
    el_on_message_t on_message;     // Required: message callback
    el_send_bytes_t send_bytes;     // Required: transmit callback
} el_config_t;

/*******************************************************************************
 * Core API
 ******************************************************************************/

/**
 * Initialize Etherlink context
 * @param ctx Context to initialize
 * @param config Configuration parameters
 * @return true on success
 */
bool el_init(el_ctx_t *ctx, const el_config_t *config);

/**
 * Reset parser state (call on communication errors/reconnect)
 * @param ctx Context
 */
void el_reset(el_ctx_t *ctx);

/**
 * Process a received byte through the parser
 * @param ctx Context
 * @param byte Received byte
 */
void el_process_byte(el_ctx_t *ctx, uint8_t byte);

/**
 * Process multiple received bytes
 * @param ctx Context
 * @param data Pointer to received data
 * @param len Number of bytes
 */
void el_process_bytes(el_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * Send a message
 * @param ctx Context
 * @param msg_id Message type identifier
 * @param payload Payload data (can be NULL if len is 0)
 * @param len Payload length (0-250)
 * @return true on success, false if payload too large
 */
bool el_send(el_ctx_t *ctx, uint8_t msg_id, const void *payload, uint8_t len);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Calculate CRC-8/CCITT for data
 * @param data Data buffer
 * @param len Data length
 * @return CRC-8 value
 */
uint8_t el_crc8(const uint8_t *data, size_t len);

/**
 * Update running CRC with one byte
 * @param crc Current CRC value
 * @param byte New byte
 * @return Updated CRC
 */
uint8_t el_crc8_update(uint8_t crc, uint8_t byte);

/*******************************************************************************
 * Helper Macros for Message Definition
 ******************************************************************************/

/**
 * Define a packed struct for a message payload
 * Usage: EL_PACKED_STRUCT(my_msg_t, { int16_t x; int16_t y; });
 */
#define EL_PACKED_STRUCT(name, fields) \
    typedef struct __attribute__((packed)) fields name

/**
 * Send a typed message (with compile-time size check)
 * Usage: EL_SEND(ctx, MSG_ID, &my_struct);
 */
#define EL_SEND(ctx, msg_id, ptr) \
    el_send((ctx), (msg_id), (ptr), sizeof(*(ptr)))

/**
 * Cast received payload to a specific type (with size validation)
 * Usage: const my_msg_t *msg = EL_CAST(my_msg_t, payload, len);
 */
#define EL_CAST(type, payload, len) \
    ((len) >= sizeof(type) ? (const type *)(payload) : NULL)

#ifdef __cplusplus
}
#endif

#endif // ETHERLINK_H
