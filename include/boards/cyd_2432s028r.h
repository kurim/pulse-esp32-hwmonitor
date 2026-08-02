#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// -----------------------------------------------------------------------------
// Pin-Definitionen ESP32-2432S028 ("CYD" - Cheap Yellow Display)
// -----------------------------------------------------------------------------
#define BOARD_NAME "ESP32-2432S028 (CYD)"
#define BOARD_HAS_DISPLAY 1
#define BOARD_HAS_TOUCH   1
#define BOARD_HAS_SD      1
#define LCD_WIDTH  320
#define LCD_HEIGHT 240

// SPI Pins
#define CYD_TFT_MISO 12
#define CYD_TFT_MOSI 13
#define CYD_TFT_SCLK 14
#define CYD_TFT_CS   15
#define CYD_TFT_DC    2
#define CYD_TFT_RST  -1 // An EN/Reset des ESP32 gekoppelt

// Touch Pins (XPT2046)
#define CYD_TOUCH_IRQ 36
#define CYD_TOUCH_MOSI 32
#define CYD_TOUCH_MISO 39
#define CYD_TOUCH_CLK 25
#define CYD_TOUCH_CS 33

// Backlight (PWM-faehig)
#define GFX_BL 21
#define BOARD_HAS_BACKLIGHT_PWM 1

// -----------------------------------------------------------------------------
// Treibereinrichtung (ILI9341 via SPI)
// -----------------------------------------------------------------------------
// HSPI, nicht der VSPI-Default (siehe CLAUDE.md) - physisch auf HSPI verdrahtet,
// Touch (XPT2046) haengt an einem eigenen VSPI-Bus, siehe initBoardTouch()
// unten. is_shared_interface=false, da Display allein auf HSPI sitzt.
inline Arduino_DataBus *bus = new Arduino_ESP32SPI(
    CYD_TFT_DC, CYD_TFT_CS, CYD_TFT_SCLK, CYD_TFT_MOSI, CYD_TFT_MISO, HSPI /* SPI Host */,
    false /* is_shared_interface */
);

inline Arduino_GFX *gfx = new Arduino_ILI9341(
    bus, CYD_TFT_RST, 1 /* Rotation: Landscape */
);

// -----------------------------------------------------------------------------
// Initialisierungs-Funktion
// -----------------------------------------------------------------------------
inline bool initBoardDisplay() {
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);

  return gfx->begin();
}

inline void boardDisplayBacklight(bool on) {
  digitalWrite(GFX_BL, on ? HIGH : LOW);
}

// Echtes Dimmen per LEDC-PWM statt nur an/aus - app_config.brightness (0-255)
// bildet sich 1:1 auf die 8-Bit-Aufloesung ab. ledcAttach() nur einmal
// aufrufen (wiederholtes Attachen auf denselben Pin ist unnoetig und beim
// arduino-esp32-3.x-API auch nicht als Update-Pfad gedacht).
inline void boardDisplaySetBrightness(uint8_t level) {
  static bool attached = false;
  if (!attached) {
    ledcAttach(GFX_BL, 5000, 8);
    attached = true;
  }
  ledcWrite(GFX_BL, level);
}

// -----------------------------------------------------------------------------
// LVGL-Anbindung: SPI-Panel hat kein adressierbares Framebuffer wie das
// RGB-Panel - LVGL rendert partiell in einen kleinen Draw-Buffer im internen
// RAM, die Flush-Callback schiebt jede Kachel per SPI raus.
// -----------------------------------------------------------------------------
inline void cyd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

// -----------------------------------------------------------------------------
// Touch (XPT2046) - Hardware-SPI ueber einen eigenen VSPI-Bus. Eigene
// SPIClass-Instanz statt der Default-SPI (die waere fuer klassisches VSPI
// mit anderen Pins belegt). Vorher testweise per Bitbang
// (XPT2046_Bitbang_Slim), aber spuerbar traege (Bit-Banging via
// digitalWrite/digitalRead in einer Schleife ist inhaerent langsamer als ein
// Hardware-SPI-Peripheral) - zurueck auf die Original-Bibliothek von
// PaulStoffregen.
// -----------------------------------------------------------------------------
inline SPIClass touchSPI(VSPI);
inline XPT2046_Touchscreen touchCtrl(CYD_TOUCH_CS, CYD_TOUCH_IRQ);

