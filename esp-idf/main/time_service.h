#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet SNTP und setzt die POSIX-Zeitzone aus app_config.
void time_service_begin(void);

#ifdef __cplusplus
}
#endif
