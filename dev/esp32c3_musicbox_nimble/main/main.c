/*
 * ESP32-C3: NimBLE peripheral, Nordic UART Service, optional UART1 bridge to RP2040.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "driver/uart.h"

static const char *TAG = "musicbox";

#define DEVICE_NAME "SB1 MIDI INTERFACE"

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 8
#define UART_RX_PIN 9
#define UART_BAUD 115200

#ifndef MUSICBOX_UART_BRIDGE
#define MUSICBOX_UART_BRIDGE 1
#endif

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_tx_val_handle;
static uint16_t g_rx_val_handle;
static uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static ble_uuid_any_t ble_svc_uuid;
static ble_uuid_any_t ble_rx_uuid;
static ble_uuid_any_t ble_tx_uuid;

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    int which = (int)(intptr_t)arg;

    if (which == 2) {
        return 0;
    }

    if (which != 1) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->om == NULL) {
        return 0;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) {
        return 0;
    }

    uint8_t buf[128];
    if (len > sizeof(buf)) {
        len = sizeof(buf);
    }
    int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "NUS RX %u bytes", (unsigned)len);
    esp_log_buffer_hex(TAG, buf, len);

#if MUSICBOX_UART_BRIDGE
    uart_write_bytes(UART_NUM, buf, len);
#endif

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_tx_val_handle != 0) {
        const uint8_t ack[] = {'O', 'K'};
        struct os_mbuf *om = ble_hs_mbuf_from_flat(ack, sizeof(ack));
        if (om) {
            rc = ble_gatts_notify_custom(g_conn_handle, g_tx_val_handle, om);
            if (rc != 0) {
                ESP_LOGW(TAG, "notify failed rc=%d", rc);
            }
        }
    }

    return 0;
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
        const ble_uuid_t *u = ctxt->chr.chr_def->uuid;
        if (ble_uuid_cmp(u, &ble_rx_uuid.u) == 0) {
            g_rx_val_handle = ctxt->chr.val_handle;
            ESP_LOGI(TAG, "registered RX handle=%u", g_rx_val_handle);
        } else if (ble_uuid_cmp(u, &ble_tx_uuid.u) == 0) {
            g_tx_val_handle = ctxt->chr.val_handle;
            ESP_LOGI(TAG, "registered TX handle=%u", g_tx_val_handle);
        }
    }
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &ble_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &ble_rx_uuid.u,
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .arg = (void *)(intptr_t)1,
                },
                {
                    .uuid = &ble_tx_uuid.u,
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .arg = (void *)(intptr_t)2,
                },
                {
                    0,
                },
            },
    },
    {
        0,
    },
};

static void musicbox_advertise(void);

static int musicbox_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            g_conn_handle = event->connect.conn_handle;
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        musicbox_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
    case BLE_GAP_EVENT_MTU:
        return 0;

    default:
        return 0;
    }
}

static void musicbox_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.uuids128 = &ble_svc_uuid.u128;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed rc=%d", rc);
        return;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    name = DEVICE_NAME;
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           musicbox_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as %s", DEVICE_NAME);
    }
}

static void ble_on_sync(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);

    musicbox_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "resetting state; reason=%d", reason);
}

static void uart_bridge_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];

    for (;;) {
        int n = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
        if (n <= 0) {
            continue;
        }
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_tx_val_handle == 0) {
            continue;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, (uint16_t)n);
        if (!om) {
            continue;
        }
        int rc = ble_gatts_notify_custom(g_conn_handle, g_tx_val_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "uart->notify rc=%d", rc);
        }
    }
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (ble_uuid_from_str(&ble_svc_uuid, "6E400001-B5A3-F393-E0A9-E50E24DCCA9E") != 0) {
        ESP_LOGE(TAG, "bad svc uuid");
        return;
    }
    if (ble_uuid_from_str(&ble_rx_uuid, "6E400002-B5A3-F393-E0A9-E50E24DCCA9E") != 0) {
        ESP_LOGE(TAG, "bad rx uuid");
        return;
    }
    if (ble_uuid_from_str(&ble_tx_uuid, "6E400003-B5A3-F393-E0A9-E50E24DCCA9E") != 0) {
        ESP_LOGE(TAG, "bad tx uuid");
        return;
    }

#if MUSICBOX_UART_BRIDGE
    uart_config_t u = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, 512, 512, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &u));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    xTaskCreate(uart_bridge_task, "uart_nus", 4096, NULL, 5, NULL);
#endif

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    nimble_port_freertos_init(ble_host_task);
}
