#pragma once

namespace TimeService {
  void begin();      // konfiguriert NTP + Timezone anhand appConfig
  void reconfigure(); // nach Config-Änderung neu anwenden
}
