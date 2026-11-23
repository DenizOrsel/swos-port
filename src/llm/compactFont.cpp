#include "compactFont.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace {

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

int charAdvanceUnits(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < kMiniFontFirst || uc >= kMiniFontFirst + kMiniFontCount)
        return 3;
    return static_cast<int>(kMiniFont[uc - kMiniFontFirst].width) + 1;
}

bool isMentionTokenChar(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || uc == '_' || uc == '-';
}

// Truncates fractional digits on percentages: "%57,23" / "%57.23" -> "%57", "57,23%" / "57.23%" -> "57%".
std::string stripPercentageDecimals(const std::string& s)
{
    if (s.find('%') == std::string::npos)
        return s;

    std::string out;
    out.reserve(s.size());
    auto isDig = [](char c) { return c >= '0' && c <= '9'; };
    size_t i = 0;
    while (i < s.size()) {
        // Pattern 1: "%57,23" or "%57.23"
        if (s[i] == '%' && i + 1 < s.size() && isDig(s[i + 1])) {
            size_t j = i + 1;
            while (j < s.size() && isDig(s[j]) && (j - (i + 1)) < 3)
                ++j;
            if (j + 1 < s.size() && (s[j] == '.' || s[j] == ',') && isDig(s[j + 1])) {
                size_t k = j + 1;
                while (k < s.size() && isDig(s[k]))
                    ++k;
                out.append(s, i, j - i);
                i = k;
                continue;
            }
        }
        // Pattern 2: "57,23%" or "57.23%"
        if (isDig(s[i]) && (i == 0 || !isDig(s[i - 1]))) {
            size_t j = i;
            while (j < s.size() && isDig(s[j]) && (j - i) < 3)
                ++j;
            if (j + 1 < s.size() && (s[j] == '.' || s[j] == ',') && isDig(s[j + 1])) {
                size_t k = j + 1;
                while (k < s.size() && isDig(s[k]))
                    ++k;
                if (k < s.size() && s[k] == '%') {
                    out.append(s, i, j - i);
                    out.push_back('%');
                    i = k + 1;
                    continue;
                }
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

bool needsDisplaySanitize(const std::string& text)
{
    bool hasPct = false;
    bool hasFrac = false;
    for (unsigned char c : text) {
        if (c >= 0x80)
            return true;
        if (c == '%')
            hasPct = true;
        else if (c == '.' || c == ',')
            hasFrac = true;
    }
    return hasPct && hasFrac;
}

} // namespace

SDL_Texture *getCompactFontAtlas(SDL_Renderer *r)
{
    if (!r)
        return nullptr;
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

int measureCompactTextUnits(const std::string& text)
{
    if (needsDisplaySanitize(text)) {
        std::string clean = sanitizeCompactMixedCase(text);
        int w = 0;
        for (char c : clean)
            w += charAdvanceUnits(c);
        return w > 0 ? w - 1 : 0;
    }
    int w = 0;
    for (char c : text)
        w += charAdvanceUnits(c);
    return w > 0 ? w - 1 : 0;
}

std::string sanitizeCompactMixedCase(const std::string& in)
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
        // Transliterate common 2-byte UTF-8 Latin/Turkish/European letters preserving case
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {
            unsigned char b2 = static_cast<unsigned char>(in[++i]);
            unsigned int cp = ((ch & 0x1Fu) << 6) | (b2 & 0x3Fu);
            switch (cp) {
            case 0x00A3: pushChar('G'); pushChar('B'); pushChar('P'); pushChar(' '); break; // £
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
            default:
                // Silently drop Unicode combining diacritical marks (e.g. NFD Turkish I+dot, c+cedilla, g+breve)
                if (cp >= 0x0300 && cp <= 0x036F)
                    break;
                break;
            }
        } else if (ch == 0xE2 && i + 2 < in.size()) {
            // Common UTF-8 punctuation: smart quotes, dashes, ellipsis
            unsigned char b2 = static_cast<unsigned char>(in[i + 1]);
            unsigned char b3 = static_cast<unsigned char>(in[i + 2]);
            if (b2 == 0x80 && (b3 == 0x93 || b3 == 0x94)) {
                pushChar('-');
                i += 2;
            } else if (b2 == 0x80 && (b3 == 0x98 || b3 == 0x99)) {
                pushChar('\'');
                i += 2;
            } else if (b2 == 0x80 && (b3 == 0x9C || b3 == 0x9D)) {
                i += 2;
            } else if (b2 == 0x80 && b3 == 0xA6) {
                pushChar('.'); pushChar('.'); pushChar('.');
                i += 2;
            }
        } else if ((ch & 0xF0) == 0xE0 && i + 2 < in.size()) {
            i += 2; // skip other 3-byte UTF-8 sequences
        } else if ((ch & 0xF8) == 0xF0 && i + 3 < in.size()) {
            i += 3; // skip 4-byte UTF-8 emojis
        } else {
            // Single-byte Windows-1254 / ISO-8859-9 Turkish fallback
            switch (ch) {
            case 0xC7: pushChar('C'); break;
            case 0xE7: pushChar('c'); break;
            case 0xD0: pushChar('G'); break;
            case 0xF0: pushChar('g'); break;
            case 0xDD: pushChar('I'); break;
            case 0xFD: pushChar('i'); break;
            case 0xD6: pushChar('O'); break;
            case 0xF6: pushChar('o'); break;
            case 0xDE: pushChar('S'); break;
            case 0xFE: pushChar('s'); break;
            case 0xDC: pushChar('U'); break;
            case 0xFC: pushChar('u'); break;
            default: break;
            }
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return stripPercentageDecimals(out);
}

std::vector<std::string> wrapCompactTextUnits(const std::string& rawText, int maxUnits)
{
    std::vector<std::string> out;
    if (maxUnits <= 16)
        return out;

    std::istringstream stream(rawText);
    std::string paragraph;
    while (std::getline(stream, paragraph)) {
        std::string clean = sanitizeCompactMixedCase(paragraph);
        if (clean.empty()) {
            if (!out.empty() && !out.back().empty())
                out.emplace_back();
            continue;
        }

        std::istringstream words(clean);
        std::string word;
        std::string current;
        while (words >> word) {
            std::string cand = current.empty() ? word : (current + " " + word);
            if (measureCompactTextUnits(cand) <= maxUnits) {
                current = std::move(cand);
            } else {
                if (!current.empty()) {
                    out.push_back(std::move(current));
                    current.clear();
                }
                while (measureCompactTextUnits(word) > maxUnits && word.size() > 1) {
                    size_t take = word.size() - 1;
                    while (take > 1 && measureCompactTextUnits(word.substr(0, take)) > maxUnits)
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

float computeCompactPixelScaleForVga(float gameScale, float ratio /* = 0.50f */)
{
    float raw = gameScale * ratio;
    if (raw < 1.35f)
        return 1.0f;
    return std::clamp(std::floor(raw + 0.5f), 2.0f, 5.0f);
}

float drawCompactText(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha /* = 255 */, bool bold /* = false */)
{
    if (!r || !atlas || text.empty())
        return x;

    std::string sanitized;
    const std::string& drawStr = needsDisplaySanitize(text)
        ? (sanitized = sanitizeCompactMixedCase(text))
        : text;
    if (drawStr.empty())
        return x;

    auto renderPass = [&](float ox, float oy, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
        SDL_SetTextureColorMod(atlas, cr, cg, cb);
        SDL_SetTextureAlphaMod(atlas, ca);
        float curX = x + ox;
        for (char c : drawStr) {
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

    Uint8 shadowAlpha = static_cast<Uint8>((static_cast<int>(alpha) * 185) / 255);
    renderPass(px * 0.75f, px * 0.75f, 8, 10, 12, shadowAlpha);
    return renderPass(0.0f, 0.0f, color.r, color.g, color.b, alpha);
}

void drawCompactTextCentered(SDL_Renderer *r, SDL_Texture *atlas, float centerX, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha /* = 255 */, bool bold /* = false */)
{
    float w = static_cast<float>(measureCompactTextUnits(text)) * px;
    drawCompactText(r, atlas, centerX - w * 0.5f, y, px, text, color, alpha, bold);
}

void drawCompactTextRightAligned(SDL_Renderer *r, SDL_Texture *atlas, float rightX, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha /* = 255 */, bool bold /* = false */)
{
    float w = static_cast<float>(measureCompactTextUnits(text)) * px;
    drawCompactText(r, atlas, rightX - w, y, px, text, color, alpha, bold);
}

void drawCompactTextWithMentions(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color baseColor, SDL_Color mentionColor, Uint8 alpha /* = 255 */, bool bold /* = false */)
{
    float curX = x;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t at = text.find('@', pos);
        if (at == std::string::npos) {
            curX = drawCompactText(r, atlas, curX, y, px, text.substr(pos), baseColor, alpha, bold);
            break;
        }
        if (at > pos) {
            curX = drawCompactText(r, atlas, curX, y, px, text.substr(pos, at - pos), baseColor, alpha, bold);
        }
        size_t end = at + 1;
        while (end < text.size() && isMentionTokenChar(text[end]))
            ++end;
        if (end > at + 1) {
            curX = drawCompactText(r, atlas, curX, y, px, text.substr(at, end - at), mentionColor, alpha, true);
            pos = end;
        } else {
            curX = drawCompactText(r, atlas, curX, y, px, "@", baseColor, alpha, bold);
            pos = at + 1;
        }
    }
}
