/**
 * Etherlink UART Transport - Implementation
 *
 * MIT License - https://github.com/user/etherlink
 */

#include "etherlink_uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "el_uart";

#define UART_RX_BUF_SIZE    1024
#define UART_TX_BUF_SIZE    512
#define RX_TASK_STACK_SIZE  2048
#define RX_TASK_PRIORITY    10

static uart_port_t uart_port = UART_NUM_1;
static el_ctx_t *protocol_ctx = NULL;
static TaskHandle_t rx_task_handle = NULL;
static bool initialized = false;

static void uart_rx_task(void *arg) {
    uint8_t *data = malloc(UART_RX_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UART RX task started");

    while (1) {
        int len = uart_read_bytes(uart_port, data, UART_RX_BUF_SIZE,
                                   pdMS_TO_TICKS(100));
        if (len > 0 && protocol_ctx) {
            el_process_bytes(protocol_ctx, data, len);
        }
    }

    free(data);
    vTaskDelete(NULL);
}

esp_err_t el_uart_init(const el_uart_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uart_port = config->port;
    protocol_ctx = config->protocol_ctx;

    // UART configuration
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(uart_port, UART_RX_BUF_SIZE,
                                         UART_TX_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(uart_port);
        return ret;
    }

    // Set pins if specified
    int tx_pin = config->tx_pin >= 0 ? config->tx_pin : UART_PIN_NO_CHANGE;
    int rx_pin = config->rx_pin >= 0 ? config->rx_pin : UART_PIN_NO_CHANGE;

    ret = uart_set_pin(uart_port, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(uart_port);
        return ret;
    }

    // Create RX task
    BaseType_t task_ret = xTaskCreate(uart_rx_task, "el_uart_rx",
                                       RX_TASK_STACK_SIZE, NULL,
                                       RX_TASK_PRIORITY, &rx_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        uart_driver_delete(uart_port);
        return ESP_FAIL;
    }

    initialized = true;
    ESP_LOGI(TAG, "Etherlink UART initialized on port %d, baud %d",
             uart_port, config->baud_rate);

    return ESP_OK;
}

void el_uart_send_raw(const uint8_t *data, size_t len) {
    el_uart_send(data, len);
}

esp_err_t el_uart_send(const uint8_t *data, size_t len) {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = uart_write_bytes(uart_port, data, len);
    if (written < 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t el_uart_deinit(void) {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (rx_task_handle) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
    }

    esp_err_t ret = uart_driver_delete(uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    protocol_ctx = NULL;
    initialized = false;

    ESP_LOGI(TAG, "Etherlink UART deinitialized");
    return ESP_OK;
}
