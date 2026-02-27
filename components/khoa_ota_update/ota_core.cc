/*
 * OTA Core - Singleton, khởi tạo, trạng thái, rollback, StartUpdate, CheckOnBoot
 */

#include "ota_common.h"

// ==================== Singleton ====================

/// Lấy instance duy nhất
OtaManager& OtaManager::GetInstance() {
    static OtaManager instance;
    return instance;
}

OtaManager::OtaManager() = default;
OtaManager::~OtaManager() = default;

// ==================== Khởi tạo ====================

/// Khởi tạo cấu hình OTA
void OtaManager::Initialize(const OtaConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;
    ESP_LOGI(TAG, "URL: %s", config_.url.c_str());
}

bool OtaManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

/// Cập nhật URL khi đang Idle/Failed
void OtaManager::SetUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != OtaState::Idle && state_ != OtaState::Failed) return;
    config_.url = url;
}

// ==================== Trạng thái ====================

OtaState OtaManager::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool OtaManager::IsUpdating() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == OtaState::Checking || state_ == OtaState::Downloading || state_ == OtaState::Verifying;
}

/// Lấy phiên bản firmware đang chạy
std::string OtaManager::GetCurrentVersion() const {
    return std::string(esp_app_get_description()->version);
}

/// Lấy thông tin phân vùng đang chạy
std::string OtaManager::GetRunningPartitionInfo() const {
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (!p) return "unknown";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (0x%08" PRIx32 ")", p->label, p->address);
    return std::string(buf);
}

// ==================== Rollback ====================

esp_err_t OtaManager::MarkValid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "MarkValid: %s", esp_err_to_name(err));
    return err;
}

bool OtaManager::IsPendingVerify() const {
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t s;
    return (esp_ota_get_state_partition(p, &s) == ESP_OK) && (s == ESP_OTA_IMG_PENDING_VERIFY);
}

esp_err_t OtaManager::Rollback() {
    ESP_LOGW(TAG, "Rollback...");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

// ==================== Hệ thống ====================

void OtaManager::Restart() {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

void OtaManager::AbortUpdate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == OtaState::Downloading || state_ == OtaState::Checking) {
        abort_requested_ = true;
        ESP_LOGW(TAG, "Huy OTA...");
    }
}

// ==================== Callback ====================

void OtaManager::SetProgressCallback(std::function<void(const OtaProgress&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_callback_ = callback;
}

/// Cập nhật state + gọi callback
void OtaManager::NotifyProgress(OtaState state, int percent, size_t downloaded,
                                 size_t total, const std::string& msg) {
    std::function<void(const OtaProgress&)> cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
        cb = progress_callback_;
    }
    if (cb) cb({state, percent, downloaded, total, msg});
}

// ==================== StartUpdate: 2 bước ====================

/// Bước 1: FetchVersion → Bước 2: PerformOta
esp_err_t OtaManager::StartUpdate() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || config_.url.empty()) return ESP_ERR_INVALID_STATE;
        if (state_ != OtaState::Idle && state_ != OtaState::Failed) return ESP_ERR_INVALID_STATE;
        abort_requested_ = false;
        state_ = OtaState::Checking;
    }

    ESP_LOGI(TAG, "==== BAT DAU OTA ====");
    ESP_LOGI(TAG, "Server: %s | Version: %s", config_.url.c_str(), GetCurrentVersion().c_str());

    // Bước 1: Kiểm tra version (retry 3 lần)
    NotifyProgress(OtaState::Checking, 0, 0, 0, "Kiem tra phien ban...");
    VersionInfo info;
    esp_err_t ret = ESP_FAIL;

    for (int i = 1; i <= 3; i++) {
        ret = FetchVersionInfo(info);
        if (ret == ESP_OK) break;
        ESP_LOGW(TAG, "[B1] Thu %d/3 that bai", i);
        if (i < 3) vTaskDelay(pdMS_TO_TICKS(3000));
    }

    if (ret != ESP_OK) {
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong ket noi duoc server!");
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = OtaState::Failed;
        return ret;
    }

    // So sánh version
    std::string cur = GetCurrentVersion();
    if (info.force) {
        ESP_LOGW(TAG, "[B1] Force update: %s -> %s", cur.c_str(), info.version.c_str());
    } else {
        if (CompareVersion(info.version, cur) <= 0 && !config_.skip_version_check) {
            ESP_LOGI(TAG, "[B1] Da la phien ban moi nhat (%s)", cur.c_str());
            NotifyProgress(OtaState::Idle, 0, 0, 0, "Da la moi nhat!");
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = OtaState::Idle;
            return ESP_ERR_INVALID_VERSION;
        }
        ESP_LOGI(TAG, "[B1] Phien ban moi: %s -> %s", cur.c_str(), info.version.c_str());
    }

    // Cập nhật URL firmware nếu server trả về
    if (!info.firmware_url.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.url = info.firmware_url;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Bước 2: Download firmware
    ESP_LOGI(TAG, "[B2] Tai firmware: %s", config_.url.c_str());
    ret = PerformOta();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "==== OTA THANH CONG ====");
        if (config_.auto_restart) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            Restart();
        }
    } else {
        ESP_LOGE(TAG, "==== OTA THAT BAI: %s ====", esp_err_to_name(ret));
    }
    return ret;
}

