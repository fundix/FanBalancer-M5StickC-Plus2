#include "webserver.h"

#include <ESPmDNS.h>
#include <Update.h>
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
  lastJson_ = buildJson(HallStats{}, VibrationStats{}, BalanceStats{}, -1, 0);

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

  // Balancing wizard control: POST /api/balance?cmd=start|trial|reset[&grams=&blade=]
  server_.on("/api/balance", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!balancer_) { req->send(503, "text/plain", "no balancer"); return; }
    const String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "";
    const float grams = req->hasParam("grams") ? req->getParam("grams")->value().toFloat() : 1.0f;
    const int   blade = req->hasParam("blade") ? req->getParam("blade")->value().toInt() : 1;
    Balancer::Cmd c = Balancer::Cmd::None;
    if (cmd == "start")      c = Balancer::Cmd::Start;
    else if (cmd == "trial") c = Balancer::Cmd::Trial;
    else if (cmd == "reset") c = Balancer::Cmd::Reset;
    if (c == Balancer::Cmd::None) { req->send(400, "text/plain", "bad cmd"); return; }
    balancer_->postCommand(c, grams, blade - 1);  // UI blades are 1-indexed
    req->send(200, "text/plain", "ok");
  });

  // OTA firmware update — flash a new build over WiFi without unplugging.
  server_.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", UPDATE_HTML);
  });
  server_.on(
      "/update", HTTP_POST,
      [this](AsyncWebServerRequest* req) {  // called once the upload finished
        const bool ok = !Update.hasError();
        auto* res = req->beginResponse(ok ? 200 : 500, "text/plain",
                                       ok ? "OK" : Update.errorString());
        res->addHeader("Connection", "close");
        req->send(res);
        if (ok) {
          rebootPending_ = true;
          rebootAtMs_    = millis() + 800;  // let the reply flush before rebooting
        }
      },
      [](AsyncWebServerRequest* req, String filename, size_t index,
         uint8_t* data, size_t len, bool final) {  // upload body, chunk by chunk
        if (index == 0) {
          Serial.printf("[ota] start: %s\n", filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
        }
        if (Update.isRunning() && Update.write(data, len) != len) {
          Update.printError(Serial);
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("[ota] success: %u bytes\n",
                          static_cast<unsigned>(index + len));
          } else {
            Update.printError(Serial);
          }
        }
      });

  // Any other path — including OS captive-portal probes — redirects to the UI,
  // so joining the AP pops the FanBalancer page automatically.
  server_.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/");
  });

  server_.begin();
}

void WebService::loop(const HallStats& hall, const VibrationStats& vib,
                      const BalanceStats& bal, int batteryPct, uint32_t uptimeS) {
  // Deferred reboot after a successful OTA (flag set from the async task).
  if (rebootPending_ && static_cast<int32_t>(millis() - rebootAtMs_) >= 0) {
    Serial.println("[ota] rebooting into new firmware");
    ESP.restart();
  }

  dns_.processNextRequest();

  const uint32_t now = millis();
  if (now - lastPushMs_ < cfg::kTelemetryMs) return;
  lastPushMs_ = now;

  String json = buildJson(hall, vib, bal, batteryPct, uptimeS);
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
                             const BalanceStats& b, int batteryPct,
                             uint32_t uptimeS) const {
  char buf[1200];
  int n = snprintf(buf, sizeof(buf),
           "{\"fw\":\"%s\",\"rpm\":%.1f,\"period_ms\":%.1f,\"pulses\":%lu,"
           "\"glitches\":%lu,\"missed\":%lu,\"status\":\"%s\",\"uptime_s\":%lu,"
           "\"heap\":%lu,\"battery_pct\":%d,\"ws_clients\":%lu,"
           "\"imu_ok\":%s,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
           "\"vib_rms\":%.4f,\"vib_peak\":%.4f,\"vib_avg\":%.4f,\"imu_hz\":%.0f,"
           "\"bal_ok\":%s,\"bal_mag\":%.4f,\"bal_phase\":%.0f,\"bal_blade\":%d,"
           "\"blades\":%u",
           cfg::kFwVersion, h.rpm, h.periodMs,
           static_cast<unsigned long>(h.pulseCount),
           static_cast<unsigned long>(h.glitchCount),
           static_cast<unsigned long>(h.missedCount), hallStatusName(h.status),
           static_cast<unsigned long>(uptimeS),
           static_cast<unsigned long>(ESP.getFreeHeap()), batteryPct,
           static_cast<unsigned long>(ws_.count()),
           v.ok ? "true" : "false", v.ax, v.ay, v.az,
           v.rms, v.peak, v.avg, v.rateHz,
           b.ok ? "true" : "false", b.mag, b.phaseDeg, b.blade,
           static_cast<unsigned>(b.bladeCount));

  if (n < 0 || n >= static_cast<int>(sizeof(buf))) return String("{}");

  if (balancer_) {
    const BalanceResult& r = balancer_->result();
    snprintf(buf + n, sizeof(buf) - n,
             ",\"bs_state\":\"%s\",\"bs_prog\":%d,\"bs_msg\":\"%s\","
             "\"bs_trial_g\":%.2f,\"bs_trial_blade\":%d,\"bs_res\":%s,"
             "\"bs_heavy_g\":%.4f,\"bs_heavy_deg\":%.0f,\"bs_corr_deg\":%.0f,"
             "\"bs_ba\":%d,\"bs_ga\":%.2f,\"bs_bb\":%d,\"bs_gb\":%.2f,"
             "\"bs_bs\":%d,\"bs_gs\":%.2f,\"bs_improve\":%.0f}",
             balancer_->stateName(), balancer_->progressPct(), balancer_->message(),
             balancer_->trialGrams(), balancer_->trialBlade() + 1,
             r.ok ? "true" : "false", r.heavyG, r.heavyDeg, r.corrDeg,
             r.bladeA + 1, r.gramsA, r.bladeB + 1, r.gramsB,
             r.bladeS + 1, r.gramsS, r.improvePct);
  } else {
    snprintf(buf + n, sizeof(buf) - n, "}");
  }
  return String(buf);
}

String WebService::ip() const {
  return WiFi.softAPIP().toString();
}

uint8_t WebService::stationCount() const {
  return WiFi.softAPgetStationNum();
}
