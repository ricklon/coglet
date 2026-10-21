# ESP-IDF component for firmware/app. The host tests compile the source
# directly and don't read this file.
idf_component_register(
    SRCS "coglet_controller.cc"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_gpio esp_timer nvs_flash json log)
