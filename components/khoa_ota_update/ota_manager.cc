/*
 * OTA Manager - Triển khai cập nhật firmware OTA với logic 3 bước:
 *   1. Kiểm tra phiên bản mới từ server (GET /version.json)
 *   2. So sánh version → nếu có bản mới → kiểm tra token 
 *   3. Token hợp lệ → mới download firmware
 */

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

static const char *TAG = "OTA_MGR";

// ==================== Singleton ====================

/// Lấy instance duy nhất của OtaManager
OtaManager& OtaManager::GetInstance() {
    static OtaManager instance;
    return instance;
}

OtaManager::OtaManager() = default;
OtaManager::~OtaManager() = default;

// ==================== Khởi tạo ====================

/// Khởi tạo cấu hình OTA (URL, token, timeout, buffer...)
void OtaManager::Initialize(const OtaConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;
    ESP_LOGI(TAG, "Da khoi tao OTA. URL: %s", config_.url.c_str());
}

/// Kiểm tra OTA đã được khởi tạo chưa
bool OtaManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

/// Cập nhật URL firmware mới (chỉ khi đang Idle)
void OtaManager::SetUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != OtaState::Idle) {
        ESP_LOGW(TAG, "Khong the doi URL khi dang cap nhat!");
        return;
    }
    config_.url = url;
    ESP_LOGI(TAG, "Da cap nhat URL: %s", url.c_str());
}

// ==================== Lấy MAC ====================

/// Lấy MAC address WiFi Station dạng chuỗi hex "aabbccddeeff"
static std::string GetMacString() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

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

