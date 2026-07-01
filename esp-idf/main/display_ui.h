#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert Display (ILI9341 via esp_lcd), LVGL (esp_lvgl_port), Touch
// (XPT2046) und baut die Oberflaeche auf. Danach laufen Redraws/Updates
// selbststaendig im LVGL-Task.
void display_ui_begin(void);

#ifdef __cplusplus
}
#endif
