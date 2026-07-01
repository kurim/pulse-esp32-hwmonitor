#include "display_ui.h"
#include <TFT_eSPI.h>
#include <time.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

static TFT_eSPI tft = TFT_eSPI();

// ------------------------------------------------------------------
// Touch (XPT2046) auf eigenem, von TFT_eSPI unabhaengigem SPI-Bus.
// Viele ESP32-2432S028 (CYD) Boards verdrahten den Touch-Controller NICHT
// auf denselben SPI-Bus wie das Display, sondern auf eigene Pins - dort
// findet TFT_eSPI's eingebautes getTouch() schlicht nichts.
// ------------------------------------------------------------------
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

static SPIClass touchSPI = SPIClass(VSPI);
static XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// Roh-ADC-Bereich des Touch-Controllers (Werksstreuung moeglich; wenn
// Antippen an Raendern nicht anspricht oder "wandert", hier nachjustieren)
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3700
#define TOUCH_RAW_Y_MIN 240
#define TOUCH_RAW_Y_MAX 3800

// Liest einen Touch und mappt Rohwerte auf Bildschirmkoordinaten passend
// zur aktuellen Rotation. Achsen sind gegenueber dem Display vertauscht,
// daher rx/ry gekreuzt. Getestet/entworfen fuer rotation=1 (Projekt-Default);
// die anderen Faelle sind ein plausibler Best-Guess und ggf. anzupassen.
static bool readTouch(uint16_t &sx, uint16_t &sy) {
  if (!touchscreen.tirqTouched() || !touchscreen.touched()) return false;
  TS_Point p = touchscreen.getPoint();

  int rx = constrain((int)p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX);
  int ry = constrain((int)p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX);

  switch (appConfig.rotation) {
    case 1: // Landscape, USB rechts - Projekt-Default
      sx = map(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 320, 0);
      sy = map(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 240, 0);
      break;
    case 3: // Landscape, USB links
      sx = map(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 320);
      sy = map(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, 240);
      break;
    case 0: // Portrait
      sx = map(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, 240);
      sy = map(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 320);
      break;
    default: // 2, Portrait gedreht
      sx = map(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 240, 0);
      sy = map(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 320, 0);
      break;
  }
  return true;
}

// Bildschirme
enum Screen { SCR_MAIN, SCR_CPU_DETAIL, SCR_GPU_DETAIL };
static Screen currentScreen = SCR_MAIN;
static Screen lastDrawnScreen = (Screen)-1; // erzwingt initialen Static-Draw
static unsigned long lastPeriodicUpdate = 0;

// Touch-Zonen (Landscape 320x240, rotation=1)
struct Rect { int16_t x, y, w, h; };
static Rect cpuTileRect  = {0,   34, 160, 160};
static Rect gpuTileRect  = {160, 34, 160, 160};
static Rect backBtnRect  = {0,   0,  70,  34};

// Farben
#define COL_BG       TFT_BLACK
#define COL_CARD     0x18C3      // dunkles Grau-Blau
#define COL_ACCENT   0x07FF      // Cyan
#define COL_GPU      0xFD20      // Orange
#define COL_TEXT     TFT_WHITE
#define COL_SUBTEXT  0x8C71      // Hellgrau
#define COL_WARN     0xF800      // Rot

// Zustand für "nur bei Änderung neu zeichnen" (vermeidet unnötiges Redraw/Flackern)
struct MainDynState {
  char timeStr[12] = "";
  char dateStr[16] = "";
  bool weatherValid = false;
  float weatherTemp = -999;
  bool wifiOk = false;
  bool mqttOk = false;
  float cpuLoad = -1, gpuLoad = -1;
  float cpuTemp = -1, gpuTemp = -1;
  float cpuPower = -1, gpuPower = -1;
  bool everReceivedShown = false;
  bool valid = false; // erstes Mal noch nichts gezeichnet
};
static MainDynState mainState;

struct DetailDynState {
  float load = -1, temp = -1, power = -1;
  uint8_t lastDrawnCount = 255; // Verlaufslänge, die zuletzt gezeichnet wurde
  bool valid = false;
};
static DetailDynState detailState;

static void drawMainStatic();
static void updateMainDynamic(bool force);
static void drawDetailStatic(bool isCpu);
static void updateDetailDynamic(bool isCpu, bool force);
static void drawGraph(int16_t x, int16_t y, int16_t w, int16_t h, float* data, uint8_t count, uint16_t color, float maxScale);

namespace DisplayUI {

void begin() {
  tft.init();
  tft.setRotation(appConfig.rotation);
  tft.fillScreen(COL_BG);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, appConfig.brightness);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, appConfig.brightness);
