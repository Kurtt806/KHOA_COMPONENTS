/*
 * OTA Manager - Quản lý cập nhật firmware qua mạng (Over-The-Air)
 * Bảo mật bằng MAC address duy nhất của ESP (Device-Id header)
 * 
 * Cách dùng:
 *   OtaManager::GetInstance().CheckOnBoot("192.168.1.2");  // 1 dòng!
 */

#ifndef _OTA_MANAGER_H_
#define _OTA_MANAGER_H_

#include <string>
#include <functional>
#include <mutex>

#include "esp_err.h"
#include "esp_ota_ops.h"

// Trạng thái OTA
enum class OtaState {
    Idle,           // Chưa làm gì
    Checking,       // Đang kiểm tra phiên bản
    Downloading,    // Đang tải firmware
    Verifying,      // Đang xác minh firmware
    Ready,          // Firmware mới sẵn sàng, cần restart
    Failed,         // Cập nhật thất bại
};

// Thông tin tiến trình OTA
struct OtaProgress {
    OtaState state;
    int percent;
    size_t bytes_downloaded;
    size_t total_bytes;
    std::string message;
};

// Thông tin phiên bản từ server
struct VersionInfo {
    std::string version;        // Phiên bản mới nhất
    std::string firmware_url;   // URL download firmware
    bool force = false;         // Bắt buộc cập nhật
};

// Cấu hình OTA
struct OtaConfig {
    std::string url;                        // URL server (VD: http://192.168.1.2:8080)
    std::string cert_pem;                   // Chứng chỉ CA cho HTTPS (rỗng = bundle mặc định)
    int timeout_ms = 60000;                 // Timeout kết nối (ms)
    size_t buffer_size = 4096;              // Buffer đọc firmware (bytes)
    bool skip_version_check = false;        // Bỏ qua so sánh version
    bool auto_restart = false;              // Tự restart sau khi OTA thành công
};

/// Singleton quản lý OTA firmware — thread-safe
class OtaManager {
public:
    static OtaManager& GetInstance();

    /// Kiểm tra OTA 1 lần khi boot (tự xử lý rollback + ghép URL + tạo task)
    void CheckOnBoot(const std::string& server_input);

    /// Khởi tạo cấu hình chi tiết
    void Initialize(const OtaConfig& config);
    bool IsInitialized() const;
    void SetUrl(const std::string& url);

    /// Bắt đầu cập nhật OTA (BLOCKING): FetchVersion → Download
    esp_err_t StartUpdate();
    void AbortUpdate();

    OtaState GetState() const;
    bool IsUpdating() const;
    std::string GetCurrentVersion() const;
    std::string GetRunningPartitionInfo() const;

    /// Rollback
    esp_err_t MarkValid();
    bool IsPendingVerify() const;
    esp_err_t Rollback();

    void Restart();
    void SetProgressCallback(std::function<void(const OtaProgress&)> callback);

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

private:
    OtaManager();
    ~OtaManager();

    /// Bước 1: Gọi server lấy thông tin version
    esp_err_t FetchVersionInfo(VersionInfo& out_info);
    /// Bước 2: Tải và ghi firmware OTA
    esp_err_t PerformOta();

    /// Gửi thông báo tiến trình
    void NotifyProgress(OtaState state, int percent, size_t downloaded,
                        size_t total, const std::string& msg);

    /// Ghép URL từ IP/domain
    static std::string BuildBaseUrl(const std::string& input);

    OtaConfig config_;
    OtaState state_ = OtaState::Idle;
    bool initialized_ = false;
    bool abort_requested_ = false;

    mutable std::mutex mutex_;
    std::function<void(const OtaProgress&)> progress_callback_;
};

#endif // _OTA_MANAGER_H_
