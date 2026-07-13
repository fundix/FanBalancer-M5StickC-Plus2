#pragma once

#include <M5Unified.h>

#include "hall.h"
#include "imu.h"

/** Everything the status screen needs for one frame. */
struct DisplayModel {
  HallStats      hall;
  VibrationStats vib;
  const char* apSsid       = "";
  String      ip;
  uint8_t     wifiStations = 0;   ///< devices joined to the AP
  int         batteryPct   = -1;  ///< -1 = unknown
  uint32_t    freeHeap     = 0;
  uint32_t    uptimeS      = 0;
  bool        diagnostics  = false;  ///< false = main page, true = diagnostics
};

/**
 * StatusDisplay — minimal status UI on the built-in 1.14" LCD (240x135).
 *
 * The primary UI is the web app served over WiFi; this screen only shows
 * connection info, live RPM and device health so the tool remains usable
 * without a browser. Rendering goes through an off-screen canvas to avoid
 * flicker, and render() is cheap enough to call at a few Hz from loop().
 */
class StatusDisplay {
 public:
  void begin();
  void render(const DisplayModel& m);

 private:
  void drawHeader(const DisplayModel& m);
  void drawMain(const DisplayModel& m);
  void drawDiagnostics(const DisplayModel& m);

  M5Canvas canvas_{&M5.Display};
};
