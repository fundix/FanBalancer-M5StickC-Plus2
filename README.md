# FanBalancer

Open-source fan balancing tool for **M5StickC Plus2** (ESP32).

The device measures vibration with the onboard MPU6886 accelerometer and
tracks the angular position of the rotor with a Hall-effect sensor (one
neodymium magnet on a blade = one pulse per revolution). The end goal is to
tell the user **which blade to weight, with how much, and where** — using the
standard single-plane influence-coefficient balancing method with a 1 g test
weight.

The primary user interface is a **web app served by the device itself**: the
StickC brings up its own WiFi access point, so it works on a ladder next to a
ceiling fan with no home network involved. The built-in 1.14" LCD only shows
status and connection info.

## Status — phase roadmap

| Phase | Content | State |
|------:|---------|:-----:|
| 1 | Hall sensor: RPM, rotation period, missed-pulse detection, web UI + status display | ✅ this build |
| 2 | IMU: MPU6886 at 500 Hz, RMS / peak / average vibration | ⬜ |
| 3 | Synchronization: per-sample angle interpolation, CSV logging | ⬜ |
| 4 | Signal processing: lock-in detection at 1× rotation frequency, basic FFT | ⬜ |
| 5 | Balancing wizard: baseline → 1 g test weight → correction vector per blade | ⬜ |

## Hardware

- **M5StickC Plus2** (ESP32-PICO-V3-02, 8 MB flash, 2 MB PSRAM, MPU6886, 1.14" LCD)
- **Hall sensor module** A3144 + LM393 comparator (the common 4-pin breakout: VCC / GND / DO / AO)
- **Small neodymium magnet** glued or taped to one blade (or the rotating hub)

### Wiring (Phase 1)

M5StickC Plus2 8-pin header:

```
Hall module          M5StickC Plus2 header
-----------          ---------------------
VCC ---------------- 3V3          (try this first — see note)
GND ---------------- GND
DO  ---------------- G26          (hardware interrupt input)
AO  ---------------- not connected
```

**Power note.** The A3144 is officially rated 4.5–24 V, but with a strong
neodymium magnet at a 5–10 mm gap the common breakout modules work fine from
3.3 V — try that first, it needs no extra parts and works on battery. If the
detection is unreliable, power VCC from the **5V** pin instead and add a
voltage divider on DO, because ESP32 inputs are **not 5 V tolerant**:

```
DO ──[ 10 kΩ ]──┬── G26
                └──[ 20 kΩ ]── GND      (5 V → 3.3 V)
```

**Magnet orientation matters.** The A3144 switches on one pole only (south
pole facing the marked face of the sensor). If you get no pulses, flip the
magnet. The onboard red LED of the StickC flashes on every accepted pulse —
spin the blade by hand to verify the wiring before mounting anything.

**If the RPM reads about double the real speed**, the LM393 comparator is
double-triggering on the magnet's leading and trailing field. The firmware's
adaptive glitch filter rejects a second edge within ¼ revolution once it has a
stable reading, but during acquisition it relies on the static floor
(`kHallGlitchMinUs` in `src/config.h`, default 20 ms ≈ 3000 RPM ceiling). Raise
it if a slow fan still double-counts; lower it only for high-RPM rotors.

**Sensor mounting.** Fix the Hall module to the stationary part (motor
housing) so the magnet passes it at a 5–10 mm gap once per revolution.

## Build & flash

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
No Arduino IDE.

```sh
cd FanBalancer
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial log, 115200 baud
```

The stock espressif32 platform has no dedicated M5StickC Plus2 board id, so
`platformio.ini` uses the `m5stick-c` definition with the Plus2's 8 MB flash
and partition table overridden. M5Unified detects the exact board at runtime.

## Usage

1. Power on. The display shows the AP name and the UI address.
2. Connect your phone/laptop to WiFi **FanBalancer** (password `balance123`,
   change it in `src/config.h`).
3. The page usually opens by itself (captive-portal redirect). If not, open
   **http://192.168.4.1** or **http://fanbalancer.local** — a wildcard DNS on
   the device makes both work even on phones that ignore mDNS.
4. The page shows live RPM, status, an RPM history chart and counters
   (pulses, missed pulses, glitches, heap, battery…), updated 4× per second
   over a WebSocket.

On the device: the **front button (A)** toggles between the status screen and
a diagnostics page. The red LED flashes once per revolution. A 1 Hz heartbeat
with all counters is printed to USB serial.

Status meanings: **OK** — steady pulse train; **UNSTABLE** — pulse periods
vary by more than ~20 % (spin-up, loose magnet, bad gap); **NO SIGNAL** — no
pulse for 3 s.

## Architecture

```
src/
├── main.cpp        wiring of modules, main loop (non-blocking, no delay())
├── config.h        all pins, tunables and texts in one place
├── hall.cpp/.h     HallSensor — ISR timestamping, ring buffer, median RPM,
│                   glitch filter, missed-pulse detection
├── display.cpp/.h  StatusDisplay — canvas-based status/diagnostics screens
├── webserver.cpp/.h WebService — WiFi AP, async HTTP + WebSocket telemetry
└── web_content.h   embedded single-page UI (no CDN, works fully offline)
```

Planned modules per the original spec: `imu` (Phase 2), `analysis`
(Phases 3–4), `balancer` (Phase 5), `storage` (settings/calibration in NVS).

## Deviations from the original specification

The project started from `FanBalancer_AI_Agent_Specification.md` (written for
M5Stack Core2). Deliberate changes:

- **Target hardware is M5StickC Plus2** instead of Core2 — lighter (less mass
  loading on the fan), has the exact MPU6886 the spec names, and exposes
  GPIO26 directly on its header.
- **Primary UI is the browser app** served from the device; the on-device
  screen is reduced to a status display. Touch-based screens from the spec
  map to web pages instead.
- **CSV export** will be offered as an HTTP download in addition to USB
  serial.
