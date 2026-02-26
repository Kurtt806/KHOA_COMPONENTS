# khoa_ota_update

Component cập nhật firmware OTA (Over-The-Air) đồng bộ cho ESP32.

## Tính năng

- ✅ Tải firmware từ HTTP/HTTPS server
- ✅ Kiểm tra phiên bản tự động (bỏ qua nếu trùng)
- ✅ Theo dõi tiến trình real-time qua callback
- ✅ Rollback về firmware cũ nếu firmware mới lỗi
- ✅ Hủy cập nhật từ thread khác
- ✅ Thread-safe (mutex nội bộ)
- ✅ Hỗ trợ SSL/TLS với custom CA certificate

## Cách dùng

### Cơ bản (HTTP không mã hóa)

```cpp
#include "ota_manager.h"

auto& ota = OtaManager::GetInstance();

// Cấu hình
OtaConfig config;
config.url = "http://192.168.1.100:8080/firmware.bin";
ota.Initialize(config);

// Theo dõi tiến trình
ota.SetProgressCallback([](const OtaProgress& p) {
    ESP_LOGI("OTA", "[%d%%] %s", p.percent, p.message.c_str());
});

// Bắt đầu cập nhật (BLOCKING)
esp_err_t ret = ota.StartUpdate();
if (ret == ESP_OK) {
    ota.Restart();  // Khởi động lại
}
```

### HTTPS với chứng chỉ

```cpp
extern const char ca_cert_pem[] asm("_binary_ca_cert_pem_start");

OtaConfig config;
config.url = "https://example.com/firmware.bin";
config.cert_pem = ca_cert_pem;
ota.Initialize(config);
```

### Rollback sau khởi động

```cpp
auto& ota = OtaManager::GetInstance();

// Kiểm tra firmware mới có đang chờ xác nhận không
if (ota.IsPendingVerify()) {
    ESP_LOGW("APP", "Firmware mới! Kiểm tra...");

    // Nếu OK → xác nhận
    if (system_check_ok()) {
        ota.MarkValid();
    } else {
        // Nếu lỗi → rollback
        ota.Rollback();  // Tự restart
    }
}
```

## API

| Method                | Mô tả                       |
| --------------------- | --------------------------- |
| `GetInstance()`       | Lấy singleton instance      |
| `Initialize(config)`  | Khởi tạo với cấu hình       |
| `SetUrl(url)`         | Đổi URL firmware            |
| `StartUpdate()`       | Bắt đầu OTA (blocking)      |
| `AbortUpdate()`       | Hủy OTA (từ thread khác)    |
| `GetState()`          | Lấy trạng thái hiện tại     |
| `IsUpdating()`        | Đang cập nhật?              |
| `GetCurrentVersion()` | Phiên bản firmware hiện tại |
| `MarkValid()`         | Xác nhận firmware mới OK    |
| `IsPendingVerify()`   | Firmware đang chờ xác nhận? |
| `Rollback()`          | Quay về firmware cũ         |
| `Restart()`           | Khởi động lại               |

## Yêu cầu

- ESP-IDF >= 5.0
- Partition table có ít nhất 2 phân vùng OTA (`ota_0`, `ota_1`)
- Component `app_update`, `esp_http_client`
