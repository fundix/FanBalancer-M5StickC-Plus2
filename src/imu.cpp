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

ImuSample Imu::sample() {
  ImuSample out;  // valid=false unless a fresh post-warm-up reading is produced
  if (!enabled_) return out;

  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
  if (now - lastSampleUs_ < cfg::kImuSamplePeriodUs) return out;
  lastSampleUs_ = now;

  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) {
    // Persistent read failure (bus fault / sensor hang): reveal it instead of
    // leaving the last values on screen as if they were live.
    if (primed_ && now - lastGoodReadUs_ > cfg::kImuStaleUs) {
      stats_.ok = false;
      stats_.rateHz = 0.0f;
    }
    return out;
  }
  lastGoodReadUs_ = now;

  // Prime the gravity estimate on the first sample so it does not spend seconds
  // ramping up from zero, and start a short warm-up window.
  if (!primed_) {
    gx_ = ax; gy_ = ay; gz_ = az;
    primed_ = true;
    lastRollupUs_  = now;
    warmupUntilUs_ = now + cfg::kImuWarmupUs;
  }

  // During warm-up, converge gravity with a fast EMA and report nothing, so a
  // first sample taken while the device is being positioned (or a settling
  // sensor) does not show up as a huge fake vibration for seconds.
  const bool  warming = now < warmupUntilUs_;
  const float alpha   = warming ? cfg::kGravityWarmupAlpha : cfg::kGravityAlpha;
  gx_ += alpha * (ax - gx_);
  gy_ += alpha * (ay - gy_);
  gz_ += alpha * (az - gz_);
  if (warming) {
    lastRollupUs_ = now;  // keep the window baseline fresh; do not accumulate yet
    return out;
  }

  const float dx = ax - gx_, dy = ay - gy_, dz = az - gz_;
  out = {true, dx, dy, dz, now};  // hand the residual to the caller's lock-in
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

  return out;
}
