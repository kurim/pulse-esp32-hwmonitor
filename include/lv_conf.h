// LVGL9-Konfiguration fuer den Arduino/PlatformIO-Rewrite - Aequivalent zu
// den LVGL-CONFIG_LV_*-Optionen in sdkconfig.defaults (esp-idf-Branch).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_CHART   1
#define LV_USE_ARC     1
#define LV_USE_BAR     1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8      1 // fuer das monochrome SSD1309-UI (Phase 2)

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 0
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_MEM_SIZE (48 * 1024U)

#endif // LV_CONF_H
