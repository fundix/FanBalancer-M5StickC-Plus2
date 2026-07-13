#include "balancer.h"

#include <cmath>

#include "config.h"

namespace {
constexpr float kTwoPi   = 6.283185307179586f;
constexpr float kDeg2Rad = 0.017453292519943295f;
constexpr float kRad2Deg = 57.29577951308232f;

float normDeg(float d) {
  d = fmodf(d, 360.0f);
  if (d < 0.0f) d += 360.0f;
  return d;
}
// Smallest signed difference a-b, in (-180,180].
float diffDeg(float a, float b) {
  float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
  return d;
}
}  // namespace

void Balancer::begin(uint8_t bladeCount) {
  bladeCount_ = bladeCount ? bladeCount : 1;
  state_ = BalanceState::Idle;
  message_ = "Ready. Spin the fan to a steady speed, then Start.";
  result_.ok = false;
}

const char* Balancer::stateName() const {
  switch (state_) {
    case BalanceState::Baseline:   return "baseline";
    case BalanceState::AwaitTrial: return "await";
    case BalanceState::Trial:      return "trial";
    case BalanceState::Result:     return "result";
    case BalanceState::Error:      return "error";
    default:                       return "idle";
  }
}

void Balancer::postCommand(Cmd cmd, float trialGrams, int trialBlade) {
  portENTER_CRITICAL(&mux_);
  pending_   = true;
  pendCmd_   = cmd;
  pendGrams_ = trialGrams;
  pendBlade_ = trialBlade;
  portEXIT_CRITICAL(&mux_);
}

void Balancer::startCapture() {
  for (int k = 0; k < 3; ++k) { capX_[k] = 0.0; capY_[k] = 0.0; }
  capWindows_ = 0;
  haveSeq_ = false;
  progressPct_ = 0;
}

void Balancer::update(const BalanceStats& live, float rpm) {
  lastRpm_ = rpm;

  // 1) Drain any queued command.
  Cmd cmd = Cmd::None;
  float g = 1.0f;
  int b = 0;
  portENTER_CRITICAL(&mux_);
  if (pending_) {
    cmd = pendCmd_; g = pendGrams_; b = pendBlade_;
    pending_ = false;
  }
  portEXIT_CRITICAL(&mux_);

  if (cmd == Cmd::Reset) {
    state_ = BalanceState::Idle;
    message_ = "Ready. Spin the fan to a steady speed, then Start.";
    result_.ok = false;
  } else if (cmd == Cmd::Start) {
    if (!live.ok) {
      state_ = BalanceState::Error;
      message_ = "No steady RPM — get the fan spinning at a constant speed first.";
    } else {
      baselineRpm_ = rpm;
      state_ = BalanceState::Baseline;
      message_ = "Measuring baseline vibration...";
      startCapture();
    }
  } else if (cmd == Cmd::Trial && state_ == BalanceState::AwaitTrial) {
    trialGrams_ = (g > 0.0f) ? g : 1.0f;
    trialBlade_ = (bladeCount_ > 0) ? ((b % bladeCount_) + bladeCount_) % bladeCount_ : 0;
    state_ = BalanceState::Trial;
    message_ = "Measuring with the test weight...";
    startCapture();
  }

  // 2) Run an active capture, averaging fresh analyzer windows.
  if (state_ == BalanceState::Baseline || state_ == BalanceState::Trial) {
    if (!live.ok) {
      state_ = BalanceState::Error;
      message_ = "Lost steady RPM during the measurement — keep it spinning and retry.";
      return;
    }
    if (!haveSeq_) {
      lastSeq_ = live.seq;
      haveSeq_ = true;
    } else if (live.seq != lastSeq_) {
      lastSeq_ = live.seq;
      for (int k = 0; k < 3; ++k) { capX_[k] += live.vx[k]; capY_[k] += live.vy[k]; }
      ++capWindows_;
      progressPct_ = static_cast<int>(capWindows_ * 100 / cfg::kBalanceCaptureWindows);
      if (capWindows_ >= cfg::kBalanceCaptureWindows) {
        if (state_ == BalanceState::Baseline) finalizeBaseline();
        else                                  finalizeTrial();
      }
    }
  }
}

