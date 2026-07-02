#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "board_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert den XPT2046 auf einem eigenen SPI-Bus, mit den Pins/der
// Aufloesung aus dem gegebenen Board-Profil (nur fuer Profile mit
// has_touch=true sinnvoll, siehe board_profiles.h).
void touch_xpt2046_init(const board_profile_t *profile);

// Liest die aktuelle Beruehrung und rechnet sie passend zur aktuellen
// app_config.rotation in Bildschirmkoordinaten um.
// Rueckgabe: true wenn beruehrt (sx/sy dann gueltig), sonst false.
bool touch_xpt2046_read(uint16_t *sx, uint16_t *sy);

#ifdef __cplusplus
}
#endif
