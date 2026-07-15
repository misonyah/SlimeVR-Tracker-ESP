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

#include "SensorManager.h"

#include <array>

#include "../debugging/Benchmark.h"
#include "SensorBuilder.h"
#include "Wire.h"

namespace SlimeVR::Sensors {

std::array<SlimeVR::Debugging::Benchmark, 2> sensorLoopBMs{
	SlimeVR::Debugging::Benchmark{"IMU1 Sensor loop"},
	SlimeVR::Debugging::Benchmark{"IMU2 Sensor loop"}
};
SlimeVR::Debugging::Benchmark sensorManagerNetworkingBM{"sensorManager Network"};

void SensorManager::setup() {
	if (m_MCP.begin_I2C()) {
		m_Logger.info("MCP initialized");
	}

	SensorBuilder sensorBuilder = SensorBuilder(this);
	uint8_t activeSensorCount = sensorBuilder.buildAllSensors();

	m_Logger.info("%d sensor(s) configured", activeSensorCount);
	// Check and scan i2c if no sensors active
	if (activeSensorCount == 0) {
		m_Logger.error(
			"Can't find I2C device on provided addresses, scanning for all I2C devices "
			"in the background"
		);
		I2CSCAN::scani2cports();
	}
}

void SensorManager::postSetup() {
	for (auto& sensor : m_Sensors) {
		if (sensor->isWorking()) {
			if (sensor->m_hwInterface != nullptr) {
				sensor->m_hwInterface->swapIn();
			}
			sensor->postSetup();
		}
	}
}

void SensorManager::update() {
	// Gather IMU data
	bool allIMUGood = true;
	size_t sensorId = 0;
	for (auto& sensor : m_Sensors) {
		if (sensorId < sensorLoopBMs.size()) {
			sensorLoopBMs[sensorId].before();
		}

		if (sensor->isWorking()) {
			if (sensor->m_hwInterface != nullptr) {
				sensor->m_hwInterface->swapIn();
			}
			sensor->motionLoop();
		}
		if (sensor->getSensorState() == SensorStatus::SENSOR_ERROR) {
			allIMUGood = false;
		}

		if (sensorId < sensorLoopBMs.size()) {
			sensorLoopBMs[sensorId].after();
		}
		sensorId++;
	}

	statusManager.setStatus(SlimeVR::Status::IMU_ERROR, !allIMUGood);

	// Autonomous self-heal: if a sensor (e.g. an I2C extension IMU) stays in
	// SENSOR_ERROR for SENSOR_AUTO_RESET_MS straight, reset the I2C bus and
	// reinitialize without waiting for a human to send the SRST serial command.
	// This is a no-op on boards where every sensor stays healthy, since
	// allIMUGood only ever goes false when a sensor actually errors out.
	if (allIMUGood) {
		m_IMUErrorSinceMs = 0;

		if (m_AutoResetPending) {
			// Sensors recovered after our reset attempt.
			networkConnection
				.sendFirmwareSelfHealNotification("auto_reset", true, m_AutoResetDetail);
			m_AutoResetPending = false;
			m_AutoResetCheckCycles = 0;
		}
	} else {
		if (m_IMUErrorSinceMs == 0) {
			m_IMUErrorSinceMs = millis();
		}

		if (m_AutoResetPending) {
			// Give the just-reset sensors a couple of update() cycles to report
			// good status before declaring the reset attempt a failure.
			m_AutoResetCheckCycles++;
			if (m_AutoResetCheckCycles >= 2) {
				networkConnection.sendFirmwareSelfHealNotification(
					"auto_reset",
					false,
					m_AutoResetDetail
				);
				m_AutoResetPending = false;
				m_AutoResetCheckCycles = 0;
			}
		} else {
			uint32_t errorDurationMs = millis() - m_IMUErrorSinceMs;
			bool cooldownElapsed = m_LastAutoResetMs == 0
									 || millis() - m_LastAutoResetMs >= SENSOR_AUTO_RESET_MS;

			if (errorDurationMs >= SENSOR_AUTO_RESET_MS && cooldownElapsed) {
				snprintf(
					m_AutoResetDetail,
					sizeof(m_AutoResetDetail),
					"extension IMU unresponsive for %lums",
					static_cast<unsigned long>(errorDurationMs)
				);
				m_Logger.warn(
					"IMU error persisted for %lums, attempting autonomous sensor reset",
					static_cast<unsigned long>(errorDurationMs)
				);

				m_LastAutoResetMs = millis();
				m_AutoResetPending = true;
				m_AutoResetCheckCycles = 0;

				resetSensors();
			}
		}
	}

	if (!networkConnection.isConnected()) {
		return;
	}

#ifndef PACKET_BUNDLING
	static_assert(false, "PACKET_BUNDLING not set");
#endif
#if PACKET_BUNDLING == PACKET_BUNDLING_BUFFERED
	uint32_t now = micros();
	bool shouldSend = false;
	bool allSensorsReady = true;
	for (auto& sensor : m_Sensors) {
		if (!sensor->isWorking()) {
			continue;
		}
		if (sensor->hasNewDataToSend()) {
			shouldSend = true;
		}
		allSensorsReady &= sensor->hasNewDataToSend();
	}

	if (now - m_LastBundleSentAtMicros < PACKET_BUNDLING_BUFFER_SIZE_MICROS) {
		shouldSend &= allSensorsReady;
	}

	if (!shouldSend) {
		return;
	}

	m_LastBundleSentAtMicros = now;
#endif

	sensorManagerNetworkingBM.before();
#if PACKET_BUNDLING != PACKET_BUNDLING_DISABLED
	networkConnection.beginBundle();
#endif

	for (auto& sensor : m_Sensors) {
		if (sensor->isWorking()) {
			sensor->sendData();
		}
	}

#if PACKET_BUNDLING != PACKET_BUNDLING_DISABLED
	networkConnection.endBundle();
#endif
	sensorManagerNetworkingBM.after();
}

uint32_t SensorManager::getLastMotionMs() const {
	uint32_t latest = 0;
	for (const auto& sensor : m_Sensors) {
		if (sensor->isWorking()) {
			uint32_t t = sensor->getLastMotionMs();
			if (t > latest) latest = t;
		}
	}
	return latest;
}

void SensorManager::setSleepMode(bool sleeping) {
	for (auto& sensor : m_Sensors) {
		if (sleeping) {
			sensor->enterSleepMode();
		} else {
			sensor->exitSleepMode();
		}
	}
}

void SensorManager::resetSensors() {
	m_Logger.info("Sensor soft-reset: clearing I2C bus and reinitializing IMUs");

	statusManager.setStatus(SlimeVR::Status::IMU_ERROR, false);

	m_Sensors.clear();

#ifdef ESP32
	Wire.end();
#endif
	auto clearResult = I2CSCAN::clearBus(PIN_IMU_SDA, PIN_IMU_SCL);
	if (clearResult != 0) {
		m_Logger.warn("I2C bus clear returned %d", clearResult);
	}
	delay(100);

	Wire.begin(static_cast<int>(PIN_IMU_SDA), static_cast<int>(PIN_IMU_SCL));
#ifdef ESP8266
	Wire.setClockStretchLimit(150000L);
#endif
#ifdef ESP32
	Wire.setTimeOut(150);
#endif
	Wire.setClock(I2C_SPEED);
	delay(100);

	setup();
	postSetup();

	m_Logger.info("Sensor soft-reset complete");
}

}  // namespace SlimeVR::Sensors
