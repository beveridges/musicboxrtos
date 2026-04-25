#include "http_sb1.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_sb1.h"

static const char *TAG = "sb1_http";

static httpd_handle_t s_srv;

static esp_err_t send_json(httpd_req_t *req, bool ok, cJSON *obj)
{
    char *txt = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!txt) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_status(req, ok ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, txt, HTTPD_RESP_USE_STRLEN);
    cJSON_free(txt);
    return err;
}

static esp_err_t h_get_config(httpd_req_t *req)
{
    sb1_settings_t s;
    sb1_settings_defaults(&s);
    esp_err_t e = sb1_settings_load(&s);
    if (e != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", esp_err_to_name(e));
        return send_json(req, false, err);
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "sta_ssid", s.sta_ssid);
    cJSON_AddBoolToObject(o, "sta_pass_set", s.sta_pass[0] != '\0');
    cJSON_AddStringToObject(o, "device_name", s.device_name);
    cJSON_AddNumberToObject(o, "uart_baud", (double)s.uart_baud);
    return send_json(req, true, o);
}

static bool merge_json_settings(cJSON *root, sb1_settings_t *s, char *errbuf, size_t errlen)
{
    cJSON *j;

    j = cJSON_GetObjectItem(root, "sta_ssid");
    if (j != NULL) {
        if (!cJSON_IsString(j)) {
            snprintf(errbuf, errlen, "sta_ssid must be a string");
            return false;
        }
        strncpy(s->sta_ssid, j->valuestring, sizeof(s->sta_ssid) - 1);
        s->sta_ssid[sizeof(s->sta_ssid) - 1] = '\0';
    }

    j = cJSON_GetObjectItem(root, "sta_pass");
    if (j != NULL) {
        if (!cJSON_IsString(j)) {
            snprintf(errbuf, errlen, "sta_pass must be a string");
            return false;
        }
        strncpy(s->sta_pass, j->valuestring, sizeof(s->sta_pass) - 1);
        s->sta_pass[sizeof(s->sta_pass) - 1] = '\0';
    }

    j = cJSON_GetObjectItem(root, "device_name");
    if (j != NULL) {
        if (!cJSON_IsString(j)) {
            snprintf(errbuf, errlen, "device_name must be a string");
            return false;
        }
        if (j->valuestring[0] == '\0') {
            snprintf(errbuf, errlen, "device_name must not be empty");
            return false;
        }
        strncpy(s->device_name, j->valuestring, sizeof(s->device_name) - 1);
        s->device_name[sizeof(s->device_name) - 1] = '\0';
    }

    j = cJSON_GetObjectItem(root, "uart_baud");
    if (j != NULL) {
        if (!cJSON_IsNumber(j)) {
            snprintf(errbuf, errlen, "uart_baud must be a number");
            return false;
        }
        double bd = cJSON_GetNumberValue(j);
        if (bd < 1200.0 || bd > 2000000.0) {
            snprintf(errbuf, errlen, "uart_baud out of range");
            return false;
        }
        s->uart_baud = (uint32_t)bd;
    }

    return true;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static esp_err_t h_put_config(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "empty body");
        return send_json(req, false, err);
    }
    if (len > 512) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "body too large");
        return send_json(req, false, err);
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    int r = httpd_req_recv(req, buf, len);
    if (r <= 0) {
        free(buf);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "recv failed");
        return send_json(req, false, err);
    }
    buf[r] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "invalid JSON");
        return send_json(req, false, err);
    }

    sb1_settings_t s;
    sb1_settings_defaults(&s);
    esp_err_t le = sb1_settings_load(&s);
    if (le != ESP_OK) {
        cJSON_Delete(root);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", esp_err_to_name(le));
        return send_json(req, false, err);
    }

    char emsg[96];
    emsg[0] = '\0';
    if (!merge_json_settings(root, &s, emsg, sizeof(emsg))) {
        cJSON_Delete(root);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", emsg);
        return send_json(req, false, err);
    }
    cJSON_Delete(root);

    le = sb1_settings_save(&s);
    if (le != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", esp_err_to_name(le));
        return send_json(req, false, err);
    }

    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "saved", true);
    cJSON_AddNumberToObject(ok, "restart_in_ms", 900);
    esp_err_t se = send_json(req, true, ok);
    if (se == ESP_OK) {
        if (xTaskCreate(restart_task, "sb1_rst", 2048, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "restart task create failed");
        }
    }
    return se;
}

