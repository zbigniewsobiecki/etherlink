/**
 * Etherlink BLE Transport
 *
 * BLE Nordic UART Service (NUS) transport for Etherlink protocol.
 * Uses NimBLE stack for lower memory footprint.
 *
 * MIT License - https://github.com/user/etherlink
 */

#ifndef ETHERLINK_BLE_H
#define ETHERLINK_BLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "etherlink.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Raw data callback type for transparent bridge mode
 */
typedef void (*el_ble_raw_rx_cb_t)(const uint8_t *data, size_t len);

/**
 * Connection event callback type
 */
typedef void (*el_ble_event_cb_t)(void);

/**
 * Configuration for Etherlink BLE transport
 */
typedef struct {
    const char *device_name;    // BLE device name (max 29 chars)
    el_ctx_t *protocol_ctx;     // Etherlink protocol context (auto-wires RX)
    el_ble_event_cb_t on_connect;    // Called on BLE connection (optional)
    el_ble_event_cb_t on_disconnect; // Called on BLE disconnect (optional)
} el_ble_config_t;

/**
 * Initialize BLE transport with Etherlink integration
 *
 * This initializes:
 * - NimBLE stack
 * - Nordic UART Service (NUS)
 * - Auto-advertising on disconnect
 *
 * If protocol_ctx is provided, received data will be automatically
 * passed to el_process_bytes(). You still need to set send_bytes
 * callback in the protocol config to el_ble_send_raw().
 *
 * @param config Configuration
 * @return ESP_OK on success
 */
esp_err_t el_ble_init(const el_ble_config_t *config);

/**
 * Send raw bytes over BLE (use as send_bytes callback)
 * @param data Data to send
 * @param len Length of data
 */
void el_ble_send_raw(const uint8_t *data, size_t len);

/**
 * Send data to connected BLE client
 * @param data Data to send
 * @param len Length of data (max ~240 bytes per call, MTU dependent)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t el_ble_send(const uint8_t *data, size_t len);

/**
 * Check if a client is connected
 * @return true if connected
 */
bool el_ble_is_connected(void);

/**
 * Get current MTU size
 * @return MTU size (default 23, can be up to 517 after negotiation)
 */
uint16_t el_ble_get_mtu(void);

/**
 * Get RSSI of current connection
 * @return RSSI in dBm (-127 to +20), or 127 if not connected/error
 */
int8_t el_ble_get_rssi(void);

/**
 * Set raw RX callback for transparent bridge mode
 *
 * When set, received BLE data is passed to this callback in addition to
 * (or instead of) the protocol parser. Useful for transparent bridges.
 *
 * @param cb Callback function, or NULL to disable
 */
void el_ble_set_raw_rx_callback(el_ble_raw_rx_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // ETHERLINK_BLE_H
