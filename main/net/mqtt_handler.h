#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet den MQTT-Client (nur wenn ein Broker-Host konfiguriert ist).
// esp-mqtt kuemmert sich selbst um Verbindungsaufbau und Reconnect.
void mqtt_handler_begin(void);

#ifdef __cplusplus
}
#endif
