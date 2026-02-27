/*
 * OTA Download - Bước 3: Tải và ghi firmware OTA
 * Kết nối HTTP(S) → đọc từng chunk → ghi vào phân vùng OTA
 */

#include "ota_common.h"

/// Thực hiện tải và ghi firmware OTA nội bộ (Bước 3)
esp_err_t OtaManager::PerformOta() {
    esp_err_t err;

    // === Kiểm tra phân vùng đích ===
    NotifyProgress(OtaState::Downloading, 0, 0, 0, "Dang kiem tra phan vung...");

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(running);

    if (update_partition == nullptr) {
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong tim thay phan vung cap nhat!");
        ESP_LOGE(TAG, "Khong tim thay phan vung OTA tiep theo!");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Phan vung dang chay: %s (offset 0x%08" PRIx32 ")", 
             running->label, running->address);
    ESP_LOGI(TAG, "Phan vung cap nhat: %s (offset 0x%08" PRIx32 ")", 
             update_partition->label, update_partition->address);

    // === Kết nối HTTP và tải firmware ===
    NotifyProgress(OtaState::Downloading, 0, 0, 0, "Dang ket noi server...");

    esp_http_client_config_t http_config = {};
    http_config.url = config_.url.c_str();
    http_config.timeout_ms = config_.timeout_ms;
    http_config.max_redirection_count = 3;
    http_config.keep_alive_enable = true;
    configure_ssl(http_config, config_.cert_pem);

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the khoi tao HTTP client!");
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the ket noi server firmware: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the ket noi server!");
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "Server tra ve loi HTTP %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Server tra ve loi HTTP!");
        return ESP_FAIL;
    }

    // === Bắt đầu ghi OTA ===
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin that bai: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Khong the bat dau ghi OTA!");
        return err;
    }

    // === Tải và ghi firmware từng phần ===
    size_t total_bytes = (content_length > 0) ? (size_t)content_length : 0;
    size_t downloaded = 0;
    int last_percent = -1;

    char *buffer = (char *)malloc(config_.buffer_size);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Khong du bo nho cap phat buffer!");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        NotifyProgress(OtaState::Failed, 0, 0, 0, "Loi cap phat bo nho!");
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        // Kiểm tra yêu cầu hủy
        bool is_aborted = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_aborted = abort_requested_;
        }

        if (is_aborted) {
            ESP_LOGW(TAG, "Cap nhat OTA bi huy boi nguoi dung!");
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            NotifyProgress(OtaState::Idle, 0, 0, 0, "Da huy cap nhat!");
            return ESP_ERR_OTA_ROLLBACK_FAILED;
        }

        int read_len = esp_http_client_read(client, buffer, config_.buffer_size);

        if (read_len < 0) {
            ESP_LOGE(TAG, "Loi doc du lieu HTTP!");
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi doc du lieu!");
            return ESP_FAIL;
        }

        if (read_len == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                ESP_LOGI(TAG, "Da tai xong firmware!");
                break;
            }
            ESP_LOGE(TAG, "Ket noi bi ngat truoc khi tai xong!");
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Ket noi bi ngat!");
            return ESP_FAIL;
        }

        // Ghi dữ liệu vào phân vùng OTA
        err = esp_ota_write(ota_handle, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write that bai: %s", esp_err_to_name(err));
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi ghi firmware!");
            return err;
        }

        downloaded += read_len;

        // Cập nhật tiến trình (chỉ khi phần trăm thay đổi để tránh spam)
        if (total_bytes > 0) {
            int percent = (int)((downloaded * 100) / total_bytes);
            if (percent != last_percent) {
                last_percent = percent;
                NotifyProgress(OtaState::Downloading, percent, downloaded, total_bytes, "Dang tai firmware...");
            }
        } else {
            // Trường hợp server không trả Content-Length (Chunked Transfer), log update mỗi 50KB
            if ((int)downloaded - last_percent >= 51200) {
                last_percent = (int)downloaded;
                NotifyProgress(OtaState::Downloading, 0, downloaded, 0, "Dang tai firmware...");
            }
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Tong cong da tai: %zu bytes", downloaded);

    // === Xác minh và hoàn tất ===
    NotifyProgress(OtaState::Verifying, 100, downloaded, total_bytes, "Dang xac minh firmware...");

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Firmware khong hop le (checksum sai)!");
        } else {
            ESP_LOGE(TAG, "esp_ota_end that bai: %s", esp_err_to_name(err));
        }
        NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Firmware khong hop le!");
        return err;
    }

    // Đặt phân vùng boot mới
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition that bai: %s", esp_err_to_name(err));
        NotifyProgress(OtaState::Failed, 0, downloaded, total_bytes, "Loi dat phan vung boot!");
        return err;
    }

    NotifyProgress(OtaState::Ready, 100, downloaded, total_bytes, 
                   "Cap nhat thanh cong! Can khoi dong lai.");
    ESP_LOGI(TAG, "Firmware moi da san sang tai phan vung: %s", update_partition->label);

    return ESP_OK;
}
