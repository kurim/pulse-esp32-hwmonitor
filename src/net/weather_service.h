#pragma once

// Startet den Hintergrund-Task, der periodisch (alle 10 Min) das Wetter von
// OpenWeatherMap abruft, sofern in app_config aktiviert.
void weather_service_begin(void);

// Wandelt eine Windrichtung in Grad (0-360) in eine 8-Punkte-Kompassangabe
// um (deutsch oder englisch je nach app_config.language).
const char *weather_wind_compass(int deg);
