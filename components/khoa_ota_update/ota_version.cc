/*
 * OTA Version - Bước 1: Gọi server kiểm tra phiên bản mới
 * Gửi POST lên server với thông tin thiết bị, nhận JSON firmware info
 */

#include "ota_common.h"

/// Gọi POST lên server, gửi thông tin thiết bị, nhận version + firmware URL
esp_err_t OtaManager::FetchVersionInfo(VersionInfo& out_info) {
    std::string url = config_.url;
    if (url.empty()) return ESP_ERR_INVALID_ARG;

    std::string mac = GetMacString();
    std::string ver = GetCurrentVersion();

    // Lấy thông tin chip
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char* chip_name = "ESP32-xx";
    switch (chip.model) {
        case CHIP_ESP32:   chip_name = "ESP32";    break;
        case CHIP_ESP32S2: chip_name = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_name = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_name = "ESP32-C3"; break;
        case CHIP_ESP32C2: chip_name = "ESP32-C2"; break;
        case CHIP_ESP32H2: chip_name = "ESP32-H2"; break;
        default: break;
    }

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    // Tạo JSON body
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "mac",     mac.c_str());
    cJSON_AddStringToObject(body, "version", ver.c_str());
    cJSON_AddStringToObject(body, "chip",    chip_name);
    cJSON_AddNumberToObject(body, "cores",   chip.cores);
    cJSON_AddNumberToObject(body, "flash_kb", (double)(flash_size / 1024));
    cJSON_AddStringToObject(body, "app_name", esp_app_get_description()->project_name);
    char* body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_FAIL;

    ESP_LOGI(TAG, "[B1] POST %s", url.c_str());

    // HTTP client
    HttpResponseCtx ctx = {(char*)calloc(1, 2049), 0, 2048};
    if (!ctx.buf) { free(body_str); return ESP_ERR_NO_MEM; }

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = config_.timeout_ms;
    cfg.method = HTTP_METHOD_POST;
    cfg.max_redirection_count = 3;
    cfg.user_data = &ctx;
    cfg.event_handler = http_event_handler;
    configure_ssl(cfg, config_.cert_pem);

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body_str); free(ctx.buf); return ESP_FAIL; }

    // Header: dùng MAC làm Device-Id (bảo mật bằng MAC duy nhất)
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_post_field(client, body_str, (int)strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body_str);

    if (err != ESP_OK) { free(ctx.buf); return err; }
    if (status != 200 || ctx.len <= 0) {
        ESP_LOGE(TAG, "[B1] HTTP %d", status);
        free(ctx.buf);
        return ESP_FAIL;
    }

    ctx.buf[ctx.len] = '\0';
    ESP_LOGI(TAG, "[B1] Response: %s", ctx.buf);

    // Parse JSON: { firmware: { version, url, force } }
    cJSON* root = cJSON_Parse(ctx.buf);
    free(ctx.buf);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON* fw = cJSON_GetObjectItem(root, "firmware");
    bool ok = false;
    if (cJSON_IsObject(fw)) {
        cJSON* v = cJSON_GetObjectItem(fw, "version");
        if (v && cJSON_IsString(v)) { out_info.version = v->valuestring; ok = true; }

        cJSON* u = cJSON_GetObjectItem(fw, "url");
        if (u && cJSON_IsString(u)) out_info.firmware_url = u->valuestring;

        cJSON* f = cJSON_GetObjectItem(fw, "force");
        if (f && cJSON_IsNumber(f)) out_info.force = (f->valueint == 1);
    }
    cJSON_Delete(root);

    if (!ok) { ESP_LOGE(TAG, "[B1] Thieu firmware.version!"); return ESP_ERR_INVALID_RESPONSE; }

    ESP_LOGI(TAG, "[B1] Server: v%s | URL: %s | Force: %s",
             out_info.version.c_str(),
             out_info.firmware_url.empty() ? "(goc)" : out_info.firmware_url.c_str(),
             out_info.force ? "YES" : "NO");
    return ESP_OK;
}
