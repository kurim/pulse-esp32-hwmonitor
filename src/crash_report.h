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
