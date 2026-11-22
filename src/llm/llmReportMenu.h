#pragma once

#include <string>
#include <vector>

void showLlmReportDialog(const char *header, const std::string& body);
std::vector<std::string> wrapLlmReportLines(const std::string& sanitizedText, int maxPixelWidth);