static esp_err_t h_get_status(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "sta_connected", sb1_wifi_sta_got_ip());

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
            char buf[20];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(o, "sta_ip", buf);
        }
    }

    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
            char buf[20];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(o, "ap_ip", buf);
        }
    }

    cJSON_AddNumberToObject(o, "uptime_ms", (double)(esp_timer_get_time() / 1000));
    return send_json(req, true, o);
}

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>SB1</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:36rem;margin:2rem auto;padding:0 1rem;"
    "background:#111;color:#eee}label{display:block;margin:.75rem 0}input{width:100%;"
    "padding:.4rem;background:#222;border:1px solid #444;color:#eee;border-radius:4px}"
    "button{margin-top:1rem;padding:.5rem 1rem;background:#2a6;border:0;color:#fff;"
    "border-radius:4px;cursor:pointer}#st{font-size:.85rem;color:#888;margin-top:1rem}"
    "code{font-size:.85rem}</style></head><body><h1>SB1 config</h1>"
    "<p>JSON API: <code>GET/PUT /api/v1/config</code>, <code>GET /api/v1/status</code></p>"
    "<form id=\"f\"><label>Wi-Fi SSID (STA)<input id=\"sta_ssid\" autocomplete=\"off\"></label>"
    "<label>Wi-Fi password <input id=\"sta_pass\" type=\"password\" "
    "placeholder=\"leave blank to keep\"></label>"
    "<label>Device / BLE name <input id=\"device_name\" maxlength=\"31\"></label>"
    "<label>UART baud <input id=\"uart_baud\" type=\"number\"></label>"
    "<button type=\"submit\">Save</button></form><div id=\"st\"></div><script>"
    "async function load(){const r=await fetch('/api/v1/config');const j=await r.json();"
    "sta_ssid.value=j.sta_ssid||'';sta_pass.placeholder=j.sta_pass_set?'(saved)':'';"
    "device_name.value=j.device_name||'';uart_baud.value=j.uart_baud||115200;}"
    "f.onsubmit=async e=>{e.preventDefault();st.textContent='Saving…';"
    "const b={sta_ssid:sta_ssid.value,device_name:device_name.value,uart_baud:+uart_baud.value};"
    "if(sta_pass.value)b.sta_pass=sta_pass.value;"
    "const r=await fetch('/api/v1/config',{method:'PUT',headers:"
    "{'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "const j=await r.json();st.textContent=j.saved?'Saved. Rebooting…':(j.error||'Error');};"
    "load();</script></body></html>";

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sb1_http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;

    esp_err_t err = httpd_start(&s_srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t u_index = {.uri = "/", .method = HTTP_GET, .handler = h_index, .user_ctx = NULL};
    httpd_uri_t u_gc = {
        .uri = "/api/v1/config", .method = HTTP_GET, .handler = h_get_config, .user_ctx = NULL};
    httpd_uri_t u_pc = {
        .uri = "/api/v1/config", .method = HTTP_PUT, .handler = h_put_config, .user_ctx = NULL};
    httpd_uri_t u_st = {
        .uri = "/api/v1/status", .method = HTTP_GET, .handler = h_get_status, .user_ctx = NULL};

    httpd_register_uri_handler(s_srv, &u_index);
    httpd_register_uri_handler(s_srv, &u_gc);
    httpd_register_uri_handler(s_srv, &u_pc);
    httpd_register_uri_handler(s_srv, &u_st);

    ESP_LOGI(TAG, "HTTP server on port %d", cfg.server_port);
    return ESP_OK;
}
