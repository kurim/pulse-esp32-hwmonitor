#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert das in app_config.display_type gewaehlte Panel (esp_lcd),
// LVGL (esp_lvgl_port), ggf. Touch (XPT2046, nur CYD-Profil) und baut die
// dazu passende Oberflaeche auf (siehe board_profiles.h). Danach laufen
// Redraws/Updates selbststaendig im LVGL-Task.
void display_ui_begin(void);

#ifdef __cplusplus
}
#endif
