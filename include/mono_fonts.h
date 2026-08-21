#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tamzen 8x16 Bold (sunaku/tamzen-font, freie Lizenz - siehe
// src/fonts/tamzen_8x16_bold.c) - 8px Vorschubbreite statt der 16px von
// lv_font_unscii_16, bei gleicher Hoehe. Fuer die "big"-Variante der
// Mono-HW-Karte (build_mono_hw_card()): schmaler als unscii_16 macht neben
// dreistelligen Werten noch Platz fuer ein 7x7-Icon in 64px Breite, ohne
// auf unscii_8 (kein "big" Eindruck mehr) zurückfallen zu muessen. 1bpp,
// kein Antialiasing - passt zum sonst durchgehend AA-freien Mono-Look.
extern const lv_font_t tamzen_8x16_bold;

#ifdef __cplusplus
}
#endif
