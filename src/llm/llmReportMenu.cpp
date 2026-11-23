#include "llmReportMenu.h"
#include "compactFont.h"
#include "darkRectangle.h"
#include "gameFieldMapping.h"
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
constexpr int kCompactLineStepUnits = 11;

std::string s_header;
std::string s_rawBody;
std::vector<std::string> s_lines;
int s_currentPage = 0;
int s_totalPages = 1;
int s_linesPerPage = 16;
int s_lastWrapUnits = 0;

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

void reflowCompactReportLayout(float gameScale)
{
    float px = computeCompactPixelScaleForVga(gameScale, 0.48f);
    float innerW = (static_cast<float>(kPanelW) - 16.0f) * gameScale;
    float innerH = (static_cast<float>(kPanelH) - 18.0f) * gameScale;
    int maxUnits = std::max(80, static_cast<int>(innerW / px));
    int linesPerPage = std::max(6, static_cast<int>(innerH / (static_cast<float>(kCompactLineStepUnits) * px)));

    if (maxUnits != s_lastWrapUnits || linesPerPage != s_linesPerPage || s_lines.empty()) {
        s_lastWrapUnits = maxUnits;
        s_linesPerPage = linesPerPage;
        s_lines = wrapCompactTextUnits(s_rawBody, maxUnits);
        s_totalPages = std::max(1, static_cast<int>((s_lines.size() + static_cast<size_t>(s_linesPerPage) - 1) / static_cast<size_t>(s_linesPerPage)));
        s_currentPage = std::clamp(s_currentPage, 0, s_totalPages - 1);
        refreshPageButtons();
    }
}

void showLlmReportDialog(const char *header, const std::string& body)
{
    s_header = header ? sanitizeReportLine(header) : "REPORT";
    s_rawBody = body;
    s_lastWrapUnits = 0;
    s_currentPage = 0;
    s_lines = wrapCompactTextUnits(s_rawBody, 420);
    if (s_lines.empty())
        return;

    s_linesPerPage = 18;
    s_totalPages = std::max(1, static_cast<int>((s_lines.size() + static_cast<size_t>(s_linesPerPage) - 1) / static_cast<size_t>(s_linesPerPage)));
    showMenu(llmReportMenu);
}

static void initLlmReportMenu()
{
    headerEntry.copyString(s_header.c_str());
    s_lastWrapUnits = 0;
    refreshPageButtons();
    setCurrentEntry(continueBtn);
}

static void drawLlmReportMenu()
{
    // Semi-transparent dark backdrop panel covering the main reading area
    drawDarkRectangle({ static_cast<float>(kPanelX), static_cast<float>(kPanelY),
                        static_cast<float>(kPanelW), static_cast<float>(kPanelH) }, true);

    SDL_Renderer *renderer = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(renderer);
    if (!renderer || !atlas)
        return;

    float gameScale = getGameScale();
    reflowCompactReportLayout(gameScale);

    float px = computeCompactPixelScaleForVga(gameScale, 0.48f);
    float panelScreenX = getGameScreenOffsetX() + static_cast<float>(kPanelX) * gameScale;
    float panelScreenY = getGameScreenOffsetY() + static_cast<float>(kPanelY) * gameScale;
    float panelScreenW = static_cast<float>(kPanelW) * gameScale;
    float panelScreenH = static_cast<float>(kPanelH) * gameScale;

    float origScaleX = 1.0f, origScaleY = 1.0f;
    SDL_RenderGetScale(renderer, &origScaleX, &origScaleY);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);

    int startIdx = s_currentPage * s_linesPerPage;
    int endIdx = std::min(static_cast<int>(s_lines.size()), startIdx + s_linesPerPage);

    float textX = panelScreenX + 8.0f * gameScale;
    float textY = panelScreenY + 7.0f * gameScale;
    float lineStepPx = static_cast<float>(kCompactLineStepUnits) * px;

    SDL_Color whiteCol{ 244, 246, 248, 255 };
    SDL_Color yellowCol{ 255, 226, 78, 255 };
    SDL_Color greenCol{ 96, 236, 134, 255 };
    SDL_Color redCol{ 248, 108, 108, 255 };
    SDL_Color mentionCol{ 118, 244, 232, 255 };

    for (int i = startIdx; i < endIdx; ++i) {
        const auto& line = s_lines[i];
        if (!line.empty()) {
            std::string upper;
            upper.reserve(line.size());
            for (char c : line)
                upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

            bool isWon = (upper.rfind("[WON]", 0) == 0);
            bool isFailed = (upper.rfind("[FAILED]", 0) == 0);
            bool isHighlight = (i == 0) ||
                               (upper.rfind("CHAIRMAN:", 0) == 0) ||
                               (upper.rfind("ALL-IN", 0) == 0) ||
                               (upper.rfind("NET MATCHDAY", 0) == 0) ||
                               (upper.rfind("UPDATED BANK", 0) == 0) ||
                               (upper.rfind("CAREER SPONSOR", 0) == 0) ||
                               (upper.rfind("SPONSOR CHALLENGE", 0) == 0);

            SDL_Color col = isWon ? greenCol : (isFailed ? redCol : (isHighlight ? yellowCol : whiteCol));
            bool bold = isWon || isFailed || isHighlight;
            drawCompactTextWithMentions(renderer, atlas, textX, textY, px, line, col, mentionCol, 255, bold);
        }
        textY += lineStepPx;
    }

    if (s_totalPages > 1) {
        char pageBuf[32];
        std::snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", s_currentPage + 1, s_totalPages);
        drawCompactTextCentered(renderer, atlas, panelScreenX + panelScreenW * 0.5f,
            panelScreenY + panelScreenH - 11.0f * px, px, pageBuf, yellowCol, 245, true);
    }

    SDL_RenderSetScale(renderer, origScaleX, origScaleY);
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
