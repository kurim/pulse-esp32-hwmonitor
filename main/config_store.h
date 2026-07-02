#pragma once
#include "shared_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert NVS (einmalig beim Boot aufrufen).
void config_store_init(void);

// Laedt die Konfiguration aus NVS in cfg. Fehlende Schluessel behalten den
// Wert, den cfg bereits enthaelt (daher vorher config_set_defaults aufrufen).
void config_store_load(app_config_t *cfg);

// Persistiert cfg in NVS.
void config_store_save(const app_config_t *cfg);

#ifdef __cplusplus
}
#endif
