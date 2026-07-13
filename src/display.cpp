#include "display.h"

#include <cstdio>

#include "config.h"

namespace {
constexpr int kHeaderH = 20;

uint16_t statusColor(HallStatus s) {
  switch (s) {
    case HallStatus::Ok:       return TFT_GREEN;
    case HallStatus::Unstable: return TFT_YELLOW;
    default:                   return TFT_RED;
  }
}
}  // namespace

void StatusDisplay::begin() {
  M5.Display.setRotation(1);  // landscape, 240x135
  M5.Display.setBrightness(cfg::kDisplayBrightness);
  canvas_.setColorDepth(16);
  canvas_.createSprite(M5.Display.width(), M5.Display.height());
}

void StatusDisplay::render(const DisplayModel& m) {
  canvas_.fillSprite(TFT_BLACK);
  drawHeader(m);
  if (m.diagnostics) {
    drawDiagnostics(m);
  } else {
    drawMain(m);
  }
  canvas_.pushSprite(0, 0);
}

void StatusDisplay::drawHeader(const DisplayModel& m) {
  canvas_.fillRect(0, 0, canvas_.width(), kHeaderH, TFT_NAVY);
  canvas_.setTextFont(2);
  canvas_.setTextColor(TFT_WHITE, TFT_NAVY);

  canvas_.setTextDatum(middle_left);
  canvas_.drawString(m.diagnostics ? "Diagnostics" : "FanBalancer", 4, kHeaderH / 2);

  canvas_.setTextDatum(middle_right);
  char right[24];
  if (m.batteryPct >= 0) {
    snprintf(right, sizeof(right), "%u dev | %d%%", m.wifiStations, m.batteryPct);
  } else {
    snprintf(right, sizeof(right), "%u dev", m.wifiStations);
  }
  canvas_.drawString(right, canvas_.width() - 4, kHeaderH / 2);
}

void StatusDisplay::drawMain(const DisplayModel& m) {
  const bool hasSignal = m.hall.status != HallStatus::NoSignal;

  // Big RPM readout (7-segment font handles digits only).
  canvas_.setTextDatum(middle_center);
  canvas_.setTextColor(TFT_WHITE, TFT_BLACK);
  if (hasSignal) {
    canvas_.setTextFont(7);
    char rpm[8];
    snprintf(rpm, sizeof(rpm), "%d", static_cast<int>(m.hall.rpm + 0.5f));
    canvas_.drawString(rpm, canvas_.width() / 2, 44);
  } else {
    canvas_.setTextFont(4);
    canvas_.drawString("--- RPM", canvas_.width() / 2, 44);
  }

  // Status line.
  canvas_.setTextFont(4);
  canvas_.setTextColor(statusColor(m.hall.status), TFT_BLACK);
  canvas_.drawString(hallStatusName(m.hall.status), canvas_.width() / 2, 82);

  // Vibration level (RMS, gravity removed).
  canvas_.setTextFont(2);
  canvas_.setTextColor(TFT_CYAN, TFT_BLACK);
  char vib[24];
  if (m.vib.ok) {
    snprintf(vib, sizeof(vib), "Vib %.3f g", m.vib.rms);
  } else {
    snprintf(vib, sizeof(vib), "Vib --");
  }
  canvas_.drawString(vib, canvas_.width() / 2, 106);

  // Connection hint.
  canvas_.setTextColor(TFT_SILVER, TFT_BLACK);
  String hint = String("http://") + m.ip;
  canvas_.drawString(hint, canvas_.width() / 2, 124);
}

void StatusDisplay::drawDiagnostics(const DisplayModel& m) {
  canvas_.setTextFont(2);
  canvas_.setTextDatum(top_left);
  canvas_.setTextColor(TFT_WHITE, TFT_BLACK);

  char line[48];
  int y = kHeaderH + 4;
  const int dy = 16;

  snprintf(line, sizeof(line), "Pulses:   %lu", static_cast<unsigned long>(m.hall.pulseCount));
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Glitches: %lu", static_cast<unsigned long>(m.hall.glitchCount));
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Missed:   %lu", static_cast<unsigned long>(m.hall.missedCount));
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Period:   %.1f ms", m.hall.periodMs);
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "IMU:      %.0f Hz", m.vib.rateHz);
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Vib rms:  %.3f g  pk %.3f", m.vib.rms, m.vib.peak);
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Acc: %+.2f %+.2f %+.2f", m.vib.ax, m.vib.ay, m.vib.az);
  canvas_.drawString(line, 6, y); y += dy;
  snprintf(line, sizeof(line), "Heap: %lukB  Up %lu:%02lu",
           static_cast<unsigned long>(m.freeHeap / 1024),
           static_cast<unsigned long>(m.uptimeS / 60),
           static_cast<unsigned long>(m.uptimeS % 60));
  canvas_.drawString(line, 6, y);
}
