idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES khoa_wifi_connect khoa_ota_update driver nvs_flash espressif__button)