/// Gọi GET /version.json để lấy version mới nhất, token yêu cầu
esp_err_t OtaManager::FetchVersionInfo(VersionInfo& out_info) {
    // Ghép URL: base_url + /version.json
    std::string version_url = config_.url;
    // Bỏ trailing slash nếu có
    if (!version_url.empty() && version_url.back() == '/') {
        version_url.pop_back();
    }
    std::string current_ver = GetCurrentVersion();
    version_url += "/version.json?mac=" + GetMacString() + "&v=" + current_ver;

    ESP_LOGI(TAG, "[B1] Kiem tra phien ban tu: %s", version_url.c_str());

    esp_http_client_config_t http_config = {};
    http_config.url = version_url.c_str();
    http_config.timeout_ms = config_.timeout_ms;
    http_config.method = HTTP_METHOD_GET;

    // Cấu hình SSL
    if (!config_.cert_pem.empty()) {
        http_config.cert_pem = config_.cert_pem.c_str();
    } else {
        http_config.crt_bundle_attach = nullptr;
        http_config.skip_cert_common_name_check = true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Khong the khoi tao HTTP client!");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the ket noi server version: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "Server tra ve HTTP %d khi lay version!", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Đọc response body (giới hạn 1KB cho JSON nhỏ)
    int max_len = (content_length > 0 && content_length < 1024) ? content_length : 1024;
    char* buffer = (char*)malloc(max_len + 1);
    if (buffer == nullptr) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int read_len = esp_http_client_read(client, buffer, max_len);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGE(TAG, "Khong doc duoc du lieu version!");
        free(buffer);
        return ESP_FAIL;
    }

    buffer[read_len] = '\0';
    ESP_LOGI(TAG, "Response version: %s", buffer);

    // Parse JSON: {"version": "1.2.0", "firmware_url": "..."}
    cJSON* root = cJSON_Parse(buffer);
    free(buffer);

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

// ==================== Hash FNV-1a 64-bit ====================

/// Hash chuỗi token + MAC bằng FNV-1a 64-bit → mỗi thiết bị có hash duy nhất
std::string OtaManager::HashToken64(const std::string& token) {
    // FNV-1a 64-bit constants
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET;
    for (char c : token) {
        hash ^= (uint8_t)c;
        hash *= FNV_PRIME;
    }

    // Chuyển sang chuỗi hex 16 ký tự
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
    return std::string(hex);
}



// ==================== Bước 2: Đăng ký + Polling chờ admin duyệt ====================

/// Gửi GET /token-status?mac=xxx kiểm tra trạng thái duyệt
/// Trả về: ESP_OK = approved, ESP_ERR_INVALID_RESPONSE = denied, ESP_ERR_NOT_FINISHED = pending
static esp_err_t PollTokenStatus(const std::string& base_url, const std::string& mac,
                                  int timeout_ms, const std::string& cert_pem, std::string& out_firmware_url) {
    // Ghép URL: base + /token-status?mac=xxx
    std::string poll_url = base_url;
    if (!poll_url.empty() && poll_url.back() == '/') poll_url.pop_back();
    poll_url += "/token-status?mac=" + mac;

    esp_http_client_config_t http_config = {};
    http_config.url = poll_url.c_str();
    http_config.timeout_ms = timeout_ms;
    http_config.method = HTTP_METHOD_GET;

    if (!cert_pem.empty()) {
        http_config.cert_pem = cert_pem.c_str();
    } else {
        http_config.crt_bundle_attach = nullptr;
        http_config.skip_cert_common_name_check = true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int max_len = (content_length > 0 && content_length < 256) ? content_length : 256;
    char* resp = (char*)malloc(max_len + 1);
    if (resp == nullptr) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int read_len = esp_http_client_read(client, resp, max_len);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        free(resp);
        return ESP_FAIL;
    }

    resp[read_len] = '\0';

    // Parse JSON: {"status": "pending"/"approved"/"denied"}
    cJSON* root = cJSON_Parse(resp);
    free(resp);
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

    // Đọc URL từ response trả về nếu được duyệt (Bước 3 mới)
    cJSON* url_item = cJSON_GetObjectItem(root, "firmware_url");
    if (result == ESP_OK && url_item && cJSON_IsString(url_item)) {
        out_firmware_url = url_item->valuestring;
    }

    cJSON_Delete(root);
    return result;
}

/// Gửi POST đăng ký thiết bị, sau đó polling chờ admin duyệt trên web
esp_err_t OtaManager::ValidateToken() {
    std::string device_token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        device_token = config_.device_token;
    }

    // Lấy MAC address
    std::string mac = GetMacString();
    std::string token_hash;

    if (device_token.empty()) {
        // Chưa có VIBO-KEY → gửi MAC lên server để admin kích hoạt
        ESP_LOGW(TAG, "[B2] Chua co VIBO-KEY. Gui MAC len server cho admin kich hoat...");
        token_hash = "";
    } else {
        // Có VIBO-KEY → tạo hash = FNV-1a(KEY + MAC)
        std::string combined = device_token + mac;
        token_hash = HashToken64(combined);
        ESP_LOGI(TAG, "[B2] MAC: %s | Hash(KEY+MAC): %s", mac.c_str(), token_hash.c_str());
    }

    // ===== Bước 2a: POST /validate-token đăng ký yêu cầu =====
    // Lấy thông tin thiết bị
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

    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "token_hash", token_hash.c_str());
    cJSON_AddStringToObject(body, "mac", mac.c_str());
    // Thông tin thiết bị
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

    esp_http_client_config_t http_config = {};
    http_config.url = validate_url.c_str();
    http_config.timeout_ms = config_.timeout_ms;
    http_config.method = HTTP_METHOD_POST;

    if (!config_.cert_pem.empty()) {
        http_config.cert_pem = config_.cert_pem.c_str();
    } else {
        http_config.crt_bundle_attach = nullptr;
        http_config.skip_cert_common_name_check = true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        free(body_str);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[B2] Khong the ket noi server: %s", esp_err_to_name(err));
        free(body_str);
        esp_http_client_cleanup(client);
        return err;
    }

    int written = esp_http_client_write(client, body_str, body_len);
    free(body_str);

    if (written < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    // Đọc response body để parse status
    int resp_max = (content_length > 0 && content_length < 512) ? content_length : 512;
    char* resp_buf = (char*)malloc(resp_max + 1);
    int resp_read = 0;
    if (resp_buf) {
        resp_read = esp_http_client_read(client, resp_buf, resp_max);
        if (resp_read > 0) resp_buf[resp_read] = '\0';
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "[B2] Dang ky: HTTP %d", status_code);

    if (status_code != 200) {
        ESP_LOGE(TAG, "[B2] Server tu choi dang ky: HTTP %d", status_code);
        free(resp_buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Parse response: {"status": "pending"/"denied"}
    if (resp_buf && resp_read > 0) {
        cJSON* resp_json = cJSON_Parse(resp_buf);
        free(resp_buf);
        if (resp_json) {
            cJSON* st = cJSON_GetObjectItem(resp_json, "status");
            if (st && cJSON_IsString(st)) {
                if (strcmp(st->valuestring, "denied") == 0) {
                    ESP_LOGE(TAG, "[B2] ✗ Token KHONG HOP LE! Server tu choi.");
                    cJSON_Delete(resp_json);
                    return ESP_ERR_INVALID_RESPONSE;
                }
                ESP_LOGI(TAG, "[B2] Server response: status=%s", st->valuestring);
                if (strcmp(st->valuestring, "approved") == 0) {
                     cJSON* url_item = cJSON_GetObjectItem(resp_json, "firmware_url");
                     if (url_item && cJSON_IsString(url_item)) {
                          // DO NOT overwrite config_.url yet, as we need the base url for polling.
                          // Store it in a temporary variable or just wait until polling is done.
                          // Actually, we don't need to poll if it's already approved.
                          std::lock_guard<std::mutex> lock(mutex_);
                          config_.url = url_item->valuestring;
                          ESP_LOGI(TAG, "[B2] Nhan duoc firmware URL tu server: %s", config_.url.c_str());
                          cJSON_Delete(resp_json);
                          return ESP_OK; // Return immediately if approved! No need to poll.
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
        // Kiểm tra abort
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
            ESP_LOGI(TAG, "[B2] ✓ Admin DA DUYET! Cho phep download.");
            if (!fw_url.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                config_.url = fw_url;
                ESP_LOGI(TAG, "[B2] Nhau duoc firmware URL tu polling: %s", config_.url.c_str());
            }
            return ESP_OK;
        } else if (poll_ret == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGE(TAG, "[B2] ✗ Admin TU CHOI! Huy cap nhat.");
            return ESP_ERR_INVALID_RESPONSE;
        }

        // Pending → tiếp tục chờ
        if (poll_count % 6 == 0) {
            // Log mỗi 30s để không spam
            ESP_LOGI(TAG, "[B2] Van dang cho admin duyet... (%ds/%ds)",
                     elapsed_ms / 1000, config_.approval_timeout_ms / 1000);
        }
    }

    ESP_LOGE(TAG, "[B2] Het thoi gian cho admin duyet (%ds)!", config_.approval_timeout_ms / 1000);
    return ESP_ERR_TIMEOUT;
}


// ==================== Cập nhật (Logic 3 bước) ====================

/// Bắt đầu quá trình OTA: Kiểm tra version → Xác thực token → Download
esp_err_t OtaManager::StartUpdate() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            ESP_LOGE(TAG, "OTA chua duoc khoi tao!");
            return ESP_ERR_INVALID_STATE;
        }
        if (state_ != OtaState::Idle && state_ != OtaState::Failed) {
            ESP_LOGW(TAG, "OTA dang chay, khong the bat dau lai!");
            return ESP_ERR_INVALID_STATE;
        }
        if (config_.url.empty()) {
            ESP_LOGE(TAG, "URL server trong!");
            return ESP_ERR_INVALID_ARG;
        }
        abort_requested_ = false;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BAT DAU KIEM TRA CAP NHAT OTA");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Server: %s", config_.url.c_str());
    ESP_LOGI(TAG, "Phien ban hien tai: %s", GetCurrentVersion().c_str());

    // ===== BƯỚC 1: Kiểm tra phiên bản =====
    NotifyProgress(OtaState::Checking, 0, 0, 0, "Dang kiem tra phien ban moi...");

    VersionInfo server_info;
    esp_err_t ret = FetchVersionInfo(server_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[B1] Khong the lay thong tin version tu server!");
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the ket noi server version!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ret;
    }

    // So sánh version: server phải CAO HƠN hiện tại
    std::string current_ver = GetCurrentVersion();
    int cmp = CompareVersion(server_info.version, current_ver);

    if (cmp <= 0) {
        ESP_LOGI(TAG, "[B1] Phien ban hien tai (%s) da la moi nhat (server: %s). Khong can cap nhat.",
                 current_ver.c_str(), server_info.version.c_str());
        NotifyProgress(OtaState::Idle, 0, 0, 0, "Phien ban da la moi nhat!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Idle;
        }
        return ESP_ERR_INVALID_VERSION;
    }

    ESP_LOGI(TAG, "[B1] ✓ Co phien ban moi: %s → %s", 
             current_ver.c_str(), server_info.version.c_str());

    // ===== BƯỚC 2: Xác thực token (bắt buộc) =====
    NotifyProgress(OtaState::ValidatingToken, 0, 0, 0, "Dang xac thuc token...");

    ret = ValidateToken();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[B2] Token khong hop le! Huy cap nhat.");
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Token khong hop le!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ret;
    }

    ESP_LOGI(TAG, "[B2] ✓ Token hop le!");

    // ===== BƯỚC 3: Download firmware =====
    ESP_LOGI(TAG, "[B3] Bat dau tai firmware...");

    // Không nạp cứng /firmware.bin nữa, config_.url đã được cập nhật từ Bước 2
    ESP_LOGI(TAG, "[B3] Su dung URL: %s", config_.url.c_str());

    // Thực hiện OTA (blocking)
    ret = PerformOta();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  CAP NHAT OTA THANH CONG!");
        ESP_LOGI(TAG, "========================================");
        if (config_.auto_restart) {
            ESP_LOGI(TAG, "Tu dong khoi dong lai sau 3 giay...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Restart();
        }
    } else {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  CAP NHAT OTA THAT BAI: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "========================================");
    }

    return ret;
}