void Balancer::finalizeBaseline() {
  int best = 0;
  double bestMag = -1.0;
  for (int k = 0; k < 3; ++k) {
    const double mx = capX_[k] / capWindows_;
    const double my = capY_[k] / capWindows_;
    const double m = mx * mx + my * my;
    if (m > bestMag) { bestMag = m; best = k; }
  }
  axis_ = best;
  v0x_ = static_cast<float>(capX_[best] / capWindows_);
  v0y_ = static_cast<float>(capY_[best] / capWindows_);

  state_ = BalanceState::AwaitTrial;
  progressPct_ = 100;
  message_ = "Stop the fan, attach a known test weight to a blade, spin back up to the "
             "same speed, set grams & blade, then Measure.";
}

void Balancer::finalizeTrial() {
  v1x_ = static_cast<float>(capX_[axis_] / capWindows_);
  v1y_ = static_cast<float>(capY_[axis_] / capWindows_);
  compute();
}

void Balancer::compute() {
  // Trial imbalance vector T = m at the chosen blade angle (magnet = 0).
  const float thetaB = trialBlade_ * (kTwoPi / bladeCount_);
  const float Tx = trialGrams_ * cosf(thetaB);
  const float Ty = trialGrams_ * sinf(thetaB);

  // dV = V1 - V0 (the measured response to T).
  const float dVx = v1x_ - v0x_;
  const float dVy = v1y_ - v0y_;
  const float den = dVx * dVx + dVy * dVy;
  if (std::sqrt(den) < cfg::kBalanceMinTrialEffect) {
    state_ = BalanceState::Error;
    message_ = "The test weight barely moved the reading — use a heavier weight and retry.";
    result_.ok = false;
    return;
  }

  // U0 = V0 / a = V0 * T / dV  (all complex).
  const float numx = v0x_ * Tx - v0y_ * Ty;   // V0 * T
  const float numy = v0x_ * Ty + v0y_ * Tx;
  const float u0x = (numx * dVx + numy * dVy) / den;  // (V0*T) / dV
  const float u0y = (numy * dVx - numx * dVy) / den;

  result_.heavyG   = std::sqrt(u0x * u0x + u0y * u0y);
  result_.heavyDeg = normDeg(std::atan2(u0y, u0x) * kRad2Deg);

  // Correction is opposite the heavy spot.
  const float ucx = -u0x, ucy = -u0y;
  const float ucMag = result_.heavyG;
  result_.corrDeg = normDeg(std::atan2(ucy, ucx) * kRad2Deg);

  const float stepDeg = 360.0f / bladeCount_;

  // Single-blade option: project the correction onto the nearest blade.
  int nS = static_cast<int>(std::lround(result_.corrDeg / stepDeg)) % bladeCount_;
  const float dS = diffDeg(result_.corrDeg, nS * stepDeg) * kDeg2Rad;
  result_.bladeS     = nS;
  result_.gramsS     = ucMag * cosf(dS);
  result_.improvePct = (1.0f - fabsf(sinf(dS))) * 100.0f;

  // Exact two-blade split across the blades straddling the correction angle
  // (trig is periodic, so wrapping the upper blade index to 0 is harmless).
  int a = static_cast<int>(std::floor(result_.corrDeg / stepDeg)) % bladeCount_;
  if (a < 0) a += bladeCount_;
  const int bld = (a + 1) % bladeCount_;
  const float ta = a * stepDeg * kDeg2Rad;
  const float tb = bld * stepDeg * kDeg2Rad;
  const float detM = cosf(ta) * sinf(tb) - cosf(tb) * sinf(ta);  // = sin(step) > 0
  float wa = (ucx * sinf(tb) - ucy * cosf(tb)) / detM;
  float wb = (ucy * cosf(ta) - ucx * sinf(ta)) / detM;
  if (wa < 0.0f) wa = 0.0f;  // guard rounding at the blade boundaries
  if (wb < 0.0f) wb = 0.0f;
  result_.bladeA = a;   result_.gramsA = wa;
  result_.bladeB = bld; result_.gramsB = wb;

  result_.ok = true;
  state_ = BalanceState::Result;

  // The influence coefficient assumes the same speed for both captures.
  if (baselineRpm_ > 1.0f &&
      fabsf(lastRpm_ - baselineRpm_) / baselineRpm_ > cfg::kBalanceMaxRpmDrift) {
    message_ = "Done — but RPM drifted between measurements; re-run at a steady speed for accuracy.";
  } else {
    message_ = "Add the weight, then run again to verify and fine-tune.";
  }
}
