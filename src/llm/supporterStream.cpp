#include "supporterStream.h"
#include "gameTime.h"
#include "llmClient.h"
#include "matchTelemetry.h"
#include "render.h"
#include "stats.h"
#include "swos.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <sstream>

namespace {

uint64_t s_streamRequestId = 0;
int s_cooldownFrames = 0;
int s_ambientTimerFrames = 0;
int s_staggerTimerFrames = 0;
bool s_kickoffTriggered = false;
bool s_userHidden = false;
int s_avatarSeq = 0;

std::deque<SupporterTweet> s_pendingBatch;
std::deque<SupporterTweet> s_visibleTweets;

// Compact proportional 5x8 mixed-case ASCII pixel font (0x20..0x7E)
// Rows 0..6 = cap height / baseline, row 7 = descenders (g, j, p, q, y, ',', ';')
struct MiniGlyph {
    uint8_t width;
    uint8_t rows[8];
};

constexpr int kMiniFontFirst = 0x20;
constexpr int kMiniFontCount = 95;
constexpr int kMiniCellW = 6;
constexpr int kMiniCellH = 8;

constexpr MiniGlyph kMiniFont[kMiniFontCount] = {
    { 3, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, // ' '
    { 1, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00 } }, // '!'
    { 3, { 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, // '"'
    { 5, { 0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00, 0x00 } }, // '#'
    { 5, { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00 } }, // '$'
    { 5, { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00 } }, // '%'
    { 5, { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00 } }, // '&'
    { 1, { 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, // '\''
    { 3, { 0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00 } }, // '('
    { 3, { 0x10, 0x08, 0x04, 0x04, 0x04, 0x08, 0x10, 0x00 } }, // ')'
    { 5, { 0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00, 0x00 } }, // '*'
    { 5, { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00 } }, // '+'
    { 2, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10 } }, // ','
    { 4, { 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00 } }, // '-'
    { 1, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00 } }, // '.'
    { 4, { 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x00 } }, // '/'
    { 4, { 0x0C, 0x12, 0x12, 0x12, 0x12, 0x12, 0x0C, 0x00 } }, // '0'
    { 3, { 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00 } }, // '1'
    { 4, { 0x0C, 0x12, 0x02, 0x04, 0x08, 0x10, 0x1E, 0x00 } }, // '2'
    { 4, { 0x0C, 0x12, 0x02, 0x0C, 0x02, 0x12, 0x0C, 0x00 } }, // '3'
    { 4, { 0x04, 0x0C, 0x14, 0x1E, 0x04, 0x04, 0x04, 0x00 } }, // '4'
    { 4, { 0x1E, 0x10, 0x1C, 0x02, 0x02, 0x12, 0x0C, 0x00 } }, // '5'
    { 4, { 0x0C, 0x10, 0x1C, 0x12, 0x12, 0x12, 0x0C, 0x00 } }, // '6'
    { 4, { 0x1E, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08, 0x00 } }, // '7'
    { 4, { 0x0C, 0x12, 0x12, 0x0C, 0x12, 0x12, 0x0C, 0x00 } }, // '8'
    { 4, { 0x0C, 0x12, 0x12, 0x0E, 0x02, 0x04, 0x18, 0x00 } }, // '9'
    { 1, { 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00 } }, // ':'
    { 2, { 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x08, 0x10 } }, // ';'
    { 3, { 0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00, 0x00 } }, // '<'
    { 4, { 0x00, 0x00, 0x1E, 0x00, 0x1E, 0x00, 0x00, 0x00 } }, // '='
    { 3, { 0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00, 0x00 } }, // '>'
    { 4, { 0x0C, 0x12, 0x02, 0x04, 0x08, 0x00, 0x08, 0x00 } }, // '?'
    { 5, { 0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00 } }, // '@'
    { 5, { 0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00 } }, // 'A'
    { 4, { 0x1C, 0x12, 0x12, 0x1C, 0x12, 0x12, 0x1C, 0x00 } }, // 'B'
    { 4, { 0x0E, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0E, 0x00 } }, // 'C'
    { 4, { 0x1C, 0x12, 0x12, 0x12, 0x12, 0x12, 0x1C, 0x00 } }, // 'D'
    { 4, { 0x1E, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x1E, 0x00 } }, // 'E'
    { 4, { 0x1E, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x10, 0x00 } }, // 'F'
    { 5, { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E, 0x00 } }, // 'G'
    { 4, { 0x12, 0x12, 0x12, 0x1E, 0x12, 0x12, 0x12, 0x00 } }, // 'H'
    { 3, { 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00 } }, // 'I'
    { 4, { 0x06, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C, 0x00 } }, // 'J'
    { 4, { 0x12, 0x14, 0x18, 0x18, 0x14, 0x12, 0x12, 0x00 } }, // 'K'
    { 4, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1E, 0x00 } }, // 'L'
    { 5, { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00 } }, // 'M'
    { 5, { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00 } }, // 'N'
    { 4, { 0x0C, 0x12, 0x12, 0x12, 0x12, 0x12, 0x0C, 0x00 } }, // 'O'
    { 4, { 0x1C, 0x12, 0x12, 0x1C, 0x10, 0x10, 0x10, 0x00 } }, // 'P'
    { 5, { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00 } }, // 'Q'
    { 4, { 0x1C, 0x12, 0x12, 0x1C, 0x14, 0x12, 0x12, 0x00 } }, // 'R'
    { 4, { 0x0E, 0x10, 0x10, 0x0C, 0x02, 0x02, 0x1C, 0x00 } }, // 'S'
    { 5, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 } }, // 'T'
    { 4, { 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x0C, 0x00 } }, // 'U'
    { 5, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 } }, // 'V'
    { 5, { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00 } }, // 'W'
    { 5, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00 } }, // 'X'
    { 5, { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00 } }, // 'Y'
    { 4, { 0x1E, 0x02, 0x04, 0x04, 0x08, 0x10, 0x1E, 0x00 } }, // 'Z'
    { 3, { 0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00 } }, // '['
    { 4, { 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x02, 0x00 } }, // '\\'
    { 3, { 0x1C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1C, 0x00 } }, // ']'
    { 3, { 0x08, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, // '^'
    { 4, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x00 } }, // '_'
    { 2, { 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, // '`'
    { 4, { 0x00, 0x00, 0x0C, 0x02, 0x0E, 0x12, 0x0E, 0x00 } }, // 'a'
    { 4, { 0x10, 0x10, 0x1C, 0x12, 0x12, 0x12, 0x1C, 0x00 } }, // 'b'
    { 4, { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E, 0x00 } }, // 'c'
    { 4, { 0x02, 0x02, 0x0E, 0x12, 0x12, 0x12, 0x0E, 0x00 } }, // 'd'
    { 4, { 0x00, 0x00, 0x0C, 0x12, 0x1E, 0x10, 0x0E, 0x00 } }, // 'e'
    { 3, { 0x04, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x00 } }, // 'f'
    { 4, { 0x00, 0x00, 0x0E, 0x12, 0x12, 0x0E, 0x02, 0x1C } }, // 'g'
    { 4, { 0x10, 0x10, 0x1C, 0x12, 0x12, 0x12, 0x12, 0x00 } }, // 'h'
    { 1, { 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 } }, // 'i'
    { 3, { 0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x14, 0x08 } }, // 'j'
    { 4, { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 } }, // 'k'
    { 2, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x08, 0x00 } }, // 'l'
    { 5, { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x00 } }, // 'm'
    { 4, { 0x00, 0x00, 0x1C, 0x12, 0x12, 0x12, 0x12, 0x00 } }, // 'n'
    { 4, { 0x00, 0x00, 0x0C, 0x12, 0x12, 0x12, 0x0C, 0x00 } }, // 'o'
    { 4, { 0x00, 0x00, 0x1C, 0x12, 0x12, 0x1C, 0x10, 0x10 } }, // 'p'
    { 4, { 0x00, 0x00, 0x0E, 0x12, 0x12, 0x0E, 0x02, 0x02 } }, // 'q'
    { 3, { 0x00, 0x00, 0x14, 0x18, 0x10, 0x10, 0x10, 0x00 } }, // 'r'
    { 4, { 0x00, 0x00, 0x0E, 0x10, 0x0C, 0x02, 0x1C, 0x00 } }, // 's'
    { 3, { 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x04, 0x00 } }, // 't'
    { 4, { 0x00, 0x00, 0x12, 0x12, 0x12, 0x12, 0x0E, 0x00 } }, // 'u'
    { 5, { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 } }, // 'v'
    { 5, { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00 } }, // 'w'
    { 4, { 0x00, 0x00, 0x12, 0x12, 0x0C, 0x12, 0x12, 0x00 } }, // 'x'
    { 4, { 0x00, 0x00, 0x12, 0x12, 0x12, 0x0E, 0x02, 0x1C } }, // 'y'
    { 4, { 0x00, 0x00, 0x1E, 0x02, 0x04, 0x08, 0x1E, 0x00 } }, // 'z'
    { 3, { 0x04, 0x08, 0x08, 0x10, 0x08, 0x08, 0x04, 0x00 } }, // '{'
    { 1, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 } }, // '|'
    { 3, { 0x10, 0x08, 0x08, 0x04, 0x08, 0x08, 0x10, 0x00 } }, // '}'
    { 5, { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00 } }, // '~'
};

