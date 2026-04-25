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
#include "esp_timer.h"
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

#include "driver/gpio.h"
#include "driver/uart.h"

#include "ble_midi.h"
#include "config_store.h"
#include "http_sb1.h"
#include "sb1_uart_rp2040.h"
#include "wifi_sb1.h"

#include "os/os_mbuf.h"

static const char *TAG = "musicbox";

/** Maximum duration of one BLE link (central connected); then we disconnect (HCI 0x13). */
#define SB1_BLE_CONN_LIMIT_S 60

static sb1_settings_t s_sb1;

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 8
#define UART_RX_PIN 9
#define UART_BAUD 115200

#ifndef MUSICBOX_UART_BRIDGE
#define MUSICBOX_UART_BRIDGE 1
#endif

/** Framed MIDI to RP2040: SOH + len + payload (see RP2040 sb1_link_task.c). */
#define SB1_UART_FRAME_SOH 0x01

/* RP2040 RX samples this line (ESP32 TX) for "partner powered" before enabling its UART TX.
 * Drive high before WiFi/BLE so the partner sees idle-high long before uart_driver_install. */
#if MUSICBOX_UART_BRIDGE
static void sb1_uart_bridge_tx_idle_high_early(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << UART_TX_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(UART_TX_PIN, 1);
}
#endif

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static esp_timer_handle_t s_ble_conn_limit_tm;
static uint16_t g_tx_val_handle;
static uint16_t g_rx_val_handle;
static uint16_t g_midi_val_handle;
static uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static ble_uuid_any_t ble_svc_uuid;
static ble_uuid_any_t ble_rx_uuid;
static ble_uuid_any_t ble_tx_uuid;
static ble_uuid_any_t ble_midi_svc_uuid;
static ble_uuid_any_t ble_midi_chr_uuid;

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static void ble_midi_uart_forward_cb(const uint8_t *msg, size_t msg_len, void *user)
{
    (void)user;
#if MUSICBOX_UART_BRIDGE
    if (msg_len == 0 || msg_len > 255) {
        return;
    }
    uint8_t hdr[2] = { SB1_UART_FRAME_SOH, (uint8_t)msg_len };
    uart_write_bytes(UART_NUM, hdr, 2);
    uart_write_bytes(UART_NUM, msg, msg_len);
#else
    (void)msg;
    (void)msg_len;
#endif
}

static int gatt_svr_midi_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t cap[2] = {0x80, 0x80};
        int rc = os_mbuf_append(ctxt->om, cap, sizeof(cap));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
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

    ESP_LOGI(TAG, "BLE MIDI RX %u bytes", (unsigned)len);
    ESP_LOG_BUFFER_HEX(TAG, buf, len);
    ble_midi_decode(buf, len, ble_midi_uart_forward_cb, NULL);
    return 0;
}

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
    ESP_LOG_BUFFER_HEX(TAG, buf, len);

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
        } else if (ble_uuid_cmp(u, &ble_midi_chr_uuid.u) == 0) {
            g_midi_val_handle = ctxt->chr.val_handle;
            ESP_LOGI(TAG, "registered BLE MIDI handle=%u", g_midi_val_handle);
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
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &ble_midi_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &ble_midi_chr_uuid.u,
                    .access_cb = gatt_svr_midi_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                             BLE_GATT_CHR_F_NOTIFY,
                    .arg = NULL,
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

/* NimBLE's ble_addr_to_str / BLE_ADDR_STR_LEN are not always visible in ESP-IDF 5.x headers. */
#define SB1_BLE_ADDR_STR_BYTES 24

static void sb1_ble_addr_to_str(const ble_addr_t *addr, char *out, size_t out_len)
{
    if (addr == NULL || out == NULL || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x", addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

#if MUSICBOX_UART_BRIDGE
/* RP2040 UART link: SB1BT,0 = disconnected; SB1BT,1,<name> = BLE peer (comma-free). */
static void sb1_uart_send_bt_status(bool connected, const char *name)
{
    char line[128];
    if (!connected) {
        snprintf(line, sizeof(line), "SB1BT,0\n");
    } else {
        const char *n = (name && name[0]) ? name : "CONNECTED";
        snprintf(line, sizeof(line), "SB1BT,1,%s\n", n);
    }
    uart_write_bytes(UART_NUM, line, strlen(line));
}
#else
static void sb1_uart_send_bt_status(bool connected, const char *name)
{
    (void)connected;
    (void)name;
}
#endif

#if MUSICBOX_UART_BRIDGE
void sb1_uart_send_wifi_status(bool sta_connected)
{
    char line[32];
    snprintf(line, sizeof(line), "SB1WF,%c\n", sta_connected ? '1' : '0');
    uart_write_bytes(UART_NUM, line, strlen(line));
}
#else
void sb1_uart_send_wifi_status(bool sta_connected)
{
    (void)sta_connected;
}
#endif

static void ble_conn_limit_timer_cb(void *arg)
{
    (void)arg;
    ble_hs_lock();
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "BLE session limit (%ds), terminating connection", SB1_BLE_CONN_LIMIT_S);
        /* HCI Remote User Terminated Connection */
        int rc = ble_gap_terminate(g_conn_handle, 0x13);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_gap_terminate rc=%d", rc);
        }
    }
    ble_hs_unlock();
}

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
            char addr_buf[SB1_BLE_ADDR_STR_BYTES];
            sb1_ble_addr_to_str(&desc.peer_id_addr, addr_buf, sizeof(addr_buf));
            sb1_uart_send_bt_status(true, addr_buf);
            if (s_ble_conn_limit_tm) {
                esp_timer_stop(s_ble_conn_limit_tm);
                esp_err_t te =
                    esp_timer_start_once(s_ble_conn_limit_tm, (uint64_t)SB1_BLE_CONN_LIMIT_S * 1000000ull);
                if (te != ESP_OK) {
                    ESP_LOGW(TAG, "conn limit timer: %s", esp_err_to_name(te));
                }
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        if (s_ble_conn_limit_tm) {
            esp_timer_stop(s_ble_conn_limit_tm);
        }
        sb1_uart_send_bt_status(false, NULL);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        musicbox_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "ATT MTU=%u", (unsigned)event->mtu.value);
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

    /* Legacy AD is 31 bytes max; two 128-bit UUIDs do not fit with flags. */
    fields.uuids128 = &ble_svc_uuid.u128;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed rc=%d", rc);
        return;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    name = s_sb1.device_name[0] ? s_sb1.device_name : "SB1 MIDI INTERFACE";
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
        ESP_LOGI(TAG, "advertising as %s", name);
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

