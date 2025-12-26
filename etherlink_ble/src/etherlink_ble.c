/**
 * Etherlink BLE Transport - Implementation
 *
 * Uses NimBLE stack for lower memory footprint.
 *
 * MIT License - https://github.com/user/etherlink
 */

#include "etherlink_ble.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "el_ble";

// Nordic UART Service UUIDs (128-bit)
// Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX Char: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E (write)
// TX Char: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E (notify)

static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t nus_tx_handle;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t current_mtu = 23;
static el_ctx_t *protocol_ctx = NULL;
static uint8_t own_addr_type;

// Forward declarations
static int nus_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static void el_ble_advertise(void);

// GATT service definition
static const struct ble_gatt_svc_def nus_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // RX characteristic (client writes to this)
                .uuid = &nus_rx_uuid.u,
                .access_cb = nus_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // TX characteristic (server notifies on this)
                .uuid = &nus_tx_uuid.u,
                .access_cb = nus_chr_access,
                .val_handle = &nus_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, // Terminator
        },
    },
    { 0 }, // Terminator
};

static int nus_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Data received from client
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t buf[512];

        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }

        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        if (rc == 0 && protocol_ctx) {
            // Auto-wire to Etherlink protocol parser
            el_process_bytes(protocol_ctx, buf, len);
        }
    }
    return 0;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Connected, handle=%d", conn_handle);
            } else {
                conn_handle = BLE_HS_CONN_HANDLE_NONE;
                el_ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            current_mtu = 23;
            // Reset protocol parser on disconnect
            if (protocol_ctx) {
                el_reset(protocol_ctx);
            }
            el_ble_advertise();
            break;

        case BLE_GAP_EVENT_MTU:
            current_mtu = event->mtu.value;
            ESP_LOGI(TAG, "MTU updated to %d", current_mtu);
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            el_ble_advertise();
            break;
    }
    return 0;
}

static void el_ble_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    // Flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include TX power
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // Include device name
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return;
    }

    // Set scan response with service UUID
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&nus_svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan rsp: %d", rc);
    }

    // Start advertising
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE reset, reason=%d", reason);
}

static void ble_on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    el_ble_advertise();
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t el_ble_init(const el_ble_config_t *config) {
    if (!config || !config->device_name) {
        return ESP_ERR_INVALID_ARG;
    }

    // Suppress verbose NimBLE logging
    esp_log_level_set("NimBLE", ESP_LOG_ERROR);

    protocol_ctx = config->protocol_ctx;

    // Initialize NVS (required for BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure host
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;

    // Set device name
    int rc = ble_svc_gap_device_name_set(config->device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
    }

    // Initialize GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(nus_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT config: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(nus_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return ESP_FAIL;
    }

    // Start host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "Etherlink BLE initialized, device: %s", config->device_name);
    return ESP_OK;
}

void el_ble_send_raw(const uint8_t *data, size_t len) {
    el_ble_send(data, len);
}

esp_err_t el_ble_send(const uint8_t *data, size_t len) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, nus_tx_handle, om);
    if (rc != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool el_ble_is_connected(void) {
    return conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

uint16_t el_ble_get_mtu(void) {
    return current_mtu;
}

int8_t el_ble_get_rssi(void) {
    int8_t rssi = 127;  // Invalid value
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_conn_rssi(conn_handle, &rssi);
    }
    return rssi;
}
