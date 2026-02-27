#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "driver/gpio.h"

// Include các component
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "ota_manager.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "APP_MAIN";

#define BUTTON_GPIO GPIO_NUM_0


// Sử dụng extern "C" để tương thích với app_main của ESP-IDF (C linkage)
extern "C" void app_main(void)
{
    // 1. Khởi tạo NVS (Bắt buộc cho WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Khoi tao WifiManager...");

    // 2. Lấy WifiManager Instance
    auto& manager = WifiManager::GetInstance();

    // 3. Cấu hình
    WifiManagerConfig config;
    config.ssid_prefix = "KHOA-WIFI"; // Tên AP sẽ là KHOA-WIFI_XXXX
    manager.Initialize(config);

    // 4. Đăng ký nhận sự kiện (Callback)
    manager.SetEventCallback([](WifiEvent event) {
        switch (event) {
            case WifiEvent::Scanning:
                ESP_LOGI(TAG, "Dang quet WiFi...");
                break;
            case WifiEvent::Connecting:
                ESP_LOGI(TAG, "Dang ket noi WiFi...");
                break;
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "Da ket noi WiFi thanh cong!");
                // Gọi OTA ngay khi có IP
                OtaManager::GetInstance().CheckOnBoot(
                    WifiManager::GetInstance().GetOtaUrl()
                );
                break;
            case WifiEvent::Disconnected:
                ESP_LOGW(TAG, "Mat ket noi WiFi!");
                break;
            case WifiEvent::ConfigModeEnter:
                ESP_LOGW(TAG, ">>> Vao che do cau hinh AP (192.168.4.1) <<<");
                break;
            case WifiEvent::ConfigModeExit:
                ESP_LOGI(TAG, "Thoat che do cau hinh");
                break;
        }
    });

    // 5. Logic tự động chọn chế độ
    // Sử dụng thư viện iot_button
    button_config_t btn_cfg = {
        .long_press_time = 3000,
        .short_press_time = 50,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO,
        .active_level = 0,
    };

    button_handle_t btn_handle = NULL;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle);

    if (err == ESP_OK && btn_handle) {
        // Đăng ký callback khi nhấn nút (PRESS_DOWN hoặc PRESS_UP tùy nhu cầu, ở đây dùng PRESS_DOWN để nhạy)
        iot_button_register_cb(btn_handle, BUTTON_PRESS_DOWN, NULL, [](void *arg, void *data) {
            ESP_LOGW("BUTTON", "Nut BOOT/Config (GPIO6) duoc nhan. Bat buoc vao che do AP...");
            auto& manager = WifiManager::GetInstance();
            manager.StartConfigAp(); 
        }, NULL);
    } else {
        ESP_LOGE(TAG, "Khong the khoi tao nut BOOT: %s", esp_err_to_name(err));
    }

    // Kiểm tra xem đã lưu WiFi nào chưa
    auto& ssid_list = SsidManager::GetInstance().GetSsidList();

    if (ssid_list.empty()) {
        ESP_LOGW(TAG, "Chua co WiFi nao duoc luu. Bat che do AP...");
        manager.StartConfigAp();
    } else {
        ESP_LOGI(TAG, "Tim thay WiFi da luu: %s. Dang ket noi...", ssid_list[0].ssid.c_str());
        manager.StartStation();
    }

    // --- Thông số cấu hình nâng cao ---
    ESP_LOGI(TAG, "VIBOKEY: %s", manager.GetViboKey().c_str());
    ESP_LOGI(TAG, "GSheet1: %s", manager.GetGoogleSheetUrl1().c_str());
    ESP_LOGI(TAG, "GSheet2: %s", manager.GetGoogleSheetUrl2().c_str());
    ESP_LOGI(TAG, "OTA_URL: %s", manager.GetOtaUrl().c_str());

}

// khoa_wifi_connect TOKEN
// $env:IDF_COMPONENT_API_TOKEN="mZz8Rz5Q3YDt2f4s4EL0VPa9mk3YfFk6p1dNV7L-bIQTrwNmLTJrFpDRo4lD-FUgSdAQfLXegHdBONe44UYB5Q"