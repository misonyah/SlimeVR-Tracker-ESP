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

#include "PowerManager.h"

#include "../GlobalVars.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

namespace SlimeVR {

void PowerManager::setup() {
	m_Logger.info(
		"Power management: idle=%lus search=%lus",
		(unsigned long)(POWER_IDLE_TIMEOUT_MS / 1000),
		(unsigned long)(POWER_SEARCH_TIMEOUT_MS / 1000)
	);
	// Search deadline starts now so a cold boot with no server sleeps after 1 min.
	m_SearchDeadlineMs = millis() + POWER_SEARCH_TIMEOUT_MS;
}

void PowerManager::update() {
	uint32_t now = millis();

	if (m_State == PowerState::SLEEPING) {
		// Enforce minimum sleep time to avoid rapid wake/sleep cycling.
		if (now - m_SleepStartMs < POWER_MIN_SLEEP_MS) {
			return;
		}
		// Wake on any motion detected after we fell asleep.
		if (sensorManager.getLastMotionMs() > m_SleepStartMs) {
			exitSleep();
		}
		return;
	}

	// ── AWAKE ──────────────────────────────────────────────────────────────

	if (networkConnection.isConnected()) {
		m_SearchDeadlineMs = 0;  // server found; cancel search timeout
	}

	uint32_t lastMotion = sensorManager.getLastMotionMs();
	bool totallyIdle = (now - lastMotion >= POWER_IDLE_TIMEOUT_MS);

	if (totallyIdle) {
		m_Logger.info("Power: idle for %lu s, sleeping", (unsigned long)(POWER_IDLE_TIMEOUT_MS / 1000));
		enterSleep();
		return;
	}

	// If we woke from sleep and haven't found the server yet, give up after the deadline.
	if (m_SearchDeadlineMs != 0 && now >= m_SearchDeadlineMs
		&& !networkConnection.isConnected()) {
		m_Logger.info("Power: no server in %lu s, sleeping", (unsigned long)(POWER_SEARCH_TIMEOUT_MS / 1000));
		enterSleep();
	}
}

void PowerManager::enterSleep() {
	if (m_State == PowerState::SLEEPING) return;
	m_Logger.info("Power: AWAKE -> SLEEPING");
	m_State = PowerState::SLEEPING;
	m_SleepStartMs = millis();
	ledManager.setForcedOff(true);
	sensorManager.setSleepMode(true);
#ifdef ESP8266
	WiFi.forceSleepBegin();
#endif
}

void PowerManager::exitSleep() {
	if (m_State == PowerState::AWAKE) return;
	m_Logger.info("Power: SLEEPING -> AWAKE (motion detected)");
	m_State = PowerState::AWAKE;
#ifdef ESP8266
	WiFi.forceSleepWake();
	delay(1);
#endif
	sensorManager.setSleepMode(false);
	ledManager.setForcedOff(false);
	// One-minute window to find the server before sleeping again.
	m_SearchDeadlineMs = millis() + POWER_SEARCH_TIMEOUT_MS;
}

}  // namespace SlimeVR
