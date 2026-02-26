/*
 * OTA Core - Singleton, khởi tạo, trạng thái, rollback, restart, callback
 */

#include "ota_common.h"

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

/// Hủy cập nhật đang chạy (gọi từ thread khác)
void OtaManager::AbortUpdate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == OtaState::Downloading || state_ == OtaState::Checking) {
        abort_requested_ = true;
        ESP_LOGW(TAG, "Yeu cau huy cap nhat OTA...");
    }
}
