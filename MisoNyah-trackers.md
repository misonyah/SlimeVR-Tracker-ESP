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

Three-state idle/charging logic so you never have to worry about LEDs at night or
dead batteries from forgotten charging.

| State      | Trigger                             | Effect                                       |
| ---------- | ----------------------------------- | -------------------------------------------- |
| **ACTIVE** | Server is connected                 | Full tracking, LED normal                    |
| **IDLE**   | Not charging + no server for 20 min | LED forced off, Wi-Fi alive (instant resume) |
| **DOCKED** | USB/charging detected               | LED forced off, never auto-idles             |

**Charging detection** (pick one — no hardware change needed for the default):

- **Default (voltage):** voltage ≥ 4.25 V is treated as USB present. Works without any extra wiring because the ADC reads slightly higher when USB is connected on most SlimeVR v1.2 board designs.
- **Better (TP4056 CHRG pin):** add `#define PIN_CHARGING <gpio>` in `src/defines.h`. The TP4056 CHRG pin is open-drain active-low while charging. Wire it to an unused GPIO with a pull-up.

**Tuning knobs** (add to `src/defines.h` to override defaults):

```cpp
#define POWER_IDLE_TIMEOUT_MS   (20UL * 60UL * 1000UL)  // 20 min until IDLE
#define CHARGING_VOLTAGE_THRESHOLD  4.25f                // V; USB-present threshold
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

---

### 5. Serial Reset Command

Open the serial monitor at 115200 baud and type:

```
SRST
```

The tracker will reply with `Sensor soft-reset: clearing I2C bus and reinitializing IMUs`.

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
- Tracker 0 (192.168.2.15) has a second IMU connected via I2C (extension tracker).
- All trackers run BNO085 at the firmware defaults (no magnetometer).
