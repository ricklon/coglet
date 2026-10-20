// Coglet firmware: a voice-and-servo robot that agent-hub drives as an MCP
// device. Every boot stays released: nothing here moves a servo.

#include "board.h"
#include "console.h"
#include "mcp.h"
#include "session.h"
#include "settings.h"
#include "wifi.h"
#include "coglet_controller.h"

#include <driver/gpio.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#define TAG "coglet"

namespace {

// A click on the boot button stops a reply that's playing. GPIO9 is a
// strapping pin, so it's only ever read.
void ButtonTask(void*) {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&config);
    int held = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            held++;
        } else {
            if (held >= 2 && held < 50) session::Interrupt();  // 40 ms .. 1 s
            held = 0;
        }
    }
}

}  // namespace

extern "C" void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unreadable; erasing it (Wi-Fi, hub URL and calibration are lost)");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "Coglet %s on %s, device id %s", esp_app_get_description()->version,
             BOARD_NAME, device::MacAddress().c_str());

    // Released: /OE high, all channels off, until a person engages it.
    CogletController::Instance().Start({SERVO_I2C_PORT, SERVO_I2C_SDA_PIN, SERVO_I2C_SCL_PIN,
                                        SERVO_OE_PIN, SERVO_PCA9685_ADDR});
    mcp::RegisterTools();
    console::Start();
    session::Start();
    wifi::Start(session::NetworkUp, session::NetworkDown);
    xTaskCreate(ButtonTask, "button", 2048, nullptr, 4, nullptr);
}
