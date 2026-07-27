# MisoNyah's SlimeVR Tracker Setup

Forked from [SlimeVR/SlimeVR-Tracker-ESP](https://github.com/SlimeVR/SlimeVR-Tracker-ESP).  
Build target: **BOARD_SLIMEVR** (ESP8266 / ESP-12E, BNO085 IMU — v1.0/v1.1 hardware)

---

## Tracker Inventory

| #   | IP           | MAC               | IMU    | Sensors              | Assigned to        |
| --- | ------------ | ----------------- | ------ | -------------------- | ------------------ |
| 0   | 192.168.2.15 | B4:8A:0A:D9:12:D9 | BNO085 | 2 (main + extension) | Left ankle + foot  |
| 1   | 192.168.2.19 | AC:0B:FB:F3:C1:99 | BNO085 | 1                    |
| 2   | 192.168.2.22 | 08:3A:F2:C3:EA:06 | BNO085 | 1                    |
| 3   | 192.168.2.14 | 08:3A:F2:C4:F3:D6 | BNO085 | 1                    |
| 4   | 192.168.2.46 | 08:3A:F2:C3:E4:3D | BNO085 | 1                    |
| 5   | 192.168.2.18 | 08:3A:8D:CA:7C:AF | BNO085 | 2 (main + extension) | Right ankle + foot |
| 6   | 192.168.2.20 | AC:0B:FB:EE:28:97 | BNO085 | 1                    |
| 7   | 192.168.2.26 | AC:0B:FB:F1:E1:ED | BNO085 | 1                    |

IPs may change on DHCP renewal — check the SlimeVR server log at  
`%APPDATA%\dev.slimevr.SlimeVR\logs\slimevr-server_*.log` for current IPs.

---

## What Was Changed

### 1. Power Management (`src/power/PowerManager.{h,cpp}`)

Motion-based sleep (this replaced the older charging-detection / ACTIVE-IDLE-DOCKED
model). Two states, driven by the BNO085's own motion/stability classifier:

| State        | Trigger                                             | Effect                                                                                |
| ------------ | -------------------------------------------------- | ------------------------------------------------------------------------------------- |
| **AWAKE**    | Motion detected                                     | Full tracking, Wi-Fi on, LED heartbeat                                                 |
| **SLEEPING** | No motion for `POWER_IDLE_TIMEOUT_MS` (20 min) **and** server disconnected | **Wi-Fi modem sleep — `WiFi.forceSleepBegin()`, radio OFF**; BNO085 → 2 Hz; LED off    |

Wake happens when the BNO085 detects motion after sleep started → `exitSleep()` →
`WiFi.forceSleepWake()`. A `POWER_MIN_SLEEP_MS` (30 s) floor prevents rapid
wake/sleep cycling; on waking the tracker has `POWER_SEARCH_TIMEOUT_MS` (60 s) to
find the server or it sleeps again.

**Behavior:**

- **Sleep is gated on server presence** (`networkConnection.isConnected()`): while the
  SlimeVR server is connected the tracker stays awake regardless of motion, so a
  stationary tracker **never drops off Wi-Fi mid-session.** It only sleeps once the
  server is actually gone (PC off, SlimeVR closed, or Wi-Fi lost — `m_Connected` flips
  false 3 s after the last server packet) *and* there's been no motion for 20 min.
- **Battery tradeoff:** trackers left powered on but not worn *while SlimeVR keeps
  running* stay fully awake and drain (a few hours). Power them off between sessions,
  or switch to a light-idle that keeps Wi-Fi up (skip only `forceSleepBegin()`) if you
  want idle battery savings while connected.
- **"Instant resume" is not instant** — waking from a real (server-gone) sleep does a
  Wi-Fi re-associate + SlimeVR re-handshake, a few seconds.

**Tuning knobs** (defined in `src/power/PowerManager.h`, override in `src/defines.h`):

```cpp
#define POWER_IDLE_TIMEOUT_MS   (20UL * 60UL * 1000UL)  // no-motion time until SLEEPING
#define POWER_SEARCH_TIMEOUT_MS (60UL * 1000UL)         // post-wake window to find server
#define POWER_MIN_SLEEP_MS      (30UL * 1000UL)         // min sleep before motion can wake
```

---

### 2. LED Silent Mode (`src/status/LEDManager.{h,cpp}`)

`LEDManager::setForcedOff(bool)` — PowerManager calls this during IDLE and DOCKED states.
All LED patterns are suppressed while forced off. The LED turns back on automatically
when the state returns to ACTIVE (server reconnects and not charging).

---

### 3. Sensor Soft-Reset (`src/sensors/SensorManager.{h,cpp}`)

`SensorManager::resetSensors()` — re-initializes the I2C bus and all IMUs without
a full ESP reboot. Use this when an extension tracker stops responding.

**What it does:**

1. Clears the in-memory sensor list
2. Runs the I2C bus-clear routine (`clearBus`)
3. Re-calls `Wire.begin()` with original clock settings
4. Re-runs `SensorManager::setup()` + `postSetup()`

The server is notified automatically on the next heartbeat cycle.

---

### 4. Autonomous Extension Recovery (`src/sensors/SensorManager.{h,cpp}`, `src/network/connection.{h,cpp}`)

`SensorManager::update()` now tracks how long `allIMUGood` has been continuously
false. If an I2C extension sensor (or any sensor) stays in `SENSOR_ERROR` for
`SENSOR_AUTO_RESET_MS` (default **5000 ms**, tunable in `src/defines.h`) straight,
the tracker calls `resetSensors()` on its own — the same soft-reset the `SRST`
serial command triggers — with no human needed to power-cycle it or send a command.

**Behavior:**

1. Timer starts the moment any sensor reports `SENSOR_ERROR`; it resets to zero as
   soon as all sensors report good again.
2. Once the timer crosses `SENSOR_AUTO_RESET_MS`, `resetSensors()` runs
   automatically (I2C bus clear + `Wire.begin()` + `setup()`/`postSetup()`, same as
   section 3 above).
3. **Cooldown:** a repeat auto-reset attempt won't fire again for another
   `SENSOR_AUTO_RESET_MS` after the previous attempt, so a permanently broken
   extension cable can't spam resets in a tight loop.
4. **No-op on single-sensor boards:** `allIMUGood` only ever goes false when a
   sensor actually errors, so this firmware is safe to flash to all 8 trackers —
   boards without an extension sensor simply never trigger it.

**Out-of-band UDP notification:** after every auto-reset attempt (success or
failure, checked 1-2 `update()` cycles later), the tracker sends a fire-and-forget
plain-JSON UDP packet — no ack expected — to port `FIRMWARE_NOTIFY_PORT` (default
**6970**, also in `src/defines.h`) at the same server host it already talks to on
port 6969:

```json
{"mac":"B4:8A:0A:D9:12:D9","event":"auto_reset","success":true,"detail":"extension IMU unresponsive for 5000ms"}
```

This is handled by `Connection::sendFirmwareSelfHealNotification()`
(`src/network/connection.h`/`.cpp`) — a small standalone send via the connection's
existing `WiFiUDP`, independent of the binary packet framing used for the primary
SlimeVR server protocol. It's picked up by the `FirmwareNotificationListener` module
in the companion `VrSessionMonitor` app (separate repo) for desktop-side logging —
there is no reply and the tracker doesn't wait for one.

**✅ Verified working (2026-07-27).** OTA-flashed to tracker 0 (192.168.2.15); with
the extension physically unplugged for ~2 s, the SlimeVR server showed sensor 1 go
to `SENSOR_ERROR` (status 2), `resetSensors()` fired 5 s later, and **both sensors
recovered with no power-cycle** — followed by
`{"event":"auto_reset","success":true,"detail":"extension IMU unresponsive for 5000ms"}`
on 6970. Note: a physical **reconnect alone does not** recover the extension —
once a sensor latches `working = false`, `SensorManager::update()` skips its
`motionLoop()` (it only runs `motionLoop()` while `isWorking()`), so `resetSensors()`
or a reboot is required to re-detect it. The autonomous auto-reset provides exactly
that; before this firmware, the fix was a manual power-cycle.

---

### 5. Serial Reset Command

Open the serial monitor at 115200 baud and type:

```
SRST
```

The tracker will reply with `Sensor soft-reset: clearing I2C bus and reinitializing IMUs`.

> **⚠️ Serial is unavailable on these boards over a plain USB hookup.** In testing
> (2026-07-27) the CH340 enumerated as a COM port but the ESP produced **no output at
> any baud** and esptool could not enter the bootloader without bridging the flash
> pads (GPIO0→GND) — TX and the DTR/RTS auto-reset lines aren't wired through. So
> `SRST` and the serial monitor don't work here. **Use OTA to flash** (section below)
> and the UDP 6970 notification / SlimeVR server log to observe behavior instead.

---

## Building & Flashing

### Build

```powershell
cd C:\Users\darkf\git\SlimeVR_improved
python -m platformio run -e BOARD_SLIMEVR
```

Output: `.pio\build\BOARD_SLIMEVR\firmware.bin`

### Flash all trackers via OTA

```powershell
$firmware = "C:\Users\darkf\git\SlimeVR_improved\.pio\build\BOARD_SLIMEVR\firmware.bin"
$espota   = "C:\Users\darkf\.platformio\packages\framework-arduinoespressif8266\tools\espota.py"
$ips = @("192.168.2.15","192.168.2.19","192.168.2.22","192.168.2.14","192.168.2.46","192.168.2.18","192.168.2.20","192.168.2.26")

foreach ($ip in $ips) {
    Write-Host "=== $ip ===" -ForegroundColor Cyan
    python $espota -i $ip -p 8266 -a SlimeVR-OTA -f $firmware
}
```

OTA password: `SlimeVR-OTA` (default, set in `src/credentials.h`)

### Flash a single tracker

```powershell
python $espota -i 192.168.2.15 -p 8266 -a SlimeVR-OTA -f $firmware
```

---

## Notes

- IPs are assigned by DHCP — they can change after router restart.
- The v1.0 board pin map used by this build: SDA=14, SCL=12, INT=16, INT2=13, Battery=A0 (pin 17), LED=2 (inverted).
- Trackers 0 (192.168.2.15, left ankle+foot) and 5 (192.168.2.18, right ankle+foot) have a second IMU via I2C (extension) — the auto-heal in section 4 targets these; the single-sensor boards never trigger it.
- All trackers run BNO085 at the firmware defaults (no magnetometer).
- **Flashing is OTA-only in practice** — USB serial/bootloader needs the flash pads bridged (see section 5), so `python -m platformio run -t upload` over USB won't work without that. The tracker must be **awake** (move it) and on Wi-Fi for OTA to reach it.
