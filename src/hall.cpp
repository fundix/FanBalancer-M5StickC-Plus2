#include "hall.h"

#include <esp_timer.h>

#include <algorithm>

const char* hallStatusName(HallStatus s) {
  switch (s) {
    case HallStatus::Ok:       return "OK";
    case HallStatus::Unstable: return "UNSTABLE";
    default:                   return "NO SIGNAL";
  }
}

void HallSensor::begin(int pin, bool fallingEdge, uint32_t glitchMinUs, uint64_t timeoutUs) {
  pin_         = pin;
  glitchMinUs_ = glitchMinUs;
  timeoutUs_   = timeoutUs;

  // Internal pull-up as a safety net; most LM393 modules drive DO push-pull
  // or have their own pull-up, so this only matters with a bare A3144.
  pinMode(pin_, INPUT_PULLUP);
  attachInterruptArg(digitalPinToInterrupt(pin_), &HallSensor::isr, this,
                     fallingEdge ? FALLING : RISING);
}

void IRAM_ATTR HallSensor::isr(void* arg) {
  auto* self = static_cast<HallSensor*>(arg);
  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());

  portENTER_CRITICAL_ISR(&self->mux_);
  const uint64_t last = self->lastPulseUs_;
  if (last != 0) {
    const uint64_t delta = now - last;

    // Reject edges arriving implausibly soon after the previous one. The static
    // floor guards during acquisition; once a median exists, update() publishes
    // an adaptive floor (~1/4 rotation) that also rejects LM393 comparator
    // double-triggering on the magnet's leading/trailing field at slow speeds.
    uint32_t floor = self->glitchMinUs_;
    if (self->adaptiveFloorUs_ > floor) floor = self->adaptiveFloorUs_;
    if (delta < floor) {
      // Electrical noise or comparator chatter — count it, keep the timebase.
      self->glitchCount_ = self->glitchCount_ + 1;
      portEXIT_CRITICAL_ISR(&self->mux_);
      return;
    }

    if (delta <= self->timeoutUs_) {
      // A genuine rotation period.
      self->periods_[self->head_] =
          static_cast<uint32_t>(std::min<uint64_t>(delta, UINT32_MAX));
      self->head_ = (self->head_ + 1) % kRingSize;
      if (self->validPeriods_ < kRingSize) self->validPeriods_ = self->validPeriods_ + 1;
    } else {
      // Silence longer than the timeout: the rotor was stopped and restarted.
      // Treat this pulse as a fresh timebase and drop the stale periods, so the
      // reading re-qualifies from scratch instead of resurrecting the old RPM.
      self->validPeriods_ = 0;
    }
  }
  self->lastPulseUs_ = now;
  self->pulseCount_  = self->pulseCount_ + 1;
  portEXIT_CRITICAL_ISR(&self->mux_);
}

void HallSensor::update() {
  // Take a consistent snapshot of the ISR state.
  uint32_t periods[kRingSize];
  uint32_t count, glitches;
  uint64_t lastUs;
  uint8_t  head, valid;

  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < kRingSize; ++i) periods[i] = periods_[i];
  count    = pulseCount_;
  glitches = glitchCount_;
  lastUs   = lastPulseUs_;
  head     = head_;
  valid    = validPeriods_;
  portEXIT_CRITICAL(&mux_);

  stats_.pulseCount  = count;
  stats_.glitchCount = glitches;
  stats_.lastPulseUs = lastUs;
  stats_.missedCount = missedCount_;

  const uint64_t now    = static_cast<uint64_t>(esp_timer_get_time());
  const uint32_t stored = valid;  // only periods measured since the last (re)start

  if (stored < 2 || lastUs == 0 || (now - lastUs) > timeoutUs_) {
    stats_.status   = HallStatus::NoSignal;
    stats_.rpm      = 0.0f;
    stats_.periodMs = 0.0f;
    adaptiveFloorUs_ = 0;  // fall back to the static floor while re-acquiring
    lastCheckedPulseCount_ = count;
    return;
  }

  // Median over the most recent periods — robust against a single outlier
  // (e.g. one missed pulse), unlike a plain average.
  const size_t n = std::min<size_t>(stored, kWindow);
  uint32_t window[kWindow];
  for (size_t k = 0; k < n; ++k) {
    window[k] = periods[(head + kRingSize - 1 - k) % kRingSize];
  }
  std::sort(window, window + n);
  const uint32_t median = window[n / 2];
  const uint32_t newest = periods[(head + kRingSize - 1) % kRingSize];

  stats_.periodMs = median / 1000.0f;
  stats_.rpm      = 60'000'000.0f / static_cast<float>(median);

  // Publish an adaptive glitch floor for the ISR. Reference the second-largest
  // recent period (window[n-2]) — robust to one missed-pulse outlier, and in a
  // double-triggering sensor (LM393 firing as the magnet enters AND leaves) it
  // is the true revolution, while the spurious edge is a small fraction of it.
  // A quarter of that reference rejects the spurious edge without ever touching
  // a real speed change (a rotor cannot speed up >4x in one revolution). Engages
  // after 4 periods so it never disturbs spin-up.
  adaptiveFloorUs_ = (n >= 4) ? window[n - 2] / 4 : 0;

  // A skipped detection shows up as one period at roughly twice the median.
  // (Stored periods are capped at timeoutUs_, so 3*median cannot overflow.)
  if (count != lastCheckedPulseCount_ && newest > median + median / 2 &&
      newest < 3 * median) {
    ++missedCount_;
    stats_.missedCount = missedCount_;
  }
  lastCheckedPulseCount_ = count;

  // Stability: spread of the window relative to the median, but excluding the
  // extreme sample at each end once we have enough periods — so the single
  // missed-pulse outlier the median already tolerates doesn't read as UNSTABLE.
  const uint32_t spread =
      (n >= 4) ? (window[n - 2] - window[1]) : (window[n - 1] - window[0]);
  stats_.status = (n >= 4 && spread > median / 5) ? HallStatus::Unstable
                                                  : HallStatus::Ok;
}
