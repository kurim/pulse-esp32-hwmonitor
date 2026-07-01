#pragma once
#include "shared_state.h"

namespace MqttHandler {
  void begin();
  void loop();
  void reconfigure(); // nach Config-Änderung neu verbinden
}