#endif

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(1);

  lastDrawnScreen = (Screen)-1; // erzwingt ersten Static-Draw in loop()
}

void notifyDataChanged() {
  // kein expliziter Trigger nötig - loop() prüft Werte alle 1s selbst
}

void loop() {
  uint16_t tx, ty;
  bool touched = readTouch(tx, ty);

  if (touched) {
    static unsigned long lastTouchMs = 0;
    if (millis() - lastTouchMs > 300) { // Entprellen
      lastTouchMs = millis();
      Serial.printf("[Touch] tx=%u ty=%u screen=%d\n", tx, ty, (int)currentScreen);

      if (currentScreen == SCR_MAIN) {
        if (tx >= cpuTileRect.x && tx < cpuTileRect.x + cpuTileRect.w &&
            ty >= cpuTileRect.y && ty < cpuTileRect.y + cpuTileRect.h) {
          currentScreen = SCR_CPU_DETAIL;
        } else if (tx >= gpuTileRect.x && tx < gpuTileRect.x + gpuTileRect.w &&
                   ty >= gpuTileRect.y && ty < gpuTileRect.y + gpuTileRect.h) {
          currentScreen = SCR_GPU_DETAIL;
        }
      } else {
        if (tx >= backBtnRect.x && tx < backBtnRect.x + backBtnRect.w &&
            ty >= backBtnRect.y && ty < backBtnRect.y + backBtnRect.h) {
          currentScreen = SCR_MAIN;
        }
      }
    }
  }

  bool screenChanged = (currentScreen != lastDrawnScreen);
  bool periodicDue = (millis() - lastPeriodicUpdate > 1000);

  if (!screenChanged && !periodicDue) return; // nichts zu tun -> kein unnötiges SPI-Geschreibe

  if (screenChanged) {
    // Statisches Layout (Rahmen, Labels, Hintergründe) nur einmal pro Screen-Wechsel zeichnen
    tft.fillScreen(COL_BG);
    if (currentScreen == SCR_MAIN) {
      drawMainStatic();
      mainState.valid = false; // erzwingt vollständiges dynamisches Redraw
    } else {
      drawDetailStatic(currentScreen == SCR_CPU_DETAIL);
      detailState.valid = false;
    }
    lastDrawnScreen = currentScreen;
  }

  if (periodicDue) {
    lastPeriodicUpdate = millis();
    if (currentScreen == SCR_MAIN) {
      updateMainDynamic(!mainState.valid);
      mainState.valid = true;
    } else {
      updateDetailDynamic(currentScreen == SCR_CPU_DETAIL, !detailState.valid);
      detailState.valid = true;
    }
  } else if (screenChanged) {
    // Screen gerade gewechselt, aber periodicDue war false -> trotzdem einmal Werte zeichnen
    if (currentScreen == SCR_MAIN) {
      updateMainDynamic(true);
      mainState.valid = true;
    } else {
      updateDetailDynamic(currentScreen == SCR_CPU_DETAIL, true);
      detailState.valid = true;
    }
  }
}

} // namespace DisplayUI

// ==================================================================
// MAIN SCREEN
// ==================================================================
static void drawMainStatic() {
  // Top-Bar Hintergrund
  tft.fillRect(0, 0, 320, 32, COL_CARD);
  tft.fillCircle(316, 6, 3, COL_SUBTEXT); // Platzhalter, wird dynamisch neu gefärbt

  // Tiles: Karten-Hintergrund + Rahmen + Label (ändern sich nie)
  auto drawTileFrame = [](Rect r, const char* label, uint16_t accent) {
    tft.fillRoundRect(r.x + 6, r.y + 6, r.w - 12, r.h - 12, 8, COL_CARD);
    tft.drawRoundRect(r.x + 6, r.y + 6, r.w - 12, r.h - 12, 8, accent);
    int16_t cx = r.x + r.w / 2;
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(accent, COL_CARD);
    tft.drawString(label, cx, r.y + 18, 4);
    // Balken-Rahmen (statisch, nur Füllung ändert sich später)
    int barW = r.w - 40;
    int barX = r.x + 20;
    int barY = r.y + 110;
    tft.drawRect(barX, barY, barW, 10, COL_SUBTEXT);
    tft.setTextDatum(TL_DATUM);
  };
  drawTileFrame(cpuTileRect, "CPU", COL_ACCENT);
  drawTileFrame(gpuTileRect, "GPU", COL_GPU);
}

