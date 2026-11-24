#include "transferImpact.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameControls.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "keyBuffer.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "log.h"
#include "menuBackground.h"
#include "menus.h"
#include "pressCharisma.h"
#include "render.h"
#include "stadiumFacilities.h"
#include "swos.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

PendingTransferMatchEffect s_pendingNextMatch;
PendingTransferMatchEffect s_activeThisMatch;
std::string s_recentTransferHeadline;

// 4-match Reinvest Promise tracking after selling a star player
bool s_reinvestPromiseActive = false;
int s_reinvestMatchesLeft = 0;
std::string s_soldStarName;

uint8_t clampSwosSkill(int v)
{
    return static_cast<uint8_t>(std::clamp(v, 0, 7));
}

void adjustByteSkill(byte& f, int d)
{
    if (d != 0)
        f = clampSwosSkill(static_cast<int>(f) + d);
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string cleanName(const char *raw, size_t maxLen)
{
    std::string out;
    for (size_t i = 0; i < maxLen && raw[i] != '\0'; ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c >= 0x20 && c <= 0x7E)
            out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string formatPoundsShort(int64_t amt)
{
    bool neg = amt < 0;
    int64_t a = neg ? -amt : amt;
    char buf[48];
    if (a >= 1000000)
        std::snprintf(buf, sizeof(buf), "%sGBP %.2fM", neg ? "-" : "", static_cast<double>(a) / 1000000.0);
    else if (a >= 1000)
        std::snprintf(buf, sizeof(buf), "%sGBP %lldK", neg ? "-" : "", static_cast<long long>(a / 1000));
    else
        std::snprintf(buf, sizeof(buf), "%sGBP %lld", neg ? "-" : "", static_cast<long long>(a));
    return buf;
}

int32_t readVmClubCash()
{
    return *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
}

void applyVmClubCashDelta(int32_t delta)
{
    if (delta == 0)
        return;
    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    int64_t next = std::clamp<int64_t>(static_cast<int64_t>(*vmBal) + delta, -2000000000LL, 2000000000LL);
    *vmBal = static_cast<int32_t>(next);

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activePlayer().newBalance = *vmBal;
    }
}

void applyCharismaChange(int8_t delta)
{
    if (delta == 0)
        return;
    int16_t cp = 0; uint16_t ints = 0; int64_t lsb = 0; uint8_t st = 0; std::string lq;
    getPressCareerTotals(cp, ints, lsb, st, lq);
    setPressCareerTotals(applyCharismaDelta(cp, delta), ints, lsb, st, lq);
}

int readVmDivisionTier()
{
    uint16_t raw = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
    return std::clamp<int>(static_cast<int>(raw), 0, 3);
}

std::string readCurrentCareerClubName()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        if (!mgr.activePlayer().teamName.empty())
            return mgr.activePlayer().teamName;
    }
    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    if (hdr && hdr->teamName[0] != '\0')
        return hdr->teamName;
    return "THE CLUB";
}

bool resolveHumanCareerSide(const TeamGame *topTeam, const TeamGame *bottomTeam, bool& outHumanIsTeam1)
{
    if (!topTeam || !bottomTeam)
        return false;
    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();

    bool t1 = (swos.topTeamData.playerNumber != 0 || swos.topTeamData.playerCoachNumber != 0 || swos.topTeamData.isPlCoach != 0);
    bool t2 = (swos.bottomTeamData.playerNumber != 0 || swos.bottomTeamData.playerCoachNumber != 0 || swos.bottomTeamData.isPlCoach != 0);
    if (!t1 && !t2) {
        if (tf1) t1 = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2) t2 = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }
    if (t1 == t2 && swos.g_gameType == kGameTypeCareer) {
        std::string clubUp = toUpperAscii(readCurrentCareerClubName());
        bool m1 = (toUpperAscii(topTeam->teamName) == clubUp);
        bool m2 = (toUpperAscii(bottomTeam->teamName) == clubUp);
        if (m1 != m2) {
            t1 = m1;
            t2 = m2;
        }
    }
    if (t1 == t2)
        return false;
    outHumanIsTeam1 = t1;
    return true;
}

PlayerFile *playerFileAtSlot(int slot)
{
    if (slot < 0 || slot >= kCurrentMatchPlayersSize)
        return nullptr;
    size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                 static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
    if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
        return nullptr;
    return reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
}

