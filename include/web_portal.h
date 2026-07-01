#pragma once

namespace WebPortal {
  void begin();   // verbindet WLAN (oder startet Config-AP) und startet Webserver
  void loop();    // DNS-Captive-Portal-Handling, Restart-Scheduling
}
