#include "time_service.h"
#include "shared_state.h"
#include <Arduino.h>

namespace TimeService {

void begin() {
  // configTzTime setzt NTP-Server + POSIX-Timezone in einem Aufruf
  configTzTime(appConfig.tz, appConfig.ntp_server);
}

void reconfigure() {
  begin();
}

} // namespace TimeService
