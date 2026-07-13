#pragma once

#include <Arduino.h>

#include <cstdint>

#include "analysis.h"

/** Wizard state, mirrored to the web UI. */
enum class BalanceState : uint8_t { Idle, Baseline, AwaitTrial, Trial, Result, Error };

/** Correction recommendation produced from the influence-coefficient solve. */
struct BalanceResult {
  bool  ok        = false;
  float heavyG    = 0.0f;   ///< original imbalance magnitude, g at the test radius
  float heavyDeg  = 0.0f;   ///< heavy-spot angle from the magnet, [0,360)
  float corrDeg   = 0.0f;   ///< correction angle (opposite the heavy spot)
  // Exact two-blade split (achieves the full correction vector).
  int   bladeA    = 0; float gramsA = 0.0f;
  int   bladeB    = 0; float gramsB = 0.0f;
  // Simple single-blade alternative and its predicted improvement.
  int   bladeS    = 0; float gramsS = 0.0f; float improvePct = 0.0f;
};

/**
 * Balancer — single-plane influence-coefficient balancing wizard (Phase 5).
 *
 * Flow: START captures the baseline 1x vector V0; the user attaches a known
 * test weight to a known blade; a second capture gives V1. The complex
 * influence coefficient a = (V1 - V0) / T (T = the trial imbalance vector)
 * absorbs the unknown sensor mounting rotation and structural phase lag, so
 * U0 = V0 / a is the *calibrated* original imbalance in blade coordinates. The
 * correction is -U0, reported as an exact two-blade split and a simpler
 * single-blade option.
 *
 * Commands arrive from the async web task via postCommand() and are drained in
 * update() on the loop task; captures average several analyzer windows.
 */
class Balancer {
 public:
  enum class Cmd : uint8_t { None, Start, Trial, Reset };

  void begin(uint8_t bladeCount);

  /** Queue a command (safe to call from the async web task). */
  void postCommand(Cmd cmd, float trialGrams, int trialBlade);

  /** Advance the state machine from loop() using the live lock-in vector. */
  void update(const BalanceStats& live, float rpm);

  BalanceState         state() const { return state_; }
  const char*          stateName() const;
  int                  progressPct() const { return progressPct_; }
  const char*          message() const { return message_; }
  const BalanceResult& result() const { return result_; }
  float                trialGrams() const { return trialGrams_; }
  int                  trialBlade() const { return trialBlade_; }

 private:
  void startCapture();
  void finalizeBaseline();
  void finalizeTrial();
  void compute();

  uint8_t      bladeCount_  = 3;
  BalanceState state_       = BalanceState::Idle;
  int          progressPct_ = 0;
  const char*  message_     = "";

  // Capture accumulators (per axis), summed over kBalanceCaptureWindows windows.
  double   capX_[3] = {0, 0, 0};
  double   capY_[3] = {0, 0, 0};
  uint32_t capWindows_ = 0;
  uint32_t lastSeq_    = 0;
  bool     haveSeq_    = false;

  int   axis_        = 0;      ///< measurement axis chosen at baseline, reused for trial
  float v0x_ = 0, v0y_ = 0;    ///< baseline phasor on axis_
  float v1x_ = 0, v1y_ = 0;    ///< trial phasor on axis_
  float baselineRpm_ = 0.0f;
  float lastRpm_     = 0.0f;
  float trialGrams_  = 1.0f;
  int   trialBlade_  = 0;

  BalanceResult result_{};

  // Command inbox: written by the async task, drained by the loop task.
  portMUX_TYPE  mux_       = portMUX_INITIALIZER_UNLOCKED;
  volatile bool pending_   = false;
  volatile Cmd  pendCmd_   = Cmd::None;
  volatile float pendGrams_ = 1.0f;
  volatile int   pendBlade_ = 0;
};
