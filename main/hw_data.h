#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Parst ein JSON-Objekt mit den vom PC-Client gelieferten Hardwarewerten
// (cpu_load/cpu_temp/cpu_power/gpu_load/gpu_temp/gpu_power, alle optional)
// und aktualisiert hw_info sowie die Verlaufs-Ringpuffer (cpu_history/
// gpu_history). Wird sowohl vom MQTT- als auch vom seriellen Handler
// genutzt, da beide dasselbe Payload-Format liefern (siehe README.md).
void hw_data_apply_json(const char *data, int len);

#ifdef __cplusplus
}
#endif