SDL_Renderer *s_atlasRenderer = nullptr;
SDL_Texture *s_fontAtlas = nullptr;

SDL_Texture *getMiniFontAtlas(SDL_Renderer *r)
{
    if (s_fontAtlas && s_atlasRenderer == r)
        return s_fontAtlas;

    s_atlasRenderer = r;
    s_fontAtlas = nullptr;

    constexpr int kAtlasW = kMiniFontCount * kMiniCellW;
    constexpr int kAtlasH = kMiniCellH;

    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
    if (!tex)
        return nullptr;

    std::vector<uint32_t> pixels(kAtlasW * kAtlasH, 0u);
    for (int g = 0; g < kMiniFontCount; ++g) {
        const auto& glyph = kMiniFont[g];
        int baseX = g * kMiniCellW;
        for (int row = 0; row < kMiniCellH; ++row) {
            uint8_t bits = glyph.rows[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (0x10u >> col))
                    pixels[row * kAtlasW + baseX + col] = 0xFFFFFFFFu;
            }
        }
    }

    SDL_UpdateTexture(tex, nullptr, pixels.data(), kAtlasW * sizeof(uint32_t));
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
    s_fontAtlas = tex;
    return s_fontAtlas;
}

int miniCharAdvance(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < kMiniFontFirst || uc >= kMiniFontFirst + kMiniFontCount)
        return 3;
    return static_cast<int>(kMiniFont[uc - kMiniFontFirst].width) + 1;
}