// Rohe XPT2046-ADC-Grenzwerte (0-4095) kommen aus app_config.touch_x_min/...
// (siehe shared_state.h) statt fest hier verdrahtet zu sein - ueber den
// "Touch kalibrieren"-Knopf im Einstellungen-Overlay neu setzbar (siehe
// display_layout.cpp), ohne Neuflashen. Anders als die vorher genutzte
// Bitbang-Bibliothek kennt PaulStoffregens Original kein setCalibration() -
// die Werte werden deshalb hier in modul-lokalen Variablen gespiegelt statt
// in der Bibliothek gespeichert. app_config ist an dieser Stelle im Header
// noch nicht deklariert (board_config.h wird aus shared_state.h VOR der
// app_config_t-Definition eingebunden) - deshalb nimmt
// applyTouchCalibration() die Werte als Parameter entgegen, statt sie selbst
// zu lesen; aufgerufen wird sie aus main.cpp (nach initLvglDisplay()) bzw.
// nach einer Live-Kalibrierung erneut aus display_layout.cpp. xRaw folgt
// direkt der Bildschirmbreite, yRaw direkt der Bildschirmhoehe, OHNE
// Vertauschen - entgegen der urspruenglichen Annahme braucht dieser
// Touch-Layer bei diesem Board/dieser Rotation keine Achsen-Transformation,
// siehe cyd_touch_read_cb() unten. Dieselbe physische Beruehrungsflaeche wie
// zuvor (nur ein anderer Software-Treiber fuer denselben XPT2046-Chip), die
// bereits gemessenen Grenzwerte gelten unveraendert weiter.
inline int16_t s_touchXMin, s_touchXMax, s_touchYMin, s_touchYMax;

inline void initBoardTouch() {
  touchSPI.begin(CYD_TOUCH_CLK, CYD_TOUCH_MISO, CYD_TOUCH_MOSI, CYD_TOUCH_CS);
  touchCtrl.begin(touchSPI);
}

inline void applyTouchCalibration(int16_t xMin, int16_t xMax, int16_t yMin, int16_t yMax) {
  s_touchXMin = xMin;
  s_touchXMax = xMax;
  s_touchYMin = yMin;
  s_touchYMax = yMax;
}

// LVGL-Input-Device-Callback (Polling, kein IRQ-getriebenes Event - reicht
// fuer Touch-Interaktion bei den ueblichen LVGL-Refreshraten).
inline void cyd_touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
  if (touchCtrl.touched()) {
    TS_Point p = touchCtrl.getPoint();
    data->point.x = constrain(map(p.x, s_touchXMin, s_touchXMax, 0, LCD_WIDTH), 0, LCD_WIDTH - 1);
    data->point.y = constrain(map(p.y, s_touchYMin, s_touchYMax, 0, LCD_HEIGHT), 0, LCD_HEIGHT - 1);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

inline void initLvglDisplay() {
  // Buffer bewusst klein gehalten: klassischer ESP32 hat nur ~320 KB DRAM,
  // und seit ESPAsyncWebServer/AsyncTCP/WiFiManager fuer den Captive-Portal-
  // Fallback tatsaechlich mitgelinkt werden (wifi_provision.cpp), ist das
  // .dram0.bss-Segment knapp. 12 Zeilen reichen fuer partielles Rendering.
  //
  // alignas(LV_DRAW_BUF_ALIGN) ist Pflicht: lv_display_set_buffers() prueft
  // die Ausrichtung per LV_ASSERT_FORMAT_MSG (buf1 == lv_draw_buf_align(...)),
  // UNABHAENGIG von LV_USE_ASSERT_*-Flags. Ohne explizite Ausrichtung landet
  // ein reines "static lv_color_t[]" nur auf dem 2-Byte-Alignment von
  // uint16_t, nicht zwangslaeufig auf den von LVGL geforderten 4 Byte - schlaegt
  // der Assert fehl, ist LV_ASSERT_HANDLER per Default ein stilles while(1);
  // (kein Log, da LV_USE_LOG=0), also ein kompletter Hang ohne jede Ausgabe.
  static lv_color_t drawBuf[LCD_WIDTH * 12] alignas(LV_DRAW_BUF_ALIGN);
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, cyd_flush_cb);

  initBoardTouch();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, cyd_touch_read_cb);
}