#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <gt911_lite.h>
// -----------------------------------------------------------------------------
// Pin-Definitionen Guition JC8048W550
// -----------------------------------------------------------------------------
#define BOARD_NAME "Guition JC8048W550"
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCH 1
#define BOARD_HAS_SD 1
#define LCD_WIDTH 800
#define LCD_HEIGHT 480
// RGB Timing / Control
#define GFX_DE 40
#define GFX_VSYNC 41
#define GFX_HSYNC 39
#define GFX_PCLK 42
// RGB 565 Data Pins
#define GFX_R0 45
#define GFX_R1 48
#define GFX_R2 47
#define GFX_R3 21
#define GFX_R4 14
#define GFX_G0 5
#define GFX_G1 6
#define GFX_G2 7
#define GFX_G3 15
#define GFX_G4 16
#define GFX_G5 4
#define GFX_B0 8
#define GFX_B1 3
#define GFX_B2 46
#define GFX_B3 9
#define GFX_B4 1

// Backlight Pin (PWM-faehig)
#define GFX_BL 2
#define BOARD_HAS_BACKLIGHT_PWM 1

// Touch (GT911) & SD Card Defs
#define TOUCH_GT911_SDA 19
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_INT 18
#define TOUCH_GT911_RST 38
#define SD_SPI_CS 10
#define SD_SPI_MOSI 11
#define SD_SPI_SCLK 12
#define SD_SPI_MISO 13
#define black RGB565_BLACK
#define white RGB565_WHITE
#define yellow RGB565_YELLOW
#define blue RGB565_BLUE
#define red RGB565_RED

// -----------------------------------------------------------------------------
// Treibereinrichtung (ST7262 RGB-Parallel)
// -----------------------------------------------------------------------------

inline Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    GFX_DE, GFX_VSYNC, GFX_HSYNC, GFX_PCLK,
    GFX_R0, GFX_R1, GFX_R2, GFX_R3, GFX_R4,
    GFX_G0, GFX_G1, GFX_G2, GFX_G3, GFX_G4, GFX_G5,
    GFX_B0, GFX_B1, GFX_B2, GFX_B3, GFX_B4,
      1, 40, 48, 40,        // hsync_polarity (1), front_porch (40), pulse_width (48), back_porch (40)
      1, 13, 1, 31,         // vsync_polarity (1), front_porch (13), pulse_width (1), back_porch (31)
      1,                    // pclk_active_neg (1 = fallende Flanke)
      16000000,             // prefer_speed (16 MHz ist Standard für dieses Panel!)
      false,                // useBigEndian
      0,                    // de_idle_high
      0,                    // pclk_idle_high
      800 * 40              // bounce_buffer_size_px (16 Zeilen Bounce Buffer)
);

inline Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbpanel, 0, true
);

// -----------------------------------------------------------------------------
// Initialisierungs-Funktion
// -----------------------------------------------------------------------------

inline bool initBoardDisplay()
{
  static bool isDisplayInitialized = false;
  if (isDisplayInitialized)
    return true;
  // Backlight/Panel-Enable so frueh wie moeglich low, damit interne
  // Spannungswandler des Panels (VGH/VGL o.ae.) Zeit zum Entladen haben -
  // ein reiner EN-Reset des ESP32 schaltet diese Rail nicht ab.
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);
  const uint8_t syncPins[] = {GFX_DE, GFX_VSYNC, GFX_HSYNC, GFX_PCLK};
  for (uint8_t p : syncPins)
  {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  delay(300);
  bool ok = gfx->begin();
  isDisplayInitialized = ok;
  return ok;
}

inline void boardDisplayBacklight(bool on)
{
  digitalWrite(GFX_BL, on ? HIGH : LOW);
}

// Echtes Dimmen per LEDC-PWM statt nur an/aus - siehe cyd_2432s028r.h fuer
// dieselbe Begruendung. Anders als dort aber NICHT 1:1 auf die 8-Bit-PWM-
// Duty abgebildet: dieses Board wird erst oberhalb von ca. 210/255 Duty
// ueberhaupt sichtbar hell, darunter bleibt das Backlight komplett dunkel
// (vermutlich ein Mindest-Duty, den der Backlight-Treiber/Boost-Kreis
// braucht, um ueberhaupt zu regeln - gemessen am Geraet, nicht in den
// Panel-Unterlagen dokumentiert). Ohne Remap waeren ~80% des Reglerwegs
// unbrauchbar (immer "aus"). Deshalb 1..255 auf den tatsaechlich sichtbaren
// Bereich [kMinVisibleDuty..255] strecken, nur level==0 bleibt hart aus.

inline void boardDisplaySetBrightness(uint8_t level)
{
  static bool attached = false;
  if (!attached)
  {
    ledcAttach(GFX_BL, 5000, 8);
    attached = true;
  }
  if (level == 0)
  {
    ledcWrite(GFX_BL, 0);
    return;
  }
  constexpr uint8_t kMinVisibleDuty = 200; // Sicherheitsabstand unter den gemessenen ~210
  uint32_t duty = kMinVisibleDuty + ((uint32_t)(level - 1) * (255 - kMinVisibleDuty)) / 254;
  ledcWrite(GFX_BL, (uint8_t)duty);
}

