#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet den Hintergrund-Task, der periodisch (alle 10 Min) das Wetter von
// OpenWeatherMap abruft, sofern in app_config aktiviert.
void weather_service_begin(void);

// Wandelt eine Windrichtung in Grad (0-360) in eine deutsche
// 8-Punkte-Kompassangabe ("N","NO","O","SO","S","SW","W","NW") um.
const char *weather_wind_compass(int deg);

#ifdef __cplusplus
}
#endif
