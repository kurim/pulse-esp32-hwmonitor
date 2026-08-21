#pragma once

// Startet den MQTT-Client (nur wenn ein Broker-Host konfiguriert ist).
void mqtt_handler_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden (PubSubClient ist nicht
// async wie esp-mqtt - kuemmert sich um Reconnect und eingehende Pakete).
void mqtt_handler_loop(void);

// Trennt die MQTT-Verbindung und unterdrueckt Reconnect-Versuche, bis
// mqtt_handler_resume() aufgerufen wird - gibt den vom Client gehaltenen
// TCP/Puffer-Speicher frei. Auf Boards ohne PSRAM (CYD, C3, siehe
// CLAUDE.md) reicht der freie Heap fuer den GitHub-FOTA-TLS-Handshake
// sonst nicht zuverlaessig aus (github_ota.cpp), da MQTT parallel zum
// Sensor-Broker verbunden bleibt. Eingehende Messwerte sind waehrend eines
// laufenden Update-Checks/-Downloads ohnehin irrelevant (das Geraet startet
// bei Erfolg neu). No-op, wenn MQTT nicht aktiv ist.
void mqtt_handler_pause(void);

// Hebt mqtt_handler_pause() auf - der naechste mqtt_handler_loop()-Aufruf
// versucht wieder normal zu (re-)verbinden.
void mqtt_handler_resume(void);

// Wendet geaenderte MQTT-Einstellungen (Host/Port/User/Pass/Topic) bzw. einen
// geaenderten hw_source sofort an, ohne Reboot - siehe web_portal.cpp
// applyConfigFields(). Trennt eine bestehende Verbindung, mqtt_handler_loop()
// verbindet sich dank der bestehenden 5s-Reconnect-Schleife automatisch mit
// den neuen Werten neu (bzw. bleibt getrennt, falls hw_source jetzt USB ist).
void mqtt_handler_apply_config(void);