static void updateTileDynamic(Rect r, float load, float temp, float power, uint16_t accent,
                               float &lastLoad, float &lastTemp, float &lastPower, bool force) {
  int16_t cx = r.x + r.w / 2;

  if (force || load != lastLoad) {
    // Lade-Prozentzahl: Bereich gezielt löschen, dann neu zeichnen
    // (Font 7 ist ~48px hoch; Rechteck muss ab exakt der Textposition beginnen
    // und die volle Glyphenhöhe + Puffer abdecken, sonst bleiben Reste stehen)
    tft.fillRect(r.x + 10, r.y + 52, r.w - 20, 54, COL_CARD);
    char loadBuf[8];
    snprintf(loadBuf, sizeof(loadBuf), "%.0f%%", load);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_TEXT, COL_CARD);
    tft.drawString(loadBuf, cx, r.y + 55, 7);
    tft.setTextDatum(TL_DATUM);

    // Balken-Füllung (nur innerer Bereich, Rahmen bleibt stehen)
    int barW = r.w - 40;
    int barX = r.x + 20;
    int barY = r.y + 110;
    tft.fillRect(barX + 1, barY + 1, barW - 2, 8, COL_CARD);
    int fillW = (int)((barW - 2) * constrain(load, 0, 100) / 100.0);
    if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW, 8, accent);

    lastLoad = load;
  }

  if (force || temp != lastTemp || power != lastPower) {
    tft.fillRect(r.x + 10, r.y + 130, r.w - 20, 22, COL_CARD);
    char infoBuf[24];
    snprintf(infoBuf, sizeof(infoBuf), "%.0f%cC  %.0fW", temp, 0xB0, power);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_SUBTEXT, COL_CARD);
    tft.drawString(infoBuf, cx, r.y + 132, 2);
    tft.setTextDatum(TL_DATUM);
    lastTemp = temp;
    lastPower = power;
  }
}

static void updateMainDynamic(bool force) {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  char timeBuf[12], dateBuf[16];

  if (ti.tm_year > 100) {
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &ti);
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &ti);
  } else {
    strcpy(timeBuf, "--:--:--");
    dateBuf[0] = '\0';
  }

  if (force || strcmp(timeBuf, mainState.timeStr) != 0) {
    tft.fillRect(2, 2, 122, 28, COL_CARD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_TEXT, COL_CARD);
    tft.drawString(timeBuf, 8, 16, 4);
    tft.setTextDatum(TL_DATUM);
    strlcpy(mainState.timeStr, timeBuf, sizeof(mainState.timeStr));
  }

  if (force || strcmp(dateBuf, mainState.dateStr) != 0) {
    tft.fillRect(126, 2, 96, 28, COL_CARD);
    if (dateBuf[0] != '\0') {
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(COL_SUBTEXT, COL_CARD);
      tft.drawString(dateBuf, 130, 16, 2);
      tft.setTextDatum(TL_DATUM);
    }
    strlcpy(mainState.dateStr, dateBuf, sizeof(mainState.dateStr));
  }

  bool weatherChanged = (weatherInfo.valid != mainState.weatherValid) ||
                         (weatherInfo.valid && weatherInfo.tempC != mainState.weatherTemp);
  if (force || weatherChanged) {
    tft.fillRect(226, 2, 90, 28, COL_CARD);
    tft.setTextDatum(MR_DATUM);
    if (weatherInfo.valid) {
      char wbuf[24];
      snprintf(wbuf, sizeof(wbuf), "%.0f%cC", weatherInfo.tempC, 0xB0);
      tft.setTextColor(COL_TEXT, COL_CARD);
      tft.drawString(wbuf, 312, 16, 4);
    } else if (appConfig.weather_enabled) {
      tft.setTextColor(COL_SUBTEXT, COL_CARD);
      tft.drawString("Wetter --", 312, 16, 2);
    }
    tft.setTextDatum(TL_DATUM);
    mainState.weatherValid = weatherInfo.valid;
    mainState.weatherTemp = weatherInfo.tempC;
  }

  if (force || wifiConnected != mainState.wifiOk || mqttConnected != mainState.mqttOk) {
    uint16_t statusColor = (wifiConnected && mqttConnected) ? TFT_GREEN :
                            (wifiConnected ? TFT_YELLOW : COL_WARN);
    tft.fillCircle(316, 6, 3, statusColor);
    mainState.wifiOk = wifiConnected;
    mainState.mqttOk = mqttConnected;
  }

  updateTileDynamic(cpuTileRect, hwInfo.cpuLoad, hwInfo.cpuTemp, hwInfo.cpuPower, COL_ACCENT,
                     mainState.cpuLoad, mainState.cpuTemp, mainState.cpuPower, force);
  updateTileDynamic(gpuTileRect, hwInfo.gpuLoad, hwInfo.gpuTemp, hwInfo.gpuPower, COL_GPU,
                     mainState.gpuLoad, mainState.gpuTemp, mainState.gpuPower, force);

  if (force || hwInfo.everReceived != mainState.everReceivedShown) {
    tft.fillRect(0, 198, 320, 20, COL_BG);
    if (!hwInfo.everReceived) {
      tft.setTextColor(COL_SUBTEXT, COL_BG);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("Warte auf MQTT-Daten vom PC-Client...", 160, 200, 2);
      tft.setTextDatum(TL_DATUM);
    }
    mainState.everReceivedShown = hwInfo.everReceived;
  }
}

