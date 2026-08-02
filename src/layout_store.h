#pragma once
#include <Arduino.h>

// Persistiertes Dashboard-Editor-Layout, eigener NVS-Namespace ("pulselayout")
// getrennt von app_config - anders als z.B. pin_overrides ist das Layout-JSON
// variabel lang statt ein festes C-Struct, passt also nicht ins bestehende
// putBytes()-Blob-Muster in config_store.cpp.

// Hartes Limit: die NVS-Partition ist projektweit nur 20 KB gross und wird
// von allen Config-Keys gemeinsam genutzt (siehe partitions_4mb/8mb/16mb.csv).
constexpr size_t kMaxLayoutJsonBytes = 4096;

// Speichert json unter dem einzigen Layout-Key. Liefert false (und speichert
// nichts) wenn json laenger als kMaxLayoutJsonBytes ist - bewusst kein
// stillschweigendes Abschneiden.
bool layout_store_save(const String &json);

// Leerer String = kein gespeichertes Layout vorhanden (main.cpp faellt dann
// auf das generierte Grundlayout zurueck, siehe layout_default_json()).
String layout_store_load(void);

void layout_store_clear(void);
