#pragma once

// Boot-Screen: zentriertes ASCII-Logo + darunter eine Statuszeile fuer den
// Provisionierungsfortschritt (siehe displayDrawSetBootStatus()). Muss nach
// lv_init() und initLvglDisplay() aufgerufen werden.
void displayDrawBootScreen();

// Aktualisiert die Statuszeile unter dem Boot-Logo (z.B. "Verbinde mit
// WLAN..." oder "Setup-Modus..."), no-op bevor displayDrawBootScreen() lief
// oder nachdem displayDrawInit() den Boot-Screen bereits abgeloest hat.
void displayDrawSetBootStatus(const char *text);

// Normale UI. Setzt voraus, dass displayDrawBootScreen() vorher lief (entfernt
// dessen Logo/Statuszeile) und baut das Hauptlayout auf. Waehlt automatisch
// zwischen dem runden Arc-Dashboard (GC9A01, siehe displayIsRound()) und dem
// generischen Layout-Editor-Pfad (rechteckige Card-Widgets ODER Mono-
// mono_*-Widgets, siehe display_layout.cpp - main.cpp ruft layout_apply()
// direkt im Anschluss auf).
void displayDrawInit();

// Aktualisiert die Live-Werte des runden Arc-Dashboards (CPU/GPU/Uhrzeit)
// aus hw_info - no-op, wenn gerade kein rundes Dashboard aktiv ist (z.B.
// rechteckiges Board, oder der Dashboard-Editor hat den Screen ersetzt).
// Periodisch aus main.cpp loop() aufrufen, gedrosselt.
void displayDrawUpdate();

// Liest die BOOT-Taste als manuellen Standby-Umschalter - rundes Display
// ueber displayRoundButtonPoll() (nur ESP32-C3), alle anderen Boards mit
// Standby-Widgets (BOARD_GENERIC, CYD, JC8048W550) ueber
// displayMonoButtonPoll() (siehe dortige Kommentare in
// display_round.h/display_layout.h fuer die jeweiligen No-op-Faelle) -
// UNGEDROSSELT jede loop()-Iteration aufrufen, sonst koennte ein kurzer
// Tastendruck zwischen zwei displayDrawUpdate()-Takten (500ms) verloren
// gehen.
void displayDrawButtonPoll();

bool displayIsBootAnimFinished();