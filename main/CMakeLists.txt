# CMakeLists.txt para el componente main

idf_component_register(
    SRCS
        "main.cpp"
        "wifi_manager.cpp"
        "bridge_rs485.cpp"
        "can_bus.cpp"
        "stepper_control.cpp"
        "stepper_motor_encoder.c"
        "status_led.cpp"
    INCLUDE_DIRS
        "."
        "../components/FastAccelStepper/src"
    REQUIRES
        driver
        esp_wifi
        esp_http_server
        esp_netif
        esp_event
        nvs_flash
        lwip
        led_strip
)