std::string upgradePrimarySkill(PlayerFile *pf)
{
    if (!pf)
        return {};
    auto pos = static_cast<PlayerPosition>((pf->positionAndFace >> 5) & 0x07u);
    auto bumpHigh = [](uint8_t& b) -> bool {
        uint8_t v = (b >> 4) & 0x07u;
        if (v >= 7) return false;
        b = static_cast<uint8_t>((b & 0x8Fu) | ((v + 1) << 4));
        return true;
    };
    auto bumpLow = [](uint8_t& b) -> bool {
        uint8_t v = b & 0x07u;
        if (v >= 7) return false;
        b = static_cast<uint8_t>((b & 0xF8u) | (v + 1));
        return true;
    };

    if (pos == PlayerPosition::kAttacker) {
        if (bumpLow(pf->speedFinishing)) return "FINISHING";
        if (bumpHigh(pf->shootingHeading)) return "SHOOTING";
        if (bumpHigh(pf->speedFinishing)) return "SPEED";
    } else if (pos == PlayerPosition::kMidfielder || pos == PlayerPosition::kLeftWing || pos == PlayerPosition::kRightWing) {
        if (bumpLow(pf->passing)) return "PASSING";
        if (bumpLow(pf->tacklingBallControl)) return "BALL CONTROL";
        if (bumpHigh(pf->speedFinishing)) return "SPEED";
    } else {
        if (bumpHigh(pf->tacklingBallControl)) return "TACKLING";
        if (bumpLow(pf->shootingHeading)) return "HEADING";
        if (bumpLow(pf->passing)) return "PASSING";
    }
    if (bumpLow(pf->passing)) return "PASSING";
    if (bumpHigh(pf->shootingHeading)) return "SHOOTING";
    if (bumpLow(pf->speedFinishing)) return "FINISHING";
    if (bumpHigh(pf->speedFinishing)) return "SPEED";
    return {};
}

struct InteractiveChoiceItem {
    std::string title;
    std::string gainLine;
    std::string riskLine;
    int32_t cashCost = 0; // positive cost required
};

