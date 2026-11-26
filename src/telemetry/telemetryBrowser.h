#pragma once

#include <cstdint>
#include <string>

// Checks whether the local Next.js telemetry server responds to an HTTP request within timeoutMs.
bool isTelemetryServerReachable(const std::string& host, uint16_t port, int timeoutMs = 600);

// Opens the in-game embedded Microsoft Edge WebView2 browser pointing at http://localhost:3000.
// If the server is offline, displays a styled "TELEMETRY SERVER IS OFFLINE" screen with Retry/Back.
void showTelemetryBrowserMenu();
