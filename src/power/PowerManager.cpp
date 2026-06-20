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

namespace SlimeVR {

static const char* stateNames[] = {"ACTIVE", "IDLE", "DOCKED"};

void PowerManager::setup() {
#ifdef PIN_CHARGING
	pinMode(PIN_CHARGING, INPUT_PULLUP);
	m_Logger.info("Charging detection via PIN_CHARGING=%d (active-low)", PIN_CHARGING);
#else
	m_Logger.info("No charging pin defined; DOCKED state disabled");
#endif
	m_LastConnectedMs = millis();
}

bool PowerManager::detectCharging() const {
#ifdef PIN_CHARGING
	// TP4056 CHRG pin: open-drain, pulled LOW while charging
	return digitalRead(PIN_CHARGING) == LOW;
#else
	// Voltage-based detection is unreliable on v1.2 boards: TP4056 charges to
	// 4.2V which is below any safe threshold, so we can't distinguish charging
	// from a nearly-full battery. Disable DOCKED state unless PIN_CHARGING is wired.
	return false;
#endif
}

void PowerManager::applyState(PowerState newState) {
	if (m_State == newState) {
		return;
	}

	m_Logger.info(
		"Power: %s -> %s",
		stateNames[static_cast<int>(m_State)],
		stateNames[static_cast<int>(newState)]
	);
	m_State = newState;

	// Suppress all LED activity while not actively tracking
	bool silent = (newState == PowerState::IDLE || newState == PowerState::DOCKED);
	ledManager.setForcedOff(silent);
}

void PowerManager::update() {
	m_Charging = detectCharging();

	if (networkConnection.isConnected()) {
		m_LastConnectedMs = millis();
	}

	if (m_Charging) {
		applyState(PowerState::DOCKED);
		return;
	}

	if (millis() - m_LastConnectedMs >= POWER_IDLE_TIMEOUT_MS) {
		applyState(PowerState::IDLE);
		return;
	}

	applyState(PowerState::ACTIVE);
}

}  // namespace SlimeVR