// Reusable crisp 3-choice transfer decision modal (supports [1/2/3], mouse click, & [ESC] -> defaultChoiceOnEsc)
int runTransferDecisionModal(
    const std::string& headerTitle,
    const std::string& situationHeader,
    const std::string& situationBody,
    const InteractiveChoiceItem choices[3],
    int defaultChoiceOnEsc,
    uint64_t asyncLlmReqId = 0)
{
#ifdef SWOS_TEST
    (void)headerTitle; (void)situationHeader; (void)situationBody;
    (void)choices; (void)asyncLlmReqId;
    return defaultChoiceOnEsc;
#else
    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return defaultChoiceOnEsc;

    setStandardMenuBackgroundImage();
    menuFadeIn();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();

    std::string liveBody = situationBody;
    std::string errMsg;
    Uint32 errTick = 0;
    bool prevKeys[4] = {};
    bool prevMouse = false;
    Uint32 openTick = SDL_GetTicks();

    const SDL_Color whiteCol{ 244, 246, 248, 255 };
    const SDL_Color mutedCol{ 166, 178, 188, 255 };
    const SDL_Color yellowCol{ 255, 224, 78, 255 };
    const SDL_Color cyanCol{ 118, 232, 246, 255 };
    const SDL_Color greenCol{ 96, 238, 134, 255 };
    const SDL_Color redCol{ 248, 114, 114, 255 };
    const SDL_Color orangeCol{ 250, 178, 84, 255 };

    auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
        int u = measureCompactTextUnits(s);
        if (u <= 0 || maxW <= 1.0f) return basePx;
        float needed = static_cast<float>(u) * basePx;
        return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
    };

    int chosen = defaultChoiceOnEsc;
    while (true) {
        processControlEvents();

        if (asyncLlmReqId != 0) {
            std::string reply;
            if (tryTakeLlmResponse(asyncLlmReqId, reply)) {
                asyncLlmReqId = 0;
                std::string clean = sanitizeCompactMixedCase(reply);
                if (!clean.empty()) {
                    if (clean.size() > 175) clean.resize(175);
                    liveBody = std::move(clean);
                }
            }
        }

        drawMenuBackground();
        drawDarkRectangle({ 8.0f, 4.0f, 304.0f, 14.0f }, true);
        drawDarkRectangle({ 8.0f, 21.0f, 304.0f, 38.0f }, true);
        drawDarkRectangle({ 8.0f, 62.0f, 304.0f, 112.0f }, true);
        drawDarkRectangle({ 8.0f, 177.0f, 304.0f, 18.0f }, true);

        float gameScale = getGameScale();
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();
        float px = computeCompactPixelScaleForVga(gameScale, 0.42f);

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        auto toRect = [&](float vx, float vy, float vw, float vh) -> SDL_FRect {
            return { offX + vx * gameScale, offY + vy * gameScale, vw * gameScale, vh * gameScale };
        };

        int32_t bank = readVmClubCash();
        drawCompactText(r, atlas, offX + 14.0f * gameScale, offY + 7.5f * gameScale,
            fitPx(headerTitle, px * 0.94f, 198.0f * gameScale), headerTitle, yellowCol, 255, true);
        std::string bankTxt = "BANK: " + formatPoundsShort(bank);
        drawCompactTextRightAligned(r, atlas, offX + 306.0f * gameScale, offY + 7.5f * gameScale,
            fitPx(bankTxt, px * 0.86f, 92.0f * gameScale), bankTxt, greenCol, 255, true);

        drawCompactText(r, atlas, offX + 14.0f * gameScale, offY + 24.5f * gameScale,
            fitPx(situationHeader, px * 0.86f, 290.0f * gameScale), situationHeader, cyanCol, 255, true);

        int bUnits = std::max(60, static_cast<int>((288.0f * gameScale) / (px * 0.88f)));
        auto bLines = wrapCompactTextUnits(liveBody, bUnits);
        float by = offY + 34.0f * gameScale;
        for (size_t i = 0; i < bLines.size() && i < 3; ++i) {
            drawCompactText(r, atlas, offX + 14.0f * gameScale, by, px * 0.88f, bLines[i], whiteCol, 255, false);
            by += 9.0f * px * 0.88f;
        }

        SDL_FRect cRects[3];
        for (int i = 0; i < 3; ++i) {
            const auto& c = choices[i];
            bool ok = (c.cashCost <= 0 || bank >= c.cashCost);
            cRects[i] = toRect(14.0f, 67.0f + static_cast<float>(i) * 34.5f, 292.0f, 31.5f);

            SDL_SetRenderDrawColor(r, ok ? 20 : 28, ok ? 32 : 20, ok ? 38 : 24, 215);
            SDL_RenderFillRectF(r, &cRects[i]);
            SDL_SetRenderDrawColor(r, ok ? 120 : 180, ok ? 210 : 90, ok ? 230 : 90, 200);
            SDL_RenderDrawRectF(r, &cRects[i]);

            std::string ct = "[" + std::to_string(i + 1) + "] " + c.title;
            drawCompactText(r, atlas, cRects[i].x + 5.0f, cRects[i].y + 3.0f,
                fitPx(ct, px * 0.88f, cRects[i].w - 10.0f), ct, ok ? yellowCol : redCol, 255, true);

            drawCompactText(r, atlas, cRects[i].x + 5.0f, cRects[i].y + 3.0f + 9.5f * px * 0.86f,
                fitPx("EFFECT: " + c.gainLine, px * 0.80f, cRects[i].w - 10.0f), "EFFECT: " + c.gainLine, greenCol, 250, false);

            std::string rl = (ok ? "TRADE-OFF: " : "INSUFFICIENT FUNDS: ") + c.riskLine;
            drawCompactText(r, atlas, cRects[i].x + 5.0f, cRects[i].y + 3.0f + 18.5f * px * 0.84f,
                fitPx(rl, px * 0.78f, cRects[i].w - 10.0f), rl, ok ? orangeCol : redCol, 240, false);
        }

        bool showErr = !errMsg.empty() && (SDL_GetTicks() - errTick < 2500);
        std::string foot = showErr ? errMsg : "[1 / 2 / 3] OR CLICK CHOICE   |   [ESC] DEFAULT OPTION 3";
        drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 183.0f * gameScale,
            fitPx(foot, px * 0.82f, 294.0f * gameScale), foot, showErr ? redCol : mutedCol, 255, true);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(r, origSX, origSY);
        updateScreen(false);

        bool ready = (SDL_GetTicks() - openTick) >= 160;
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        bool cur[4] = {
            keys && (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1]),
            keys && (keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2]),
            keys && (keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3]),
            keys && (keys[SDL_SCANCODE_ESCAPE] != 0),
        };
        int mx = 0, my = 0;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        bool mouseDown = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool clicked = ready && mouseDown && !prevMouse;

        bool done = false;
        if (ready) {
            for (int i = 0; i < 3; ++i) {
                bool hit = clicked && mx >= cRects[i].x && mx <= cRects[i].x + cRects[i].w &&
                           my >= cRects[i].y && my <= cRects[i].y + cRects[i].h;
                if ((cur[i] && !prevKeys[i]) || hit) {
                    if (choices[i].cashCost > 0 && bank < choices[i].cashCost) {
                        errMsg = "NOT ENOUGH FUNDS (" + formatPoundsShort(choices[i].cashCost) + " REQUIRED)";
                        errTick = SDL_GetTicks();
                    } else {
                        chosen = i;
                        done = true;
                    }
                }
            }
            if (!done && cur[3] && !prevKeys[3]) {
                chosen = defaultChoiceOnEsc;
                done = true;
            }
        }
        for (int i = 0; i < 4; ++i) prevKeys[i] = cur[i];
        prevMouse = mouseDown;
        if (done) break;
        SDL_Delay(16);
    }

    menuFadeOut();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();
    return chosen;