// ==================================================================
// DETAIL SCREEN (CPU/GPU Verlauf)
// ==================================================================
static void drawDetailStatic(bool isCpu) {
  uint16_t accent = isCpu ? COL_ACCENT : COL_GPU;

  tft.fillRoundRect(backBtnRect.x + 6, backBtnRect.y + 4, backBtnRect.w - 12, backBtnRect.h - 8, 6, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("< Zurueck", backBtnRect.x + backBtnRect.w / 2, backBtnRect.y + backBtnRect.h / 2, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(accent, COL_BG);
  tft.drawString(isCpu ? "CPU Verlauf" : "GPU Verlauf", 312, 8, 4);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.drawString("Auslastung (%) - Verlauf", 12, 102, 1);
  tft.drawRect(12, 114, 296, 60, COL_SUBTEXT);

  tft.drawString("Temperatur (C) - Verlauf", 12, 180, 1);
  tft.drawRect(12, 192, 296, 44, COL_SUBTEXT);
}

static void updateDetailDynamic(bool isCpu, bool force) {
  History &hist = isCpu ? cpuHistory : gpuHistory;
  uint16_t accent = isCpu ? COL_ACCENT : COL_GPU;
  float load  = isCpu ? hwInfo.cpuLoad  : hwInfo.gpuLoad;
  float temp  = isCpu ? hwInfo.cpuTemp  : hwInfo.gpuTemp;
  float power = isCpu ? hwInfo.cpuPower : hwInfo.gpuPower;

  if (force || load != detailState.load || temp != detailState.temp || power != detailState.power) {
    tft.fillRect(12, 40, 220, 58, COL_BG);
    char buf[32];
    tft.setTextColor(COL_TEXT, COL_BG);
    snprintf(buf, sizeof(buf), "Auslastung: %.1f %%", load);
    tft.drawString(buf, 12, 42, 2);
    snprintf(buf, sizeof(buf), "Temperatur: %.1f %cC", temp, 0xB0);
    tft.drawString(buf, 12, 60, 2);
    snprintf(buf, sizeof(buf), "Leistung:   %.1f W", power);
    tft.drawString(buf, 12, 78, 2);
    detailState.load = load;
    detailState.temp = temp;
    detailState.power = power;
  }

  // Graphen nur neu zeichnen, wenn sich tatsächlich ein neuer Sample-Wert angesammelt hat
  bool newSample = (hist.count != detailState.lastDrawnCount) ||
                    (hist.count > 0 && hist.load[HIST_LEN - 1] != detailState.load);
  if (force || newSample) {
    tft.fillRect(13, 115, 294, 58, COL_BG); // innerer Bereich, Rahmen bleibt stehen
    drawGraph(12, 114, 296, 60, hist.load, hist.count, accent, 100.0f);

    tft.fillRect(13, 193, 294, 42, COL_BG);
    drawGraph(12, 192, 296, 44, hist.temp, hist.count, TFT_ORANGE, 120.0f);

    detailState.lastDrawnCount = hist.count;
  }
}

static void drawGraph(int16_t x, int16_t y, int16_t w, int16_t h, float* data, uint8_t count, uint16_t color, float maxScale) {
  if (count < 2) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_SUBTEXT, COL_BG);
    tft.drawString("noch keine Daten", x + w / 2, y + h / 2, 1);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  int startIdx = HIST_LEN - count;
  float stepX = (float)(w - 4) / (float)(count - 1);

  int16_t prevX = -1, prevY = -1;
  for (int i = 0; i < count; i++) {
    float v = data[startIdx + i];
    if (v > maxScale) v = maxScale;
    if (v < 0) v = 0;
    int16_t px = x + 2 + (int16_t)(i * stepX);
    int16_t py = y + h - 2 - (int16_t)((v / maxScale) * (h - 4));
    if (prevX >= 0) {
      tft.drawLine(prevX, prevY, px, py, color);
    }
    prevX = px;
    prevY = py;
  }
}