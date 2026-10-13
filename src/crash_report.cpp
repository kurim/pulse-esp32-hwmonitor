#include "crash_report.h"
#include <Arduino.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace {

esp_reset_reason_t s_reason = ESP_RST_UNKNOWN;
size_t s_dumpAddr = 0; // absolute Flash-Adresse des Dump-Images
size_t s_dumpSize = 0; // 0 = kein gueltiger Dump

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

    // esp_core_dump_image_check() validiert die Pruefsumme des Images; ein
    // halb geschriebener Dump (Reset waehrend des Schreibens) gilt als leer.
    if (esp_core_dump_image_check() == ESP_OK) {
        size_t addr = 0, size = 0;
        if (esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0) {
            s_dumpAddr = addr;
            s_dumpSize = size;
        }
    }

    log_w("Reset-Grund: %s, Core Dump: %u Bytes", crash_report_reset_reason(),
          (unsigned)s_dumpSize);
}

const char *crash_report_reset_reason(void)
{
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
