#pragma once
#include "board_profiles.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert den GT911-Kapazitiv-Touch (I2C) fuer das Guition-JC8048W550-
// Profil. Gibt bei Erfolg ein esp_lcd_touch_handle_t zurueck (weiter an
// esp_lvgl_port's lvgl_port_add_touch() gereicht, siehe display_ui.c), sonst
// NULL - Fehler werden hier geloggt, nicht per esp_err_t propagiert (gleiches
// "Panel bleibt aus, Webportal bleibt erreichbar"-Muster wie beim
// Display-Init selbst).
esp_lcd_touch_handle_t touch_gt911_init(const board_profile_t *profile);

#ifdef __cplusplus
}
#endif
