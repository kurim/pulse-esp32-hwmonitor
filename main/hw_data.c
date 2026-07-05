#include "hw_data.h"
#include "shared_state.h"
#include "cJSON.h"

static int64_t s_last_history_push;
#define HISTORY_PUSH_INTERVAL_MS 1000

void hw_data_apply_json(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        return;
    }

    cJSON *v;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_load"))))  hw_info.cpu_load  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_temp"))))  hw_info.cpu_temp  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_power")))) hw_info.cpu_power = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_load"))))  hw_info.gpu_load  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_temp"))))  hw_info.gpu_temp  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_power")))) hw_info.gpu_power = (float)v->valuedouble;
    cJSON_Delete(root);

    hw_info.last_update_ms = now_ms();
    hw_info.ever_received  = true;

    // Verlauf hoechstens 1x/Sek aktualisieren (unabhaengig von Publish-Rate).
    if (now_ms() - s_last_history_push >= HISTORY_PUSH_INTERVAL_MS) {
        s_last_history_push = now_ms();
        history_push(&cpu_history, hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        history_push(&gpu_history, hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
    }
}
