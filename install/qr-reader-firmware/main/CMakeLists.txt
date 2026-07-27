idf_component_register(
  SRCS
    "app_main.cpp"
    "app_state.cpp"
    "clock_service.cpp"
    "config_json.cpp"
    "door_contact.cpp"
    "ds3231_driver.cpp"
    "exit_button.cpp"
    "http_server.cpp"
    "identity_client.cpp"
    "qr_reader.cpp"
    "relay.cpp"
    "scan_service.cpp"
    "storage.cpp"
    "sync_task.cpp"
    "wifi_manager.cpp"
  INCLUDE_DIRS "."
  EMBED_FILES "web/setup.html" "web/wifi.html"
  REQUIRES
    viaaccess_core
    app_update
    driver
    esp_driver_i2c
    esp_http_client
    esp_http_server
    esp_netif
    esp_timer
    esp_wifi
    esp-tls
    json
    nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
