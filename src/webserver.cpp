#include "webserver.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include <cstdio>
#include <mutex>

#include "web_content.h"

void WebService::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(cfg::kApSsid, cfg::kApPassword)) {
    Serial.println("[web] softAP() failed to start");
  }

  // A phone one metre away needs a fraction of full TX power; lowering it
  // roughly halves peak current draw, which matters on the Plus2's small cell
  // (no PMIC) and reduces the risk of a brownout reset during a TX burst.
  WiFi.setTxPower(WIFI_POWER_11dBm);

  // Wildcard DNS: every hostname a joined client resolves points at the device,
  // so http://fanbalancer.local works even on phones that ignore mDNS, and the
  // OS captive-portal probe lands on our UI (redirected by onNotFound below).
  dns_.start(53, "*", WiFi.softAPIP());

  if (MDNS.begin(cfg::kMdnsName)) {
    MDNS.addService("http", "tcp", cfg::kHttpPort);
  }

  // Seed the cache with a real frame so a client connecting during the first
  // telemetry period receives valid JSON instead of the "{}" placeholder.
  lastJson_ = buildJson(HallStats{}, VibrationStats{}, -1, 0);

  ws_.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                     AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
      String snapshot;
      {
        std::lock_guard<std::mutex> lock(jsonMux_);
        snapshot = lastJson_;  // copy under the lock; loop() may be reassigning it
      }
      client->text(snapshot);  // immediate snapshot, no wait for the next tick
    }
  });
  server_.addHandler(&ws_);

  server_.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", INDEX_HTML);
  });
  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String snapshot;
    {
      std::lock_guard<std::mutex> lock(jsonMux_);
      snapshot = lastJson_;
    }
    req->send(200, "application/json", snapshot);
  });
  // Any other path — including OS captive-portal probes — redirects to the UI,
  // so joining the AP pops the FanBalancer page automatically.
  server_.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/");
  });

  server_.begin();
}

void WebService::loop(const HallStats& hall, const VibrationStats& vib,
                      int batteryPct, uint32_t uptimeS) {
  dns_.processNextRequest();

  const uint32_t now = millis();
  if (now - lastPushMs_ < cfg::kTelemetryMs) return;
  lastPushMs_ = now;

  String json = buildJson(hall, vib, batteryPct, uptimeS);
  {
    std::lock_guard<std::mutex> lock(jsonMux_);
    lastJson_ = json;
  }

  ws_.cleanupClients();
  if (ws_.count() > 0) {
    ws_.textAll(json);
  }
}

String WebService::buildJson(const HallStats& h, const VibrationStats& v,
                             int batteryPct, uint32_t uptimeS) const {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"fw\":\"%s\",\"rpm\":%.1f,\"period_ms\":%.1f,\"pulses\":%lu,"
           "\"glitches\":%lu,\"missed\":%lu,\"status\":\"%s\",\"uptime_s\":%lu,"
           "\"heap\":%lu,\"battery_pct\":%d,\"ws_clients\":%lu,"
           "\"imu_ok\":%s,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
           "\"vib_rms\":%.4f,\"vib_peak\":%.4f,\"vib_avg\":%.4f,\"imu_hz\":%.0f}",
           cfg::kFwVersion, h.rpm, h.periodMs,
           static_cast<unsigned long>(h.pulseCount),
           static_cast<unsigned long>(h.glitchCount),
           static_cast<unsigned long>(h.missedCount), hallStatusName(h.status),
           static_cast<unsigned long>(uptimeS),
           static_cast<unsigned long>(ESP.getFreeHeap()), batteryPct,
           static_cast<unsigned long>(ws_.count()),
           v.ok ? "true" : "false", v.ax, v.ay, v.az,
           v.rms, v.peak, v.avg, v.rateHz);
  return String(buf);
}

String WebService::ip() const {
  return WiFi.softAPIP().toString();
}

uint8_t WebService::stationCount() const {
  return WiFi.softAPgetStationNum();
}
