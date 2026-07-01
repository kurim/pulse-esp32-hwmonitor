#pragma once
#include "shared_state.h"

namespace DisplayUI {
  void begin();
  void loop();           // Touch-Handling + Redraw-Timing
  void notifyDataChanged(); // erzwingt Redraw beim nächsten loop()
}
