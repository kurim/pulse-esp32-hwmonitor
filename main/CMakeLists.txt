idf_component_register(
    SRCS
        "app_main.c"
        "config_store.c"
        "shared_state.c"
        "display/board_profiles.c"
        "display/display_ui.c"
        "display/display_ui_rect.c"
        "display/display_ui_round.c"
        "display/display_ui_mono.c"
        "display/ui_strings.c"
        "display/font_mdi_icons_20.c"
        "display/font_mdi_icons_10.c"
        "display/touch_xpt2046.c"
        "net/web_portal.c"
        "net/mqtt_handler.c"
        "net/time_service.c"
        "net/weather_service.c"
    # "." fuer app_main.c/config_store.h/shared_state.h; "display"/"net"
    # zusaetzlich, damit die bestehenden #include "foo.h"-Zeilen (bare
    # Dateiname, kein Unterordner-Praefix) unveraendert weiter aufloesen -
    # jeder Ordner wird dem Compiler als eigener -I-Include-Pfad uebergeben.
    INCLUDE_DIRS "." "display" "net"
    # Nur Core-/Built-in-Komponenten hier auffuehren. Managed Components
    # (lvgl, esp_lvgl_port, esp_lcd_ili9341/ili9488/st7796/gc9a01,
    # espressif/mqtt, espressif/cjson) werden ueber main/idf_component.yml
    # eingebunden. SSD1306/SSD1309 sind bereits Teil des Core-esp_lcd (s.u.),
    # brauchen also keinen Eintrag in idf_component.yml.
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_http_client
        app_update
        esp_lcd
        esp_timer
        esp_driver_spi
        esp_driver_gpio
        esp_driver_ledc
        # display_ui.c (SSD1309-Profil) nutzt den neuen i2c_master-Treiber
        # (driver/i2c_master.h) statt der alten driver/i2c.h-API - letztere
        # liefert auf ESP-IDF 6.0.2 nicht mehr den von esp_lcd_new_panel_io_i2c
        # erwarteten i2c_master_bus_handle_t.
        esp_driver_i2c
)
