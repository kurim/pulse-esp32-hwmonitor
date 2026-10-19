#pragma once

#include <stddef.h>
#include <stdint.h>

// Absturz-Diagnose: Reset-Grund des letzten Starts und - falls der letzte
// Absturz einen Core Dump in die "coredump"-Partition geschrieben hat (siehe
// partitions_*.csv) - dessen Groesse und Auslesen. Alles ohne statische
// Puffer (DRAM-Reserve beim CYD, siehe CLAUDE.md).

// Einmal frueh in setup() aufrufen: loggt den Reset-Grund und prueft die
// Coredump-Partition.
void crash_report_begin(void);

// Kurzer, stabiler Name des Reset-Grunds ("panic", "task_wdt", "brownout", ...).
const char *crash_report_reset_reason(void);

// true, wenn der Reset durch einen Fehler ausgeloest wurde (Panic, Watchdog,
// Brownout), nicht durch Einschalten/Neustart/OTA.
bool crash_report_reset_was_crash(void);

// Groesse des gespeicherten Core Dumps in Bytes, 0 = keiner vorhanden/gueltig.
size_t crash_report_dump_size(void);

// Liest bis zu len Bytes ab Offset aus dem Dump. Rueckgabe: gelesene Bytes
// (0 am Ende oder bei Fehler).
size_t crash_report_dump_read(size_t offset, uint8_t *buf, size_t len);

// Loescht den gespeicherten Core Dump. true bei Erfolg.
bool crash_report_dump_erase(void);

// Software-Watchdog fuer loop(): crash_report_loop_heartbeat() muss in jedem
// loop()-Durchlauf aufgerufen werden. Ist der letzte Aufruf laenger als
// CRASH_REPORT_LOOP_TIMEOUT_MS her, loest ein esp_timer-Callback abort() aus
// -> Panic mit Core Dump (alle Tasks inkl. dem haengenden loopTask), danach
// Reset. Bewusst NICHT der Task-Watchdog des IDF: dessen Timeout ist global
// (CONFIG_ESP_TASK_WDT_TIMEOUT_S = 60 s, wegen async_tcp, siehe
// platformio.ini) und liesse sich nicht nur fuer loop() verkuerzen.
#define CRASH_REPORT_LOOP_TIMEOUT_MS 20000

// Am ENDE von setup() aufrufen - setup() darf laenger dauern (z.B. das
// OTA-Update beim Boot, siehe ota_boot_run_pending_action()).
void crash_report_watchdog_start(void);
void crash_report_loop_heartbeat(void);

// Markiert den aktuellen loop()-Abschnitt (nur String-Literale uebergeben, es
// wird nur der Zeiger gemerkt). Loest der Watchdog aus, landet der Name samt
// Dauer im NVS und ist nach dem Neustart ueber crash_report_loop_stall()
// lesbar - der Core Dump zeigt nur das ENDE des Haengers (loopTask steht dann
// schon wieder im delay(1)), nicht die blockierende Stelle.
void crash_report_stage(const char *name);

// "<Stage> <ms>" des letzten Watchdog-Abbruchs, "" wenn es keinen gab.
const char *crash_report_loop_stall(void);

// Merkt sich einen eigenen Neustart-Grund (max. 15 Zeichen, z.B. "wifi_lost")
// im NVS und startet neu. crash_report_reset_reason() liefert ihn beim
// naechsten Boot statt "software". Kehrt nicht zurueck.
void crash_report_restart(const char *reason);

// Loggt alle 5 Minuten freien Heap, Minimum seit Boot und groessten
// zusammenhaengenden Block (Heap-Erschoepfung/-Fragmentierung ist der
// Hauptverdacht bei "Webportal tot, UI laeuft noch"). Aus loop() aufrufen.
void crash_report_heap_log(void);
