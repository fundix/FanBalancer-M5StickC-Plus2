#include "imu.h"

#include <M5Unified.h>
#include <esp_timer.h>

#include <cmath>

#include "config.h"

void Imu::begin() {
  enabled_    = M5.Imu.isEnabled();
  stats_.ok   = enabled_;
  if (!enabled_) {
    Serial.println("[imu] no IMU detected — vibration disabled");
  }
}

void Imu::sample() {
  if (!enabled_) return;

  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
  if (now - lastSampleUs_ < cfg::kImuSamplePeriodUs) return;
  lastSampleUs_ = now;

  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

  // Prime the gravity estimate on the first sample so it does not spend seconds
  // ramping up from zero (which would read as a huge fake vibration at start).
  if (!primed_) {
    gx_ = ax; gy_ = ay; gz_ = az;
    primed_ = true;
    lastRollupUs_ = now;
  }

  // Track gravity (and slow tilt) with a slow EMA; the residual is the AC part.
  gx_ += cfg::kGravityAlpha * (ax - gx_);
  gy_ += cfg::kGravityAlpha * (ay - gy_);
  gz_ += cfg::kGravityAlpha * (az - gz_);

  const float dx = ax - gx_, dy = ay - gy_, dz = az - gz_;
  const float sq  = dx * dx + dy * dy + dz * dz;
  const float mag = sqrtf(sq);

  sumSq_  += sq;
  sumMag_ += mag;
  if (mag > peak_) peak_ = mag;
  ++count_;

  const uint64_t elapsed = now - lastRollupUs_;
  if (elapsed >= cfg::kImuWindowUs && count_ > 0) {
    stats_.ax     = ax;
    stats_.ay     = ay;
    stats_.az     = az;
    stats_.rms    = sqrtf(sumSq_ / count_);
    stats_.avg    = sumMag_ / count_;
    stats_.peak   = peak_;
    stats_.rateHz = count_ * 1'000'000.0f / static_cast<float>(elapsed);
    stats_.ok     = true;

    sumSq_ = 0.0f;
    sumMag_ = 0.0f;
    peak_ = 0.0f;
    count_ = 0;
    lastRollupUs_ = now;
  }
}