// ==================== BuildBaseUrl ====================

/// IP → http://IP:8080 | Domain → https://domain | URL đầy đủ → giữ nguyên
std::string OtaManager::BuildBaseUrl(const std::string& input) {
    if (input.empty()) return "";
    if (input.find("http") != std::string::npos) return input;

    bool is_ip = true;
    for (char c : input) {
        if (!isdigit(c) && c != '.') { is_ip = false; break; }
    }
    return is_ip ? ("http://" + input + ":8080") : ("https://" + input);
}

// ==================== CheckOnBoot ====================

/// Kiểm tra OTA 1 lần khi boot: rollback + ghép URL + tạo task
void OtaManager::CheckOnBoot(const std::string& server_input) {
    auto& ota = GetInstance();

    // Rollback tự động
    if (ota.IsPendingVerify()) {
        ota.MarkValid();
        ESP_LOGI(TAG, "Firmware moi hop le!");
    }

    ESP_LOGI(TAG, "Version: %s | Partition: %s",
             ota.GetCurrentVersion().c_str(), ota.GetRunningPartitionInfo().c_str());

    std::string base_url = BuildBaseUrl(server_input);
    if (base_url.empty()) { ESP_LOGW(TAG, "URL OTA rong, bo qua."); return; }

    OtaConfig cfg;
    cfg.url = base_url;
    cfg.auto_restart = true;
    ota.Initialize(cfg);

    // Callback log mặc định
    ota.SetProgressCallback([](const OtaProgress& p) {
        switch (p.state) {
            case OtaState::Checking:    ESP_LOGI(TAG, "[OTA] Kiem tra..."); break;
            case OtaState::Downloading: ESP_LOGI(TAG, "[OTA] Tai: %d%% (%zu/%zu)", p.percent, p.bytes_downloaded, p.total_bytes); break;
            case OtaState::Ready:       ESP_LOGI(TAG, "[OTA] Thanh cong! Restart..."); break;
            case OtaState::Failed:      ESP_LOGE(TAG, "[OTA] Loi: %s", p.message.c_str()); break;
            default: break;
        }
    });

    // Tạo task OTA
    auto* url_copy = new std::string(base_url);
    auto task_fn = [](void* arg) {
        auto* url = static_cast<std::string*>(arg);
        esp_err_t ret = OtaManager::GetInstance().StartUpdate();
        if (ret == ESP_ERR_INVALID_VERSION) ESP_LOGI(TAG, "[OTA] Da la moi nhat.");
        else if (ret != ESP_OK) ESP_LOGW(TAG, "[OTA] Loi: %s", esp_err_to_name(ret));
        delete url;
        vTaskDelete(NULL);
    };

    if (xTaskCreate(task_fn, "ota_boot", 8192, url_copy, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Tao task OTA that bai!");
        delete url_copy;
    }
}
