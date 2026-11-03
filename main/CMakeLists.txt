idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." EMBED_FILES "index.html"
    REQUIRES bootloader_support esp_hw_support esp_wifi esp_event esp_netif esp_http_server nvs_flash json)