int measureMiniTextUnits(const std::string& text)
{
    int w = 0;
    for (char c : text)
        w += miniCharAdvance(c);
    return w > 0 ? w - 1 : 0;
}

float drawMiniText(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha, bool bold = false)
{
    if (!atlas || text.empty())
        return x;

    auto renderPass = [&](float ox, float oy, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
        SDL_SetTextureColorMod(atlas, cr, cg, cb);
        SDL_SetTextureAlphaMod(atlas, ca);
        float curX = x + ox;
        for (char c : text) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < kMiniFontFirst || uc >= kMiniFontFirst + kMiniFontCount) {
                curX += 3.0f * px;
                continue;
            }
            int idx = uc - kMiniFontFirst;
            const auto& g = kMiniFont[idx];
            if (uc != ' ') {
                SDL_Rect src{ idx * kMiniCellW, 0, g.width, kMiniCellH };
                SDL_FRect dst{ curX, y + oy, g.width * px, kMiniCellH * px };
                SDL_RenderCopyF(r, atlas, &src, &dst);
                if (bold) {
                    SDL_FRect dstBold{ curX + 0.5f * px, y + oy, g.width * px, kMiniCellH * px };
                    SDL_RenderCopyF(r, atlas, &src, &dstBold);
                }
            }
            curX += (g.width + 1) * px;
        }
        return curX;
    };

    // Subtle crisp drop shadow for contrast over bright/muddy/snowy pitches
    Uint8 shadowAlpha = static_cast<Uint8>((static_cast<int>(alpha) * 185) / 255);
    renderPass(px * 0.75f, px * 0.75f, 8, 10, 12, shadowAlpha);
    return renderPass(0.0f, 0.0f, color.r, color.g, color.b, alpha);
}

