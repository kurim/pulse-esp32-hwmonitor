#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet den Hintergrund-Task, der periodisch (alle 10 Min) das Wetter von
// OpenWeatherMap abruft, sofern in app_config aktiviert.
void weather_service_begin(void);

#ifdef __cplusplus
}
#endif
