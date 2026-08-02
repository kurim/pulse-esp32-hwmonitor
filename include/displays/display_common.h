#pragma once

// Gemeinsames Fundament fuer die SPI-TFT-Treiber unter include/displays/.
// Wird von mehreren Display-Headern gleichzeitig eingebunden (Ziel:
// alle in einem Firmware-Image, Auswahl zur Laufzeit) - alles hier drin
// muss daher kollisionsfrei mehrfach inkludierbar sein (keine gleich
// benannten globalen Variablen/Funktionen ausserhalb von Namespaces).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "pin_override.h"

// VSPI existiert nur auf dem klassischen ESP32 (siehe CLAUDE.md, Arduino_GFX-
// Fallstrick zu spi_num/FSPI/HSPI) - FSPI ist dort der Flash-Bus, auf
// S2/S3/C2/C3/... dagegen der korrekte zweite SPI-Host. Ein gemeinsamer
// Konstantenname haelt die drei Display-Header (gc9a01.h, ili9488.h,
// st7796s.h) portabel, ohne dass jeder einzeln zwischen den Targets
// unterscheiden muss.
#if CONFIG_IDF_TARGET_ESP32
inline constexpr uint8_t kGenericSpiHost = VSPI;
#else
inline constexpr uint8_t kGenericSpiHost = FSPI;
#endif

// Wendet Pin-Overrides aus app_config.pin_overrides (siehe shared_state.h,
// per Webportal editierbar) auf die Default-Pins eines Displaytreibers an.
// override darf nullptr sein (kein Override verfuegbar/gewuenscht - Defaults
// bleiben unangetastet). Nur Felder != PIN_UNSET ersetzen den Default, siehe
// CLAUDE.md ("PIN_UNSET, nicht -1, markiert kein Override").
inline void applyPinOverrides(const pin_override_t *override, pin_override_t &out) {
  if (!override) return;

  if (override->mosi != PIN_UNSET) out.mosi = override->mosi;
  if (override->miso != PIN_UNSET) out.miso = override->miso;
  if (override->sclk != PIN_UNSET) out.sclk = override->sclk;
  if (override->cs   != PIN_UNSET) out.cs   = override->cs;
  if (override->dc   != PIN_UNSET) out.dc   = override->dc;
  if (override->rst  != PIN_UNSET) out.rst  = override->rst;
  if (override->bl   != PIN_UNSET) out.bl   = override->bl;
}

// Gemeinsame LVGL-Anbindung fuer alle Arduino_GFX-SPI-Panels hier: kein
// adressierbarer Framebuffer wie beim RGB-Panel, also partielles Rendern
// in einen kleinen Draw-Buffer im internen RAM, Flush schiebt jede
// Kachel per SPI raus.
inline void spiPanelFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  Arduino_GFX *panel = static_cast<Arduino_GFX *>(lv_display_get_user_data(disp));
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  panel->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

inline lv_display_t *initLvglDisplaySPI(Arduino_GFX *panel, int16_t w, int16_t h, size_t bufLines = 20) {
  static lv_color_t *drawBuf = nullptr;
  drawBuf = (lv_color_t *)malloc(sizeof(lv_color_t) * w * bufLines);

  lv_display_t *disp = lv_display_create(w, h);
  lv_display_set_user_data(disp, panel);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(lv_color_t) * w * bufLines,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, spiPanelFlushCb);
  return disp;
}
