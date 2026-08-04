#pragma once

// Parst ein JSON-Objekt mit den vom PC-Client gelieferten Hardwarewerten
// (cpu_load/cpu_temp/cpu_power/gpu_load/gpu_temp/gpu_power, alle optional)
// und aktualisiert hw_info sowie die Verlaufs-Ringpuffer (cpu_history/
// gpu_history). Jeder weitere numerische Top-Level-Key landet zusaetzlich
// in dyn_values (siehe shared_state.h) - die PC-App liefert mehr Felder als
// nur die 6 festen, der Mono-Layout-Editor kann darauf beliebig binden.
// Wird sowohl vom MQTT- als auch vom seriellen Handler genutzt, da beide
// dasselbe Payload-Format liefern (siehe README.md).
void hw_data_apply_json(const char *data, int len);
