/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2024 SlimeVR Contributors

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/
#pragma once

#include <Arduino.h>

#include "../logging/Logger.h"

// How long without server contact before entering IDLE (default: 20 minutes).
// Only applies when not charging. Override in defines.h or platformio.ini.
#ifndef POWER_IDLE_TIMEOUT_MS
#define POWER_IDLE_TIMEOUT_MS (20UL * 60UL * 1000UL)
#endif

// Battery voltage above this level is treated as USB/charging present when no
// PIN_CHARGING is defined. A healthy LiPo discharges below ~4.2V; USB presence
// can push readings slightly higher on some voltage-divider designs.
#ifndef CHARGING_VOLTAGE_THRESHOLD
#define CHARGING_VOLTAGE_THRESHOLD 4.25f
#endif

namespace SlimeVR {

enum class PowerState : uint8_t {
	// Server connected (or recently was). Full tracking. LED behaves normally.
	ACTIVE,
	// Not charging and server has been absent for POWER_IDLE_TIMEOUT_MS.
	// Wi-Fi stays alive for instant resume. LED is forced off.
	IDLE,
	// USB / charging pin detected. LED is forced off. Never auto-idles.
	DOCKED,
};

class PowerManager {
public:
	void setup();
	void update();

	PowerState getState() const { return m_State; }
	bool isCharging() const { return m_Charging; }

private:
	bool detectCharging() const;
	void applyState(PowerState newState);

	PowerState m_State = PowerState::ACTIVE;
	bool m_Charging = false;

	// millis() timestamp of the last observed server connection
	unsigned long m_LastConnectedMs = 0;

	Logging::Logger m_Logger = Logging::Logger("PowerManager");
};

}  // namespace SlimeVR
