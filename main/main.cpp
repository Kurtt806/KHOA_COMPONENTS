#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "driver/gpio.h"

// Include các lib
#include "iot_button.h"
#include "button_gpio.h"

// Include các component
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "ota_manager.h"

static const char *TAG = "APP_MAIN";

#define BUTTON_GPIO GPIO_NUM_0


void init_nvs(void){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Da khoi tao NVS thanh cong");
}

void init_wifi(void){
    ESP_LOGI(TAG, "Khoi tao WifiManager...");

    // Lấy WifiManager Instance
    auto& manager = WifiManager::GetInstance();

    // Cấu hình tiền tố cho tên trạm phát WiFi (AP)
    WifiManagerConfig config;
    config.ssid_prefix = "KHOA-WIFI"; // Tên AP sẽ là KHOA-WIFI_XXXX
    manager.Initialize(config);

    // Đăng ký Callback lắng nghe các sự kiện WiFi
    manager.SetEventCallback([](WifiEvent event) {
        switch (event) {
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "Da ket noi WiFi thanh cong!");
                // Gọi kiểm tra cập nhật (OTA) ngay khi có mạng (IP)
                OtaManager::GetInstance().CheckOnBoot(
                    WifiManager::GetInstance().GetOtaUrl()
                );
                break;
            case WifiEvent::ConfigModeEnter:
                ESP_LOGW(TAG, "Vao che do cau hinh AP (192.168.4.1)");
                break;
            default:
                break;
        }
    });

    // Kiểm tra danh sách WiFi đã lưu trong bộ nhớ
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

void init_button(void){
    // Khởi tạo các tham số theo thứ tự cấu trúc để tránh warning missing-field-initializers
    button_config_t btn_cfg = {
        3000,   // long_press_time
        50      // short_press_time
    };

    button_gpio_config_t gpio_cfg = {
        BUTTON_GPIO, // gpio_num
        0,           // active_level
        false,       // enable_power_save
        false        // disable_pull
    };

    button_handle_t btn_handle = NULL;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle);

    if (err == ESP_OK && btn_handle) {
        // Đăng ký callback khi nút được nhấn xuống
        iot_button_register_cb(btn_handle, BUTTON_PRESS_DOWN, NULL, [](void *arg, void *data) {
            ESP_LOGW("BUTTON", "Nut BOOT/Config (GPIO0) duoc nhan. Bat buoc vao che do AP...");
            auto& manager = WifiManager::GetInstance();
            manager.StartConfigAp(); 
        }, NULL);
        ESP_LOGI(TAG, "Da khoi tao nut bam thanh cong tren GPIO%d", BUTTON_GPIO);
    } else {
        ESP_LOGE(TAG, "Khong the khoi tao nut BOOT: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Hàm chính của ứng dụng
 * Hàm này sẽ được ESP-IDF gọi lên đầu tiên. Cần giữ kiểu extern "C" do 
 * giới hạn liên kết (C linkage) của hệ điều hành.
 */
extern "C" void app_main(void)
{
    // 1. Khởi tạo bộ nhớ NVS (Cần thiết trước khi dùng WiFi)
    init_nvs();

    // 2. Cấu hình và kết nối WiFi
    init_wifi();

    // 3. Khởi tạo nút logic chọn chế độ
    init_button();
}

// khoa_wifi_connect TOKEN
// $env:IDF_COMPONENT_API_TOKEN="mZz8Rz5Q3YDt2f4s4EL0VPa9mk3YfFk6p1dNV7L-bIQTrwNmLTJrFpDRo4lD-FUgSdAQfLXegHdBONe44UYB5Q"