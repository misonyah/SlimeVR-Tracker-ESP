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

#include "WebCommand.h"

#if defined(ESP8266) || defined(ESP32)

#include <Arduino.h>

#include "../GlobalVars.h"

namespace SlimeVR::Network {

void WebCommand::setup() {
	m_Server.on("/", [this]() { handleRoot(); });
	m_Server.on("/reset", [this]() { handleReset(); });
	m_Server.onNotFound([this]() { handleNotFound(); });
	// m_Server.begin() is deferred to update() so it starts only after WiFi connects.
	// Starting ESP8266WebServer before WiFi is up corrupts the UDP socket used by the
	// SlimeVR tracker protocol, causing motion data to never be sent.
}

void WebCommand::update() {
	if (!m_Enabled) {
#ifdef ESP8266
		if (WiFi.status() == WL_CONNECTED) {
#elif defined(ESP32)
		if (WiFi.status() == WL_CONNECTED) {
#endif
			m_Server.begin();
			m_Enabled = true;
			m_Logger.info("HTTP command server started on port 80");
		}
		return;
	}
	m_Server.handleClient();
}

void WebCommand::handleRoot() {
	float volt = battery.getVoltage();
	float lvl = battery.getLevel();

	char buf[256];
	snprintf(
		buf,
		sizeof(buf),
		"<html><body>"
		"<h2>SlimeVR Tracker</h2>"
		"<p>Firmware: " FIRMWARE_VERSION "</p>"
		"<p>Battery: %.2fV (%.0f%%)</p>"
		"<p>Status: %lu</p>"
		"<p><a href='/reset'>Soft-reset sensors</a></p>"
		"</body></html>",
		volt,
		lvl * 100.0f,
		(unsigned long)statusManager.getStatus()
	);
	m_Server.send(200, "text/html", buf);
}

void WebCommand::handleReset() {
	m_Logger.info("Sensor reset requested via HTTP");
	m_Server.send(200, "text/plain", "Sensor soft-reset triggered\n");
	// Defer actual reset to next loop tick so the HTTP response can be sent first
	globalTimer.in(10, [](void*) -> bool {
		sensorManager.resetSensors();
		return false;
	});
}

void WebCommand::handleNotFound() {
	m_Server.send(404, "text/plain", "Not found\n");
}

}  // namespace SlimeVR::Network

#endif  // defined(ESP8266) || defined(ESP32)
