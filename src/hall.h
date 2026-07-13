#pragma once

#include <Arduino.h>

/** Rotation status derived from Hall pulse timing. */
enum class HallStatus : uint8_t {
  NoSignal,  ///< fewer than 2 pulses seen, or last pulse older than the timeout
  Ok,        ///< steady pulse train
  Unstable,  ///< pulse periods vary too much (spin-up, loose magnet, bad gap)
};

/** Human-readable name for a HallStatus (used by display, web UI and logs). */
const char* hallStatusName(HallStatus s);

/** Snapshot of everything the rest of the firmware needs from the Hall sensor. */
struct HallStats {
  float      rpm         = 0.0f;  ///< 0 when no signal
  float      periodMs    = 0.0f;  ///< median rotation period, 0 when unknown
  uint32_t   pulseCount  = 0;     ///< accepted pulses since boot
  uint32_t   glitchCount = 0;     ///< pulses rejected by the glitch filter
  uint32_t   missedCount = 0;     ///< periods that looked like a skipped pulse (~2x median)
  uint64_t   lastPulseUs = 0;     ///< esp_timer timestamp of the last accepted pulse
  HallStatus status      = HallStatus::NoSignal;
};

/**
 * HallSensor — measures rotation period and RPM from a single magnet passing
 * a Hall-effect switch once per revolution.
 *
 * Design: the ISR only timestamps edges (esp_timer, sub-microsecond) and
 * stores pulse-to-pulse periods in a small ring buffer. All math — median
 * RPM, stability classification, missed-pulse detection — happens in
 * update(), called from the main loop. State shared with the ISR is guarded
 * by a spinlock, so getters never race.
 *
 * The Hall pulse is also the 0-degree phase reference used by later phases.
 */
class HallSensor {
 public:
  /**
   * Attach the interrupt and start measuring.
   * @param pin          GPIO with the module's digital output
   * @param fallingEdge  true if the output goes LOW when the magnet passes
   * @param glitchMinUs  periods shorter than this are counted as glitches
   * @param timeoutUs    silence longer than this means NO SIGNAL
   */
  void begin(int pin, bool fallingEdge, uint32_t glitchMinUs, uint64_t timeoutUs);

  /** Recompute rpm/status from the ring buffer. Call from loop(). */
  void update();

  /** Latest computed statistics (valid after update()). */
  const HallStats& stats() const { return stats_; }

  /**
   * Interpolated rotor angle in [0, 2*pi) at esp_timer timestamp @p tUs, with
   * the magnet pulse as 0 (Phase 3 reference). Returns false when there is no
   * steady rotation to interpolate against.
   */
  bool angleAt(uint64_t tUs, float& angleRad) const;

 private:
  static constexpr size_t kRingSize = 16;  ///< stored periods (power of two)
  static constexpr size_t kWindow   = 8;   ///< periods used for the median

  static void IRAM_ATTR isr(void* arg);

  // --- written by the ISR, read by update() under the spinlock ---
  portMUX_TYPE      mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile uint64_t lastPulseUs_ = 0;
  volatile uint32_t pulseCount_  = 0;
  volatile uint32_t glitchCount_ = 0;
  volatile uint32_t periods_[kRingSize] = {};
  volatile uint8_t  head_ = 0;
  volatile uint8_t  validPeriods_    = 0;  ///< fresh periods since the last (re)start, saturates at kRingSize
  volatile uint32_t adaptiveFloorUs_ = 0;  ///< update()-published min plausible period; 0 = static floor only

  // --- main-loop side ---
  int       pin_          = -1;
  uint32_t  glitchMinUs_  = 0;
  uint64_t  timeoutUs_    = 0;
  uint32_t  missedCount_  = 0;
  uint32_t  lastCheckedPulseCount_ = 0;
  HallStats stats_{};
};
