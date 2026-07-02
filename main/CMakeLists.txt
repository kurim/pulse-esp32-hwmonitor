idf_component_register(
    SRCS
        "app_main.c"
        "config_store.c"
        "shared_state.c"
        "board_profiles.c"
        "display_ui.c"
        "font_mdi_icons_20.c"
        "touch_xpt2046.c"
        "web_portal.c"
        "mqtt_handler.c"
        "time_service.c"
        "weather_service.c"
    INCLUDE_DIRS "."
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
        # "driver" statt esp_driver_i2c: touch_xpt2046.c/display_ui.c nutzen
        # bewusst die alte driver/i2c.h-API (i2c_param_config/i2c_driver_install),
        # die "driver" als Kompatibilitaets-Wrapper um esp_driver_i2c bereitstellt.
        driver
)
