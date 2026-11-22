#include "llmReportMenu.h"
#include "darkRectangle.h"
#include "menus.h"
#include "render.h"
#include "text.h"
#include "llmReport.mnu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

using namespace LlmReportMenu;

namespace {

constexpr int kPanelX = 8;
constexpr int kPanelY = 20;
constexpr int kPanelW = 304;
constexpr int kPanelH = 156;
constexpr int kTextPaddingX = 10;
constexpr int kTextMaxPixelWidth = kPanelW - 2 * kTextPaddingX;
constexpr int kLinesPerPage = 13;
constexpr int kLineSpacingY = 10;

std::string s_header;
std::vector<std::string> s_lines;
int s_currentPage = 0;
int s_totalPages = 1;

std::string sanitizeReportLine(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    bool lastSpace = false;
    auto pushChar = [&](char c) {
        if (c == ' ') {
            if (!lastSpace && !out.empty()) {
                out.push_back(' ');
                lastSpace = true;
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    };

    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(in[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            pushChar(' ');
            continue;
        }
        if (ch == '*' || ch == '#' || ch == '`')
            continue;
        if (ch >= 0x20 && ch <= 0x7E) {
            pushChar(static_cast<char>(std::toupper(ch)));
            continue;
        }
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {
            unsigned char b2 = static_cast<unsigned char>(in[++i]);
            unsigned int cp = ((ch & 0x1Fu) << 6) | (b2 & 0x3Fu);
            switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
                pushChar('A'); break;
            case 0x00C7: case 0x00E7: case 0x0106: case 0x0107: case 0x010C: case 0x010D:
                pushChar('C'); break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
                pushChar('E'); break;
            case 0x011E: case 0x011F:
                pushChar('G'); break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            case 0x0130: case 0x0131:
                pushChar('I'); break;
            case 0x00D1: case 0x00F1:
                pushChar('N'); break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
                pushChar('O'); break;
            case 0x015E: case 0x015F: case 0x0160: case 0x0161:
                pushChar('S'); break;
            case 0x00DF:
                pushChar('S'); pushChar('S'); break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
                pushChar('U'); break;
            case 0x00DD: case 0x00FD: case 0x00FF:
                pushChar('Y'); break;
            case 0x017D: case 0x017E:
                pushChar('Z'); break;
            case 0x0110: case 0x0111:
                pushChar('D'); break;
            default:
                break;
            }
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

int measureTextWidth(const std::string& str)
{
    int px = getStringPixelLength(str.c_str(), false);
    if (px <= 0 && !str.empty())
        return static_cast<int>(str.size()) * 6;
    return px;
}

void refreshPageButtons()
{
    if (s_totalPages <= 1) {
        prevPageEntry.hide();
        nextPageEntry.hide();
        return;
    }

    if (s_currentPage > 0)
        prevPageEntry.show();
    else
        prevPageEntry.hide();

    if (s_currentPage + 1 < s_totalPages)
        nextPageEntry.show();
    else
        nextPageEntry.hide();
}

} // namespace

std::vector<std::string> wrapLlmReportLines(const std::string& rawText, int maxPixelWidth)
{
    std::vector<std::string> out;
    if (maxPixelWidth <= 12)
        return out;

    std::istringstream stream(rawText);
    std::string paragraph;
    while (std::getline(stream, paragraph)) {
        std::string clean = sanitizeReportLine(paragraph);
        if (clean.empty()) {
            if (!out.empty() && !out.back().empty())
                out.emplace_back();
            continue;
        }

        std::istringstream words(clean);
        std::string word;
        std::string current;
        while (words >> word) {
            std::string candidate = current.empty() ? word : (current + " " + word);
            if (measureTextWidth(candidate) <= maxPixelWidth) {
                current = std::move(candidate);
            } else {
                if (!current.empty()) {
                    out.push_back(std::move(current));
                    current.clear();
                }
                // Handle a single long word/token wider than the panel
                while (measureTextWidth(word) > maxPixelWidth && word.size() > 1) {
                    size_t take = word.size() - 1;
                    while (take > 1 && measureTextWidth(word.substr(0, take)) > maxPixelWidth)
                        --take;
                    out.push_back(word.substr(0, take));
                    word = word.substr(take);
                }
                current = std::move(word);
            }
        }
        if (!current.empty())
            out.push_back(std::move(current));
    }

    while (!out.empty() && out.back().empty())
        out.pop_back();
    return out;
}

void showLlmReportDialog(const char *header, const std::string& body)
{
    s_header = header ? sanitizeReportLine(header) : "REPORT";
    s_lines = wrapLlmReportLines(body, kTextMaxPixelWidth);
    if (s_lines.empty())
        return;

    s_currentPage = 0;
    s_totalPages = std::max(1, static_cast<int>((s_lines.size() + kLinesPerPage - 1) / kLinesPerPage));
    showMenu(llmReportMenu);
}

static void initLlmReportMenu()
{
    headerEntry.copyString(s_header.c_str());
    refreshPageButtons();
    setCurrentEntry(continueBtn);
}

static void drawLlmReportMenu()
{
    // Semi-transparent dark backdrop panel covering the main reading area
    drawDarkRectangle({ static_cast<float>(kPanelX), static_cast<float>(kPanelY),
                        static_cast<float>(kPanelW), static_cast<float>(kPanelH) }, true);

    int startIdx = s_currentPage * kLinesPerPage;
    int endIdx = std::min(static_cast<int>(s_lines.size()), startIdx + kLinesPerPage);

    int textY = kPanelY + 8;
    for (int i = startIdx; i < endIdx; ++i) {
        const auto& line = s_lines[i];
        if (!line.empty()) {
            bool isHighlight = (i == 0) || (line.rfind("CHAIRMAN:", 0) == 0);
            int color = isHighlight ? kYellowText : kWhiteText2;
            drawText(kPanelX + kTextPaddingX, textY, line.c_str(), kTextMaxPixelWidth, color, false);
        }
        textY += kLineSpacingY;
    }

    if (s_totalPages > 1) {
        char pageBuf[32];
        std::snprintf(pageBuf, sizeof(pageBuf), "PAGE %d / %d", s_currentPage + 1, s_totalPages);
        drawTextCentered(kPanelX + kPanelW / 2, kPanelY + kPanelH - 12, pageBuf, kTextMaxPixelWidth, kYellowText, false);
    }
}

static void llmReportPrevPage()
{
    if (s_currentPage > 0) {
        --s_currentPage;
        refreshPageButtons();
        if (s_currentPage == 0)
            setCurrentEntry(continueBtn);
    }
}

static void llmReportNextPage()
{
    if (s_currentPage + 1 < s_totalPages) {
        ++s_currentPage;
        refreshPageButtons();
        if (s_currentPage + 1 >= s_totalPages)
            setCurrentEntry(continueBtn);
    }
}

static void llmReportClose()
{
    SetExitMenuFlag();
}
