#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Bildschirm-Aus-Zeitplan (Issue #74): schaltet das Display zu festgelegten
// Uhrzeiten komplett dunkel - entweder einheitlich fuer die ganze Woche oder
// pro Wochentag (app_config.screen_sched_mode). Unabhaengig vom
// Signalverlust-Standby (standby_timeout_s), das eine Uhr anzeigt.

// Reine Zeitplan-Logik ohne Hardwarezugriff: true, wenn laut Zeitplan zur
// lokalen Zeit `t` dunkel sein soll. Beruecksichtigt Fenster ueber
// Mitternacht (off_min > on_min gehoert zum Tag, an dem es BEGINNT).
bool screen_schedule_is_off(const struct tm &t);

// MQTT-Override (Topic "<mqtt_topic>/screen", Payload on/off/auto). Nur im
// RAM - ein Reboot faellt auf AUTO zurueck.
enum ScreenOverride : uint8_t { SCREEN_OVERRIDE_AUTO = 0, SCREEN_OVERRIDE_ON = 1, SCREEN_OVERRIDE_OFF = 2 };
void screen_schedule_set_override(ScreenOverride o);
// Parst "on"/"off"/"auto" (case-insensitive); false bei unbekanntem Text.
bool screen_schedule_set_override_text(const char *payload, size_t len);

// Antippen/BOOT-Taste: weckt den Bildschirm fuer 30 s, danach gilt wieder
// der Zeitplan. Aus den Button-Pollern aufrufbar; Touch wird zusaetzlich
// selbst ueber LVGLs Inaktivitaetszeit erkannt.
void screen_schedule_notify_activity(void);

// Aus loop() aufrufen (nur wenn ein Display existiert und das Dashboard
// bereits steht). Ohne NTP-Sync bleibt das Display an (fail-safe).
void screen_schedule_poll(void);

// Schaltet Zeitplan-/Override-Zustand sofort neu durch, z.B. nach einer
// geaenderten Konfiguration im Webportal.
void screen_schedule_apply_config(void);
