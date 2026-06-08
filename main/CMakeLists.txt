# CMakeLists.txt para el componente main

idf_component_register(
    SRCS
        "main.cpp"
        "wifi_manager.cpp"
        "bridge_rs485.cpp"
        "can_bus.cpp"
        "status_led.cpp"
        "relay_control.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_wifi
        esp_http_server
        esp_timer
        esp_netif
        esp_event
        nvs_flash
        lwip
        led_strip
)