/// Hủy cập nhật đang chạy (gọi từ thread khác)
void OtaManager::AbortUpdate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == OtaState::Downloading || state_ == OtaState::Checking) {
        abort_requested_ = true;
        ESP_LOGW(TAG, "Yeu cau huy cap nhat OTA...");
    }
}

/// Thực hiện tải và ghi firmware OTA nội bộ (Bước 3)
esp_err_t OtaManager::PerformOta() {
    esp_err_t err;

    // === Kiểm tra phân vùng đích ===
    NotifyProgress(OtaState::Downloading, 0, 0, 0, "Dang kiem tra phan vung...");

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(running);

    if (update_partition == nullptr) {
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong tim thay phan vung cap nhat!");
        ESP_LOGE(TAG, "Khong tim thay phan vung OTA tiep theo!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Phan vung dang chay: %s (offset 0x%08" PRIx32 ")", 
             running->label, running->address);
    ESP_LOGI(TAG, "Phan vung cap nhat: %s (offset 0x%08" PRIx32 ")", 
             update_partition->label, update_partition->address);

    // === Kết nối HTTP và tải firmware ===
    NotifyProgress(OtaState::Downloading, 0, 0, 0, "Dang ket noi server...");

    esp_http_client_config_t http_config = {};
    http_config.url = config_.url.c_str();
    http_config.timeout_ms = config_.timeout_ms;

    // Cấu hình SSL nếu có chứng chỉ
    if (!config_.cert_pem.empty()) {
        http_config.cert_pem = config_.cert_pem.c_str();
    } else {
        http_config.crt_bundle_attach = nullptr;
        http_config.skip_cert_common_name_check = true;
    }

    http_config.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the khoi tao HTTP client!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the ket noi server firmware: %s", esp_err_to_name(err));
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the ket noi server!");
        esp_http_client_cleanup(client);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "Server tra ve loi HTTP %d", status_code);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Server tra ve loi HTTP!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ESP_FAIL;
    }

    // === Bắt đầu ghi OTA ===
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin that bai: %s", esp_err_to_name(err));
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the bat dau ghi OTA!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return err;
    }

    // === Tải và ghi firmware từng phần ===
    size_t total_bytes = (content_length > 0) ? (size_t)content_length : 0;
    size_t downloaded = 0;
    int last_percent = -1;

    // Cấp phát buffer đọc dữ liệu
    char *buffer = (char *)malloc(config_.buffer_size);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Khong du bo nho cap phat buffer!");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        // Kiểm tra yêu cầu hủy
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abort_requested_) {
                ESP_LOGW(TAG, "Cap nhat OTA bi huy boi nguoi dung!");
                free(buffer);
                esp_ota_abort(ota_handle);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                state_ = OtaState::Idle;
                NotifyProgress(OtaState::Idle, 0, 0, 0, "Da huy cap nhat!");
                return ESP_ERR_OTA_ROLLBACK_FAILED;
            }
        }

        int read_len = esp_http_client_read(client, buffer, config_.buffer_size);

        if (read_len < 0) {
            ESP_LOGE(TAG, "Loi doc du lieu HTTP!");
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = OtaState::Failed;
            }
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi doc du lieu!");
            return ESP_FAIL;
        }

        if (read_len == 0) {
            // Đã tải xong hoặc server đóng kết nối
            if (esp_http_client_is_complete_data_received(client)) {
                ESP_LOGI(TAG, "Da tai xong firmware!");
                break;
            }
            ESP_LOGE(TAG, "Ket noi bi ngat truoc khi tai xong!");
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = OtaState::Failed;
            }
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Ket noi bi ngat!");
            return ESP_FAIL;
        }

        // Ghi dữ liệu vào phân vùng OTA
        err = esp_ota_write(ota_handle, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write that bai: %s", esp_err_to_name(err));
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = OtaState::Failed;
            }
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi ghi firmware!");
            return err;
        }

        downloaded += read_len;

        // Cập nhật tiến trình
        int percent = 0;
        if (total_bytes > 0) {
            percent = (int)((downloaded * 100) / total_bytes);
        }

        // Chỉ gửi callback khi phần trăm thay đổi (tránh spam)
        if (percent != last_percent) {
            last_percent = percent;
            NotifyProgress(OtaState::Downloading, percent, downloaded, total_bytes, "Dang tai firmware...");
            ESP_LOGI(TAG, "Tien do: %d%% (%zu / %zu bytes)", percent, downloaded, total_bytes);
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Tong cong da tai: %zu bytes", downloaded);

    // === Xác minh và hoàn tất ===
    NotifyProgress(OtaState::Verifying, 100, downloaded, total_bytes, "Dang xac minh firmware...");

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Firmware khong hop le (checksum sai)!");
        } else {
            ESP_LOGE(TAG, "esp_ota_end that bai: %s", esp_err_to_name(err));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Firmware khong hop le!");
        return err;
    }

    // Đặt phân vùng boot mới
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition that bai: %s", esp_err_to_name(err));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi dat phan vung boot!");
        return err;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = OtaState::Ready;
    }

    NotifyProgress(OtaState::Ready, 100, downloaded, total_bytes, 
                   "Cap nhat thanh cong! Can khoi dong lai.");
    ESP_LOGI(TAG, "Firmware moi da san sang tai phan vung: %s", update_partition->label);

    return ESP_OK;
}

