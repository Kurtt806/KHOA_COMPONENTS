# KHOA_WIFI_CONNECT - Bộ công cụ kết nối WiFi thông minh cho ESP-IDF

Bộ component này giúp quản lý kết nối WiFi hoàn chỉnh cho ESP, bao gồm chế độ Station (kết nối WiFi nhà) và Access Point (phát WiFi cấu hình qua giao diện Web hiện đại phong cách VS Code).

## Các Tính Năng Chính

- **Giao diện Web cấu hình (Captive Portal)**: Tự động hiện trang cấu hình khi kết nối vào WiFi của ESP.
- **Phong cách VS Code Dark Mode**: Giao diện Moss Green chuyên nghiệp, hỗ trợ tiếng Việt.
- **Cấu hình nâng cao**: Lưu trữ VIBO-KEY (8 số), Google Sheet 1 & 2, và URL OTA tùy chỉnh.
- **Quản lý đa mạng**: Lưu trữ và tự động kết nối lại nhiều mạng WiFi đã lưu.
- **Tùy chỉnh công suất phát**: Điều chỉnh công suất WiFi tối ưu (mặc định 20dBm).

---

## Hướng dẫn sử dụng

### 1. Khởi tạo và Cấu hình

```cpp
#include "wifi_manager.h"

// 1. Lấy instance duy nhất
auto& wifi = WifiManager::GetInstance();

// 2. Cấu hình (tùy chọn)
WifiManagerConfig config;
config.ssid_prefix = "KHOA_WIFI"; // Tên WiFi phát ra (VD: KHOA_WIFI-A1B2)
config.language = "vi-VN";         // Ngôn ngữ giao diện Web

// 3. Khởi tạo
wifi.Initialize(config);
```

### 2. Quản lý sự kiện (Callbacks)

Sử dụng callback để theo dõi trạng thái mạng:

```cpp
wifi.SetEventCallback([](WifiEvent event) {
    switch (event) {
        case WifiEvent::Scanning:
            ESP_LOGI("APP", "Đang quét WiFi...");
            break;
        case WifiEvent::Connected:
            ESP_LOGI("APP", "Đã kết nối! IP: %s", WifiManager::GetInstance().GetIpAddress().c_str());
            break;
        case WifiEvent::ConfigModeEnter:
            ESP_LOGI("APP", "Đã bật chế độ cấu hình AP");
            break;
    }
});
```

### 3. Điều khiển Chế độ

```cpp
// Chế độ Station (Kết nối WiFi đã lưu)
wifi.StartStation();

// Chế độ Config AP (Phát WiFi để cài đặt)
wifi.StartConfigAp();

// Dừng các chế độ
wifi.StopStation();
wifi.StopConfigAp();
```

---

## Danh sách API chính (Dùng trong code Main)

### Thông tin kết nối

- `bool IsConnected()`: Kiểm tra đã có mạng hay chưa.
- `std::string GetIpAddress()`: Lấy địa chỉ IP hiện tại.
- `std::string GetSsid()`: Lấy tên WiFi đang kết nối.
- `int GetRssi()`: Lấy độ mạnh tín hiệu (dBm).
- `std::string GetMacAddress()`: Lấy địa chỉ MAC của thiết bị.

### Lấy thông số cấu hình nâng cao (Lưu trong NVS)

Các hàm này cực kỳ hữu ích để gọi ra sử dụng trong logic ứng dụng:

- `std::string GetViboKey()`: Lấy mã VIBO-KEY (8 ký tự số).
- `std::string GetGoogleSheetUrl1()`: Lấy link Google Sheet 1.
- `std::string GetGoogleSheetUrl2()`: Lấy link Google Sheet 2.
- `std::string GetOtaUrl()`: Lấy link Server cập nhật Firmware OTA.

---

## Tính năng Nút nhấn (BOOT/Config)

Component này được thiết kế để tích hợp với thư viện `iot_button`. Bạn có thể dễ dàng gán GPIO 6 làm nút bấm cứng để bắt buộc vào chế độ cấu hình:

```cpp
// Trong main.cpp
button_config_t btn_cfg = { .long_press_time = 3000 };
button_gpio_config_t gpio_cfg = { .gpio_num = 6, .active_level = 0 };
button_handle_t btn_handle;
iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle);

iot_button_register_cb(btn_handle, BUTTON_PRESS_DOWN, NULL, [](void *arg, void *data) {
    WifiManager::GetInstance().StartConfigAp();
}, NULL);
```

---

## Cấu trúc lưu trữ NVS (Namespace: "wifi")

Nếu bạn muốn truy cập trực tiếp qua thư viện NVS của ESP-IDF:

- `vibo_key`: string (max 8)
- `gs_url`: string
- `gs_url_2`: string
- `ota_url`: string
- `max_tx_power`: int8
- `remember_bssid`: u8 (0/1)
- `sleep_mode`: u8 (0/1)
