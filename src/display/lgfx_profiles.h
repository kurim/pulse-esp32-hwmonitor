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
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = true;
            cfg.use_lock   = true;
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
            cfg.rgb_order       = true; // CYD-Panel ist BGR-verdrahtet
            cfg.invert          = false; // auf realer Hardware verifiziert (siehe esp-idf-Branch)
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
