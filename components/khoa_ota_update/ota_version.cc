/*
 * OTA Version - Bước 1: Kiểm tra phiên bản từ server
 * GET /version.json → so sánh với firmware hiện tại
 */

#include "ota_common.h"

// ==================== So sánh phiên bản ====================

/// So sánh 2 chuỗi version theo semantic versioning (VD: "1.2.3" vs "1.3.0")
/// Trả về: >0 nếu a > b, 0 nếu a == b, <0 nếu a < b
int OtaManager::CompareVersion(const std::string& a, const std::string& b) {
    int a_major = 0, a_minor = 0, a_patch = 0;
    int b_major = 0, b_minor = 0, b_patch = 0;

    sscanf(a.c_str(), "%d.%d.%d", &a_major, &a_minor, &a_patch);
    sscanf(b.c_str(), "%d.%d.%d", &b_major, &b_minor, &b_patch);

    if (a_major != b_major) return a_major - b_major;
    if (a_minor != b_minor) return a_minor - b_minor;
    return a_patch - b_patch;
}

// ==================== Bước 1: Lấy thông tin version từ server ====================

/// Gọi GET /version.json để lấy version mới nhất
esp_err_t OtaManager::FetchVersionInfo(VersionInfo& out_info) {
    // Ghép URL: base_url + /version.json?mac=xxx&v=yyy
    std::string version_url = config_.url;
    if (!version_url.empty() && version_url.back() == '/') {
        version_url.pop_back();
    }
    std::string current_ver = GetCurrentVersion();
    version_url += "/version.json?mac=" + GetMacString() + "&v=" + current_ver;

    ESP_LOGI(TAG, "[B1] Kiem tra phien ban tu: %s", version_url.c_str());

    // Buffer nhận response body
    HttpResponseCtx ctx = {};
    ctx.max = 1024;
    ctx.buf = (char*)calloc(1, ctx.max + 1);
    if (!ctx.buf) return ESP_ERR_NO_MEM;

    // Cấu hình HTTP client
    esp_http_client_config_t http_config = {};
    http_config.url = version_url.c_str();
    http_config.timeout_ms = config_.timeout_ms;
    http_config.method = HTTP_METHOD_GET;
    http_config.max_redirection_count = 3;
    http_config.user_data = &ctx;
    http_config.event_handler = http_event_handler;
    configure_ssl(http_config, config_.cert_pem);

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Khong the khoi tao HTTP client!");
        free(ctx.buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request that bai: %s", esp_err_to_name(err));
        free(ctx.buf);
        return err;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "Server tra ve HTTP %d khi lay version!", status_code);
        free(ctx.buf);
        return ESP_FAIL;
    }

    if (ctx.len <= 0) {
        ESP_LOGE(TAG, "Khong doc duoc du lieu version!");
        free(ctx.buf);
        return ESP_FAIL;
    }

    ctx.buf[ctx.len] = '\0';
    ESP_LOGI(TAG, "Response version: %s", ctx.buf);

    // Parse JSON: {"version": "1.2.0"}
    cJSON* root = cJSON_Parse(ctx.buf);
    free(ctx.buf);

    if (root == nullptr) {
        ESP_LOGE(TAG, "Loi parse JSON version!");
        return ESP_FAIL;
    }

    cJSON* ver_item = cJSON_GetObjectItem(root, "version");
    if (ver_item && cJSON_IsString(ver_item)) {
        out_info.version = ver_item->valuestring;
    } else {
        ESP_LOGE(TAG, "Thieu truong 'version' trong JSON!");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "[B1] Phien ban server: %s", out_info.version.c_str());
    return ESP_OK;
}