#if MUSICBOX_UART_BRIDGE
/* UART → BLE MIDI notify: only when buffer starts with a MIDI status byte (avoids SB1BT text). */
static void uart_try_ble_midi_notify(const uint8_t *buf, int n)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_midi_val_handle == 0) {
        return;
    }
    if (n <= 0 || buf[0] < 0x80) {
        return;
    }
    size_t i = 0;
    while (i < (size_t)n) {
        uint8_t st = buf[i];
        size_t mlen = ble_midi_message_len(st);
        if (mlen == 0 || i + mlen > (size_t)n) {
            break;
        }
        uint8_t pkt[64];
        size_t plen = ble_midi_encode(pkt, sizeof(pkt), buf + i, mlen, ble_midi_timestamp_ms());
        if (plen == 0) {
            break;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, (uint16_t)plen);
        if (!om) {
            break;
        }
        int rc = ble_gatts_notify_custom(g_conn_handle, g_midi_val_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "uart->BLE MIDI notify rc=%d", rc);
        }
        i += mlen;
    }
}
#endif

static void uart_bridge_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];

    for (;;) {
        int n = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
        if (n <= 0) {
            continue;
        }
#if MUSICBOX_UART_BRIDGE
        uart_try_ble_midi_notify(buf, n);
#endif
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

#if MUSICBOX_UART_BRIDGE
    sb1_uart_bridge_tx_idle_high_early();
#endif

    sb1_settings_defaults(&s_sb1);
    ESP_ERROR_CHECK(sb1_settings_load(&s_sb1));

    sb1_wifi_init();
    ESP_ERROR_CHECK(sb1_http_start());

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
    if (ble_uuid_from_str(&ble_midi_svc_uuid, "03B80E5A-EDE8-4B33-A751-6CE34EC4C700") != 0) {
        ESP_LOGE(TAG, "bad BLE MIDI svc uuid");
        return;
    }
    if (ble_uuid_from_str(&ble_midi_chr_uuid, "7772E5DB-3868-4112-A1A9-F2669D106BE3") != 0) {
        ESP_LOGE(TAG, "bad BLE MIDI chr uuid");
        return;
    }

#if MUSICBOX_UART_BRIDGE
    uart_config_t u = {
        .baud_rate = (int)s_sb1.uart_baud ? (int)s_sb1.uart_baud : UART_BAUD,
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

    const esp_timer_create_args_t conn_limit = {
        .callback = ble_conn_limit_timer_cb,
        .name = "sb1_ble_conn_lim",
    };
    ESP_ERROR_CHECK(esp_timer_create(&conn_limit, &s_ble_conn_limit_tm));

    nimble_port_freertos_init(ble_host_task);
}
