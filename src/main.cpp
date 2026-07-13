/**
 * FanBalancer — main entry point.
 *
 * Phase 1: Hall sensor RPM measurement + WiFi web UI + on-device status
 * screen. Wiring, usage and the phase roadmap live in README.md.
 *
 * Loop rules (per spec): no blocking calls, no delay() — everything is
 * driven by millis()/esp_timer; the single vTaskDelay(1) only yields the
 * core to the RTOS.
 */
#include <M5Unified.h>

#include "config.h"
#include "display.h"
#include "hall.h"
#include "imu.h"
#include "webserver.h"

namespace {
HallSensor    hall;
Imu           imu;
StatusDisplay statusDisplay;
WebService    web;

bool     diagnosticsPage = false;
uint32_t lastDisplayMs   = 0;
uint32_t lastLogMs       = 0;
uint32_t ledOffAtMs      = 0;
uint32_t lastSeenPulses  = 0;
uint32_t lastBatteryMs   = 0;
int      batteryPct      = -1;   // cached; -1 until the first read
}  // namespace

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);  // also asserts the Plus2 power-hold pin (G4)
  Serial.begin(115200);

  pinMode(cfg::kLedPin, OUTPUT);
  digitalWrite(cfg::kLedPin, LOW);

  statusDisplay.begin();
  hall.begin(cfg::kHallPin, cfg::kHallFallingEdge, cfg::kHallGlitchMinUs,
             cfg::kHallTimeoutUs);
  imu.begin();
  web.begin();

  Serial.printf("\nFanBalancer %s (Phase 1)\n", cfg::kFwVersion);
  Serial.printf("AP: %s  pass: %s  UI: http://%s  (http://%s.local)\n",
                cfg::kApSsid, cfg::kApPassword, web.ip().c_str(),
                cfg::kMdnsName);
}

void loop() {
  M5.update();
  hall.update();
  imu.sample();  // reads at ~500 Hz internally; cheap on other iterations
  const HallStats& hs = hall.stats();
  const VibrationStats& vs = imu.stats();
  const uint32_t now = millis();

  // Flash the red LED once per accepted Hall pulse — instant wiring feedback.
  if (hs.pulseCount != lastSeenPulses) {
    lastSeenPulses = hs.pulseCount;
    digitalWrite(cfg::kLedPin, HIGH);
    ledOffAtMs = now + cfg::kLedFlashMs;
    if (ledOffAtMs == 0) ledOffAtMs = 1;  // 0 is the disarmed sentinel; avoid it at the millis() wrap
  }
  if (ledOffAtMs != 0 && static_cast<int32_t>(now - ledOffAtMs) >= 0) {
    digitalWrite(cfg::kLedPin, LOW);
    ledOffAtMs = 0;
  }

  // Front button toggles between status and diagnostics pages.
  if (M5.BtnA.wasClicked()) diagnosticsPage = !diagnosticsPage;

  // Battery percentage moves on a minutes scale; reading the ADC every ~1 ms
  // loop pass only wastes CPU the later phases (IMU, FFT) will want. Cache it.
  if (batteryPct < 0 || now - lastBatteryMs >= 1000) {
    lastBatteryMs = now;
    batteryPct = M5.Power.getBatteryLevel();
  }
  web.loop(hs, vs, batteryPct, now / 1000);

  if (now - lastDisplayMs >= cfg::kDisplayPeriodMs) {
    lastDisplayMs = now;
    DisplayModel m;
    m.hall         = hs;
    m.vib          = vs;
    m.apSsid       = cfg::kApSsid;
    m.ip           = web.ip();
    m.wifiStations = web.stationCount();
    m.batteryPct   = batteryPct;
    m.freeHeap     = ESP.getFreeHeap();
    m.uptimeS      = now / 1000;
    m.diagnostics  = diagnosticsPage;
    statusDisplay.render(m);
  }

  // 1 Hz heartbeat on the serial console — debugging without WiFi.
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    Serial.printf("rpm=%.1f period=%.1fms status=%s vib_rms=%.4fg peak=%.4fg "
                  "imu=%.0fHz pulses=%lu missed=%lu glitches=%lu heap=%lu\n",
                  hs.rpm, hs.periodMs, hallStatusName(hs.status),
                  vs.rms, vs.peak, vs.rateHz,
                  static_cast<unsigned long>(hs.pulseCount),
                  static_cast<unsigned long>(hs.missedCount),
                  static_cast<unsigned long>(hs.glitchCount),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }

  vTaskDelay(1);  // yield; keeps CPU usage low without blocking semantics
}