#endif
}

} // namespace

// --- Pure Deterministic Helpers ---

int32_t getMarqueeSigningThresholdGbp(int divisionTier)
{
    switch (std::clamp(divisionTier, 0, 3)) {
    case 0: return 3000000; // Premiership >= GBP 3M
    case 1: return 1000000; // Division 1  >= GBP 1M
    case 2: return  700000; // Division 2  >= GBP 700K
    case 3:
    default: return 400000; // Division 3  >= GBP 400K
    }
}

IncomingTransferTier classifyIncomingTransfer(
    int32_t marketValueGbp, int divisionTier, int totalSkills, int maxSingleSkill, int32_t avgSquadValueGbp)
{
    if (marketValueGbp >= getMarqueeSigningThresholdGbp(divisionTier))
        return IncomingTransferTier::kMarqueeStar;

    // SWOS Hidden Gem: strong skill-to-price ratio (affordable vs squad average yet packs real SWOS skills!)
    if ((totalSkills >= 28 && marketValueGbp <= std::max<int32_t>(300000, avgSquadValueGbp)) ||
        (maxSingleSkill >= 6 && marketValueGbp <= 250000)) {
        return IncomingTransferTier::kHiddenGem;
    }
    return IncomingTransferTier::kSquadAddition;
}

int32_t computeMarqueeShirtSalesWindfall(int32_t marketValueGbp, int commercialLevel)
{
    if (marketValueGbp <= 0)
        return 0;
    // 8% of player market value scaled by Commercial/VIP level (1.0x at L1 up to 1.6x at L5), capped cleanly
    double commMult = 1.0 + static_cast<double>(std::clamp(commercialLevel - 1, 0, 4)) * 0.15;
    double raw = static_cast<double>(marketValueGbp) * 0.08 * commMult;
    int32_t rounded = static_cast<int32_t>(std::llround(raw / 1000.0) * 1000LL);
    return std::clamp<int32_t>(rounded, 15000, 450000);
}

void applyPendingTransferEffectToTeam(TeamGame& myTeam, const PendingTransferMatchEffect& eff)
{
    if (!eff.active)
        return;
    std::string targetUp = toUpperAscii(eff.targetPlayerName);
    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        auto& pl = myTeam.players[i];
        if (pl.shortName[0] == '\0')
            continue;
        bool match = (eff.targetShirtNumber > 0 && static_cast<int>(pl.shirtNumber) == eff.targetShirtNumber) ||
                     (!targetUp.empty() && toUpperAscii(pl.shortName) == targetUp);
        if (!match)
            continue;
        adjustByteSkill(pl.speed, eff.speedDelta);
        adjustByteSkill(pl.finishing, eff.finishingDelta);
        adjustByteSkill(pl.passing, eff.passingDelta);
        adjustByteSkill(pl.ballControl, eff.controlDelta);
    }
}

// --- Career & Match Lifecycle Hooks ---

void resetTransferImpactState()
{
    s_pendingNextMatch = PendingTransferMatchEffect{};
    s_activeThisMatch = PendingTransferMatchEffect{};
    s_recentTransferHeadline.clear();
    s_reinvestPromiseActive = false;
    s_reinvestMatchesLeft = 0;
    s_soldStarName.clear();
}

