#pragma once

#include <cstdint>

/**
 * Result of the 1x synchronous lock-in — the imbalance vector.
 *
 * `mag` is the amplitude of the once-per-revolution vibration (noise rejected
 * by the averaging), a clean "how unbalanced" number to minimise. `phaseDeg`
 * is the rotor angle, measured from the magnet, at which the dominant axis'
 * vibration peaks — an *indicative* heavy-spot direction. The exact
 * angle->correction mapping (and grams) needs the Phase 5 test-weight
 * calibration, because the structural phase lag is not known a priori.
 */
struct BalanceStats {
  bool     ok        = false;  ///< true only while locked to a steady rotation
  float    mag       = 0.0f;   ///< 1x vibration magnitude (combined axes), g
  float    phaseDeg  = 0.0f;   ///< heavy-spot phase vs magnet, [0,360)
  int      blade     = 0;      ///< nearest blade (0 = magnet blade)
  uint8_t  bladeCount = 3;
  // Per-axis 1x complex phasor (amplitude-scaled cos/sin means), g. The Phase 5
  // wizard picks one axis and uses (vx,vy) as its complex vibration measurement.
  float    vx[3]     = {0, 0, 0};
  float    vy[3]     = {0, 0, 0};
  uint32_t seq       = 0;      ///< increments on every rollup (a fresh window)
};

/**
 * BalanceAnalyzer — synchronous (lock-in) detection of the 1x-rotation
 * vibration referenced to the Hall magnet pulse.
 *
 * For every gravity-removed accelerometer sample, tagged with the instantaneous
 * rotor angle, it accumulates the projection onto cos(angle) and sin(angle) for
 * each axis. Averaged over ~1 s (many revolutions) the uncorrelated broadband
 * noise cancels, leaving only the rotation-synchronous component — its amplitude
 * and phase. This is the core of a single-plane influence-coefficient balancer.
 *
 * Fed and read from the single Arduino loop task; no locking needed.
 */
class BalanceAnalyzer {
 public:
  void begin(uint8_t bladeCount);
  void setBladeCount(uint8_t bladeCount);

  /** One gravity-removed sample (g) tagged with rotor angle (rad) and time. */
  void addSample(float dx, float dy, float dz, float angleRad, uint64_t tUs);

  /** No valid rotation reference this tick — drop the in-flight window. */
  void noReference();

  const BalanceStats& stats() const { return stats_; }

 private:
  void rollup(uint64_t nowUs);

  double   xs_[3] = {0, 0, 0};   ///< per-axis Sum(d*cos(angle))
  double   ys_[3] = {0, 0, 0};   ///< per-axis Sum(d*sin(angle))
  uint32_t count_ = 0;
  uint64_t windowStartUs_ = 0;
  BalanceStats stats_{};
};
