#include "clubLogo.h"
#include "file.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "swos.h"

#ifndef SWOS_TEST
#include <SDL_image.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace {

std::string toUpperTrimmed(const char *s)
{
    if (!s)
        return {};
    std::string out;
    for (const char *p = s; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 0x20 && c <= 0x7E)
            out.push_back(static_cast<char>(std::toupper(c)));
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ')
        ++start;
    return out.substr(start);
}

std::string slugifyTeam(const std::string& upper)
{
    std::string slug;
    bool prevDash = true;
    for (unsigned char c : upper) {
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            slug.push_back(static_cast<char>(std::tolower(c)));
            prevDash = false;
        } else if (!prevDash) {
            slug.push_back('-');
            prevDash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    return slug;
}

#ifndef SWOS_TEST
struct LogoCacheState {
    SDL_Renderer *renderer = nullptr;
    bool manifestLoaded = false;
    std::unordered_map<std::string, std::string> manifest; // UPPER_SWOS_NAME -> rel path under assets/logos/
    std::unordered_map<std::string, SDL_Texture *> textures; // cacheKey -> tex (nullptr for miss)
};

LogoCacheState s_cache;

void resetCacheIfRendererChanged(SDL_Renderer *r)
{
    if (s_cache.renderer == r)
        return;
    for (auto& kv : s_cache.textures) {
        if (kv.second)
            SDL_DestroyTexture(kv.second);
    }
    s_cache.textures.clear();
    s_cache.renderer = r;
}

void ensureManifestLoaded()
{
    if (s_cache.manifestLoaded)
        return;
    s_cache.manifestLoaded = true;

    std::string rel = "assets/logos/manifest.txt";
    std::string full = resolveModAssetPath(rel.c_str());
    std::ifstream in(full);
    if (!in.is_open())
        in.open(rel);
    if (!in.is_open())
        return;

    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        size_t sep = line.find('|');
        if (sep == std::string::npos || sep == 0 || sep + 1 >= line.size())
            continue;
        std::string key = line.substr(0, sep);
        std::string val = line.substr(sep + 1);
        s_cache.manifest[key] = val;
    }
}

SDL_Texture *loadLogoFile(SDL_Renderer *r, const std::string& relUnderLogos)
{
    std::string rel = "assets/logos/" + relUnderLogos;
    std::string full = resolveModAssetPath(rel.c_str());
    SDL_Texture *tex = IMG_LoadTexture(r, full.c_str());
    if (!tex)
        tex = IMG_LoadTexture(r, rel.c_str());
    if (tex) {
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

bool drawCenteredSquareLogo(SDL_Renderer *r, SDL_Texture *tex, float x, float y, float size, Uint8 alpha)
{
    if (!r || !tex || size <= 1.0f)
        return false;
    int tw = 0, th = 0;
    SDL_FRect dst{ x, y, size, size };
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) == 0 && tw > 0 && th > 0) {
        float aspect = static_cast<float>(tw) / static_cast<float>(th);
        if (aspect > 1.02f) {
            float dh = size / aspect;
            dst = { x, y + (size - dh) * 0.5f, size, dh };
        } else if (aspect < 0.98f) {
            float dw = size * aspect;
            dst = { x + (size - dw) * 0.5f, y, dw, size };
        }
    }
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderCopyF(r, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
    return true;
}
#endif

std::string mapCountryAndDivToLeagueKey(int countryFileNo, int divisionTier)
{
    int div = std::clamp(divisionTier, 0, 3);
    switch (countryFileNo) {
    case 1:  return "austria_bundesliga";
    case 2:  return "belgium_pro";
    case 4:  return "croatia_hnl";
    case 6:  return "czech_liga";
    case 7:  return "denmark_superliga";
    case 8:
        if (div == 0) return "england_premier";
        if (div == 1) return "england_div1";
        if (div == 2) return "england_div2";
        return "england_div3";
    case 13: return (div == 0) ? "france_ligue1" : "france_ligue2";
    case 14: return (div == 0) ? "germany_bundesliga" : "germany_2bundesliga";
    case 15: return "greece_super";
    case 20: return (div == 0) ? "italy_seriea" : "italy_serieb";
    case 25: return "netherlands_eredivisie";
    case 27: return "norway_eliteserien";
    case 28: return "poland_ekstraklasa";
    case 29: return "portugal_primeira";
    case 31: return "russia_premier";
    case 33: return "scotland_premier";
    case 35: return (div == 0) ? "spain_laliga" : "spain_segunda";
    case 36: return "sweden_allsvenskan";
    case 37: return "switzerland_super";
    case 38: return "turkey_superlig";
    case 39: return "ukraine_premier";
    case 43: return "argentina_primera";
    case 46: return "brazil_seriea";
    case 55: return "japan_j1";
    case 73: return "usa_mls";
    case 80: return "uefa_euro";
    case 82: return "copa_america";
    case 81:
    case 83:
    case 84:
    case 85: return "fifa_worldcup";
    default: return {};
    }
}

} // namespace