std::string trimStream(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string sanitizeStreamText(const std::string& in)
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
        if (ch == '*' || ch == '`' || ch == '"')
            continue;
        if (ch >= 0x20 && ch <= 0x7E) {
            pushChar(static_cast<char>(ch));
            continue;
        }
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {
            unsigned char b2 = static_cast<unsigned char>(in[++i]);
            unsigned int cp = ((ch & 0x1Fu) << 6) | (b2 & 0x3Fu);
            switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5: pushChar('A'); break;
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: pushChar('a'); break;
            case 0x00C7: case 0x0106: case 0x010C: pushChar('C'); break;
            case 0x00E7: case 0x0107: case 0x010D: pushChar('c'); break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: pushChar('E'); break;
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: pushChar('e'); break;
            case 0x011E: pushChar('G'); break;
            case 0x011F: pushChar('g'); break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: case 0x0130: pushChar('I'); break;
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: case 0x0131: pushChar('i'); break;
            case 0x00D1: pushChar('N'); break;
            case 0x00F1: pushChar('n'); break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8: pushChar('O'); break;
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8: pushChar('o'); break;
            case 0x015E: case 0x0160: pushChar('S'); break;
            case 0x015F: case 0x0161: pushChar('s'); break;
            case 0x00DF: pushChar('s'); pushChar('s'); break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: pushChar('U'); break;
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: pushChar('u'); break;
            case 0x00DD: pushChar('Y'); break;
            case 0x00FD: case 0x00FF: pushChar('y'); break;
            case 0x017D: pushChar('Z'); break;
            case 0x017E: pushChar('z'); break;
            case 0x0110: pushChar('D'); break;
            case 0x0111: pushChar('d'); break;
            default: break;
            }
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Wrap message text inline right after "Handle: " on line 1, and full width on lines 2..maxLines
std::vector<std::string> wrapInlineMessage(const std::string& msg, int firstLineMaxUnits, int nextLineMaxUnits, int maxLines)
{
    std::vector<std::string> words;
    std::istringstream iss(msg);
    std::string w;
    while (iss >> w)
        words.push_back(std::move(w));

    std::vector<std::string> lines;
    std::string cur;
    int curLimit = firstLineMaxUnits;

    for (const auto& word : words) {
        if (cur.empty()) {
            int wUnits = measureMiniTextUnits(word);
            if (wUnits <= curLimit || !lines.empty()) {
                cur = word;
            } else {
                // First word doesn't fit after handle; push empty line 1 so message starts cleanly on line 2
                lines.push_back("");
                if (static_cast<int>(lines.size()) >= maxLines)
                    return lines;
                curLimit = nextLineMaxUnits;
                cur = word;
            }
        } else {
            std::string cand = cur + " " + word;
            if (measureMiniTextUnits(cand) <= curLimit) {
                cur = std::move(cand);
            } else {
                lines.push_back(std::move(cur));
                if (static_cast<int>(lines.size()) >= maxLines)
                    return lines;
                curLimit = nextLineMaxUnits;
                cur = word;
            }
        }
    }

    if (!cur.empty() && static_cast<int>(lines.size()) < maxLines)
        lines.push_back(std::move(cur));
    if (lines.empty())
        lines.push_back("");
    return lines;
}

SDL_Color swosKitColorToRgb(word colorIdx)
{
    switch (colorIdx & 0x0F) {
    case 0: return { 170, 175, 185, 255 }; // gray
    case 1: return { 238, 240, 245, 255 }; // white
    case 2: return { 42, 46, 54, 255 };    // black
    case 3: return { 235, 140, 42, 255 };  // orange
    case 4: return { 215, 48, 52, 255 };   // red
    case 5: return { 44, 98, 210, 255 };   // blue
    case 6: return { 135, 86, 52, 255 };   // brown
    case 7: return { 105, 195, 245, 255 }; // light blue
    case 8: return { 54, 178, 76, 255 };   // green
    case 9: return { 242, 206, 54, 255 };  // yellow
    default: return { 110, 180, 235, 255 };
    }
}

void drawCompactAvatar(SDL_Renderer *r, float ax, float ay, float px, SupporterSide side, int variant, Uint8 alpha)
{
    auto fillUnit = [&](float ux, float uy, float uw, float uh, Uint8 red, Uint8 g, Uint8 b, Uint8 a) {
        SDL_SetRenderDrawColor(r, red, g, b, static_cast<Uint8>((static_cast<int>(a) * alpha) / 255));
        SDL_FRect fr{ ax + ux * px, ay + uy * px, uw * px, uh * px };
        SDL_RenderFillRectF(r, &fr);
    };

    if (side == SupporterSide::kNeutral && (variant % 2 == 0)) {
        // Mini 8x8 pixel football
        fillUnit(1, 0, 6, 8, 235, 238, 242, 245);
        fillUnit(0, 1, 8, 6, 235, 238, 242, 245);
        fillUnit(3, 3, 2, 2, 30, 34, 42, 245);
        fillUnit(1, 1, 1, 1, 30, 34, 42, 245);
        fillUnit(6, 1, 1, 1, 30, 34, 42, 245);
        fillUnit(1, 6, 1, 1, 30, 34, 42, 245);
        fillUnit(6, 6, 1, 1, 30, 34, 42, 245);
        return;
    }

    if (side == SupporterSide::kHome || side == SupporterSide::kAway) {
        const TeamGame& team = (side == SupporterSide::kHome) ? swos.topTeamInGame : swos.bottomTeamInGame;
        SDL_Color primary = swosKitColorToRgb(team.prShirtCol);
        SDL_Color accent = swosKitColorToRgb(team.prStripesCol);
        // Mini 8x8 club crest / shield
        fillUnit(0, 0, 8, 6, 224, 228, 236, 235);
        fillUnit(1, 6, 6, 1, 224, 228, 236, 235);
        fillUnit(2, 7, 4, 1, 224, 228, 236, 235);
        fillUnit(1, 1, 6, 5, primary.r, primary.g, primary.b, 245);
        fillUnit(2, 6, 4, 1, primary.r, primary.g, primary.b, 245);
        fillUnit(3, 1, 2, 5, accent.r, accent.g, accent.b, 245);
        return;
    }

    // Mini 8x8 retro pixel supporter face
    fillUnit(1, 0, 6, 2, 98, 64, 38, 245);         // hair
    fillUnit(1, 2, 6, 4, 238, 188, 150, 245);      // face
    fillUnit(2, 3, 1, 1, 24, 26, 32, 245);         // left eye
    fillUnit(5, 3, 1, 1, 24, 26, 32, 245);         // right eye
    fillUnit(1, 6, 6, 2, 70, 132, 214, 245);       // collar
}

void enqueueSupporterBatchRequest(const char *triggerContext)
{
    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    std::string prompt = formatTelemetryPromptBlock(snap);
    prompt += "\nTrigger: " + std::string(triggerContext ? triggerContext : "LIVE FLOW");
    prompt += "\nGenerate 2 to 3 short live supporter chat messages reacting to this exact moment (clock, score, territory, events).";
    prompt += "\nFormat STRICTLY one message per line as: HANDLE|SIDE|MESSAGE";
    prompt += "\n- HANDLE: realistic 1997 fan nickname (no spaces, max 13 chars, mixed case e.g. Fan_atic, Terrace97, GoalHunter)";
    prompt += "\n- SIDE: HOME (supports " + snap.team1 + "), AWAY (supports " + snap.team2 + "), or NEUTRAL";
    prompt += "\n- MESSAGE: short punchy fan reaction in normal sentence case (4 to 12 words). If minute >= 85 and margin <= 1, leading fans beg the ref to blow the whistle!";
    prompt += "\nOutput ONLY the HANDLE|SIDE|MESSAGE lines. No intro, no numbering, no markdown.";

    const std::string sys = "You simulate a live 1997 football fan chat stream watching the match. "
                            "Be passionate, varied, and strictly faithful to the telemetry facts (score, minute, scorer, finish type, stats). "
                            "Use natural sentence case (not all caps). Output strictly HANDLE|SIDE|MESSAGE lines only.";

    s_streamRequestId = enqueueLlmRequest(sys, prompt);
    s_cooldownFrames = 240; // ~4s minimum spacing between requests
    s_ambientTimerFrames = std::max(5, cfg.telemetry.streamUpdateSeconds) * 60;
}

} // namespace

