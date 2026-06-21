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

// No motion for this long → enter sleep (Wi-Fi off, BNO085 low-power).
#ifndef POWER_IDLE_TIMEOUT_MS
#define POWER_IDLE_TIMEOUT_MS (20UL * 60UL * 1000UL)
#endif

// After waking from sleep: if no SlimeVR server found within this window, sleep again.
#ifndef POWER_SEARCH_TIMEOUT_MS
#define POWER_SEARCH_TIMEOUT_MS (60UL * 1000UL)
#endif

// Minimum time to stay asleep before motion can trigger another wake.
// Prevents rapid wake/sleep cycling when the tracker is moved while the server is down.
#ifndef POWER_MIN_SLEEP_MS
#define POWER_MIN_SLEEP_MS (30UL * 1000UL)
#endif

namespace SlimeVR {

enum class PowerState : uint8_t {
	// Wi-Fi on, BNO085 at full rate, LED heartbeat active.
	AWAKE,
	// Wi-Fi off (modem sleep), BNO085 at ~2 Hz, LED off. Wakes on motion.
	SLEEPING,
};

class PowerManager {
public:
	void setup();
	void update();

	PowerState getState() const { return m_State; }
	bool isSleeping() const { return m_State == PowerState::SLEEPING; }

private:
	void enterSleep();
	void exitSleep();

	PowerState m_State = PowerState::AWAKE;

	// millis() when we entered SLEEPING; used to enforce POWER_MIN_SLEEP_MS.
	uint32_t m_SleepStartMs = 0;

	// Deadline for finding the SlimeVR server after a wake from sleep.
	// 0 = no deadline active (server was connected at least once since last wake).
	uint32_t m_SearchDeadlineMs = 0;

	Logging::Logger m_Logger = Logging::Logger("PowerManager");
};

}  // namespace SlimeVR
