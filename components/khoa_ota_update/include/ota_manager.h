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

// ============================================================================
// Internal Helpers & Includes (Gộp từ ota_common.h cũ)
// ============================================================================

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

extern "C" esp_err_t esp_crt_bundle_attach(void *conf);


/// Buffer nhận HTTP response body
struct HttpResponseCtx {
    char* buf;
    int len;
    int max;
};

/// Event handler HTTP — đọc response vào buffer
static inline esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    auto* c = (HttpResponseCtx*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && c && c->buf) {
        int n = evt->data_len;
        if (c->len + n > c->max) n = c->max - c->len;
        if (n > 0) { memcpy(c->buf + c->len, evt->data, n); c->len += n; }
    }
    return ESP_OK;
}

/// Cấu hình SSL cho HTTP client
static inline void configure_ssl(esp_http_client_config_t& cfg, const std::string& cert_pem) {
    cfg.buffer_size = 2048;
    cfg.keep_alive_enable = true;
    if (!cert_pem.empty()) cfg.cert_pem = cert_pem.c_str();
    else cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check = false;
}

/// Lấy MAC WiFi Station dạng "aabbccddeeff"
static inline std::string GetMacString() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

/// So sánh semantic version ("1.2.3" vs "1.3.0")
static inline int CompareVersion(const std::string& a, const std::string& b) {
    int a1=0,a2=0,a3=0, b1=0,b2=0,b3=0;
    sscanf(a.c_str(), "%d.%d.%d", &a1, &a2, &a3);
    sscanf(b.c_str(), "%d.%d.%d", &b1, &b2, &b3);
    if (a1 != b1) return a1 - b1;
    if (a2 != b2) return a2 - b2;
    return a3 - b3;
}

#endif // _OTA_MANAGER_H_
