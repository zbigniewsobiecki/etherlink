# Etherlink

A lightweight binary protocol for ESP32 communication over BLE or Serial UART.

Designed for embedded systems with:
- Minimal overhead (4 bytes per frame)
- CRC-8 error detection
- Up to 250 bytes payload
- Zero-copy callback architecture
- Transport-agnostic core

## Components

| Component | Description |
|-----------|-------------|
| `etherlink` | Core protocol (no dependencies) |
| `etherlink_ble` | BLE Nordic UART Service transport |
| `etherlink_uart` | Serial UART transport |

## Installation

### Using ESP Component Manager

```bash
idf.py add-dependency "etherlink"
idf.py add-dependency "etherlink_ble"  # Optional: for BLE
idf.py add-dependency "etherlink_uart" # Optional: for UART
```

### Using Local Path

In your project's `main/idf_component.yml`:

```yaml
dependencies:
  etherlink:
    path: "../etherlink/etherlink"
  etherlink_ble:
    path: "../etherlink/etherlink_ble"
```

## Quick Start (BLE)

```c
#include "etherlink.h"
#include "etherlink_ble.h"

// Protocol context
static el_ctx_t el_ctx;

// Message handler
static void on_message(uint8_t msg_id, const void *payload, uint8_t len) {
    switch (msg_id) {
        case 0x80:  // Example command
            // Handle command...
            break;
    }
}

void app_main(void) {
    // Initialize protocol with BLE send callback
    el_config_t el_config = {
        .on_message = on_message,
        .send_bytes = el_ble_send_raw,
    };
    el_init(&el_ctx, &el_config);

    // Initialize BLE transport (auto-wires received data to protocol)
    el_ble_config_t ble_config = {
        .device_name = "MyDevice",
        .protocol_ctx = &el_ctx,
    };
    el_ble_init(&ble_config);

    // Send telemetry
    while (1) {
        if (el_ble_is_connected()) {
            uint8_t data[] = {0x01, 0x02, 0x03};
            el_send(&el_ctx, 0x10, data, sizeof(data));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

## Quick Start (UART)

```c
#include "etherlink.h"
#include "etherlink_uart.h"

static el_ctx_t el_ctx;

static void on_message(uint8_t msg_id, const void *payload, uint8_t len) {
    // Handle messages...
}

void app_main(void) {
    // Initialize protocol
    el_config_t el_config = {
        .on_message = on_message,
        .send_bytes = el_uart_send_raw,
    };
    el_init(&el_ctx, &el_config);

    // Initialize UART transport
    el_uart_config_t uart_config = {
        .port = UART_NUM_1,
        .baud_rate = 115200,
        .tx_pin = 17,
        .rx_pin = 16,
        .protocol_ctx = &el_ctx,
    };
    el_uart_init(&uart_config);
}
```

## Protocol Specification

### Frame Format

```
[SYNC] [MSG_ID] [LENGTH] [PAYLOAD...] [CRC8]
 0xA5   1 byte   1 byte   0-250 bytes  1 byte
```

| Field | Size | Description |
|-------|------|-------------|
| SYNC | 1 | Frame start marker (0xA5) |
| MSG_ID | 1 | Message type identifier |
| LENGTH | 1 | Payload length (0-250) |
| PAYLOAD | 0-250 | User data |
| CRC8 | 1 | CRC-8/CCITT over MSG_ID + LENGTH + PAYLOAD |

### Message ID Conventions

| Range | Usage |
|-------|-------|
| 0x00-0x0F | System messages (ping, pong, version, error) |
| 0x10-0x7F | Telemetry (device → host) |
| 0x80-0xFE | Commands (host → device) |
| 0xFF | Reserved |

### Defining Message Payloads

```c
#include "etherlink.h"

// Define packed struct for your message
EL_PACKED_STRUCT(my_sensor_t, {
    uint32_t timestamp;
    int16_t temperature;  // 0.01°C units
    int16_t humidity;     // 0.01% units
});

// Send message
my_sensor_t sensor = {
    .timestamp = get_time_ms(),
    .temperature = 2350,  // 23.50°C
    .humidity = 6500,     // 65.00%
};
EL_SEND(&el_ctx, 0x10, &sensor);

// Receive message
void on_message(uint8_t msg_id, const void *payload, uint8_t len) {
    if (msg_id == 0x10) {
        const my_sensor_t *data = EL_CAST(my_sensor_t, payload, len);
        if (data) {
            printf("Temp: %.2f°C\n", data->temperature / 100.0f);
        }
    }
}
```

## API Reference

### Core Protocol (`etherlink.h`)

```c
// Initialize context
bool el_init(el_ctx_t *ctx, const el_config_t *config);

// Reset parser state
void el_reset(el_ctx_t *ctx);

// Process received bytes
void el_process_byte(el_ctx_t *ctx, uint8_t byte);
void el_process_bytes(el_ctx_t *ctx, const uint8_t *data, size_t len);

// Send message
bool el_send(el_ctx_t *ctx, uint8_t msg_id, const void *payload, uint8_t len);

// CRC utilities
uint8_t el_crc8(const uint8_t *data, size_t len);
```

### BLE Transport (`etherlink_ble.h`)

```c
esp_err_t el_ble_init(const el_ble_config_t *config);
void el_ble_send_raw(const uint8_t *data, size_t len);
esp_err_t el_ble_send(const uint8_t *data, size_t len);
bool el_ble_is_connected(void);
uint16_t el_ble_get_mtu(void);
```

### UART Transport (`etherlink_uart.h`)

```c
esp_err_t el_uart_init(const el_uart_config_t *config);
void el_uart_send_raw(const uint8_t *data, size_t len);
esp_err_t el_uart_send(const uint8_t *data, size_t len);
esp_err_t el_uart_deinit(void);
```

## License

MIT License - see [LICENSE](LICENSE)
