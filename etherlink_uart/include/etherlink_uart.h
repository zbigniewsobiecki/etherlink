/**
 * Etherlink UART Transport
 *
 * Serial UART transport for Etherlink protocol.
 *
 * MIT License - https://github.com/user/etherlink
 */

#ifndef ETHERLINK_UART_H
#define ETHERLINK_UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "etherlink.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration for Etherlink UART transport
 */
typedef struct {
    uart_port_t port;           // UART port number (UART_NUM_0, UART_NUM_1, etc.)
    int baud_rate;              // Baud rate (e.g., 115200)
    int tx_pin;                 // TX GPIO pin (-1 for default)
    int rx_pin;                 // RX GPIO pin (-1 for default)
    el_ctx_t *protocol_ctx;     // Etherlink protocol context (auto-wires RX)
} el_uart_config_t;

/**
 * Initialize UART transport with Etherlink integration
 *
 * This initializes:
 * - ESP-IDF UART driver
 * - RX event task that feeds data to Etherlink parser
 *
 * If protocol_ctx is provided, received data will be automatically
 * passed to el_process_bytes(). You still need to set send_bytes
 * callback in the protocol config to el_uart_send_raw().
 *
 * @param config Configuration
 * @return ESP_OK on success
 */
esp_err_t el_uart_init(const el_uart_config_t *config);

/**
 * Send raw bytes over UART (use as send_bytes callback)
 * @param data Data to send
 * @param len Length of data
 */
void el_uart_send_raw(const uint8_t *data, size_t len);

/**
 * Send data over UART
 * @param data Data to send
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t el_uart_send(const uint8_t *data, size_t len);

/**
 * Deinitialize UART transport
 * @return ESP_OK on success
 */
esp_err_t el_uart_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // ETHERLINK_UART_H
