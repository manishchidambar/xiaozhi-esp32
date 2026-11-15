#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// DeskBuddy on ESP32-C3 SuperMini (4MB, no PSRAM)
// Onboard: BOOT=GPIO9, blue LED=GPIO8 (unused)
// C3 has only ONE I2S port, so mic (INMP441) and amp (MAX98357A)
// share BCLK and WS in duplex mode. Duplex clock = output rate,
// so input and output rates must match.
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_7   // amp BCLK + mic SCK
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_4   // amp LRC  + mic WS
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_1   // mic SD
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_3   // amp DIN

#define BOOT_BUTTON_GPIO    GPIO_NUM_9   // onboard BOOT button
#define TOUCH_BUTTON_GPIO   GPIO_NUM_10  // TTP223 OUT (active HIGH)

#define DISPLAY_SDA_PIN GPIO_NUM_5
#define DISPLAY_SCL_PIN GPIO_NUM_6
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define SH1106
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

#endif // _BOARD_CONFIG_H_
