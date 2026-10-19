#include "crash_report.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_timer.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace {

esp_reset_reason_t s_reason = ESP_RST_UNKNOWN;
size_t s_dumpAddr = 0; // absolute Flash-Adresse des Dump-Images
size_t s_dumpSize = 0; // 0 = kein gueltiger Dump
char   s_customReason[16] = ""; // Grund eines gewollten Neustarts, siehe crash_report_restart()
char   s_loopStall[24] = ""; // "<Stage> <ms>" des letzten Watchdog-Abbruchs, siehe wdtCheck()
volatile uint32_t s_loopBeatMs = 0;
volatile const char *s_stage = "";
esp_timer_handle_t s_wdtTimer = nullptr;

void wdtCheck(void *)
{
    // signed: der Heartbeat (anderer Core) kann nach millis() gesetzt werden -> -1/-2 ms
    // waeren als uint32_t ~4,29e9 und loesten faelschlich aus.
    int32_t stalled = (int32_t)(millis() - s_loopBeatMs);
    if (stalled > (int32_t)CRASH_REPORT_LOOP_TIMEOUT_MS) {
        // abort() -> Panic-Handler schreibt den Core Dump (alle Tasks).
        log_e("loop() haengt seit %u ms in Stage \"%s\" - abort()", (unsigned)stalled, s_stage);
        // Stage ueberlebt den Reset nur im NVS (kein RTC/DRAM-Puffer, siehe CLAUDE.md).
        char buf[sizeof(s_loopStall)];
        snprintf(buf, sizeof(buf), "%.14s %u", s_stage, (unsigned)stalled);
        Preferences p;
        if (p.begin("pulsediag", false)) {
            p.putString("stall", buf);
            p.end();
        }
        abort();
    }
}

const esp_partition_t *dumpPartition()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
}

} // namespace

void crash_report_begin(void)
{
    s_reason = esp_reset_reason();
    s_dumpSize = 0;

    // Eigener Neustart-Grund aus dem letzten Lauf (crash_report_restart()):
    // einmal lesen und sofort loeschen, damit er nur den direkt folgenden
    // Boot beschreibt.
    s_customReason[0] = '\0';
    Preferences p;
    if (p.begin("pulsediag", false)) {
        String r = p.getString("restart", "");
        if (r.length() > 0) {
            strlcpy(s_customReason, r.c_str(), sizeof(s_customReason));
            p.remove("restart");
        }
        p.end();
    }

    s_loopStall[0] = '\0';
    if (p.begin("pulsediag", false)) {
        String r = p.getString("stall", "");
        strlcpy(s_loopStall, r.c_str(), sizeof(s_loopStall));
        p.remove("stall");
        p.end();
    }

    // esp_core_dump_image_check() validiert die Pruefsumme des Images; ein
    // halb geschriebener Dump (Reset waehrend des Schreibens) gilt als leer.
    if (esp_core_dump_image_check() == ESP_OK) {
        size_t addr = 0, size = 0;
        if (esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0) {
            s_dumpAddr = addr;
            s_dumpSize = size;
        }
    }

    log_w("Reset-Grund: %s, Core Dump: %u Bytes, loop-Stall: %s", crash_report_reset_reason(),
          (unsigned)s_dumpSize, s_loopStall[0] ? s_loopStall : "-");
}

const char *crash_report_reset_reason(void)
{
    if (s_customReason[0] != '\0' && s_reason == ESP_RST_SW) return s_customReason;
    switch (s_reason) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
    }
}

bool crash_report_reset_was_crash(void)
{
    if (s_customReason[0] != '\0' && s_reason == ESP_RST_SW) return true;
    switch (s_reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

size_t crash_report_dump_size(void)
{
    return s_dumpSize;
}

size_t crash_report_dump_read(size_t offset, uint8_t *buf, size_t len)
{
    const esp_partition_t *part = dumpPartition();
    if (!part || s_dumpSize == 0 || offset >= s_dumpSize || s_dumpAddr < part->address) return 0;
    if (len > s_dumpSize - offset) len = s_dumpSize - offset;
    size_t partOff = (s_dumpAddr - part->address) + offset;
    if (esp_partition_read(part, partOff, buf, len) != ESP_OK) return 0;
    return len;
}

bool crash_report_dump_erase(void)
{
    if (esp_core_dump_image_erase() != ESP_OK) return false;
    s_dumpSize = 0;
    return true;
}

void crash_report_watchdog_start(void)
{
    if (s_wdtTimer) return;
    s_loopBeatMs = millis();
    esp_timer_create_args_t args = {};
    args.callback = wdtCheck;
    args.name = "loop_wdt";
    if (esp_timer_create(&args, &s_wdtTimer) == ESP_OK) {
        esp_timer_start_periodic(s_wdtTimer, 1000 * 1000);
    }
}

void crash_report_loop_heartbeat(void)
{
    s_loopBeatMs = millis();
}

void crash_report_stage(const char *name)
{
    s_stage = name;
}

const char *crash_report_loop_stall(void)
{
    return s_loopStall;
}

void crash_report_restart(const char *reason)
{
    Preferences p;
    if (p.begin("pulsediag", false)) {
        p.putString("restart", reason);
        p.end();
    }
    ESP.restart();
}

void crash_report_heap_log(void)
{
    static uint32_t last = 0;
    if (millis() - last < 5UL * 60UL * 1000UL) return;
    last = millis();
    log_w("Heap: frei %u, Minimum %u, groesster Block %u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}
