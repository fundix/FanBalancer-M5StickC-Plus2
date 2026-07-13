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
constexpr const char* kFwVersion = "0.5.0-phase5";

// ---- Hall sensor (A3144 + LM393 module, digital output) -------------------
constexpr int      kHallPin         = 26;         // module DO -> header pin G26
constexpr bool     kHallFallingEdge = true;       // DO is pulled LOW while the magnet passes
constexpr uint32_t kHallGlitchMinUs = 20'000;     // reject pulses closer than this (noise/bounce), caps at 3000 RPM
constexpr uint64_t kHallTimeoutUs   = 3'000'000;  // no pulse for this long -> NO SIGNAL

// ---- Rotor ----------------------------------------------------------------
constexpr uint8_t  kDefaultBladeCount = 3;        // used from Phase 5 on, user-configurable later

// ---- IMU (MPU6886 accelerometer) ------------------------------------------
// NOTE: the achieved rate is loop-limited to ~230 Hz (loop() yields 1 tick per
// pass), not the 2000 us gate — plenty for a 1-3.3 Hz fan (~70 samples/rev).
// True uniform 500 Hz (FIFO or a timer task) is deferred to Phase 4, which
// needs evenly-spaced samples for the FFT/lock-in.
constexpr uint32_t kImuSamplePeriodUs = 2'000;    // 500 Hz sampling ceiling (spec target)
constexpr uint32_t kImuWindowUs       = 200'000;  // vibration rollup window (5 Hz reporting)
constexpr float    kGravityAlpha      = 0.001f;   // steady-state EMA weight; tau ~2 s
                                                  // (high-passes out gravity/tilt, keeps 1x-RPM vibration)
constexpr float    kGravityWarmupAlpha = 0.05f;   // fast EMA at boot; tau ~40 ms, converges gravity quickly
constexpr uint32_t kImuWarmupUs        = 500'000; // discard vibration during this initial settle
constexpr uint32_t kImuStaleUs         = 600'000; // no successful read for this long -> flag IMU stale

// ---- Balancing (Phase 3+4: 1x synchronous lock-in referenced to the magnet) --
constexpr uint32_t kBalanceWindowUs = 1'000'000;  // integrate the 1x vector over ~1 s (several revs)
constexpr uint32_t kBalanceMinSamples = 64;       // need at least this many tagged samples per window

// ---- Balancing wizard (Phase 5: influence-coefficient single-plane balance) ---
constexpr uint32_t kBalanceCaptureWindows = 3;    // average this many 1 s windows per capture (~3 s)
constexpr float    kBalanceMinTrialEffect = 0.0008f;  // min |V1-V0| (g) for the trial weight to count
constexpr float    kBalanceMaxRpmDrift    = 0.12f;    // baseline vs trial RPM must match within 12%

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
