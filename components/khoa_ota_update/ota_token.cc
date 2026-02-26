/*
 * OTA Token - Bước 2: Xác thực token + polling chờ admin duyệt
 * POST /validate-token → polling GET /token-status
 */

#include "ota_common.h"

// ==================== Hash FNV-1a 64-bit ====================

/// Hash chuỗi token + MAC bằng FNV-1a 64-bit → mỗi thiết bị có hash duy nhất
std::string OtaManager::HashToken64(const std::string& token) {
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET;
    for (char c : token) {
        hash ^= (uint8_t)c;
        hash *= FNV_PRIME;
    }

    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
    return std::string(hex);
}

// ==================== Polling trạng thái duyệt ====================

/// Gửi GET /token-status?mac=xxx kiểm tra trạng thái duyệt
/// Trả về: ESP_OK = approved, ESP_ERR_INVALID_RESPONSE = denied, ESP_ERR_NOT_FINISHED = pending
static esp_err_t PollTokenStatus(const std::string& base_url, const std::string& mac,
                                  int timeout_ms, const std::string& cert_pem, std::string& out_firmware_url) {
    std::string poll_url = base_url;
    if (!poll_url.empty() && poll_url.back() == '/') poll_url.pop_back();
    poll_url += "/token-status?mac=" + mac;

    // Cấu hình HTTP client
    esp_http_client_config_t http_config = {};
    http_config.url = poll_url.c_str();
    http_config.timeout_ms = timeout_ms;
    http_config.method = HTTP_METHOD_GET;
    http_config.max_redirection_count = 3;

    HttpResponseCtx ctx = {};
    ctx.max = 256;
    ctx.buf = (char*)calloc(1, ctx.max + 1);
    if (!ctx.buf) return ESP_ERR_NO_MEM;

    http_config.user_data = &ctx;
    http_config.event_handler = http_event_handler;
    configure_ssl(http_config, cert_pem);

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        free(ctx.buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200 || ctx.len <= 0) {
        free(ctx.buf);
        return ESP_FAIL;
    }

    ctx.buf[ctx.len] = '\0';

    // Parse JSON: {"status": "pending"/"approved"/"denied"}
    cJSON* root = cJSON_Parse(ctx.buf);
    free(ctx.buf);
    if (root == nullptr) return ESP_FAIL;

    cJSON* status_item = cJSON_GetObjectItem(root, "status");
    esp_err_t result = ESP_FAIL;

    if (status_item && cJSON_IsString(status_item)) {
        if (strcmp(status_item->valuestring, "approved") == 0) {
            result = ESP_OK;
        } else if (strcmp(status_item->valuestring, "denied") == 0) {
            result = ESP_ERR_INVALID_RESPONSE;
        } else if (strcmp(status_item->valuestring, "pending") == 0) {
            result = ESP_ERR_NOT_FINISHED;
        }
    }

    // Đọc firmware URL nếu được duyệt
    cJSON* url_item = cJSON_GetObjectItem(root, "firmware_url");
    if (result == ESP_OK && url_item && cJSON_IsString(url_item)) {
        out_firmware_url = url_item->valuestring;
    }

    cJSON_Delete(root);
    return result;
}

// ==================== Bước 2: Đăng ký + Polling ====================

