set(sources
    "dns_server.cc"
    "ssid_manager.cc"
    "wifi_configuration_ap.cc"
    "wifi_manager.cc"
    "wifi_station.cc")

idf_component_register(SRCS "${sources}"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server nvs_flash esp_wifi esp_timer esp_event esp_netif json
                    EMBED_FILES "assets/wifi_configuration.html" "assets/wifi_configuration_done.html")
