/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2022 TheDevMinerTV

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#pragma once

#include <i2cscan.h>

#include <memory>
#include <optional>

#include "EmptySensor.h"
#include "ErroneousSensor.h"
#include "globals.h"
#include "logging/Logger.h"
#include "sensorinterface/DirectPinInterface.h"
#include "sensorinterface/I2CPCAInterface.h"
#include "sensorinterface/I2CWireSensorInterface.h"
#include "sensorinterface/MCP23X17PinInterface.h"
#include "sensorinterface/RegisterInterface.h"
#include "sensorinterface/i2cimpl.h"

namespace SlimeVR::Sensors {

class SensorManager {
public:
	SensorManager()
		: m_Logger(SlimeVR::Logging::Logger("SensorManager")) {}
	void setup();
	void postSetup();

	void update();

	// Re-initialize the I2C bus and all sensors. Call when extension tracker
	// communication breaks or IMU_ERROR persists.
	void resetSensors();

	// Returns the most recent millis() timestamp at which any sensor detected movement.
	uint32_t getLastMotionMs() const;

	// Enter/exit low-power mode on all sensors (reduces BNO085 report rates).
	void setSleepMode(bool sleeping);

	std::vector<std::unique_ptr<::Sensor>>& getSensors() { return m_Sensors; };
	SensorTypeID getSensorType(size_t id) {
		if (id < m_Sensors.size()) {
			return m_Sensors[id]->getSensorType();
		}
		return SensorTypeID::Unknown;
	}

private:
	SlimeVR::Logging::Logger m_Logger;

	std::vector<std::unique_ptr<::Sensor>> m_Sensors;
	Adafruit_MCP23X17 m_MCP;

	uint32_t m_LastBundleSentAtMicros = micros();

	// Autonomous self-heal state (see update() / resetSensors()).
	// millis() timestamp when allIMUGood first went false; 0 while healthy.
	uint32_t m_IMUErrorSinceMs = 0;
	// millis() timestamp of the last automatic resetSensors() call; 0 if none yet.
	uint32_t m_LastAutoResetMs = 0;
	// True while waiting to observe whether a just-attempted auto-reset worked.
	bool m_AutoResetPending = false;
	// Number of update() cycles observed since the pending auto-reset attempt.
	uint8_t m_AutoResetCheckCycles = 0;
	// Reason string captured at trigger time, reused for the outcome notification.
	char m_AutoResetDetail[64] = {};

	friend class SensorBuilder;
};
}  // namespace SlimeVR::Sensors