// -----------------------------------------------------------------------------
// LVGL-Anbindung: RGB-Panel hat einen dauerhaften Framebuffer in PSRAM, den
// die GDMA kontinuierlich ausgibt - LVGL rendert im DIRECT-Modus direkt
// hinein, ein Kopieren in einen separaten Draw-Buffer entfaellt.
// -----------------------------------------------------------------------------
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp32s3/rom/cache.h"
#endif

inline void jc8048w550_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  uint16_t *fb = (uint16_t *)px_map;

  // 1. In BYTES rechnen, um Zeiger-Arithmetik-Fallen bei uint16_t zu vermeiden:
  uint32_t fb_byte_addr = (uint32_t)fb;
  
  // Offset in Bytes von der ersten Zeile bis zur x1 Position
  uint32_t start_byte_offset = ((area->y1 * LCD_WIDTH) + area->x1) * sizeof(uint16_t);
  uint32_t start_addr = fb_byte_addr + start_byte_offset;

  // 2. Gesamtlänge der betroffenen Zeilen in Bytes
  size_t total_bytes = (area->y2 - area->y1 + 1) * LCD_WIDTH * sizeof(uint16_t);

  // 3. Cache Line Alignment (32 Byte Maskierung)
  uint32_t aligned_start = start_addr & ~0x1F;
  size_t aligned_size = (total_bytes + (start_addr - aligned_start) + 31) & ~0x1F;

  // 4. L1-Cache der CPU erzwingen, die Daten sofort in den PSRAM zu flushen
  Cache_WriteBack_Addr(aligned_start, aligned_size);

  // 5. LVGL den Abschluss melden
  lv_display_flush_ready(disp);
}

// -----------------------------------------------------------------------------
// Touch (GT911) - I2C, gepollt statt IRQ-getrieben (gleiche Konvention wie
// cyd_2432s028r.h: kein IRQ-Handler, lv_indev_set_read_cb() reicht bei den
// hier ueblichen LVGL-Refreshraten).
//
// Eine erste Handimplementierung (feste I2C-Adresse 0x5D, RST/INT manuell
// fuer die Adresswahl getoggelt) hat an echter Hardware nicht funktioniert.
// tonywestonuk/gt911-arduino (GT911_Lite) probiert stattdessen BEIDE
// moeglichen Adressen (0x5D/0x14) per echtem Register-Read statt eine davon
// fest anzunehmen, und laesst RST/INT dabei komplett unangetastet - laut
// Bibliotheks-Kommentar NACKt genau diese Adress-Sondierung bei manchen
// ESP32-S3-Kombinationen auf eine leere Adress-Only-Transaktion, was fest
// verdrahtete Adress-Annahmen wie die vorherige Implementierung genau in
// diese Falle laufen laesst. Eigene TwoWire-Instanz statt der globalen Wire
// (analog zur eigenen touchSPI beim CYD, siehe cyd_2432s028r.h).
//
// Rohkoordinaten werden unveraendert (nur constrain(), kein map()) als
// Bildschirmkoordinaten verwendet - Annahme, dass der GT911 dieses
// vorkonfektionierten Panels werksseitig schon auf 800x480 kalibriert ist
// (ueblich bei fertig verklebten Touch+Panel-Modulen wie diesem). NICHT an
// echter Hardware verifiziert - liefert der Touch spuerbar falsche
// Koordinaten, bietet GT911_Lite::begin(&wire, sensitivity, w, h) eine
// dritte Ueberladung, die die Aufloesung direkt in die Chip-Konfiguration
// schreibt.

inline TwoWire touchWire(1);
inline GT911_Lite touchCtrl;
inline void initBoardTouch()
{
  // RST aktiv auf HIGH treiben, um den Chip sicher aus dem Reset zu holen -
  // INT bewusst NICHT angefasst (siehe Begruendung oben, das ist Sache der
  // Bibliothek).
  pinMode(TOUCH_GT911_RST, OUTPUT);
  digitalWrite(TOUCH_GT911_RST, HIGH);
  delay(50);
  touchWire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
  touchWire.setClock(400000);
  touchCtrl.begin(&touchWire);
}

inline void jc8048w550_touch_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
  // Rueckgabewert ("hat sich seit dem letzten read() etwas geaendert") ist
  // hier irrelevant - isTouched/x/y sind nach jedem Aufruf aktuell, ob sich
  // was geaendert hat oder nicht.
  touchCtrl.read();
  if (touchCtrl.isTouched)
  {
    data->point.x = constrain(touchCtrl.x, 0, LCD_WIDTH - 1);
    data->point.y = constrain(touchCtrl.y, 0, LCD_HEIGHT - 1);
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

inline void initLvglDisplay()
{
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_buffers(disp, gfx->getFramebuffer(), nullptr,
                         LCD_WIDTH * LCD_HEIGHT * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, jc8048w550_flush_cb);
  initBoardTouch();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, jc8048w550_touch_read_cb);
}
