#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "shared_state.h"
#include "display_draw.h"
#include "config_store.h"
#include "wifi_provision.h"
#include "web_portal.h"
#include "mqtt_handler.h"
#include "serial_handler.h"
#include "time_service.h"
#include "weather_service.h"
#include "display_layout.h"
#include "layout_store.h"

// Nur auf BOARD_GENERIC ueberhaupt wahr sein kann; steuert in loop(), ob
// LVGL-UI-Aufrufe stattfinden (siehe setup() unten).
static bool s_hasDisplay = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  log_w("Pulse ESP32 Hardware-Monitor (Version %s)", FW_VERSION);

  config_store_init();
  config_set_defaults(&app_config);
  config_store_load(&app_config);

#if defined(BOARD_GENERIC)
  // Generische Devkits (ESP32/S3/C3): welches Panel - falls ueberhaupt eins
  // angeschlossen ist - genutzt wird, steht erst zur Laufzeit in
  // app_config.display_type (Webportal-Tab "Anzeige"). DISPLAY_NONE ist kein
  // Fehler, sondern der bewusste Headless-Default (siehe CLAUDE.md).
  s_hasDisplay = boardHasDisplay();
  if (s_hasDisplay && !initBoardDisplay()) {
    log_e("initBoardDisplay() fehlgeschlagen - boote ohne Anzeige weiter");
    s_hasDisplay = false;
  }
#else
  if (!initBoardDisplay()) {
    log_e("initBoardDisplay() fehlgeschlagen!");
    return;
  }
  s_hasDisplay = true;
#endif

  lv_init();
  lv_tick_set_cb(millis);
  if (s_hasDisplay) {
    initLvglDisplay();
#if defined(BOARD_CYD_2432S028R)
    // Touch-Kalibrierwerte kommen aus app_config (siehe shared_state.h) -
    // initLvglDisplay()/initBoardTouch() koennen app_config selbst nicht
    // lesen (Header-Reihenfolge, siehe cyd_2432s028r.h), deshalb hier separat.
    applyTouchCalibration(app_config.touch_x_min, app_config.touch_x_max,
                           app_config.touch_y_min, app_config.touch_y_max);
#endif
    displayDrawBootScreen();         // zentriertes ASCII-Logo + Statuszeile
    boardDisplayBacklight(true);     // Backlight erst jetzt an
    boardDisplaySetBrightness(app_config.brightness); // no-op auf Boards ohne PWM-Pin
    layout_queue_begin();
  }

  // Reihenfolge wichtig: wifi_provision_begin() ruft WiFi.mode(WIFI_STA) auf
  // (legt synchron das STA-Netif an) - erst danach darf web_portal_begin()
  // den AsyncWebServer per .begin() tatsaechlich lauschen lassen, sonst legt
  // AsyncTCP seine interne Queue an, bevor das Netif existiert, und crasht
  // mit "assert failed: xQueueSemaphoreTake" noch in setup(). Die
  // WiFiManager-Routen werden in wifi_provision_begin() nur REGISTRIERT
  // (attachWebServer/attachUI), das ist auch vor .begin() unproblematisch.
  wifi_provision_begin();          // nicht blockierend, siehe wifi_provision.h
  web_portal_begin();               // Dashboard/REST-API, startet den Server (.begin())
  mqtt_handler_begin();
  serial_handler_begin();
  time_service_begin();
  weather_service_begin();
}