std::string handleCareerTransferImpact(
    const std::string& clubName,
    const std::vector<TransferPlayerRecord>& added,
    const std::vector<TransferPlayerRecord>& removed,
    const std::vector<TransferPlayerRecord>& prevSquad,
    bool& outHandledModalAlready)
{
    outHandledModalAlready = false;
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return {};

    int divTier = readVmDivisionTier();
    int commLevel = 1;
    {
        const auto& stad = getCurrentCareerStadium();
        if (stad.initialized)
            commLevel = stad.commercialLevel;
    }

    int64_t sumPrevVal = 0;
    for (const auto& p : prevSquad) sumPrevVal += p.valueGbp;
    int32_t avgSquadVal = prevSquad.empty() ? 150000 : static_cast<int32_t>(sumPrevVal / static_cast<int64_t>(prevSquad.size()));

    std::string impactBlock;

    // Fulfill any active Star-Sale Reinvest Promise immediately when signing a new player!
    if (!added.empty() && s_reinvestPromiseActive) {
        s_reinvestPromiseActive = false;
        s_reinvestMatchesLeft = 0;
        applyCharismaChange(+10);
        impactBlock += "PROMISE KEPT! Reinvested funds after selling " + s_soldStarName + " -> +10 CHARISMA!\n";
        s_soldStarName.clear();
    }

    auto setDebutAdrenaline = [&](const TransferPlayerRecord& p) {
        s_pendingNextMatch = PendingTransferMatchEffect{};
        s_pendingNextMatch.active = true;
        s_pendingNextMatch.label = "Debut Adrenaline for new signing " + p.name;
        s_pendingNextMatch.targetShirtNumber = p.shirtNumber;
        s_pendingNextMatch.targetPlayerName = p.name;
        s_pendingNextMatch.speedDelta = +1;
        s_pendingNextMatch.finishingDelta = +1;
        s_pendingNextMatch.passingDelta = +1;
    };

    auto applyMarqueeBonuses = [&](const TransferPlayerRecord& star) {
        int32_t thresh = getMarqueeSigningThresholdGbp(divTier);
        bool megaStar = (star.valueGbp >= (thresh * 3) / 2);
        int8_t tickBoost = (divTier <= 1) ? 3 : 2; // +GBP 3 in Prem/Div1, +GBP 2 in Div2/Div3
        int8_t demandBoost = megaStar ? 18 : 15;
        int8_t cpGain = megaStar ? 22 : 15;
        int32_t shirtWindfall = computeMarqueeShirtSalesWindfall(star.valueGbp, commLevel);

        applyStadiumTransferBuzz(tickBoost, demandBoost, 5, "MARQUEE: " + star.name);
        applyVmClubCashDelta(shirtWindfall);
        applyCharismaChange(cpGain);
        setDebutAdrenaline(star);

        s_recentTransferHeadline = "Marquee signing " + star.name + " (" + star.marketValueStr +
                                   ") sparked a 5-home-match ticket & crowd surge!";
        impactBlock += "MARQUEE SIGNING BUZZ (" + star.name + " - " + star.marketValueStr + "):\n"
                       "- STADIUM AUTO-BOOST (5 HOME GAMES): +GBP " + std::to_string(tickBoost) +
                       " Tickets & +" + std::to_string(demandBoost) + "% Crowd Demand!\n"
                       "- SHIRT SALES WINDFALL: +" + formatPoundsShort(shirtWindfall) + " banked (VIP L" + std::to_string(commLevel) + ")!\n"
                       "- BOARD & FAN CONFIDENCE: +" + std::to_string(cpGain) + " Charisma Points!\n"
                       "- DEBUT ADRENALINE: " + star.name + " gets +1 SPD/FIN/PAS on debut!\n";
        logInfo("Transfer Impact [MARQUEE]: %s (%d GBP, div %d) -> +%d ticket/+%d%% crowd (5 home), +%d shirt sales, +%d CP",
            star.name.c_str(), star.valueGbp, divTier, tickBoost, demandBoost, shirtWindfall, cpGain);
    };

    // CASE A: Player Exchange / Swap Deal (both IN and OUT)
    if (!added.empty() && !removed.empty()) {
        const auto& bestIn = added.front();
        int32_t totalIn = 0, totalOut = 0;
        for (const auto& p : added) totalIn += p.valueGbp;
        for (const auto& p : removed) totalOut += p.valueGbp;

        if (bestIn.valueGbp >= getMarqueeSigningThresholdGbp(divTier)) {
            applyMarqueeBonuses(bestIn);
        } else if (totalIn > totalOut) {
            applyCharismaChange(+12);
            applyStadiumTransferBuzz(+1, +10, 3, "SWAP COUP: " + bestIn.name);
            setDebutAdrenaline(bestIn);
            s_recentTransferHeadline = "Swap masterclass brought " + bestIn.name + " to " + clubName + "!";
            impactBlock += "SWAP DEAL MASTERCLASS (Value Upgrade!):\n"
                           "- BOARD & FANS THRILLED: +12 Charisma Points!\n"
                           "- STADIUM BUZZ (3 HOME GAMES): +GBP 1 Tickets & +10% Crowd!\n"
                           "- DEBUT ADRENALINE: " + bestIn.name + " gets +1 SPD/FIN/PAS on debut!\n";
        } else if (totalIn < totalOut) {
            applyCharismaChange(-4);
            setDebutAdrenaline(bestIn);
            s_recentTransferHeadline = "Fans questioned the swap deal bringing in " + bestIn.name;
            impactBlock += "SWAP DEAL VERDICT (Value Downgrade):\n"
                           "- SUPPORTERS GRUMBLE: -4 Charisma Points.\n"
                           "- DEBUT ADRENALINE: " + bestIn.name + " gets +1 SPD/FIN/PAS to win fans over.\n";
        } else {
            applyCharismaChange(+4);
            setDebutAdrenaline(bestIn);
            impactBlock += "SWAP DEAL VERDICT (Balanced Trade):\n"
                           "- +4 Charisma & Debut Adrenaline (+1 SPD/FIN/PAS) for " + bestIn.name + ".\n";
        }
        return impactBlock;
    }

    // CASE B: Pure Incoming Signing(s)
    if (!added.empty()) {
        const TransferPlayerRecord *best = &added.front();
        for (const auto& p : added) {
            if (p.valueGbp > best->valueGbp) best = &p;
        }

        IncomingTransferTier tier = classifyIncomingTransfer(
            best->valueGbp, divTier, best->totalSkills, best->maxSingleSkill, avgSquadVal);

        if (tier == IncomingTransferTier::kMarqueeStar) {
            applyMarqueeBonuses(*best);
        } else if (tier == IncomingTransferTier::kHiddenGem) {
            applyCharismaChange(+8);
            applyStadiumTransferBuzz(+1, +8, 2, "HIDDEN GEM: " + best->name);
            setDebutAdrenaline(*best);
            s_recentTransferHeadline = "Shrewd scouting landed hidden gem " + best->name + " (" + best->marketValueStr + ")";
            impactBlock += "SHREWD SCOUTING — HIDDEN GEM (" + best->name + "):\n"
                           "- HIGH SKILL-TO-PRICE RATIO: +8 Charisma Points!\n"
                           "- FRESH FACE BUZZ (2 HOME GAMES): +GBP 1 Tickets & +8% Crowd!\n"
                           "- DEBUT ADRENALINE: " + best->name + " gets +1 SPD/FIN/PAS on debut!\n";
        } else {
            applyCharismaChange(+3);
            applyStadiumTransferBuzz(0, +5, 2, "NEW SIGNING: " + best->name);
            setDebutAdrenaline(*best);
            s_recentTransferHeadline = clubName + " strengthened the squad with " + best->name;
            impactBlock += "SQUAD SIGNING IMPACT (" + best->name + "):\n"
                           "- +3 Charisma & +5% Crowd Buzz for 2 Home Games.\n"
                           "- DEBUT ADRENALINE: " + best->name + " gets +1 SPD/FIN/PAS on debut!\n";
        }
        return impactBlock;
    }

    // CASE C: Pure Outgoing Sale — check if we sold one of our Top-2 most expensive players!
    if (!removed.empty()) {
        const TransferPlayerRecord *sold = &removed.front();
        for (const auto& p : removed) {
            if (p.valueGbp > sold->valueGbp) sold = &p;
        }

        // Count how many players in prevSquad were strictly more valuable than `sold`
        int moreValuableCount = 0;
        for (const auto& p : prevSquad) {
            if (p.name != sold->name && p.valueGbp > sold->valueGbp)
                ++moreValuableCount;
        }

        bool isTop2Star = (prevSquad.size() >= 6 && moreValuableCount < 2 && sold->valueGbp >= avgSquadVal);
        if (isTop2Star) {
            outHandledModalAlready = true;
            InteractiveChoiceItem choices[3] = {
                { "CUT HOME TICKETS (-GBP 2 FOR 3 HOME GAMES)",
                  "Apologize to fans: 0 Charisma lost & +5% Crowd goodwill",
                  "Lower ticket revenue for your next 3 home games", 0 },
                { "PROMISE TO REINVEST THE TRANSFER FEE",
                  "Keep normal tickets; sign anyone in 4 matches -> +10 CP!",
                  "If no signing within 4 matches -> -10 Charisma!", 0 },
                { "\"BUSINESS IS BUSINESS\" (STAND FIRM)",
                  "Keep full ticket prices & all transfer cash",
                  "-8 Charisma & -10% home crowds for 3 home games", 0 },
            };

            std::string body = "Supporters outside the ground are furious that " + clubName +
                               " sold fan favourite " + sold->name + " (" + sold->marketValueStr +
                               ")! How will you handle the backlash?";
            int pick = runTransferDecisionModal(
                "STAR PLAYER SOLD  -  FAN BACKLASH!",
                "SUPPORTERS PROTEST SALE OF " + toUpperAscii(sold->name) + " (" + sold->marketValueStr + ")",
                body, choices, 2, 0);

            if (pick == 0) {
                applyStadiumTransferBuzz(-2, +5, 3, "FAN APOLOGY");
                s_recentTransferHeadline = "Club slashed home tickets -GBP 2 after selling star " + sold->name;
                impactBlock = "FAN APOLOGY: Home tickets cut -GBP 2 for 3 home games (0 CP lost, crowds stayed loyal).";
            } else if (pick == 1) {
                s_reinvestPromiseActive = true;
                s_reinvestMatchesLeft = 4;
                s_soldStarName = sold->name;
                s_recentTransferHeadline = "Boss vowed to reinvest the " + sold->name + " transfer cash!";
                impactBlock = "REINVEST PROMISE: Sign any new player within 4 matches for +10 CP (failing costs -10 CP!).";
            } else {
                applyCharismaChange(-8);
                applyStadiumTransferBuzz(0, -10, 3, "FAN BACKLASH");
                s_recentTransferHeadline = "Fans furious after selling star " + sold->name + " — attendances dipped";
                impactBlock = "FAN BACKLASH: -8 Charisma & -10% Home Crowds for 3 home games.";
            }
            logInfo("Transfer Impact [STAR SALE]: %s -> pick %d (%s)", sold->name.c_str(), pick, impactBlock.c_str());
        } else {
            impactBlock = "SQUAD DEPARTURE: " + sold->name + " moved on (" + sold->marketValueStr + ").";
        }
    }

    return impactBlock;
}

