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

#if defined(ESP8266) || defined(ESP32)

#ifdef ESP8266
#include <ESP8266WebServer.h>
#elif defined(ESP32)
#include <WebServer.h>
#endif

#include "../logging/Logger.h"

// Minimal HTTP server for Wi-Fi-accessible tracker commands.
// Browse to http://<tracker-ip>/ for status or http://<tracker-ip>/reset
// to trigger a sensor soft-reset without USB or a full reboot.

namespace SlimeVR::Network {

class WebCommand {
public:
	void setup();
	void update();

private:
	void handleRoot();
	void handleReset();
	void handleNotFound();

#ifdef ESP8266
	ESP8266WebServer m_Server{80};
#elif defined(ESP32)
	WebServer m_Server{80};
#endif

	bool m_Enabled = false;
	Logging::Logger m_Logger = Logging::Logger("WebCommand");
};

}  // namespace SlimeVR::Network

#else  // stub for non-ESP platforms (native unit tests)
namespace SlimeVR::Network {
class WebCommand {
public:
	void setup() {}
	void update() {}
};
}  // namespace SlimeVR::Network
#endif
