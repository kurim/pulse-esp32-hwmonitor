#pragma once
// ------------------------------------------------------------------
// Pinbelegung ESP32-2432S028 (CYD - "Cheap Yellow Display")
// Display (ILI9341) und Touch (XPT2046) haengen auf getrennten SPI-Bussen.
// ------------------------------------------------------------------

// ---- Display (ILI9341) auf SPI2_HOST ----
#define TFT_SPI_HOST      SPI2_HOST
#define TFT_PIN_MOSI      13
#define TFT_PIN_MISO      12
#define TFT_PIN_SCLK      14
#define TFT_PIN_CS        15
#define TFT_PIN_DC        2
#define TFT_PIN_RST       (-1)   // nicht verdrahtet
#define TFT_PIN_BL        21
#define TFT_BL_ON_LEVEL   1      // Backlight aktiv HIGH

#define TFT_H_RES         320
#define TFT_V_RES         240
#define TFT_SPI_HZ        (40 * 1000 * 1000)  // 40 MHz Pixeltakt

// ---- Touch (XPT2046) auf SPI3_HOST (eigene Pins) ----
#define TOUCH_SPI_HOST    SPI3_HOST
#define TOUCH_PIN_CLK     25
#define TOUCH_PIN_MOSI    32
#define TOUCH_PIN_MISO    39
#define TOUCH_PIN_CS      33
#define TOUCH_PIN_IRQ     36
#define TOUCH_SPI_HZ      (2 * 1000 * 1000)   // 2 MHz
