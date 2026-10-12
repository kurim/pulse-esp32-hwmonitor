# Changelog

Alle nennenswerten Änderungen an diesem Projekt werden in dieser Datei festgehalten.

Das Format basiert auf [Keep a Changelog](https://keepachangelog.com/de/1.1.0/),
und das Projekt folgt [Semantic Versioning](https://semver.org/lang/de/).

**Diese Datei ist die einzige Quelle der Wahrheit für die Firmware-Version.**
Die oberste Release-Überschrift (`## [x.y.z] - Datum`) unterhalb von
`[Unreleased]` wird beim Build von `extra_script_version.py` als `FW_VERSION`
übernommen. Ein Release geht so: `[Unreleased]` in einen neuen Abschnitt
`## [x.y.z] - JJJJ-MM-TT` umbenennen, Link unten ergänzen, committen und den
Tag `vx.y.z` pushen (der CI-Workflow bricht ab, wenn Tag und Changelog
auseinanderlaufen).

## [Unreleased]

### Added

- Bildschirm-Zeitplan: Display zu festen Uhrzeiten ausschalten, wahlweise
  einheitlich für die ganze Woche oder pro Wochentag einzeln (Web-Portal →
  Display). Ohne NTP-Sync bleibt das Display an. ([#74], [#75])
- `BOARD_GENERIC`: Backlight-Pin wird jetzt tatsächlich geschaltet
  (`boardDisplayBacklight()` war zuvor ein No-Op).
- `CHANGELOG.md` im Keep-a-Changelog-Format.

### Changed

- `FW_VERSION` wird beim Build aus dem neuesten Release-Abschnitt dieser Datei
  abgeleitet (`extra_script_version.py`), statt zusätzlich in
  `src/config/shared_state.h` gepflegt zu werden. Der Tag-Build im CI prüft nur
  noch, ob der Tag zur Changelog-Version passt.

## [1.0.7] - 2026-09-02

### Added

- Widget `mono_week` für die Kalenderwoche (Format „KW36“) im Mono-UI.

## [1.0.6] - 2026-08-21

### Added

- Live-Übernahme geänderter MQTT-, Zeit- und Wetter-Einstellungen ohne Neustart.

### Fixed

- Wetterabruf: robuster bei instabiler WLAN-Verbindung, kürzere
  Anfangsverzögerung beim ersten Abruf.

## [1.0.5] - 2026-08-21

### Added

- Backup und Wiederherstellung der Konfiguration im Web-Dashboard.
- Wert-Balken-Widget (Value Bar) mit einstellbarem Bereich und Eigenschaften.

### Fixed

- Standby wird nach dem Boot direkt ausgelöst, auch wenn weder MQTT- noch
  USB-Daten eintreffen (#62).

### Changed

- Interne Code-Umstrukturierung für bessere Lesbarkeit und Wartbarkeit.

## [1.0.4] - 2026-08-21

### Fixed

- ESP32-C3: zweite Nav-Button-Abfrage (`displayRoundButtonPoll`) wird bei
  GPIO-9-Kollision deaktiviert.

## [1.0.3] - 2026-08-21

### Fixed

- ESP32-C3: Nav-Button-Abfrage wird bei GPIO-9-Kollision mit I2C-SCL
  deaktiviert.

## [1.0.2] - 2026-08-08

### Changed

- Web-Dashboard (HTML) überarbeitet und optimiert.

## [1.0.1] - 2026-08-07

### Fixed

- Palette-Button „+ Uhr/Wetter (Vollbild, Standby)“ fügt jetzt tatsächlich ein
  Widget hinzu.

### Changed

- Redundante Kommentare entfernt (keine funktionalen Änderungen).

## [1.0.0] - 2026-08-07

### Added

- Display-Rotation.
- Neues Standby-Widget für eckige Farbdisplays.
- Web-Flasher und Dokumentation für Firmware-Updates, inkl. GPG-Signaturhinweisen.

## [0.5.0] - 2026-08-07

### Added

- Dokumentation und Web-Flasher (ESP Web Tools) für PulseMQTT.

## [0.4.9] - 2026-08-07

Erstes Release mit vollständigen Release-Notes für den PlatformIO/Arduino-Rewrite,
siehe [RELEASE_NOTES_v0.4.9.md](RELEASE_NOTES_v0.4.9.md). Frühere Tags
(`v0.3.10` – `v0.4.8`) hatten kein beschriebenes Release und sind hier nicht
einzeln aufgeführt.

[#74]: https://github.com/kurim/pulse-esp32-hwmonitor/issues/74
[#75]: https://github.com/kurim/pulse-esp32-hwmonitor/pull/75
[Unreleased]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.7...HEAD
[1.0.7]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.6...v1.0.7
[1.0.6]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.5...v1.0.6
[1.0.5]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.4...v1.0.5
[1.0.4]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v0.5.0...v1.0.0
[0.5.0]: https://github.com/kurim/pulse-esp32-hwmonitor/compare/v0.4.9...v0.5.0
[0.4.9]: https://github.com/kurim/pulse-esp32-hwmonitor/releases/tag/v0.4.9