// ==================== Trạng thái ====================

/// Lấy trạng thái OTA hiện tại
OtaState OtaManager::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

/// Kiểm tra có đang trong quá trình cập nhật không
bool OtaManager::IsUpdating() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == OtaState::Checking || 
           state_ == OtaState::ValidatingToken ||
           state_ == OtaState::Downloading || 
           state_ == OtaState::Verifying;
}

/// Lấy chuỗi phiên bản firmware đang chạy
std::string OtaManager::GetCurrentVersion() const {
    const esp_app_desc_t *desc = esp_app_get_description();
    return std::string(desc->version);
}

/// Lấy thông tin phân vùng đang chạy (tên + offset)
std::string OtaManager::GetRunningPartitionInfo() const {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return "unknown";
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (offset 0x%08" PRIx32 ")", 
             running->label, running->address);
    return std::string(buf);
}

// ==================== Rollback ====================

/// Đánh dấu firmware hiện tại hoạt động tốt (xác nhận sau OTA)
esp_err_t OtaManager::MarkValid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Da danh dau firmware hien tai la hop le!");
    } else {
        ESP_LOGE(TAG, "Loi danh dau firmware: %s", esp_err_to_name(err));
    }
    return err;
}

/// Kiểm tra firmware có đang chờ xác nhận (pending verify) không
bool OtaManager::IsPendingVerify() const {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        return ota_state == ESP_OTA_IMG_PENDING_VERIFY;
    }
    return false;
}

