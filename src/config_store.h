#pragma once
#include "shared_state.h"

// Initialisiert Preferences (einmalig beim Boot aufrufen).
void config_store_init(void);

// Laedt die Konfiguration aus Preferences/NVS in cfg. Fehlende Schluessel
// behalten den Wert, den cfg bereits enthaelt (daher vorher
// config_set_defaults aufrufen).
void config_store_load(app_config_t *cfg);

// Persistiert cfg in Preferences/NVS.
void config_store_save(const app_config_t *cfg);
