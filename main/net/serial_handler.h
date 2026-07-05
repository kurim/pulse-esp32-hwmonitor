#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet den Empfang von Hardwaredaten per USB/UART0 (dieselbe Leitung, die
// auch zum Flashen/fuer die Log-Ausgabe genutzt wird - siehe README.md).
// Alternative zu mqtt_handler_begin(), siehe app_config.hw_source.
void serial_handler_begin(void);

#ifdef __cplusplus
}
#endif