SDL_Texture *getClubLogoTexture(SDL_Renderer *r, const char *teamName)
{
#ifdef SWOS_TEST
    (void)r;
    (void)teamName;
    return nullptr;
#else
    if (!r || !teamName || !teamName[0])
        return nullptr;

    resetCacheIfRendererChanged(r);
    ensureManifestLoaded();

    std::string upper = toUpperTrimmed(teamName);
    if (upper.empty())
        return nullptr;

    std::string cacheKey = "C:" + upper;
    auto it = s_cache.textures.find(cacheKey);
    if (it != s_cache.textures.end())
        return it->second;

    SDL_Texture *tex = nullptr;
    auto mit = s_cache.manifest.find(upper);
    if (mit != s_cache.manifest.end()) {
        tex = loadLogoFile(r, mit->second);
    }
    if (!tex) {
        std::string slug = slugifyTeam(upper);
        if (!slug.empty()) {
            tex = loadLogoFile(r, "clubs/" + slug + ".png");
            if (!tex)
                tex = loadLogoFile(r, "clubs/nat-" + slug + ".png");
        }
    }

    s_cache.textures[cacheKey] = tex;
    return tex;
#endif
}

SDL_Texture *getLeagueLogoTexture(SDL_Renderer *r, const char *leagueKey)
{
#ifdef SWOS_TEST
    (void)r;
    (void)leagueKey;
    return nullptr;
#else
    if (!r || !leagueKey || !leagueKey[0])
        return nullptr;

    resetCacheIfRendererChanged(r);

    std::string key(leagueKey);
    std::string cacheKey = "L:" + key;
    auto it = s_cache.textures.find(cacheKey);
    if (it != s_cache.textures.end())
        return it->second;

    SDL_Texture *tex = loadLogoFile(r, "leagues/" + key + ".png");
    s_cache.textures[cacheKey] = tex;
    return tex;
#endif
}

bool drawClubLogo(SDL_Renderer *r, const char *teamName, float x, float y, float size, Uint8 alpha)
{
#ifdef SWOS_TEST
    (void)r; (void)teamName; (void)x; (void)y; (void)size; (void)alpha;
    return false;
#else
    return drawCenteredSquareLogo(r, getClubLogoTexture(r, teamName), x, y, size, alpha);
#endif
}

bool drawLeagueLogo(SDL_Renderer *r, const char *leagueKey, float x, float y, float size, Uint8 alpha)
{
#ifdef SWOS_TEST
    (void)r; (void)leagueKey; (void)x; (void)y; (void)size; (void)alpha;
    return false;
#else
    return drawCenteredSquareLogo(r, getLeagueLogoTexture(r, leagueKey), x, y, size, alpha);
#endif
}

std::string resolveCurrentCareerLeagueLogoKey()
{
    if (HotseatInternal::isVmNationalSelector())
        return "fifa_worldcup";
    if (swos.g_gameType != kGameTypeCareer && swos.g_gameType != kGameTypeSeason)
        return {};
    int country = static_cast<int>(SwosVM::g_memByte[CareerOffsets::kCompCountryNumber]);
    int div = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]));
    return mapCountryAndDivToLeagueKey(country, div);
}

std::string resolveCurrentMatchLeagueLogoKey()
{
    const auto *t1 = swos.playMatchTeam1Ptr.asAligned();
    const auto *t2 = swos.playMatchTeam2Ptr.asAligned();
    int c1 = t1 ? static_cast<int>(t1->teamFileNo) : -1;
    int c2 = t2 ? static_cast<int>(t2->teamFileNo) : -1;

    // National team match
    if ((c1 >= 80 && c1 <= 85) || (c2 >= 80 && c2 <= 85)) {
        if (c1 == 80 && c2 == 80)
            return "uefa_euro";
        if (c1 == 82 && c2 == 82)
            return "copa_america";
        return "fifa_worldcup";
    }

    // Cross-country club match -> European / Continental competition!
    if (c1 >= 0 && c2 >= 0 && c1 != c2 && c1 < 80 && c2 < 80) {
        if (c1 <= 41 && c2 <= 41)
            return "uefa_champions";
        if (c1 >= 43 && c1 <= 71 && c2 >= 43 && c2 <= 71)
            return "libertadores";
        return "uefa_champions";
    }

    // Domestic club match
    if (swos.g_gameType == kGameTypeCareer || swos.g_gameType == kGameTypeSeason) {
        int country = (c1 >= 0 && c1 < 80) ? c1 : static_cast<int>(SwosVM::g_memByte[CareerOffsets::kCompCountryNumber]);
        int div = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]));
        // If two clubs from the same country are in different divisions, likely domestic cup (e.g. FA Cup)
        if (t1 && t2 && t1->teamFileNo == 8 && t2->teamFileNo == 8 && t1->league != t2->league)
            return "england_facup";
        std::string key = mapCountryAndDivToLeagueKey(country, div);
        if (!key.empty())
            return key;
    }

    if (c1 >= 0 && c1 < 80) {
        int div = t1 ? static_cast<int>(t1->league) : 0;
        return mapCountryAndDivToLeagueKey(c1, div);
    }

    return {};
}
