#include "analysis.h"

#include <cmath>

#include "config.h"

namespace {
constexpr float kTwoPi   = 6.283185307179586f;
constexpr float kRad2Deg = 57.29577951308232f;
}  // namespace

void BalanceAnalyzer::begin(uint8_t bladeCount) {
  stats_.bladeCount = bladeCount ? bladeCount : 1;
  noReference();
}

void BalanceAnalyzer::setBladeCount(uint8_t bladeCount) {
  stats_.bladeCount = bladeCount ? bladeCount : 1;
}

void BalanceAnalyzer::noReference() {
  xs_[0] = xs_[1] = xs_[2] = 0.0;
  ys_[0] = ys_[1] = ys_[2] = 0.0;
  count_ = 0;
  windowStartUs_ = 0;
  stats_.ok = false;
}

void BalanceAnalyzer::addSample(float dx, float dy, float dz, float angleRad,
                                uint64_t tUs) {
  if (windowStartUs_ == 0) windowStartUs_ = tUs;

  const float c = cosf(angleRad);
  const float s = sinf(angleRad);
  xs_[0] += dx * c; ys_[0] += dx * s;
  xs_[1] += dy * c; ys_[1] += dy * s;
  xs_[2] += dz * c; ys_[2] += dz * s;
  ++count_;

  if (tUs - windowStartUs_ >= cfg::kBalanceWindowUs) rollup(tUs);
}

void BalanceAnalyzer::rollup(uint64_t nowUs) {
  if (count_ >= cfg::kBalanceMinSamples) {
    // Per axis the projection onto cos/sin recovers half the sinusoid's
    // amplitude, hence the factor 2. The dominant axis carries the clearest
    // phase; the reported magnitude combines all three.
    double sumSq = 0.0;
    double bestAmp = -1.0, bestX = 0.0, bestY = 0.0;
    for (int k = 0; k < 3; ++k) {
      const double xm = xs_[k] / count_;
      const double ym = ys_[k] / count_;
      const double amp = 2.0 * std::sqrt(xm * xm + ym * ym);
      sumSq += amp * amp;
      stats_.vx[k] = static_cast<float>(2.0 * xm);  // amplitude-scaled phasor
      stats_.vy[k] = static_cast<float>(2.0 * ym);
      if (amp > bestAmp) { bestAmp = amp; bestX = xm; bestY = ym; }
    }

    stats_.mag = static_cast<float>(std::sqrt(sumSq));

    float deg = std::atan2(static_cast<float>(bestY), static_cast<float>(bestX)) * kRad2Deg;
    if (deg < 0.0f) deg += 360.0f;
    stats_.phaseDeg = deg;

    const float step = 360.0f / stats_.bladeCount;
    int blade = static_cast<int>(std::lround(deg / step)) % stats_.bladeCount;
    stats_.blade = blade;
    stats_.ok = true;
    ++stats_.seq;  // signal a fresh measurement window to the wizard
  }

  xs_[0] = xs_[1] = xs_[2] = 0.0;
  ys_[0] = ys_[1] = ys_[2] = 0.0;
  count_ = 0;
  windowStartUs_ = nowUs;
}
