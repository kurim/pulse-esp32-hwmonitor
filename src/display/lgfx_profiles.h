#pragma once
// LovyanGFX-Geraeteklassen je Board/Panel-Profil (siehe board_profiles.h).
//
// Migrations-Phase 1: nur DISPLAY_CYD_ILI9341 (ESP32-2432S028 "CYD") ist
// fertig portiert. Die anderen 5 Profile (ILI9488/ST7796S/GC9A01/SSD1309/
// Guition JC8048W550) folgen in den Phasen 2-3 des Migrationsplans - siehe
// board_profile_get()/board_profiles.h fuer die dort bereits vorhandenen
// Pin-Tabellen.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Pins/Timings entsprechen 1:1 dem CYD-Profil in board_profiles.cpp
// (DISPLAY_CYD_ILI9341) - feste Werksverdrahtung des ESP32-2432S028-Boards.
class LGFX_CYD : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9341  _panel_instance;
    lgfx::Bus_SPI        _bus_instance;
    lgfx::Light_PWM      _light_instance;
    lgfx::Touch_XPT2046  _touch_instance;

public:
    LGFX_CYD(void)
    {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            // Frueherer Verdacht "SPI-Takt zu hoch" (40/20/10 MHz durchprobiert)
            // hat das grossflaechige Rauschen NICHT behoben und war die falsche
            // Spur. Abgleich mit einer auf echter CYD-Hardware verifiziert
            // funktionierenden Referenz-Config (jhsrennie/ESP32, LovyanGFX_CYD_
            // Settings.h) zeigt den eigentlichen Fehler: spi_3wire war hier auf
            // true gesetzt, obwohl pin_miso=12 eine echte, separate MISO-Leitung
            // konfiguriert - spi_3wire ist aber nur fuer Panels OHNE eigene MISO-
            // Leitung gedacht (Lesevorgaenge im Halbduplex ueber MOSI). Mit einer
            // real verkabelten MISO-Leitung erzwingt das einen falschen Halb-
            // duplex-Modus auf dem SPI-Peripheriegeraet - klassisches Symptom fuer
            // die beobachtete grossflaechige Bildstoerung. Die Referenz laeuft mit
            // spi_3wire=false sogar bei 55 MHz stabil; wir bleiben hier
            // konservativ bei 40 MHz.
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            // DMA war NICHT die Ursache: Streifentest A (display_ui.cpp) blieb
            // mit dma_channel=0 (synchrone Polling-Uebertragung) exakt genauso
            // verrauscht wie mit SPI_DMA_CH_AUTO - also zurueckgesetzt.
            // Naechste Spur: Anzahl der setAddrWindow()-Aufrufe (siehe
            // Streifentest A/B in display_ui.cpp).
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc   = 2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs          = 15;
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.panel_width     = 240;
            cfg.panel_height    = 320;
            cfg.offset_rotation = 0;
            // War hier faelschlich "true": LovyanGFX setzt das MADCTL-BGR-Bit
            // ueber "_cfg.rgb_order ? MAD_RGB : MAD_BGR" (Panel_LCD::
            // update_madctl()) - invers zur naheliegenden Lesart des Namens.
            // true = MAD_RGB = KEINE Hardware-R/B-Vertauschung, false =
            // MAD_BGR = Panel vertauscht R/B selbst. Fuer ein physisch BGR-
            // verdrahtetes Panel (Kompensation noetig) ist also false richtig.
            // War seit dem allerersten Commit falsch, aber unter dem grossen
            // writePixels()-Rauschen (siehe disp_flush_cb()-Historie) nicht
            // erkennbar - sichtbar erst seit dem pushImage()-Fix als generell
            // zu kuehle/blaustichige Farben (Gold/Orange/Rot wirkten cyan-
            // /blaustichig).
            cfg.rgb_order       = false;
            cfg.invert          = false; // auf realer Hardware verifiziert (siehe esp-idf-Branch)
            // Passend zu spi_3wire=false (siehe oben): echte MISO-Leitung ist
            // nutzbar, also Lesevorgaenge (Panel-Status/ID) ueber die korrekten
            // Dummy-Bit-Timings statt Halbduplex-Bitbanging abwickeln - 1:1 aus
            // der verifizierten Referenz-Config uebernommen.
            cfg.readable         = true;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 5000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        {
            auto cfg = _touch_instance.config();
            // Roh-ADC-Kalibrierung 1:1 aus touch_xpt2046.c (esp-idf-Branch)
            // uebernommen - LovyanGFX bildet x_min/x_max/y_min/y_max bereits
            // rotationsabhaengig auf Bildschirmkoordinaten ab, der eigene
            // Achsen-Tausch/Kreuz-Mapping-Code des Originals wird dadurch
            // ueberfluessig.
            cfg.x_min = 200;  cfg.x_max = 3700;
            cfg.y_min = 240;  cfg.y_max = 3800;
            // Panel_Device::convertRawXY() kombiniert Panel-Rotation und
            // touch-offset_rotation zu einem internen r=((rot+offset)&3) |
            // ((rot&4)^(offset&4)) und flippt X/Y abhaengig von r (vflip fuer
            // r in {1,2,4,7}, siehe LovyanGFX-Upstream-Quelle). offset=4 ergab
            // bei rotation=1 (Standard) r=5 (nur Achsentausch, kein Flip) -
            // auf echter Hardware fuer diese eine Rotation verifiziert, aber
            // bei rotation=3 (180°) ergibt dasselbe offset=4 r=7 (Achsentausch
            // + X- + Y-Flip) - eine andere Transformation als bei Standard.
            // Realer Hardware-Test zeigte genau das erwartete Symptom: Touch
            // bei Standard verhielt sich wie 180° und umgekehrt (vertauscht).
            // Durchrechnen aller 8 offset_rotation-Werte fuer beide von der
            // CYD-UI angebotenen Rotationen (1 und 3):
            //   offset  r@rot1  r@rot3
            //     0       1       3
            //     1       2       0
            //     2       3       1
            //     3       0       2
            //     4       5       7   (bisher, s.o. - vertauscht)
            //     5       6       4
            //     6       7       5   (liefert das gespiegelte Paar -> Fix)
            //     7       4       6
            // offset=6 tauscht genau die beiden Transformationen zwischen
            // Standard und 180° - passt exakt zur gemeldeten Vertauschung.
            // Kein CYD in dieser Sandbox verfuegbar - nach dem Flashen in
            // beiden Rotationen alle 4 Ecken antippen; falls noch nicht ganz
            // korrekt, sind laut Tabelle nur noch 0, 5 oder 7 als naechste
            // Kandidaten uebrig.
            cfg.offset_rotation = 6;
            cfg.pin_int  = 36;
            cfg.pin_sclk = 25;
            cfg.pin_mosi = 32;
            cfg.pin_miso = 39;
            cfg.pin_cs   = 33;
            cfg.freq     = 2000000;
            cfg.spi_host = VSPI_HOST; // separater Touch-Bus (SPI3), siehe board_profiles.c
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};