void onCareerWeeklyRivalTapUpEvent()
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame ||
        HotseatInternal::isVmNationalSelector() || HotseatCareerManager::get().isInitializingSeason())
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.transferNegotiations)
        return;

    // Find our highest-priced fit outfield player
    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];

    int bestSlot = -1;
    uint8_t bestPrice = 0;
    std::string starName;
    int starShirt = 0;

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t slot = matchPlayers[i];
        PlayerFile *pf = playerFileAtSlot(slot);
        if (!pf) continue;
        std::string nm = cleanName(pf->playerName, sizeof(pf->playerName));
        if (nm.empty() || nm.rfind("*ERR*", 0) == 0) continue;
        auto pos = static_cast<PlayerPosition>((pf->positionAndFace >> 5) & 0x07u);
        if (pos == PlayerPosition::kGoalkeeper) continue;
        if (pf->price >= bestPrice) {
            bestPrice = pf->price;
            bestSlot = static_cast<int>(slot);
            starName = nm;
            starShirt = static_cast<int>(pf->shirtNumber);
        }
    }
    if (bestSlot < 0 || starName.empty())
        return;

    int divTier = readVmDivisionTier();
    int32_t loyaltyCost = (divTier <= 0) ? 35000 : (divTier == 1 ? 22000 : (divTier == 2 ? 14000 : 8000));
    std::string clubName = readCurrentCareerClubName();

    uint64_t reqId = 0;
    if (cfg.enabled) {
        std::string prompt = "Club: " + clubName + ". Star player: " + starName +
                             ". Write ONE punchy 1997 transfer agent alert sentence (max 145 chars) warning that rival clubs are sniffing around " +
                             starName + " and his head could be turned. Plain text only.";
        reqId = enqueueLlmRequest(
            "You are a sharp 1997 football transfer insider delivering an urgent rival-interest alert. Plain text only.",
            prompt);
    }

    InteractiveChoiceItem choices[3] = {
        { "PAY LOYALTY BONUS (-" + formatPoundsShort(loyaltyCost) + ")",
          starName + " commits: PERMANENT +1 SKILL & +8 Charisma!",
          "Costs " + formatPoundsShort(loyaltyCost), loyaltyCost },
        { "PUT HIM IN THE SHOP WINDOW (INFLATE VALUE)",
          "FREE! Permanent +1 SWOS MARKET VALUE TIER right now!",
          "-5 Charisma (fans nervous you may sell him)", 0 },
        { "TELL RIVAL CLUBS TO BACK OFF (FREE)",
          "+5 Charisma (strong board/manager stance)",
          "30% chance " + starName + " sulks (-1 PAS/CTL next match)", 0 },
    };

    std::string fallbackBody = "Agent alert! Rival scouts have been watching " + starName +
                               " closely and are testing " + clubName + "'s resolve. How do you respond?";
    int pick = runTransferDecisionModal(
        "TRANSFER AGENT ALERT  -  RIVAL INTEREST!",
        "BIG CLUBS CIRCLING " + toUpperAscii(starName),
        fallbackBody, choices, 2, reqId);

    std::string outcome;
    if (pick == 0) {
        applyVmClubCashDelta(-loyaltyCost);
        applyCharismaChange(+8);
        std::string sk = "SKILL";
        if (PlayerFile *pf = playerFileAtSlot(bestSlot)) {
            std::string up = upgradePrimarySkill(pf);
            if (!up.empty()) sk = up;
        }
        s_recentTransferHeadline = starName + " committed his future to " + clubName + " after a loyalty bonus!";
        outcome = "LOYALTY REWARDED: " + toUpperAscii(starName) + " gained PERMANENT +1 " + sk + " & +8 Charisma!";
    } else if (pick == 1) {
        applyCharismaChange(-5);
        if (PlayerFile *pf = playerFileAtSlot(bestSlot)) {
            if (pf->price < 48) ++pf->price;
        }
        s_recentTransferHeadline = "Transfer hype boosted " + starName + "'s market value!";
        outcome = "SHOP WINDOW BUZZ: " + toUpperAscii(starName) + "'s SWOS Market Value jumped +1 TIER! (-5 Charisma).";
    } else {
        applyCharismaChange(+5);
        uint32_t roll = static_cast<uint32_t>(SDL_GetTicks() ^ (bestSlot * 2654435761u)) % 100u;
        if (roll < 30u) {
            s_pendingNextMatch = PendingTransferMatchEffect{};
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = starName + " unsettled by rejected rival interest";
            s_pendingNextMatch.targetShirtNumber = starShirt;
            s_pendingNextMatch.targetPlayerName = starName;
            s_pendingNextMatch.passingDelta = -1;
            s_pendingNextMatch.controlDelta = -1;
            outcome = "HANDS OFF! +5 Charisma, though " + toUpperAscii(starName) + " looks unsettled (-1 PAS/CTL next match).";
        } else {
            outcome = "HANDS OFF! Rivals backed down — +5 Charisma and " + toUpperAscii(starName) + " stayed focused!";
        }
    }

    logInfo("Transfer Tap-Up resolved (%s): %s", starName.c_str(), outcome.c_str());