void loop() {
  lv_timer_handler();
  wifi_provision_loop();

  static bool                 screenSwitched = false;
  static wifi_provision_phase_t lastPhase     = (wifi_provision_phase_t)-1;

  if (s_hasDisplay && !screenSwitched) {
    wifi_provision_phase_t phase = wifi_provision_get_phase();
    if (phase != lastPhase) {
      lastPhase = phase;
      if (phase == WIFI_PROVISION_SETUP_MODE) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Setup-Modus\nWLAN \"%s-Setup\" verbinden", BOARD_NAME);
        displayDrawSetBootStatus(buf);
      } else if (phase == WIFI_PROVISION_CONNECTING) {
        displayDrawSetBootStatus("Verbinde mit WLAN...");
      }
    }

    if (phase == WIFI_PROVISION_CONNECTED) {
      // ANPASSUNG: Warten, bis die ASCII-Logo-Animation im Hintergrund fertig ist!
      if (!displayIsBootAnimFinished()) {
        // Noch nicht fertig animiert? Nächsten Loop-Durchlauf abwarten.
      } else {
        // Gestaffelte Statuszeile fuer die restlichen Startphasen, bevor das
        // Dashboard erscheint - sonst haengt der Boot-Screen nach "WLAN
        // verbunden" uebergangslos auf das Hauptlayout um, ohne erkennbar
        // zu machen, dass Hardwaredaten/Zeit/Wetter noch unterwegs sind.
        // Jede Stufe hat ein Zeitlimit (kStageTimeoutMs) statt endlos zu
        // warten - fehlt z.B. der PC-Client noch, soll das Dashboard trotzdem
        // erscheinen (es zeigt dann einfach Platzhalterwerte, bis die Daten
        // eintreffen, siehe hw_info.ever_received-Handling in den
        // Display-Modulen) statt den Nutzer auszusperren.
        // kTouchCal (nur CYD, siehe touchCalibrationNeeded()) hat KEIN
        // Zeitlimit - der Kalibrier-Bildschirm ist der einzige verlaessliche
        // Weg zu kalibriertem Touch (das Einstellungen-Overlay dahinter
        // braucht selbst schon einigermassen praezisen Touch, um das
        // Zahnrad-Icon zu treffen), ein Timeout wuerde also nur zu dauerhaft
        // geratenem Touch fuehren statt zur Loesung des Problems.
        enum class BootStage { kTouchCal, kHwData, kNtp, kWeather, kReady };
        static BootStage    stage        = touchCalibrationNeeded() ? BootStage::kTouchCal
                                                                      : BootStage::kHwData;
        static bool         stageShown   = false;
        static uint32_t     stageStartMs = 0;
        // Wetter braucht ein deutlich groesseres Zeitfenster als HW-Daten/NTP:
        // der erste HTTPS-Abruf nach dem Boot scheitert haeufig (AsyncTCP/
        // WiFiManager sind noch nicht "eingeschwungen", siehe
        // weather_service.cpp) und der erfolgreiche Abruf landet in der Praxis
        // typischerweise erst 60-65s nach dem Boot (naechster 30s-Retry-Tick) -
        // mit den vorherigen 8s liefen displayDrawInit()/layout_apply() also
        // IMMER vor gueltigen Wetterdaten, die Wetterkarte blieb dauerhaft leer
        // bis zum naechsten Refresh-Zyklus.
        constexpr uint32_t kHwDataTimeoutMs  = 8000;
        constexpr uint32_t kNtpTimeoutMs     = 8000;
        constexpr uint32_t kWeatherTimeoutMs = 60000;

        auto stageElapsed = [&]() { return millis() - stageStartMs; };
        auto enterStage = [&](BootStage next) {
          stage = next;
          stageShown = false;
          stageStartMs = millis();
        };

        if (stage == BootStage::kTouchCal) {
          if (!stageShown) {
            beginFirstBootTouchCalibration();
            stageShown = true;
          }
          if (app_config.touch_calibrated) {
            enterStage(BootStage::kHwData);
          }
        } else if (stage == BootStage::kHwData) {
          if (!stageShown) {
            displayDrawSetBootStatus(app_config.hw_source == HW_SOURCE_MQTT
                                          ? "Warte auf MQTT-Daten..."
                                          : "Warte auf USB-Daten...");
            stageShown = true;
            stageStartMs = millis();
          }
          hw_data_lock();
          bool gotHwData = hw_info.ever_received;
          hw_data_unlock();
          if (gotHwData || stageElapsed() > kHwDataTimeoutMs) {
            enterStage(BootStage::kNtp);
          }
        } else if (stage == BootStage::kNtp) {
          if (!stageShown) {
            displayDrawSetBootStatus("Warte auf Zeitsynchronisation...");
            stageShown = true;
          }
          if (time_service_is_synced() || stageElapsed() > kNtpTimeoutMs) {
            enterStage(BootStage::kWeather);
          }
        } else if (stage == BootStage::kWeather) {
          if (!app_config.weather_enabled) {
            enterStage(BootStage::kReady);
          } else {
            if (!stageShown) {
              displayDrawSetBootStatus("Warte auf Wetterdaten...");
              stageShown = true;
            }
            if (weather_info.valid || stageElapsed() > kWeatherTimeoutMs) {
              enterStage(BootStage::kReady);
            }
          }
        }

        if (stage == BootStage::kReady) {
          displayDrawInit();

          if (displaySupportsLayoutEditor()) {
            String saved = layout_store_load();
            if (saved.length() == 0) {
              saved = layout_default_json(displayWidthPx(), displayHeightPx());
            }
            JsonDocument doc;
            if (deserializeJson(doc, saved) == DeserializationError::Ok) {
              layout_apply(doc["widgets"].as<JsonArrayConst>(), doc["standby_widgets"].as<JsonArrayConst>());
            }
          }
          screenSwitched = true;
        }
      }
    }
  }

  if (s_hasDisplay) {
    displayDrawButtonPoll();
    layout_queue_process();
    static uint32_t s_lastBindingRefresh = 0;
    if (millis() - s_lastBindingRefresh >= 500) {
      s_lastBindingRefresh = millis();
      layout_refresh_bindings();
      displayDrawUpdate();
    }
  }

  mqtt_handler_loop();
  serial_handler_loop();

  delay(1);
}
