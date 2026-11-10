#pragma once

// Coglet C3 and C6: Seeed XIAO ESP32-C3 and ESP32-C6, chosen by the build
// target. See hardware/WIRING.md before changing pins.
// C3 pads: D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5, D4=GPIO6, D5=GPIO7, D10=GPIO10.
// C6 pads: D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO21, D4=GPIO22, D5=GPIO23, D10=GPIO18.

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32C6
#define BOARD_TYPE "coglet-c6"
#define BOARD_NAME "coglet-c6"
#define BOARD_MANIFEST "manifest-coglet-c6.json"
#elif CONFIG_IDF_TARGET_ESP32C3
#define BOARD_TYPE "coglet-c3"
#define BOARD_NAME "coglet-c3"
#define BOARD_MANIFEST "manifest-coglet-c3.json"
#else
#error "firmware/app builds for the ESP32-C3 or ESP32-C6; the S3 builds from the xiaozhi-esp32 fork"
#endif

// Every Coglet board uses the same pads (hardware/WIRING.md), laid out by the
// S3 Coglet; only these GPIO numbers differ per board.
//
// INMP441 mic and MAX98357A amp share BCLK and WS; each has its own data line.
// D2 is the word clock and D3 carries mic data. Swapped, the amp runs without
// an LRC (it hisses) and the mic reads a clock.
#define AUDIO_SAMPLE_RATE   16000
#if CONFIG_IDF_TARGET_ESP32C6
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_1   // D1, shared
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_2   // D2, shared
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_0   // D0 -> amp DIN
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_21  // D3 <- mic SD
#else
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_3   // D1, shared
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_4   // D2, shared
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_2   // D0 -> amp DIN
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_5   // D3 <- mic SD
#endif

// Onboard boot button, active low. Also a strapping pin: input only.
#define BOOT_BUTTON_GPIO    GPIO_NUM_9

// The XIAO ESP32-C6 routes its radio through an RF switch. GPIO3 low enables
// it and GPIO14 low picks the on-board antenna. Both are driven at boot, before
// Wi-Fi starts, and are not available as pads.
#if CONFIG_IDF_TARGET_ESP32C6
#define BOARD_HAS_RF_SWITCH 1
#define RF_SWITCH_ENABLE_GPIO GPIO_NUM_3
#define RF_ANTENNA_SELECT_GPIO GPIO_NUM_14
#else
#define BOARD_HAS_RF_SWITCH 0
#endif

// PCA9685 on I2C controller 0, on the XIAO default I2C pads:
// SDA D4, SCL D5. Swapped, nothing answers on the bus.
#define SERVO_I2C_PORT      I2C_NUM_0
#if CONFIG_IDF_TARGET_ESP32C6
#define SERVO_I2C_SDA_PIN   GPIO_NUM_22  // D4
#define SERVO_I2C_SCL_PIN   GPIO_NUM_23  // D5
// /OE on D10 (GPIO18), which is not a strapping pin.
#define SERVO_OE_PIN        GPIO_NUM_18
#else
#define SERVO_I2C_SDA_PIN   GPIO_NUM_6   // D4
#define SERVO_I2C_SCL_PIN   GPIO_NUM_7   // D5
// /OE on D10. GPIO8 and GPIO9 are strapping pins, so neither is a reset-safe
// output enable.
#define SERVO_OE_PIN        GPIO_NUM_10
#endif
#define SERVO_PCA9685_ADDR  0x40

// Standard servo wiring, PCA9685 channel per role in role order:
//   0 base   1 neck_tilt  2 neck_roll  3 eye_pan  4 eye_tilt
//   5 lids   6 jaw        7 ear_left   8 ear_right
// "!coglet fit ROLE" assigns a fitted servo from this table.
#define SERVO_ROLE_CHANNELS {0, 1, 2, 3, 4, 5, 6, 7, 8}
