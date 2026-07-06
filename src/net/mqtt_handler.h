#pragma once

// Startet den MQTT-Client (nur wenn ein Broker-Host konfiguriert ist).
void mqtt_handler_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden (PubSubClient ist nicht
// async wie esp-mqtt - kuemmert sich um Reconnect und eingehende Pakete).
void mqtt_handler_loop(void);
