#pragma once

#include <cstddef>
#include <cstdint>

/**
 * FanBalancer — central compile-time configuration.
 *
 * All GPIO numbers refer to the M5StickC Plus2 external 8-pin header
 * (GND / 5V / G26 / G36-G25 / G0 / BAT / 3V3 / 5Vin) and internals.
 */
namespace cfg {

// ---- Firmware ------------------------------------------------------------
constexpr const char* kFwVersion = "0.1.0-phase1";

// ---- Hall sensor (A3144 + LM393 module, digital output) -------------------
constexpr int      kHallPin         = 26;         // module DO -> header pin G26
constexpr bool     kHallFallingEdge = true;       // DO is pulled LOW while the magnet passes
constexpr uint32_t kHallGlitchMinUs = 20'000;     // reject pulses closer than this (noise/bounce), caps at 3000 RPM
constexpr uint64_t kHallTimeoutUs   = 3'000'000;  // no pulse for this long -> NO SIGNAL

// ---- Rotor ----------------------------------------------------------------
constexpr uint8_t  kDefaultBladeCount = 3;        // used from Phase 5 on, user-configurable later

// ---- IMU (MPU6886 accelerometer) ------------------------------------------
constexpr uint32_t kImuSamplePeriodUs = 2'000;    // 500 Hz sampling target (spec)
constexpr uint32_t kImuWindowUs       = 200'000;  // vibration rollup window (5 Hz reporting)
constexpr float    kGravityAlpha      = 0.001f;   // per-sample EMA weight; tau ~2 s at 500 Hz
                                                  // (high-passes out gravity/tilt, keeps 1x-RPM vibration)

// ---- Status LED (red LED on G19, shared with the IR emitter) ---------------
constexpr int      kLedPin     = 19;              // flashes once per accepted Hall pulse
constexpr uint32_t kLedFlashMs = 25;

// ---- WiFi access point / web UI -------------------------------------------
constexpr const char* kApSsid       = "FanBalancer";
constexpr const char* kApPassword   = "balance123";   // WPA2 requires >= 8 characters
constexpr const char* kMdnsName     = "fanbalancer";  // http://fanbalancer.local
constexpr uint16_t    kHttpPort     = 80;
constexpr uint32_t    kTelemetryMs  = 250;            // WebSocket push period

// ---- On-device status display ----------------------------------------------
constexpr uint32_t kDisplayPeriodMs = 200;        // 5 Hz is plenty for a status screen
constexpr uint8_t  kDisplayBrightness = 180;

}  // namespace cfg
