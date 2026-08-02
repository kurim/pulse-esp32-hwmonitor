#include "hw_data.h"
#include "shared_state.h"
#include <ArduinoJson.h>

static int64_t s_last_history_push;
#define HISTORY_PUSH_INTERVAL_MS 1000

void hw_data_apply_json(const char *data, int len)
{
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        return;
    }

    // hw_info/cpu_history/gpu_history werden auch vom LVGL-Refresh (Core 1)
    // und vom Settings-Webserver (h_status(), eigener AsyncTCP-Task)
    // gelesen - siehe hw_data_lock() in shared_state.h.
    hw_data_lock();
    if (doc["cpu_load"].is<float>())  hw_info.cpu_load  = doc["cpu_load"];
    if (doc["cpu_temp"].is<float>())  hw_info.cpu_temp  = doc["cpu_temp"];
    if (doc["cpu_power"].is<float>()) hw_info.cpu_power = doc["cpu_power"];
    if (doc["gpu_load"].is<float>())  hw_info.gpu_load  = doc["gpu_load"];
    if (doc["gpu_temp"].is<float>())  hw_info.gpu_temp  = doc["gpu_temp"];
    if (doc["gpu_power"].is<float>()) hw_info.gpu_power = doc["gpu_power"];

    hw_info.last_update_ms = now_ms();
    hw_info.ever_received  = true;

    // Verlauf hoechstens 1x/Sek aktualisieren (unabhaengig von Publish-Rate).
    if (now_ms() - s_last_history_push >= HISTORY_PUSH_INTERVAL_MS) {
        s_last_history_push = now_ms();
        history_push(&cpu_history, hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        history_push(&gpu_history, hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
    }
    hw_data_unlock();
}