/// Rollback về firmware trước đó
esp_err_t OtaManager::Rollback() {
    ESP_LOGW(TAG, "Dang rollback ve firmware truoc...");
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "Rollback that bai: %s", esp_err_to_name(err));
    return err;
}

// ==================== Hệ thống ====================

/// Khởi động lại ESP32 để áp dụng firmware mới
void OtaManager::Restart() {
    ESP_LOGW(TAG, "Dang khoi dong lai...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

// ==================== Callback ====================

/// Đăng ký callback nhận thông báo tiến trình OTA
void OtaManager::SetProgressCallback(std::function<void(const OtaProgress&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_callback_ = callback;
}

/// Gửi thông báo tiến trình đến callback đã đăng ký
void OtaManager::NotifyProgress(OtaState state, int percent, size_t downloaded, 
                                 size_t total, const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
    }

    std::function<void(const OtaProgress&)> cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = progress_callback_;
    }

    if (cb) {
        OtaProgress progress;
        progress.state = state;
        progress.percent = percent;
        progress.bytes_downloaded = downloaded;
        progress.total_bytes = total;
        progress.message = msg;
        cb(progress);
    }
}

// ==================== Tiện ích ====================

/// Ghép URL gốc server từ IP hoặc giữ nguyên nếu đã có http://
std::string OtaManager::BuildBaseUrl(const std::string& input) {
    if (input.empty()) return "";
    if (input.find("http") != std::string::npos) return input;
    return "http://" + input + ":8080";
}

// ==================== Dùng nhanh ====================

/// Struct truyền thông tin vào task
struct OtaBootParams {
    std::string url;
    std::string token;
};

/// Kiểm tra OTA 1 lần khi boot: rollback + ghép URL + xác thực + tạo task check
void OtaManager::CheckOnBoot(const std::string& server_input) {
    CheckOnBoot(server_input, "");
}

/// Kiểm tra OTA 1 lần khi boot (có token thiết bị)
void OtaManager::CheckOnBoot(const std::string& server_input, const std::string& device_token) {
    auto& ota = GetInstance();

    // Xử lý rollback tự động
    if (ota.IsPendingVerify()) {
        ESP_LOGW(TAG, "Firmware moi dang cho xac nhan...");
        ota.MarkValid();
        ESP_LOGI(TAG, "Da xac nhan firmware moi hop le!");
    }

    // Log thông tin firmware
    ESP_LOGI(TAG, "Phien ban: %s | Phan vung: %s", 
             ota.GetCurrentVersion().c_str(), 
             ota.GetRunningPartitionInfo().c_str());

    // Ghép URL gốc server
    std::string base_url = BuildBaseUrl(server_input);
    if (base_url.empty()) {
        ESP_LOGW(TAG, "Khong co URL OTA, bo qua.");
        return;
    }

    OtaConfig config;
    config.url = base_url;
    config.device_token = device_token;
    config.skip_version_check = false;
    config.auto_restart = true;
    ota.Initialize(config);

    // Progress callback mặc định
    ota.SetProgressCallback([](const OtaProgress& p) {
        switch (p.state) {
            case OtaState::Checking:
                ESP_LOGI(TAG, "[OTA] Dang kiem tra phien ban...");
                break;
            case OtaState::ValidatingToken:
                ESP_LOGI(TAG, "[OTA] Dang xac thuc token...");
                break;
            case OtaState::Downloading:
                ESP_LOGI(TAG, "[OTA] Tai: %d%% (%zu/%zu bytes)", 
                         p.percent, p.bytes_downloaded, p.total_bytes);
                break;
            case OtaState::Ready:
                ESP_LOGI(TAG, "[OTA] Thanh cong! Dang khoi dong lai...");
                break;
            case OtaState::Failed:
                ESP_LOGE(TAG, "[OTA] That bai: %s", p.message.c_str());
                break;
            default: break;
        }
    });

    // Tạo task: delay 1s rồi check OTA 1 lần
    auto* params = new OtaBootParams{base_url, device_token};
    xTaskCreate([](void* arg) {
        auto* p = static_cast<OtaBootParams*>(arg);
        ESP_LOGI(TAG, "[OTA] Cho 1s de mang on dinh...");
        vTaskDelay(pdMS_TO_TICKS(1000));

        auto& ota = OtaManager::GetInstance();
        ESP_LOGI(TAG, "[OTA] Bat dau kiem tra cap nhat tu: %s", p->url.c_str());
        esp_err_t ret = ota.StartUpdate();

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "[OTA] Cap nhat thanh cong!");
        } else if (ret == ESP_ERR_INVALID_VERSION) {
            ESP_LOGI(TAG, "[OTA] Phien ban da la moi nhat, khong can cap nhat.");
        } else if (ret == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGE(TAG, "[OTA] Token khong hop le!");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "[OTA] Thiet bi chua cau hinh token!");
        } else {
            ESP_LOGW(TAG, "[OTA] Loi: %s", esp_err_to_name(ret));
        }

        delete p;
        vTaskDelete(NULL);
    }, "ota_boot", 8192, params, 5, NULL);
}
