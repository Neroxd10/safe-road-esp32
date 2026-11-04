idf_component_register(SRCS "main.cpp" "alerts.cpp" "hardware.cpp" INCLUDE_DIRS "." EMBED_FILES "index.html"
 REQUIRES esp-dl esp32-camera esp_new_jpeg bootloader_support esp_hw_support esp_wifi esp_event esp_netif esp_http_server nvs_flash json esp_http_client esp-tls mbedtls esp_driver_gpio esp_driver_ledc esp_driver_i2c)
foreach(model detector landmark)
 target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/${model}.espdl" BINARY ALIGN 16)
endforeach()
