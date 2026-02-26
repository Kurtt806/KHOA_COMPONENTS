set(sources
    "ota_manager.cc")

idf_component_register(SRCS "${sources}"
                    INCLUDE_DIRS "include"
                    REQUIRES app_update esp_http_client esp_https_ota esp_event nvs_flash json esp_hw_support spi_flash mbedtls)