#ifndef SWOS_TEST
    if (pick != 2) { // Instant silent exit if user pressed ESC/3; show confirmation for paid/shop-window choices
        setStandardMenuBackgroundImage();
        menuFadeIn();
        showLlmReportDialog("TRANSFER DESK UPDATE", outcome);
        menuFadeOut();
    }
#endif
}

void applyPendingTransferEffectsForMatch(TeamGame *topTeam, TeamGame *bottomTeam)
{
    s_activeThisMatch = PendingTransferMatchEffect{};
    if (!s_pendingNextMatch.active || !topTeam || !bottomTeam || swos.g_trainingGame ||
        swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return;

    bool humanIsTeam1 = true;
    if (!resolveHumanCareerSide(topTeam, bottomTeam, humanIsTeam1))
        return;

    s_activeThisMatch = s_pendingNextMatch;
    s_pendingNextMatch = PendingTransferMatchEffect{};

    TeamGame& myTeam = humanIsTeam1 ? *topTeam : *bottomTeam;
    applyPendingTransferEffectToTeam(myTeam, s_activeThisMatch);
    logInfo("Applied Transfer Match Effect: %s", s_activeThisMatch.label.c_str());
}

void onCareerMatchFinishedTransferImpact()
{
    s_activeThisMatch = PendingTransferMatchEffect{};
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || swos.gameCanceled ||
        HotseatInternal::isVmNationalSelector())
        return;

    if (s_reinvestPromiseActive) {
        --s_reinvestMatchesLeft;
        if (s_reinvestMatchesLeft <= 0) {
            s_reinvestPromiseActive = false;
            applyCharismaChange(-10);
            std::string msg = "BROKEN TRANSFER PROMISE!\n\n4 matches have passed since selling " +
                              toUpperAscii(s_soldStarName) + " without signing a replacement.\n"
                              "Supporters are fuming (-10 Charisma Points).";
            s_recentTransferHeadline = "Supporters furious over broken reinvestment promise after " + s_soldStarName + " sale";
            s_soldStarName.clear();
#ifndef SWOS_TEST
            setStandardMenuBackgroundImage();
            menuFadeIn();
            showLlmReportDialog("FAN BACKLASH", msg);
            menuFadeOut();
#endif
        }
    }
}

std::string formatTransferImpactContextForTelemetry()
{
    if (s_activeThisMatch.active && !s_activeThisMatch.label.empty())
        return "Transfer Buzz: " + s_activeThisMatch.label;
    if (!s_recentTransferHeadline.empty())
        return "Recent Transfer Buzz: " + s_recentTransferHeadline;
    return {};
}