std::vector<SupporterTweet> parseSupporterTweetBatch(const std::string& rawResponse)
{
    std::vector<SupporterTweet> parsed;
    std::istringstream stream(rawResponse);
    std::string line;
    while (std::getline(stream, line)) {
        line = trimStream(line);
        if (line.empty())
            continue;
        size_t p1 = line.find('|');
        if (p1 == std::string::npos)
            continue;
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos)
            continue;

        std::string handle = sanitizeStreamText(trimStream(line.substr(0, p1)));
        std::string sideStr = toUpperAscii(sanitizeStreamText(trimStream(line.substr(p1 + 1, p2 - p1 - 1))));
        std::string msg = sanitizeStreamText(trimStream(line.substr(p2 + 1)));

        if (!handle.empty() && handle.front() == '@')
            handle.erase(handle.begin());
        if (handle.size() > 14)
            handle.resize(14);
        if (handle.empty() || msg.empty())
            continue;
        if (msg.size() > 96)
            msg.resize(96);

        SupporterTweet tw;
        tw.handle = std::move(handle);
        if (sideStr.find("HOME") != std::string::npos)
            tw.side = SupporterSide::kHome;
        else if (sideStr.find("AWAY") != std::string::npos)
            tw.side = SupporterSide::kAway;
        else
            tw.side = SupporterSide::kNeutral;
        tw.message = std::move(msg);
        tw.avatarVariant = s_avatarSeq++;
        parsed.push_back(std::move(tw));
        if (parsed.size() >= 3)
            break;
    }
    return parsed;
}

