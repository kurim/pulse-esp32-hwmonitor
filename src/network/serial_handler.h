#pragma once

// Startet den Empfang von Hardwaredaten per USB/Serial (dieselbe Leitung,
// die auch zum Flashen/fuer die Log-Ausgabe genutzt wird).
// Alternative zu mqtt_handler_begin(), siehe app_config.hw_source.
void serial_handler_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden.
void serial_handler_loop(void);
