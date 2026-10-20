idf_component_register(
    SRCS "main.cc" "settings.cc" "wifi.cc" "hub.cc" "audio.cc" "mcp.cc"
         "session.cc" "console.cc"
    INCLUDE_DIRS "."
    REQUIRES coglet_controller esp_wifi esp_netif esp_event nvs_flash
             esp_http_client esp-tls esp_driver_i2s esp_driver_gpio
             esp_driver_usb_serial_jtag vfs json esp_timer esp_app_format
             spi_flash mbedtls)
