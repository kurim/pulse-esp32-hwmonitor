#pragma once
#include "shared_state.h"

namespace ConfigStore {
  void begin();
  void load(AppConfig &cfg);
  void save(const AppConfig &cfg);
  void resetToDefaults();
}
