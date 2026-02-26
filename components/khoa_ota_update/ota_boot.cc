/*
 * OTA Boot - Logic chính: StartUpdate (3 bước) + CheckOnBoot + BuildBaseUrl
 * Điểm vào đơn giản cho người dùng component
 */

#include "ota_common.h"

// ==================== Logic 3 bước ====================

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

    // ===== BƯỚC 1: Kiểm tra phiên bản (retry tối đa 3 lần) =====
    NotifyProgress(OtaState::Checking, 0, 0, 0, "Dang kiem tra phien ban moi...");

    VersionInfo server_info;
    esp_err_t ret = ESP_FAIL;
    const int max_retries = 3;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        ret = FetchVersionInfo(server_info);
        if (ret == ESP_OK) break;

        ESP_LOGW(TAG, "[B1] Lan thu %d/%d that bai: %s",
                 attempt, max_retries, esp_err_to_name(ret));

        if (attempt < max_retries) {
            ESP_LOGI(TAG, "[B1] Thu lai sau 3 giay...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[B1] Khong the lay thong tin version sau %d lan thu!", max_retries);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the ket noi server version!");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Failed;
        }
        return ret;
    }

    // So sánh version
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

    ESP_LOGI(TAG, "[B1] Co phien ban moi: %s -> %s", 
             current_ver.c_str(), server_info.version.c_str());

    // Chờ 1s để giải phóng tài nguyên SSL trước khi tạo connection mới
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ===== BƯỚC 2: Xác thực token =====
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

    ESP_LOGI(TAG, "[B2] Token hop le!");

    // ===== BƯỚC 3: Download firmware =====
    ESP_LOGI(TAG, "[B3] Bat dau tai firmware...");
    ESP_LOGI(TAG, "[B3] Su dung URL: %s", config_.url.c_str());

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

// ==================== Tiện ích ====================

/// Ghép URL gốc server từ IP hoặc domain
/// IP (192.168.1.2) → thêm port 8080
/// Domain (ota.vibohub.com) → dùng HTTPS
/// URL đầy đủ (http://...) → giữ nguyên
std::string OtaManager::BuildBaseUrl(const std::string& input) {
    if (input.empty()) return "";
    // Đã có scheme → giữ nguyên
    if (input.find("http") != std::string::npos) return input;

    // Kiểm tra input có phải IP không (chỉ chứa số và dấu chấm)
    bool is_ip = true;
    for (char c : input) {
        if (!isdigit(c) && c != '.') {
            is_ip = false;
            break;
        }
    }

    if (is_ip) {
        // IP → dùng HTTP + port mặc định (mạng nội bộ)
        return "http://" + input + ":8080";
    } else {
        // Domain → dùng HTTPS (reverse proxy + SSL certificate)
        return "https://" + input;
    }
}
    
// ==================== Dùng nhanh ====================

/// Struct truyền thông tin vào task
struct OtaBootParams {
    std::string url;
    std::string token;
};

/// Kiểm tra OTA 1 lần khi boot (không token)
void OtaManager::CheckOnBoot(const std::string& server_input) {
    CheckOnBoot(server_input, "");
}

/// Kiểm tra OTA 1 lần khi boot: rollback + ghép URL + xác thực + tạo task check
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
