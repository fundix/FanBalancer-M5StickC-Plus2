#pragma once

#include <cstdint>

/** Broadband vibration metrics over the most recent window. */
struct VibrationStats {
  float ax = 0, ay = 0, az = 0;  ///< latest raw acceleration, g (includes gravity)
  float rms    = 0;   ///< RMS of the gravity-removed acceleration vector, g
  float peak   = 0;   ///< peak gravity-removed |a| in the window, g
  float avg    = 0;   ///< mean gravity-removed |a| in the window, g
  float rateHz = 0;   ///< achieved sample rate over the window
  bool  ok     = false;  ///< false when no IMU is present
};

/**
 * Imu — samples the MPU6886 accelerometer and reports broadband vibration.
 *
 * sample() is called every loop iteration and self-rate-limits to ~500 Hz
 * (the spec target) via esp_timer. It runs in the Arduino loop task — the same
 * task as M5.update() — so the shared internal I2C bus is never contended.
 *
 * Gravity and slow tilt are removed with a per-axis exponential moving average,
 * leaving only the AC vibration; its sum-of-squares / peak / mean are folded
 * into VibrationStats once per window (cfg::kImuWindowUs). This raw broadband
 * level is the foundation the Phase 4 lock-in / FFT will refine to the exact
 * 1x-rotation component used for balancing.
 */
class Imu {
 public:
  /** Detect the IMU (already initialised by M5.begin) and start clean. */
  void begin();

  /** Read at most one sample per cfg::kImuSamplePeriodUs and accumulate. */
  void sample();

  /** Latest finalised metrics. */
  const VibrationStats& stats() const { return stats_; }

 private:
  bool     enabled_ = false;
  bool     primed_  = false;
  float    gx_ = 0, gy_ = 0, gz_ = 0;   ///< gravity estimate (per-axis EMA), g
  float    sumSq_   = 0.0f;             ///< Sum of (dx^2+dy^2+dz^2) over the window
  float    sumMag_  = 0.0f;             ///< Sum of |d| over the window
  float    peak_    = 0.0f;
  uint32_t count_   = 0;
  uint64_t lastSampleUs_ = 0;
  uint64_t lastRollupUs_ = 0;
  VibrationStats stats_{};
};
