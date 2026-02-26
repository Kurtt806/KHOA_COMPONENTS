/*
 * OTA Manager - Quản lý cập nhật firmware qua mạng (Over-The-Air)
 * 
 * Hỗ trợ:
 * - Tải firmware từ HTTPS/HTTP URL
 * - Kiểm tra phiên bản trước khi cập nhật
 * - Theo dõi tiến trình tải (callback progress)
 * - Rollback nếu firmware mới lỗi
 * - Thread-safe (Singleton pattern)
 * 
 * Cách dùng đơn giản:
 *   auto& ota = OtaManager::GetInstance();
 *   ota.CheckOnBoot("192.168.1.2");  // Chỉ cần 1 dòng!
 *
 * Hoặc cấu hình chi tiết:
 *   OtaConfig config;
 *   config.url = "http://192.168.1.2:8080/firmware.bin";
 *   ota.Initialize(config);
 *   esp_err_t ret = ota.StartUpdate();
 */

#ifndef _OTA_MANAGER_H_
#define _OTA_MANAGER_H_

#include <string>
#include <functional>
#include <mutex>

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

// Trạng thái OTA
enum class OtaState {
    Idle,               // Chưa làm gì
    Checking,           // Đang kiểm tra phiên bản
    ValidatingToken,    // Đang xác thực token
    Downloading,        // Đang tải firmware
    Verifying,          // Đang xác minh firmware
    Ready,              // Firmware mới sẵn sàng, cần restart
    Failed,             // Cập nhật thất bại
};

// Thông tin tiến trình OTA
struct OtaProgress {
    OtaState state;             // Trạng thái hiện tại
    int percent;                // Phần trăm hoàn thành (0-100)
    size_t bytes_downloaded;    // Số byte đã tải
    size_t total_bytes;         // Tổng số byte cần tải (0 nếu chưa biết)
    std::string message;        // Thông báo chi tiết
};

// Thông tin phiên bản từ server
struct VersionInfo {
    std::string version;        // Phiên bản firmware mới nhất trên server
    std::string firmware_url;   // URL download firmware (tùy chọn, server có thể trả về)
};

// Cấu hình OTA
struct OtaConfig {
    std::string url;                            // URL gốc server (VD: http://192.168.1.2:8080)
    std::string device_token;                   // Token của thiết bị (VIBO-KEY) để xác thực
    std::string cert_pem;                       // Chứng chỉ CA (PEM) cho HTTPS, rỗng = bỏ qua xác thực SSL
    int timeout_ms = 60000;                     // Timeout kết nối HTTP (ms) - 60s cho HTTPS qua Cloudflare
    int recv_timeout_ms = 10000;                // Timeout nhận dữ liệu (ms)
    int poll_interval_ms = 5000;                // Khoảng thời gian polling chờ admin duyệt (ms)
    int approval_timeout_ms = 300000;           // Timeout tổng chờ duyệt: 5 phút (ms)
    size_t buffer_size = 4096;                  // Kích thước buffer đọc (bytes)
    bool skip_version_check = false;            // Bỏ qua kiểm tra phiên bản
    bool auto_restart = false;                  // Tự động restart sau khi cập nhật thành công
};

/**
 * OtaManager - Singleton quản lý OTA firmware
 * Tất cả method public đều thread-safe
 */
class OtaManager {
public:
    /// Lấy instance duy nhất
    static OtaManager& GetInstance();

    // ==================== Dùng nhanh ====================

    /// Kiểm tra OTA 1 lần khi boot (tự xử lý rollback + ghép URL + tạo task chờ WiFi)
    /// Chỉ cần truyền IP hoặc URL đầy đủ, component tự làm hết
    void CheckOnBoot(const std::string& server_input);

    /// Kiểm tra OTA 1 lần khi boot (có kèm token thiết bị để xác thực)
    void CheckOnBoot(const std::string& server_input, const std::string& device_token);

    // ==================== Khởi tạo ====================

    /// Khởi tạo OTA với cấu hình chi tiết
    void Initialize(const OtaConfig& config);

    /// Kiểm tra đã khởi tạo chưa
    bool IsInitialized() const;

    /// Cập nhật URL firmware
    void SetUrl(const std::string& url);

    // ==================== Cập nhật ====================

    /// Bắt đầu cập nhật OTA (BLOCKING)
    esp_err_t StartUpdate();

    /// Hủy cập nhật đang chạy (gọi từ thread khác)
    void AbortUpdate();

    // ==================== Trạng thái ====================

    /// Lấy trạng thái hiện tại
    OtaState GetState() const;

    /// Kiểm tra có đang cập nhật không
    bool IsUpdating() const;

    /// Lấy phiên bản firmware hiện tại
    std::string GetCurrentVersion() const;

    /// Lấy thông tin phân vùng đang chạy
    std::string GetRunningPartitionInfo() const;

    // ==================== Rollback ====================

    /// Đánh dấu firmware hiện tại là hợp lệ (gọi sau khi khởi động thành công)
    esp_err_t MarkValid();

    /// Kiểm tra firmware hiện tại có đang chờ xác nhận không
    bool IsPendingVerify() const;

    /// Rollback về firmware trước đó
    esp_err_t Rollback();

    // ==================== Hệ thống ====================

    /// Khởi động lại ESP32
    void Restart();

    // ==================== Callback ====================

    /// Đăng ký callback theo dõi tiến trình
    void SetProgressCallback(std::function<void(const OtaProgress&)> callback);

    // Không cho copy/move
    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

private:
    OtaManager();
    ~OtaManager();

    /// Bước 1: Gọi server lấy thông tin version mới nhất (GET /version.json)
    esp_err_t FetchVersionInfo(VersionInfo& out_info);

    /// Bước 2: Gửi hash token lên server để xác thực (POST /validate-token)
    esp_err_t ValidateToken();

    /// Hash VIBO-KEY bằng FNV-1a 64-bit, trả về chuỗi hex 16 ký tự
    static std::string HashToken64(const std::string& token);
    static int CompareVersion(const std::string& a, const std::string& b);

    /// Thực hiện quá trình tải và ghi firmware OTA nội bộ
    esp_err_t PerformOta();

    /// Gửi thông báo tiến trình
    void NotifyProgress(OtaState state, int percent, size_t downloaded, 
                        size_t total, const std::string& msg);

    OtaConfig config_;
    OtaState state_ = OtaState::Idle;
    bool initialized_ = false;
    bool abort_requested_ = false;

    mutable std::mutex mutex_;
    std::function<void(const OtaProgress&)> progress_callback_;

    /// Ghép URL gốc server từ IP hoặc giữ nguyên nếu đã có http://
    static std::string BuildBaseUrl(const std::string& input);
};

#endif // _OTA_MANAGER_H_