/// Gửi POST đăng ký thiết bị, sau đó polling chờ admin duyệt trên web
esp_err_t OtaManager::ValidateToken() {
    std::string device_token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        device_token = config_.device_token;
    }

    std::string mac = GetMacString();
    std::string token_hash;

    if (device_token.empty()) {
        ESP_LOGW(TAG, "[B2] Chua co VIBO-KEY. Gui MAC len server cho admin kich hoat...");
        token_hash = "";
    } else {
        std::string combined = device_token + mac;
        token_hash = HashToken64(combined);
        ESP_LOGI(TAG, "[B2] MAC: %s | Hash(KEY+MAC): %s", mac.c_str(), token_hash.c_str());
    }

    // ===== Bước 2a: POST /validate-token đăng ký yêu cầu =====
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char* chip_name = "Unknown";
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_name = "ESP32";   break;
        case CHIP_ESP32S2: chip_name = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_name = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_name = "ESP32-C3"; break;
        case CHIP_ESP32C2: chip_name = "ESP32-C2"; break;
        case CHIP_ESP32H2: chip_name = "ESP32-H2"; break;
        default:           chip_name = "ESP32-xx"; break;
    }
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    const esp_app_desc_t* app_desc = esp_app_get_description();

    // Tạo JSON body
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "token_hash", token_hash.c_str());
    cJSON_AddStringToObject(body, "mac", mac.c_str());
    cJSON_AddStringToObject(body, "chip", chip_name);
    cJSON_AddNumberToObject(body, "cores", chip_info.cores);
    cJSON_AddNumberToObject(body, "flash_kb", flash_size / 1024);
    cJSON_AddStringToObject(body, "app_name", app_desc->project_name);
    cJSON_AddStringToObject(body, "app_version", app_desc->version);
    cJSON_AddStringToObject(body, "idf_version", app_desc->idf_ver);
    char* body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (body_str == nullptr) {
        ESP_LOGE(TAG, "[B2] Khong the tao JSON body!");
        return ESP_FAIL;
    }

    int body_len = strlen(body_str);
    ESP_LOGI(TAG, "[B2] POST /validate-token: %s", body_str);

    std::string validate_url = config_.url;
    if (!validate_url.empty() && validate_url.back() == '/') validate_url.pop_back();
    validate_url += "/validate-token";

    // Cấu hình HTTP client cho POST
    esp_http_client_config_t http_config = {};
    http_config.url = validate_url.c_str();
    http_config.timeout_ms = config_.timeout_ms;
    http_config.method = HTTP_METHOD_POST;
    http_config.max_redirection_count = 3;
    configure_ssl(http_config, config_.cert_pem);

    HttpResponseCtx ctx = {};
    ctx.max = 512;
    ctx.buf = (char*)calloc(1, ctx.max + 1);
    
    if (ctx.buf) {
        http_config.user_data = &ctx;
        http_config.event_handler = http_event_handler;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        free(body_str);
        if (ctx.buf) free(ctx.buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    
    free(body_str);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "[B2] Dang ky: HTTP %d", status_code);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[B2] Khong the ket noi server: %s", esp_err_to_name(err));
        if (ctx.buf) free(ctx.buf);
        return err;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "[B2] Server tu choi dang ky: HTTP %d", status_code);
        if (ctx.buf) free(ctx.buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Parse response
    char* resp_buf = ctx.buf;
    int resp_read = ctx.len;
    if (resp_buf) resp_buf[resp_read] = '\0';

    if (resp_buf && resp_read > 0) {
        cJSON* resp_json = cJSON_Parse(resp_buf);
        free(resp_buf);
        if (resp_json) {
            cJSON* st = cJSON_GetObjectItem(resp_json, "status");
            if (st && cJSON_IsString(st)) {
                if (strcmp(st->valuestring, "denied") == 0) {
                    ESP_LOGE(TAG, "[B2] Token KHONG HOP LE! Server tu choi.");
                    cJSON_Delete(resp_json);
                    return ESP_ERR_INVALID_RESPONSE;
                }
                ESP_LOGI(TAG, "[B2] Server response: status=%s", st->valuestring);
                if (strcmp(st->valuestring, "approved") == 0) {
                     cJSON* url_item = cJSON_GetObjectItem(resp_json, "firmware_url");
                     if (url_item && cJSON_IsString(url_item)) {
                          std::lock_guard<std::mutex> lock(mutex_);
                          config_.url = url_item->valuestring;
                          ESP_LOGI(TAG, "[B2] Nhan duoc firmware URL tu server: %s", config_.url.c_str());
                          cJSON_Delete(resp_json);
                          return ESP_OK;
                     }
                }
            }
            cJSON_Delete(resp_json);
        }
    } else {
        free(resp_buf);
    }

    // ===== Bước 2b: Polling chờ admin duyệt =====
    ESP_LOGI(TAG, "[B2] Da dang ky. Cho admin duyet (polling moi %dms, timeout %dms)...",
             config_.poll_interval_ms, config_.approval_timeout_ms);

    int elapsed_ms = 0;
    int poll_count = 0;

    while (elapsed_ms < config_.approval_timeout_ms) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abort_requested_) {
                ESP_LOGW(TAG, "[B2] Huy boi nguoi dung!");
                return ESP_ERR_OTA_ROLLBACK_FAILED;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
        elapsed_ms += config_.poll_interval_ms;
        poll_count++;

        NotifyProgress(OtaState::ValidatingToken, 0, 0, 0, "Cho admin duyet...");

        std::string fw_url;
        esp_err_t poll_ret = PollTokenStatus(config_.url, mac, config_.timeout_ms, config_.cert_pem, fw_url);

        if (poll_ret == ESP_OK) {
            ESP_LOGI(TAG, "[B2] Admin DA DUYET! Cho phep download.");
            if (!fw_url.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                config_.url = fw_url;
                ESP_LOGI(TAG, "[B2] Nhan duoc firmware URL tu polling: %s", config_.url.c_str());
            }
            return ESP_OK;
        } else if (poll_ret == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGE(TAG, "[B2] Admin TU CHOI! Huy cap nhat.");
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (poll_count % 6 == 0) {
            ESP_LOGI(TAG, "[B2] Van dang cho admin duyet... (%ds/%ds)",
                     elapsed_ms / 1000, config_.approval_timeout_ms / 1000);
        }
    }

    ESP_LOGE(TAG, "[B2] Het thoi gian cho admin duyet (%ds)!", config_.approval_timeout_ms / 1000);
    return ESP_ERR_TIMEOUT;
}
