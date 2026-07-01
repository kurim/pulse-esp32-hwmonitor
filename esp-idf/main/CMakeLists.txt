idf_component_register(
    SRCS
        "app_main.c"
        "config_store.c"
        "shared_state.c"
        "display_ui.c"
        "touch_xpt2046.c"
        "web_portal.c"
        "mqtt_handler.c"
        "time_service.c"
        "weather_service.c"
    INCLUDE_DIRS "."
    # Nur Core-/Built-in-Komponenten hier auffuehren. Managed Components
    # (lvgl, esp_lvgl_port, esp_lcd_ili9341, espressif/mqtt, espressif/cjson)
    # werden ueber main/idf_component.yml eingebunden.
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
)
