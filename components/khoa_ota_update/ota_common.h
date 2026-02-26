/*
 * Header nội bộ - Các include và helper dùng chung cho OTA Manager
 * File này KHÔNG public ra ngoài component
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

// Khai báo hàm C gắn bundle chứng chỉ SSL mặc định của ESP-IDF
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

// Tag dùng chung cho tất cả log OTA
static const char *TAG = "OTA_MGR";

/// Struct buffer nhận HTTP response body qua event handler
struct HttpResponseCtx {
    char* buf;
    int len;
    int max;
};

/// Event handler chung cho HTTP client - đọc response body vào buffer
static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    auto* c = (HttpResponseCtx*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && c && c->buf) {
        int copy = evt->data_len;
        if (c->len + copy > c->max) copy = c->max - c->len;
        if (copy > 0) {
            memcpy(c->buf + c->len, evt->data, copy);
            c->len += copy;
        }
    }
    return ESP_OK;
}

/// Cấu hình SSL cho esp_http_client_config_t
static void configure_ssl(esp_http_client_config_t& cfg, const std::string& cert_pem) {
    cfg.buffer_size = 2048;
    // Bật keep-alive
    cfg.keep_alive_enable = true;
    
    if (!cert_pem.empty()) {
        cfg.cert_pem = cert_pem.c_str();
    } else {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    // Gửi SNI (Server Name Indication) mặc định, để certificate validator tự check (Tránh các cloud services dập liên kết)
    cfg.skip_cert_common_name_check = false;
}

/// Lấy MAC address WiFi Station dạng chuỗi hex "aabbccddeeff"
static std::string GetMacString() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

/// So sánh 2 chuỗi version theo semantic versioning (VD: "1.2.3" vs "1.3.0")
/// Trả về: >0 nếu a > b, 0 nếu a == b, <0 nếu a < b
static inline int CompareVersion(const std::string& a, const std::string& b) {
    int a_major = 0, a_minor = 0, a_patch = 0;
    int b_major = 0, b_minor = 0, b_patch = 0;

    sscanf(a.c_str(), "%d.%d.%d", &a_major, &a_minor, &a_patch);
    sscanf(b.c_str(), "%d.%d.%d", &b_major, &b_minor, &b_patch);

    if (a_major != b_major) return a_major - b_major;
    if (a_minor != b_minor) return a_minor - b_minor;
    return a_patch - b_patch;
}

#endif // _OTA_COMMON_H_
