# MisoNyah's SlimeVR Tracker Setup

Forked from [SlimeVR/SlimeVR-Tracker-ESP](https://github.com/SlimeVR/SlimeVR-Tracker-ESP).  
Build target: **BOARD_SLIMEVR_V1_2** (ESP8266 / ESP-12E, BNO085 IMU)

---

## Tracker Inventory

| # | IP | MAC | IMU | Sensors |
|---|-----|-----|-----|---------|
| 0 | 192.168.2.15 | B4:8A:0A:D9:12:D9 | BNO085 | 2 (main + extension) |
| 1 | 192.168.2.19 | AC:0B:FB:F3:C1:99 | BNO085 | 1 |
| 2 | 192.168.2.22 | 08:3A:F2:C3:EA:06 | BNO085 | 1 |
| 3 | 192.168.2.14 | 08:3A:F2:C4:F3:D6 | BNO085 | 1 |
| 4 | 192.168.2.46 | 08:3A:F2:C3:E4:3D | BNO085 | 1 |
| 5 | 192.168.2.18 | 08:3A:8D:CA:7C:AF | BNO085 | 1 |
| 6 | 192.168.2.20 | AC:0B:FB:EE:28:97 | BNO085 | 1 |
| 7 | 192.168.2.26 | AC:0B:FB:F1:E1:ED | BNO085 | 1 |

IPs may change on DHCP renewal — check the SlimeVR server log at  
`%APPDATA%\dev.slimevr.SlimeVR\logs\slimevr-server_*.log` for current IPs.

---

## What Was Changed

### 1. Power Management (`src/power/PowerManager.{h,cpp}`)

Three-state idle/charging logic so you never have to worry about LEDs at night or
dead batteries from forgotten charging.

| State | Trigger | Effect |
|-------|---------|--------|
| **ACTIVE** | Server is connected | Full tracking, LED normal |
| **IDLE** | Not charging + no server for 20 min | LED forced off, Wi-Fi alive (instant resume) |
| **DOCKED** | USB/charging detected | LED forced off, never auto-idles |

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

### 4. Serial Reset Command

Open the serial monitor at 115200 baud and type:

```
SRST
```

The tracker will reply with `Sensor soft-reset: clearing I2C bus and reinitializing IMUs`.

---

### 5. Wi-Fi HTTP Reset (`src/network/WebCommand.{h,cpp}`)

A tiny HTTP server (port 80) runs on every tracker. Trigger a sensor reset from
any browser or curl on the same network:

```
http://192.168.2.15/         ← status page (battery, firmware version)
http://192.168.2.15/reset    ← trigger sensor soft-reset
```

Or via curl:
```bash
curl http://192.168.2.15/reset
```

---

## Building & Flashing

### Build
```powershell
cd C:\Users\darkf\git\SlimeVR_improved
python -m platformio run -e BOARD_SLIMEVR_V1_2
```
Output: `.pio\build\BOARD_SLIMEVR_V1_2\firmware.bin`

### Flash all trackers via OTA
```powershell
$firmware = "C:\Users\darkf\git\SlimeVR_improved\.pio\build\BOARD_SLIMEVR_V1_2\firmware.bin"
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
- The v1.2 board pin map used by this build: SDA=4, SCL=5, INT=2, INT2=16, Battery=A0 (pin 17), LED=2 (inverted).
- Tracker 0 (192.168.2.15) has a second IMU connected via I2C (extension tracker).
- All trackers run BNO085 at the firmware defaults (no magnetometer).
