#pragma once

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include <mutex>

#include "analysis.h"
#include "balancer.h"
#include "config.h"
#include "hall.h"
#include "imu.h"

/**
 * WebService — WiFi access point plus async HTTP/WebSocket server.
 *
 * The device brings up its own AP (no home network needed — it works on a
 * ladder next to the fan). It serves the single-page UI straight from flash
 * at "/" and pushes a telemetry JSON snapshot to every connected WebSocket
 * client each cfg::kTelemetryMs. The same snapshot is available by polling
 * GET /api/status, which is handy for curl and scripting.
 *
 * Threading: ESPAsyncWebServer runs its HTTP/WS callbacks in the async_tcp
 * task, which on this dual-core ESP32 is genuinely parallel with the Arduino
 * loop task that calls loop(). The cached telemetry String is therefore shared
 * across tasks and guarded by jsonMux_. The library guards its own client
 * list, so textAll()/count()/cleanupClients() are called only from loop().
 */
class WebService {
 public:
  /** Attach the balancing wizard so /api/balance can drive it. Call before begin(). */
  void setBalancer(Balancer* b) { balancer_ = b; }

  /** Start the AP, DNS, mDNS responder and HTTP server. */
  void begin();

  /** Service captive-portal DNS and push telemetry if due. Call from loop(). */
  void loop(const HallStats& hall, const VibrationStats& vib,
            const BalanceStats& bal, int batteryPct, uint32_t uptimeS);

  String  ip() const;
  uint8_t stationCount() const;  ///< devices associated with the AP

 private:
  String buildJson(const HallStats& h, const VibrationStats& v,
                   const BalanceStats& b, int batteryPct, uint32_t uptimeS) const;

  AsyncWebServer server_{cfg::kHttpPort};
  AsyncWebSocket ws_{"/ws"};
  DNSServer      dns_;
  Balancer*      balancer_ = nullptr;  ///< optional; drives the Phase 5 wizard
  std::mutex     jsonMux_;      ///< guards lastJson_ (loop task writes, async_tcp reads)
  String         lastJson_ = "{}";
  uint32_t       lastPushMs_ = 0;
  bool           rebootPending_ = false;  ///< set by the OTA finish handler (async task)
  uint32_t       rebootAtMs_    = 0;      ///< reboot deadline, so the HTTP reply flushes first
};