void resetSupporterStream()
{
    s_streamRequestId = 0;
    s_cooldownFrames = 0;
    s_ambientTimerFrames = 90; // ~1.5s after kickoff
    s_staggerTimerFrames = 0;
    s_kickoffTriggered = false;
    s_pendingBatch.clear();
    s_visibleTweets.clear();
}

void toggleSupporterStreamVisible()
{
    s_userHidden = !s_userHidden;
}

bool isSupporterStreamVisible()
{
    const auto& cfg = getLlmConfig();
    return cfg.enabled && cfg.features.supporterStream && cfg.telemetry.enabled && !s_userHidden;
}

void triggerSupporterStreamEvent(const char *eventLabel, bool highPriority /* = false */)
{
    if (!eventLabel || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    if (!highPriority && (s_cooldownFrames > 0 || s_streamRequestId != 0))
        return;

    enqueueSupporterBatchRequest(eventLabel);
}

void updateSupporterStream()
{
    if (swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    if (s_cooldownFrames > 0)
        --s_cooldownFrames;
    if (s_staggerTimerFrames > 0)
        --s_staggerTimerFrames;

    for (auto& tw : s_visibleTweets)
        ++tw.ageFrames;

    // Poll async LLM response without blocking
    if (s_streamRequestId != 0) {
        std::string reply;
        if (tryTakeLlmResponse(s_streamRequestId, reply)) {
            s_streamRequestId = 0;
            auto batch = parseSupporterTweetBatch(reply);
            for (auto& tw : batch)
                s_pendingBatch.push_back(std::move(tw));
            s_staggerTimerFrames = 0;
        }
    }

    // Release one tweet at a time with a natural ~2.2s stagger
    if (!s_pendingBatch.empty() && s_staggerTimerFrames <= 0) {
        s_visibleTweets.push_front(std::move(s_pendingBatch.front()));
        s_pendingBatch.pop_front();
        int maxTweets = std::clamp(cfg.telemetry.maxVisibleTweets, 3, 6);
        while (static_cast<int>(s_visibleTweets.size()) > maxTweets)
            s_visibleTweets.pop_back();
        s_staggerTimerFrames = 130; // ~2.2 seconds between staggered fan messages
    }

    // Trigger kickoff and periodic ambient match-flow reactions while match is active
    if (swos.gameStatePl == GameState::kInProgress && s_streamRequestId == 0 && s_pendingBatch.empty()) {
        if (s_ambientTimerFrames > 0) {
            --s_ambientTimerFrames;
        } else {
            if (!s_kickoffTriggered) {
                s_kickoffTriggered = true;
                enqueueSupporterBatchRequest("KICKOFF");
            } else {
                enqueueSupporterBatchRequest("LIVE MATCH FLOW");
            }
        }
    }
}

void drawSupporterStreamOverlay()
{
    if (swos.g_trainingGame || s_userHidden)
        return;
    if (showingPostGameStats() || showingUserRequestedStats())
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;
    if (s_visibleTweets.empty())
        return;

    SDL_Renderer *renderer = getRenderer();
    if (!renderer)
        return;

    SDL_Texture *atlas = getMiniFontAtlas(renderer);
    if (!atlas)
        return;

    SDL_Rect vp = getViewport();
    if (vp.w <= 0 || vp.h <= 0)
        return;

    // Compact native-pixel scale: ~14px tall crisp text at 1080p, ~10.5px at 720p, ~21px at 1440p
    float px = 1.0f;
    if (vp.h >= 900)
        px = std::clamp(std::floor(static_cast<float>(vp.h) / 520.0f + 0.5f), 2.0f, 4.0f);
    else if (vp.h >= 600)
        px = 1.5f;

    // Panel geometry in logical compact font units (~156 units wide -> ~312px on 1080p = ~16% of width)
    constexpr int kPanelUnitsW = 156;
    constexpr int kHeaderUnitsH = 15;
    constexpr int kPadX = 5;
    constexpr int kAvatarSize = 8;
    constexpr int kAvatarGap = 4;
    constexpr int kTextStartUnits = kPadX + kAvatarSize + kAvatarGap;
    constexpr int kTextMaxUnits = kPanelUnitsW - kTextStartUnits - kPadX;
    constexpr int kLineStepUnits = 10;
    constexpr int kTweetGapUnits = 4;

    int maxPanelUnitsH = static_cast<int>((static_cast<float>(vp.h) * 0.46f) / px);
    int maxBodyBottom = maxPanelUnitsH - 4;

    struct LaidOutTweet {
        int yUnits;
        int alpha;
        const SupporterTweet *tw;
        std::string handleLabel;
        int handleUnits;
        std::vector<std::string> lines;
    };

    std::vector<LaidOutTweet> layout;
    int curY = kHeaderUnitsH + 4;

    for (size_t i = 0; i < s_visibleTweets.size(); ++i) {
        const auto& tw = s_visibleTweets[i];
        std::string handleLabel = tw.handle + ": ";
        int handleW = measureMiniTextUnits(handleLabel) + 2;
        int firstLineAvail = std::max(0, kTextMaxUnits - handleW);
        auto lines = wrapInlineMessage(tw.message, firstLineAvail, kTextMaxUnits, 3);
        int blockH = std::max(kAvatarSize + 1, static_cast<int>(lines.size()) * kLineStepUnits) + kTweetGapUnits;

        if (curY + blockH > maxBodyBottom)
            break;

        int fadeIn = std::min(255, 65 + tw.ageFrames * 15);
        int posFade = (i >= 4) ? 150 : (i == 3 ? 200 : 255);
        int alpha = std::min(fadeIn, posFade);

        layout.push_back({ curY, alpha, &tw, std::move(handleLabel), handleW, std::move(lines) });
        curY += blockH;
    }

    if (layout.empty())
        return;

    int panelUnitsH = curY + 2;
    float panelW = kPanelUnitsW * px;
    float panelH = panelUnitsH * px;
    float margin = 6.0f * px;
    float panelX = static_cast<float>(vp.w) - panelW - margin;
    float panelY = margin;

    float origScaleX = 1.0f, origScaleY = 1.0f;
    SDL_RenderGetScale(renderer, &origScaleX, &origScaleY);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Sleek semi-transparent dark glass card (leaves pitch clearly visible behind/around it)
    SDL_SetRenderDrawColor(renderer, 14, 18, 16, 152);
    SDL_FRect panelRect{ panelX, panelY, panelW, panelH };
    SDL_RenderFillRectF(renderer, &panelRect);

    // Subtle darker header strip
    SDL_SetRenderDrawColor(renderer, 10, 14, 12, 182);
    SDL_FRect hdrBand{ panelX + px * 0.5f, panelY + px * 0.5f, panelW - px, kHeaderUnitsH * px };
    SDL_RenderFillRectF(renderer, &hdrBand);

    // Crisp hairline border + header separator
    SDL_SetRenderDrawColor(renderer, 160, 174, 162, 115);
    SDL_RenderDrawRectF(renderer, &panelRect);
    SDL_SetRenderDrawColor(renderer, 150, 164, 152, 85);
    SDL_RenderDrawLineF(renderer, panelX + 2.0f * px, panelY + kHeaderUnitsH * px,
        panelX + panelW - 2.0f * px, panelY + kHeaderUnitsH * px);

    // Compact red "• LIVE" badge pill
    SDL_SetRenderDrawColor(renderer, 220, 44, 44, 232);
    SDL_FRect livePill{ panelX + 5.0f * px, panelY + 3.0f * px, 29.0f * px, 9.0f * px };
    SDL_RenderFillRectF(renderer, &livePill);

    // Pulsing white live dot
    bool dotBright = ((SDL_GetTicks() / 450) & 1) == 0;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, dotBright ? 255 : 125);
    SDL_FRect liveDot{ panelX + 7.5f * px, panelY + 6.5f * px, 2.0f * px, 2.0f * px };
    SDL_RenderFillRectF(renderer, &liveDot);

    // Header text
    drawMiniText(renderer, atlas, panelX + 12.0f * px, panelY + 4.0f * px, px,
        "LIVE", { 255, 255, 255, 255 }, 255, true);
    drawMiniText(renderer, atlas, panelX + 38.0f * px, panelY + 4.0f * px, px,
        "CHAT STREAM", { 238, 244, 240, 255 }, 245, true);

    // 2. Render each compact supporter message with inline handle + avatar
    for (const auto& item : layout) {
        float rowY = panelY + item.yUnits * px;
        drawCompactAvatar(renderer, panelX + kPadX * px, rowY, px,
            item.tw->side, item.tw->avatarVariant, static_cast<Uint8>(item.alpha));

        SDL_Color handleColor = (item.tw->side == SupporterSide::kHome) ? SDL_Color{ 122, 198, 255, 255 } :
                                (item.tw->side == SupporterSide::kAway) ? SDL_Color{ 255, 214, 96, 255 } :
                                                                          SDL_Color{ 176, 232, 196, 255 };
        float textX = panelX + kTextStartUnits * px;
        float afterHandleX = drawMiniText(renderer, atlas, textX, rowY, px,
            item.handleLabel, handleColor, static_cast<Uint8>(item.alpha), true);

        for (size_t li = 0; li < item.lines.size(); ++li) {
            if (item.lines[li].empty())
                continue;
            float lineX = (li == 0) ? afterHandleX : textX;
            float lineY = rowY + static_cast<float>(li * kLineStepUnits) * px;
            drawMiniText(renderer, atlas, lineX, lineY, px,
                item.lines[li], { 244, 246, 248, 255 }, static_cast<Uint8>(item.alpha), false);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(renderer, origScaleX, origScaleY);
}
