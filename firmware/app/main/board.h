#pragma once

// Coglet C3: Seeed XIAO ESP32-C3. See hardware/WIRING.md before changing pins.
// C3 pads: D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5, D4=GPIO6, D5=GPIO7, D10=GPIO10.

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#define BOARD_TYPE "coglet-c3"
#define BOARD_NAME "coglet-c3"
#define BOARD_MANIFEST "manifest-coglet-c3.json"

// Every Coglet board uses the same pads (hardware/WIRING.md), laid out by the
// S3 Coglet; only these GPIO numbers differ per board.
//
// INMP441 mic and MAX98357A amp share BCLK and WS; each has its own data line.
// D2 is the word clock and D3 carries mic data. Swapped, the amp runs without
// an LRC (it hisses) and the mic reads a clock.
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_3   // D1, shared
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_4   // D2, shared
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_2   // D0 -> amp DIN
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_5   // D3 <- mic SD

// Onboard boot button, active low. Also a strapping pin: input only.
#define BOOT_BUTTON_GPIO    GPIO_NUM_9

// PCA9685 on the C3's only I2C controller, on the XIAO default I2C pads:
// SDA D4, SCL D5. Swapped, nothing answers on the bus.
#define SERVO_I2C_PORT      I2C_NUM_0
#define SERVO_I2C_SDA_PIN   GPIO_NUM_6   // D4
#define SERVO_I2C_SCL_PIN   GPIO_NUM_7   // D5
// /OE on D10. GPIO8 and GPIO9 are strapping pins, so neither is a reset-safe
// output enable.
#define SERVO_OE_PIN        GPIO_NUM_10
#define SERVO_PCA9685_ADDR  0x40

// Standard servo wiring, PCA9685 channel per role in role order:
//   0 base   1 neck_tilt  2 neck_roll  3 eye_pan  4 eye_tilt
//   5 lids   6 jaw        7 ear_left   8 ear_right
// "!coglet fit ROLE" assigns a fitted servo from this table.
#define SERVO_ROLE_CHANNELS {0, 1, 2, 3, 4, 5, 6, 7, 8}
