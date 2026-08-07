#pragma once
#include "shared_state.h"

void config_store_init(void);

// Laedt die Konfiguration aus Preferences/NVS in cfg. Fehlende Schluessel
// behalten den Wert, den cfg bereits enthaelt (daher vorher
// config_set_defaults aufrufen).
void config_store_load(app_config_t *cfg);

void config_store_save(const app_config_t *cfg);

// Loescht den kompletten "cydcfg"-NVS-Namespace (alle app_config-Felder).
// Die WiFiManager-eigenen gespeicherten Zugangsdaten (Namespace "pulsewifi")
// sind davon NICHT betroffen - siehe web_portal.cpp fuer den vollstaendigen
// Werkseinstellungen-Reset.
void config_store_factory_reset(void);
