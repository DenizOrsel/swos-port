#pragma once

#include <cstdint>
#include <string>

// Checks whether the telemetry dashboard server responds to an HTTP/HTTPS request within timeoutMs.
bool isTelemetryServerReachable(const std::string& host, uint16_t port, int timeoutMs = 2500, bool useHttps = true);

// Opens the in-game embedded Microsoft Edge WebView2 browser pointing at https://swosalytics.vercel.app/.
// If the server is offline, displays a styled "TELEMETRY SERVER IS OFFLINE" screen with Retry/Back.
void showTelemetryBrowserMenu();
