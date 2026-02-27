/*
 * Header nội bộ - Include và helper dùng chung cho OTA Manager
 */

#ifndef _OTA_COMMON_H_
#define _OTA_COMMON_H_

#include "ota_manager.h"

#include <cstring>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "cJSON.h"
#include "esp_mac.h"

extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

static const char *TAG = "OTA";

/// Buffer nhận HTTP response body
struct HttpResponseCtx {
    char* buf;
    int len;
    int max;
};

/// Event handler HTTP — đọc response vào buffer
static inline esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    auto* c = (HttpResponseCtx*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && c && c->buf) {
        int n = evt->data_len;
        if (c->len + n > c->max) n = c->max - c->len;
        if (n > 0) { memcpy(c->buf + c->len, evt->data, n); c->len += n; }
    }
    return ESP_OK;
}

/// Cấu hình SSL cho HTTP client
static inline void configure_ssl(esp_http_client_config_t& cfg, const std::string& cert_pem) {
    cfg.buffer_size = 2048;
    cfg.keep_alive_enable = true;
    if (!cert_pem.empty()) cfg.cert_pem = cert_pem.c_str();
    else cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check = false;
}

/// Lấy MAC WiFi Station dạng "aabbccddeeff"
static inline std::string GetMacString() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

/// So sánh semantic version ("1.2.3" vs "1.3.0")
static inline int CompareVersion(const std::string& a, const std::string& b) {
    int a1=0,a2=0,a3=0, b1=0,b2=0,b3=0;
    sscanf(a.c_str(), "%d.%d.%d", &a1, &a2, &a3);
    sscanf(b.c_str(), "%d.%d.%d", &b1, &b2, &b3);
    if (a1 != b1) return a1 - b1;
    if (a2 != b2) return a2 - b2;
    return a3 - b3;
}

#endif // _OTA_COMMON_H_
