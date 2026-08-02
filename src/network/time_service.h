#pragma once

// Startet SNTP und setzt die POSIX-Zeitzone aus app_config.
void time_service_begin(void);

// true, sobald SNTP tatsaechlich synchronisiert hat (nicht nur configTzTime()
// aufgerufen wurde - das startet SNTP nur asynchron, die erste echte
// Antwort kann je nach NTP-Server 1-2s dauern). Ueber eine Epoch-
// Plausibilitaetsschwelle statt der esp-idf-eigenen SNTP-Status-API, um
// keinen weiteren esp-idf-Header hier einzubinden - vor der Synchronisation
// liefert time(nullptr) einen Wert nahe 0 (Board-Boot-Referenzpunkt), nie
// etwas jenseits dieser Schwelle. Fuer main.cpp's Boot-Sequenz (siehe dort).
bool time_service_is_synced(void);
