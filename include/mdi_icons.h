#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t mdi_16;
extern const lv_font_t mdi_20;
extern const lv_font_t mdi_22;
extern const lv_font_t mdi_24;
extern const lv_font_t mdi_28;

#define MDI_COG                         "\xEE\x80\x80" // U+E000
#define MDI_WEATHER_PARTLY_RAINY        "\xEE\xA4\x80" // U+E900
#define MDI_FLASH                       "\xEE\xA4\x81" // U+E901
#define MDI_GPU                         "\xEE\xA4\x82" // U+E902
#define MDI_CHIP                        "\xEE\xA4\x83" // U+E903
#define MDI_THERMOMETER                 "\xEE\xA4\x84" // U+E904
#define MDI_RAIN_CHANCE_OUTLINE         "\xEE\xA4\x85" // U+E905
#define MDI_WATER                       "\xEE\xA4\x86" // U+E906
#define MDI_WATER_PERCENT               "\xEE\xA4\x87" // U+E907
#define MDI_WINDY                       "\xEE\xA4\x88" // U+E908
#define MDI_WEATHER_SNOWY               "\xEE\xA4\x89" // U+E909
#define MDI_WEATHER_LIGHTNING           "\xEE\xA4\x8A" // U+E90A
#define MDI_WEATHER_HAIL                "\xEE\xA4\x8B" // U+E90B
#define MDI_WEATHER_RAINY               "\xEE\xA4\x8C" // U+E90C
#define MDI_WEATHER_SUNNY               "\xEE\xA4\x8D" // U+E90D
#define MDI_WEATHER_CLOUDY              "\xEE\xA4\x8E" // U+E90E
#define MDI_WEATHER_PARTLY_CLOUDY       "\xEE\xA4\x8F" // U+E90F
#define MDI_WEATHER_NIGHT               "\xEE\xA4\x90" // U+E910
#define MDI_WEATHER_NIGHT_PARTLY_CLOUDY "\xEE\xA4\x91" // U+E911

#ifdef __cplusplus
}
#endif