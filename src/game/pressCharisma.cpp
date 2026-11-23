#include "pressCharisma.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "log.h"
#include "matchTelemetry.h"
#include "menuBackground.h"
#include "menus.h"
#include "render.h"
#include "sponsorChallenge.h"
#include "stats.h"
#include "swos.h"
#include "text.h"
#include "pressConference.mnu.h"

#include <SimpleIni.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

using namespace PressConferenceMenu;

namespace {

CareerPressState s_state;
PressConferencePrompt s_activePrompt;
PressMatchContext s_lastPreContext;
uint64_t s_pendingPreRequestId = 0;
uint64_t s_pendingPostRequestId = 0;
bool s_matchEligible = false;
bool s_humanIsTeam1 = true;
uint32_t s_pressSeed = 1;

// Matchday gate receipts multiplier tracking
int32_t s_preMatchGateReceipts = 0;
bool s_gateReceiptsPending = false;
int32_t s_lastGateReceiptsDelta = 0;

// Season bonus one-shot guard (per season number + teamId)
int32_t s_lastBonusSeasonKey = -1;

// Native menu interaction state
bool s_showingReaction = false;
int s_selectedOptionIdx = -1;
int16_t s_cpBeforeChoice = 50;
int8_t s_lastAppliedDelta = 0;
Uint32 s_reactionShownTick = 0;
bool s_prevKeyDown[6] = { false, false, false, false, false, false }; // 1, 2, 3, 4, ESC, Enter/Space

std::string trimStr(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpperStr(const std::string& s)
{
    std::string out = sanitizeCompactMixedCase(s);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string sanitizePressField(const std::string& in, size_t maxLen)
{
    std::string out = sanitizeCompactMixedCase(in);
    if (out.size() > maxLen) {
        out.resize(maxLen);
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }
    return out;
}

std::string replaceTokens(std::string tmpl, const std::string& club, const std::string& opp, const std::string& score)
{
    auto sub = [](std::string& s, const char *tok, const std::string& val) {
        size_t tokLen = std::strlen(tok);
        size_t pos = 0;
        while ((pos = s.find(tok, pos)) != std::string::npos) {
            s.replace(pos, tokLen, val);
            pos += val.size();
        }
    };
    sub(tmpl, "{CLUB}", club.empty() ? "your club" : club);
    sub(tmpl, "{OPP}", opp.empty() ? "the opposition" : opp);
    sub(tmpl, "{SCORE}", score.empty() ? "today's scoreline" : score);
    return tmpl;
}

ManagerialStance parseStanceString(const std::string& raw)
{
    std::string u = toUpperStr(trimStr(raw));
    if (u.find("HUMBLE") != std::string::npos || u.find("CAUTIOUS") != std::string::npos || u.find("MODEST") != std::string::npos)
        return ManagerialStance::kHumble;
    if (u.find("CONFIDENT") != std::string::npos || u.find("BOAST") != std::string::npos || u.find("BOLD") != std::string::npos)
        return ManagerialStance::kConfident;
    if (u.find("TACTICAL") != std::string::npos || u.find("ANALYTIC") != std::string::npos)
        return ManagerialStance::kTactical;
    if (u.find("HOSTILE") != std::string::npos || u.find("MIND") != std::string::npos || u.find("FIERY") != std::string::npos || u.find("AGGRESSIVE") != std::string::npos)
        return ManagerialStance::kHostile;
    return ManagerialStance::kNone;
}

std::string readCurrentManagerName()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        std::string full = trimStr(std::string(p.managerName) + " " + std::string(p.managerSurname));
        if (!full.empty())
            return sanitizePressField(full, 28);
    }
    char firstBuf[10] = {};
    char surBuf[14] = {};
    std::memcpy(firstBuf, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 9);
    std::memcpy(surBuf, &SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 13);
    std::string full = trimStr(std::string(firstBuf) + " " + std::string(surBuf));
    return full.empty() ? "Manager" : sanitizePressField(full, 28);
}

int readDivisionTierLocal()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers())
        return static_cast<int>(mgr.activePlayer().careerTeamLeague);
    return static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]));
}

bool resolveHumanCareerSideForPress(const TeamGame *topTeam, const TeamGame *bottomTeam, bool& outHumanIsTeam1)
{
    if (!topTeam || !bottomTeam || swos.g_trainingGame || swos.g_gameType != kGameTypeCareer)
        return false;

    const TeamGeneralInfo *team1Info = (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame)
        ? &swos.topTeamData : &swos.bottomTeamData;
    const TeamGeneralInfo *team2Info = (team1Info == &swos.topTeamData)
        ? &swos.bottomTeamData : &swos.topTeamData;

    bool t1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0 || team1Info->isPlCoach != 0);
    bool t2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0 || team2Info->isPlCoach != 0);

    if (!t1Human && !t2Human) {
        const TeamFile *tf1 = swos.gameTeam1.asAligned();
        const TeamFile *tf2 = swos.gameTeam2.asAligned();
        if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
        if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();
        if (tf1)
            t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2)
            t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }

    if (t1Human == t2Human) {
        std::string careerClub;
        const auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            careerClub = toUpperStr(mgr.activePlayer().teamName);
        } else {
            const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
            if (hdr && hdr->teamName[0] != '\0')
                careerClub = toUpperStr(hdr->teamName);
        }
        if (!careerClub.empty()) {
            bool m1 = (toUpperStr(topTeam->teamName) == careerClub);
            bool m2 = (toUpperStr(bottomTeam->teamName) == careerClub);
            if (m1 != m2) {
                t1Human = m1;
                t2Human = m2;
            }
        }
    }

    if (t1Human == t2Human)
        return false;

    outHumanIsTeam1 = t1Human;
    return true;
}

void syncPressFromHotseatIfActive()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        s_state.charismaPoints = std::clamp<int16_t>(p.pressCharismaPoints, 0, 100);
        s_state.interviewsCompleted = p.pressInterviewsCompleted;
        s_state.lastSeasonCharismaBonus = p.pressLastSeasonBonus;
        s_state.lastPreMatchStance = static_cast<ManagerialStance>(
            std::min<uint8_t>(p.pressLastPreMatchStance, static_cast<uint8_t>(ManagerialStance::kHostile)));
        s_state.lastPreMatchQuote = p.pressLastPreMatchQuote;
    }
}

void syncPressToHotseatIfActive()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        auto& p = mgr.activePlayer();
        p.pressCharismaPoints = s_state.charismaPoints;
        p.pressInterviewsCompleted = s_state.interviewsCompleted;
        p.pressLastSeasonBonus = s_state.lastSeasonCharismaBonus;
        p.pressLastPreMatchStance = static_cast<uint8_t>(s_state.lastPreMatchStance);
        p.pressLastPreMatchQuote = s_state.lastPreMatchQuote;
    }
}

void pushRecentFormResult(int clubGoals, int oppGoals)
{
    char tag = (clubGoals > oppGoals) ? 'W' : ((clubGoals < oppGoals) ? 'L' : 'D');
    std::string entry = std::string(1, tag) + " " + std::to_string(clubGoals) + "-" + std::to_string(oppGoals);
    s_state.recentForm.push_back(std::move(entry));
    while (s_state.recentForm.size() > 5)
        s_state.recentForm.erase(s_state.recentForm.begin());
}

std::string formatRecentFormLine(const std::vector<std::string>& form)
{
    if (form.empty())
        return "None yet";
    std::string out;
    for (size_t i = 0; i < form.size(); ++i) {
        if (i > 0) out += ", ";
        out += form[i];
    }
    return out;
}

bool waitForPressLlmWithSpinner(uint64_t reqId, int maxWaitMs, const char *label, std::string& outReply)
{
    if (reqId == 0)
        return false;
    if (tryTakeLlmResponse(reqId, outReply))
        return true;

#ifdef SWOS_TEST
    return waitForLlmResponse(reqId, maxWaitMs, outReply);
#else
    static const char kSpin[4] = { '|', '/', '-', '\\' };
    Uint32 start = SDL_GetTicks();
    Uint32 deadline = start + static_cast<Uint32>(std::clamp(maxWaitMs, 250, 4000));
    int frame = 0;

    setStandardMenuBackgroundImage();
    while (SDL_GetTicks() < deadline) {
        if (tryTakeLlmResponse(reqId, outReply))
            return true;

        drawMenuBackground();
        drawDarkRectangle({ 30.0f, 76.0f, 260.0f, 48.0f }, true);

        int spinIdx = (frame / 3) & 3;
        int dotsCount = (frame / 6) & 3;
        std::string dots(static_cast<size_t>(dotsCount), '.');
        std::string line1 = std::string("[") + kSpin[spinIdx] + "] " + label + dots;

        SDL_Renderer *r = getRenderer();
        SDL_Texture *atlas = getCompactFontAtlas(r);
        if (r && atlas) {
            float gameScale = getGameScale();
            float px = computeCompactPixelScaleForVga(gameScale, 0.48f);
            float centerX = getGameScreenOffsetX() + (static_cast<float>(kVgaWidth) * 0.5f) * gameScale;
            float baseY = getGameScreenOffsetY() + 76.0f * gameScale;
            float origSX = 1.0f, origSY = 1.0f;
            SDL_RenderGetScale(r, &origSX, &origSY);
            SDL_RenderSetScale(r, 1.0f, 1.0f);

            drawCompactTextCentered(r, atlas, centerX, baseY + 10.0f * gameScale, px,
                line1, { 255, 226, 78, 255 }, 255, true);
            drawCompactTextCentered(r, atlas, centerX, baseY + 22.0f * gameScale, px,
                "Journalists are preparing their question...", { 244, 246, 248, 255 }, 245, false);
            drawCompactTextCentered(r, atlas, centerX, baseY + 34.0f * gameScale, px,
                "Press ESC to use instant fallback", { 176, 210, 238, 255 }, 230, false);

            SDL_RenderSetScale(r, origSX, origSY);
        }

        updateScreen(false);
        SDL_Delay(33);
        processControlEvents();

        if (SDL_GetTicks() - start > 200) {
            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
            if (keys && keys[SDL_SCANCODE_ESCAPE])
                return false;
        }
        ++frame;
    }
    return tryTakeLlmResponse(reqId, outReply);
#endif
}

struct OfflinePressTemplate {
    const char *category; // DERBY, UNDERDOG, FAVORITE, GENERAL, BIG_WIN, HEAVY_LOSS, DRAW, NARROW_WIN, NARROW_LOSS, CONFIDENT_BACKFIRE, CONFIDENT_VINDICATED
    const char *journalist;
    const char *question;
    struct Opt {
        ManagerialStance stance;
        const char *text;
        int8_t delta;
        const char *reaction;
    } opts[4];
};

// 42 context-tagged offline templates (20 pre-match + 22 post-match)
static const OfflinePressTemplate kPreMatchTemplates[] = {
    { "DERBY", "Martin Tyler (Daily Sport)",
      "Derby day against {OPP}! The whole city is electric. Can {CLUB} handle the heat?",
      { { ManagerialStance::kHumble, "Derbies are decided by small margins. We respect {OPP} deeply.", 2, "The press appreciates your calm professionalism." },
        { ManagerialStance::kConfident, "This city belongs to {CLUB}. We are going out to dominate them!", 4, "Supporters roar with pride at your fearless derby battle cry!" },
        { ManagerialStance::kTactical, "Winning the midfield duels and keeping compact lines will decide it.", 3, "Pundits nod at your sharp tactical composure." },
        { ManagerialStance::kHostile, "Ask {OPP}'s boss if he slept last night - we are not worried.", -2, "Sparks fly in the press room as headlines write themselves!" } } },
    { "DERBY", "Murat Kaya (Spor Gazetesi)",
      "Huge rivalry clash with {OPP} today. Are your players feeling the pressure from the terraces?",
      { { ManagerialStance::kHumble, "Every supporter expects passion, and we will give 100 percent.", 2, "A steady, well-received answer from the manager." },
        { ManagerialStance::kConfident, "Pressure is for {OPP}, not us. Our boys thrive on derby nights!", 4, "Fans love the bold swagger ahead of kickoff!" },
        { ManagerialStance::kTactical, "We trained all week on set pieces and quick transitions for {OPP}.", 3, "Reporters praise your detailed match preparation." },
        { ManagerialStance::kHostile, "Stop inventing drama. We do our talking on the grass.", -4, "A frosty exchange leaves reporters scribbling furiously." } } },
    { "DERBY", "Piet van Dijk (Euro Football)",
      "Rivalry matches against {OPP} can make or break a season. What is your message to the fans?",
      { { ManagerialStance::kHumble, "Stay behind the lads for 90 minutes; {OPP} will test us.", 2, "Warm applause from the local beat writers." },
        { ManagerialStance::kConfident, "Get ready to celebrate tonight - {CLUB} will own this pitch!", 5, "Front-page material! Ticket demand is sky-high!" },
        { ManagerialStance::kTactical, "Discipline and keeping eleven men on the pitch will be decisive.", 3, "Measured leadership that impresses neutral observers." },
        { ManagerialStance::kHostile, "{OPP} always talk loud before kickoff and vanish after it.", -3, "Explosive mind games! Rival fans are fuming." } } },
    { "DERBY", "Claire Bennett (World Soccer)",
      "History weighs heavily whenever {CLUB} faces {OPP}. Are you ready for a war out there?",
      { { ManagerialStance::kHumble, "We know what this fixture means and we have worked tirelessly.", 2, "Respectful and grounded before a fierce battle." },
        { ManagerialStance::kConfident, "We do not fear history - today we write a new chapter for {CLUB}!", 4, "Charismatic rallying cry that fires up the dressing room." },
        { ManagerialStance::kTactical, "We have spotted spaces behind {OPP}'s full-backs to exploit.", 3, "Analysts love the clear gameplan." },
        { ManagerialStance::kHostile, "If they want a physical battle, they picked the wrong club.", -2, "Tough-talking stance divides the press corps." } } },
    { "DERBY", "Gianluca Rossi (Calcio 97)",
      "All eyes are on {CLUB} vs {OPP} today. Who has the psychological edge before kickoff?",
      { { ManagerialStance::kHumble, "Both sides start level at 0-0. We must earn every inch.", 2, "Classic dignified pre-match demeanor." },
        { ManagerialStance::kConfident, "Look at our eyes in the tunnel - the edge is 100 percent ours!", 4, "Pure box-office confidence for the TV cameras." },
        { ManagerialStance::kTactical, "Controlling tempo in the opening twenty minutes is the key.", 2, "Calm and cerebral breakdown of the derby." },
        { ManagerialStance::kHostile, "Their manager looks nervous already. That tells you enough.", -3, "Cheeky mind games stir up a media storm." } } },

    { "FAVORITE", "Brian Moore (Matchday Echo)",
      "Everyone expects {CLUB} to brush {OPP} aside today. Could complacency be your biggest rival?",
      { { ManagerialStance::kHumble, "There are no easy games in football. {OPP} deserve full respect.", 3, "Smart management keeping feet firmly on the ground." },
        { ManagerialStance::kConfident, "When {CLUB} plays at full tempo, nobody in this division lives with us!", 4, "Bold statement that delights the home crowd." },
        { ManagerialStance::kTactical, "Against deep blocks, patience and width will unlock {OPP}.", 3, "Crisp tactical clarity admired by pundits." },
        { ManagerialStance::kHostile, "Do not insult my players by suggesting they lack hunger.", -4, "Testy snap creates awkward tension in the press room." } } },
    { "FAVORITE", "Sarah Jenkins (Sunday Football)",
      "Bookmakers have {CLUB} as heavy favourites against {OPP}. Are three points guaranteed?",
      { { ManagerialStance::kHumble, "Points are won on grass, never on paper. We must stay sharp.", 3, "Textbook leadership respected across the league." },
        { ManagerialStance::kConfident, "We are hunting a big win and entertaining football today!", 4, "Supporters love an ambitious, attacking promise." },
        { ManagerialStance::kTactical, "High pressing early should force mistakes in {OPP}'s half.", 2, "Focused tactical insight." },
        { ManagerialStance::kHostile, "Bookmakers know nothing about football. Next question.", -5, "Dismissive tone costs you goodwill with the media." } } },
    { "FAVORITE", "Hans Muller (Kicker 97)",
      "{CLUB} are flying high right now. Can {OPP} possibly spring a shock today?",
      { { ManagerialStance::kHumble, "Football always punishes arrogance. We prepare for {OPP}'s best.", 3, "Wise words that earn broad respect." },
        { ManagerialStance::kConfident, "Not on our watch! Expect goals and total control from {CLUB}.", 4, "Confident swagger that grabs tomorrow's back pages." },
        { ManagerialStance::kTactical, "Preventing counter-attacks while pushing numbers forward is vital.", 3, "Sharp strategic answer." },
        { ManagerialStance::kHostile, "If we lose to them you can write whatever nonsense you like.", -4, "Risky hostility raises eyebrows among journalists." } } },
    { "FAVORITE", "David Coleman (BBC Sport)",
      "Fans are turning up expecting goals from {CLUB} against {OPP}. Will you go all-out attack?",
      { { ManagerialStance::kHumble, "Winning comes first, whether 1-0 or 4-0. Respect to {OPP}.", 2, "Pragmatic and sensible." },
        { ManagerialStance::kConfident, "We always play to thrill the terraces - buckle up!", 5, "Electric charisma! The whole press room smiles." },
        { ManagerialStance::kTactical, "Balance between attacking flair and defensive cover is everything.", 3, "Solid, balanced coaching philosophy." },
        { ManagerialStance::kHostile, "You reporters complain when we attack and complain when we defend.", -5, "Snapping at the press hurts your public image." } } },
    { "FAVORITE", "Kemal Demir (Futbol Dunyasi)",
      "Your squad looks stronger on paper than {OPP}. Are you resting any key stars today?",
      { { ManagerialStance::kHumble, "Every single player in our squad must fight for this shirt.", 3, "Great team-first message." },
        { ManagerialStance::kConfident, "Whoever wears the {CLUB} shirt today will outclass {OPP}!", 4, "Inspiring confidence in the entire squad." },
        { ManagerialStance::kTactical, "We picked the exact XI suited to {OPP}'s defensive shape.", 2, "Methodical selection logic." },
        { ManagerialStance::kHostile, "My team sheet is none of your business until kickoff.", -4, "Cold shoulder annoys the reporters." } } },

    { "UNDERDOG", "John Motson (Match of the Day)",
      "Many pundits are writing {CLUB} off against a strong {OPP} side. How do you plan to upset them?",
      { { ManagerialStance::kHumble, "{OPP} are a top side, but with heart and unity anything can happen.", 3, "Underdog spirit wins hearts across the country!" },
        { ManagerialStance::kConfident, "Let them underestimate us - {CLUB} are going to shock everyone!", 5, "Fearless David-vs-Goliath swagger! Fans adore it." },
        { ManagerialStance::kTactical, "Stay compact, close the gaps, and hit {OPP} hard on the break.", 3, "Smart counter-attacking blueprint." },
        { ManagerialStance::kHostile, "Pundits sit in warm studios and know nothing about real fight.", -3, "Combative tone grabs headlines but ruffles feathers." } } },
    { "UNDERDOG", "Elena Alvarez (Marca 97)",
      "Facing {OPP} is a daunting test for {CLUB}. Would you happily take a draw right now?",
      { { ManagerialStance::kHumble, "A point would be valuable, though we will always compete to win.", 2, "Honest and realistic assessment." },
        { ManagerialStance::kConfident, "Never! We step onto the pitch to beat {OPP}, not to survive!", 4, "Brave mentality that lifts the supporters." },
        { ManagerialStance::kTactical, "If we frustrate their midfield early, the crowd gets restless.", 3, "Astute game management." },
        { ManagerialStance::kHostile, "What a defeatist question. We did not travel here to roll over.", -2, "Fiery edge shows passion, though a bit prickly." } } },
    { "UNDERDOG", "Barry Davies (Sportsnight)",
      "Your recent run has been tough, and now {OPP} await. Is morale still intact in the dressing room?",
      { { ManagerialStance::kHumble, "Tough spells test character, and these lads are sticking together.", 3, "Dignified defence of your squad earns admiration." },
        { ManagerialStance::kConfident, "Today is the turning point - watch {CLUB} silence every doubter!", 4, "Bold rally that sparks belief." },
        { ManagerialStance::kTactical, "We simplified our defensive shape this week to stop cheap goals.", 2, "Reassuringly practical." },
        { ManagerialStance::kHostile, "Morale is fine. The only negativity comes from your headlines.", -5, "Defensive lash-out invites harsher scrutiny." } } },
    { "UNDERDOG", "Marco Bellini (Gazzetta)",
      "{OPP} have dangerous attackers. How will {CLUB} stop them running riot today?",
      { { ManagerialStance::kHumble, "Every player will have to defend as a unit and fight for every ball.", 2, "Respectful and gritty." },
        { ManagerialStance::kConfident, "Attack is our best defence - let {OPP} worry about stopping us!", 4, "Thrilling bravery that excites neutral fans." },
        { ManagerialStance::kTactical, "Tight marking in the box and cutting supply lines from the wings.", 3, "Tactical discipline wins praise." },
        { ManagerialStance::kHostile, "We are not afraid of big names. Reputations do not score goals.", -1, "Blunt and defiant." } } },
    { "UNDERDOG", "Ahmet Yilmaz (Tribun 97)",
      "Critics say {CLUB} lack firepower against teams like {OPP}. Your reply?",
      { { ManagerialStance::kHumble, "We are working hard in finishing drills and improving every week.", 2, "Humble, constructive tone." },
        { ManagerialStance::kConfident, "Keep watching - our strikers are ready to explode today!", 4, "Strong public backing boosts striker confidence." },
        { ManagerialStance::kTactical, "Creating better final-third angles will bring the goals naturally.", 3, "Calm analytical response." },
        { ManagerialStance::kHostile, "Critics should try scoring in front of 40,000 screaming fans.", -4, "Antagonizing the press lowers your standing." } } },

    { "GENERAL", "Des Lynam (Grandstand)",
      "Welcome ahead of {CLUB} versus {OPP}. What will be the decisive factor out there today?",
      { { ManagerialStance::kHumble, "Hard work, humility, and taking our chances when they arrive.", 2, "Well-spoken and respected." },
        { ManagerialStance::kConfident, "Our tempo and winning mentality - {CLUB} are ready to shine!", 4, "Upbeat charm wins over the room." },
        { ManagerialStance::kTactical, "Winning second balls and controlling the pace through midfield.", 3, "Insightful coaching take." },
        { ManagerialStance::kHostile, "Hopefully the referee actually watches the match this time.", -4, "Pre-match swipe at officials draws disapproval." } } },
    { "GENERAL", "Laura Hughes (FourFourTwo)",
      "How has training been this week leading up to {OPP}?",
      { { ManagerialStance::kHumble, "Very focused. The lads know {OPP} will make us work hard.", 2, "Steady and grounded." },
        { ManagerialStance::kConfident, "Electric! The boys are flying and hungry for three points!", 4, "Infectious enthusiasm fans love to hear." },
        { ManagerialStance::kTactical, "Sharp sessions on pressing triggers and defending set pieces.", 3, "Professional and detailed." },
        { ManagerialStance::kHostile, "Training is private. Watch the match if you want answers.", -4, "Curt reply chills the press conference." } } },
    { "GENERAL", "Bulent Karpat (Star Spor)",
      "Supporters have been debating tactics all week before {OPP}. Any surprises planned?",
      { { ManagerialStance::kHumble, "No magic tricks, just honest graft for the badge.", 2, "Classic fan-friendly humility." },
        { ManagerialStance::kConfident, "We play on the front foot and let {OPP} chase shadows!", 4, "Confident flair that lights up the broadcast." },
        { ManagerialStance::kTactical, "A couple of midfield adjustments to win territory higher up.", 3, "Intrigues the tactical analysts." },
        { ManagerialStance::kHostile, "Even if I had surprises, I would not tell you lot.", -3, "Grumpy jab gets mixed laughs." } } },
    { "GENERAL", "Pierre Moreau (L'Equipe 97)",
      "This match against {OPP} could build real momentum for {CLUB}. How vital is today?",
      { { ManagerialStance::kHumble, "Every match is a cup final in a long season. One game at a time.", 2, "Composed and seasoned." },
        { ManagerialStance::kConfident, "Win today and this stadium will believe anything is possible!", 5, "Charismatic headline quote!" },
        { ManagerialStance::kTactical, "Consistency in our defensive spacing is what builds streaks.", 3, "Smart long-term perspective." },
        { ManagerialStance::kHostile, "You call every single week decisive. Let us just play football.", -4, "Impatient answer rubs journalists the wrong way." } } },
    { "GENERAL", "Gary Lineker (BBC Studio)",
      "Everything is set for {CLUB} against {OPP}. Are you enjoying the challenge right now?",
      { { ManagerialStance::kHumble, "It is a privilege to manage {CLUB}. We give everything today.", 3, "Warm, classy response loved by all." },
        { ManagerialStance::kConfident, "Loving every second - especially when we take three points!", 4, "Witty, confident charm!" },
        { ManagerialStance::kTactical, "Solving tactical puzzles like {OPP} is why we love this job.", 3, "Thoughtful and engaging." },
        { ManagerialStance::kHostile, "I enjoy football. Press conferences, not so much.", -4, "Blunt humour, though reporters mark you down." } } },
};

static const OfflinePressTemplate kPostMatchTemplates[] = {
    { "CONFIDENT_BACKFIRE", "Piet van Dijk (Daily Sport)",
      "You talked so boldly before kickoff, yet {CLUB} fell {SCORE} to {OPP}. Do you regret those words?",
      { { ManagerialStance::kHumble, "I got it wrong today. {OPP} were better and I take full responsibility.", 4, "The press respects your rare honesty after eating humble pie." },
        { ManagerialStance::kTactical, "We lost shape in key moments and {OPP} punished us. Back to work.", 1, "Calm damage control after a painful setback." },
        { ManagerialStance::kConfident, "One battle lost, not the war. Judge {CLUB} at season's end!", -3, "Defiant, though critics call it stubborn after a defeat." },
        { ManagerialStance::kHostile, "Next question. I am not answering ridiculous traps.", -7, "Snapping after a failed boast causes a PR disaster!" } } },
    { "CONFIDENT_BACKFIRE", "Murat Kaya (Spor Gazetesi)",
      "After promising fireworks before kickoff, {SCORE} against {OPP} is a bitter pill. What went wrong?",
      { { ManagerialStance::kHumble, "My words raised expectations and we fell short. Apologies to our fans.", 4, "Supporters appreciate owning up to it." },
        { ManagerialStance::kTactical, "Our finishing let us down and our transitions were too slow.", 1, "Honest technical analysis." },
        { ManagerialStance::kConfident, "We will bounce back stronger next week - write that down!", -2, "Bold, but reporters remain sceptical tonight." },
        { ManagerialStance::kHostile, "Blame the pitch, blame the ref, blame whatever sells your papers.", -7, "Excuses and anger tank your media reputation." } } },
    { "CONFIDENT_BACKFIRE", "Martin Tyler (Matchday Echo)",
      "Your pre-match swagger backfired badly in this {SCORE} defeat to {OPP}. Is your credibility dented?",
      { { ManagerialStance::kHumble, "Credit to {OPP}. I always back my lads, and today we learn from it.", 3, "Graceful recovery under intense scrutiny." },
        { ManagerialStance::kTactical, "Individual errors decided it, not a lack of belief.", 0, "Neutral, guarded reply." },
        { ManagerialStance::kConfident, "I will never stop backing {CLUB} to win every match!", -2, "Loyalty admired by some, mocked by rival press." },
        { ManagerialStance::kHostile, "You have been waiting months to ask that. Enjoy your headline.", -6, "Bitter exchange dominates tomorrow's back pages." } } },

    { "CONFIDENT_VINDICATED", "Des Lynam (Grandstand)",
      "You backed {CLUB} boldly before kickoff and delivered {SCORE} against {OPP}! Pure box office!",
      { { ManagerialStance::kConfident, "I told you! When {CLUB} believes, nobody can live with us!", 7, "ICONIC! The press room erupts - pure managerial aura!" },
        { ManagerialStance::kHumble, "All credit goes to the players and our incredible fans today.", 5, "Classy humility after backing up your bold prediction!" },
        { ManagerialStance::kTactical, "Every single tactical trigger we worked on clicked to perfection.", 4, "Masterclass reputation soaring!" },
        { ManagerialStance::kHostile, "Tell the doubters to stay quiet next time!", -1, "Fiery gloating wins ultras, cools neutral writers." } } },
    { "CONFIDENT_VINDICATED", "Sarah Jenkins (World Soccer)",
      "Talked the talk and walked the walk! {SCORE} over {OPP} proves you meant every word!",
      { { ManagerialStance::kConfident, "This is just the beginning for {CLUB}. Enjoy the ride!", 7, "Superstar charisma! Commercial interest is booming!" },
        { ManagerialStance::kHumble, "The squad made my words look smart. Proud of every one of them.", 5, "Winning grace makes you a media darling." },
        { ManagerialStance::kTactical, "We controlled the zones we targeted and finished ruthlessly.", 4, "Pundits hail a tactical triumph." },
        { ManagerialStance::kHostile, "Maybe next week you will ask better questions.", -2, "Unnecessary needle slightly sours a golden night." } } },
    { "CONFIDENT_VINDICATED", "Kemal Demir (Futbol Dunyasi)",
      "A magnificent {SCORE} triumph over {OPP} after your fearless pre-match promise! How does it feel?",
      { { ManagerialStance::kConfident, "Unbelievable! Remember tonight - {CLUB} are unstoppable!", 6, "Electric soundbite replayed all week on TV!" },
        { ManagerialStance::kHumble, "Joy for the supporters first and foremost. Now we stay focused.", 4, "Grounded class." },
        { ManagerialStance::kTactical, "Collective football and spacing won us this match cleanly.", 4, "Pundits love the analytical clarity." },
        { ManagerialStance::kHostile, "Now go ask {OPP}'s manager how he feels!", -2, "Cheeky needle gets headlines but costs dignity." } } },

    { "BIG_WIN", "John Motson (BBC Sport)",
      "What a demolition! {CLUB} crushed {OPP} {SCORE}. Is that the best display of your reign?",
      { { ManagerialStance::kConfident, "Total football! Nights like this are why fans pack the stadium!", 6, "Headline gold! Fans and sponsors are thrilled!" },
        { ManagerialStance::kHumble, "The players were magnificent, and we stay hungry for the next one.", 4, "Classy winner's poise." },
        { ManagerialStance::kTactical, "Our pressing and movement off the ball tore {OPP} apart.", 4, "Tactical genius narrative builds!" },
        { ManagerialStance::kHostile, "We could have scored two more if the referee had woken up.", -3, "Complaining after a rout baffles reporters." } } },
    { "BIG_WIN", "Gianluca Rossi (Calcio 97)",
      "A resounding {SCORE} victory against {OPP}! Are rival clubs starting to fear {CLUB}?",
      { { ManagerialStance::kConfident, "If they were not worried before kickoff, they certainly are now!", 6, "Swashbuckling charisma steals the spotlight!" },
        { ManagerialStance::kHumble, "We only focus on ourselves, but tonight was special for our fans.", 4, "Respectful and well-liked." },
        { ManagerialStance::kTactical, "Dominating territory and ruthless finishing made the difference.", 3, "Crisp summary." },
        { ManagerialStance::kHostile, "Let them fear us or hate us - I do not care either way.", -2, "Edgy quote sparks lively debate." } } },
    { "BIG_WIN", "Hans Muller (Kicker 97)",
      "{SCORE} against {OPP} in style! How proud are you of your attacking play today?",
      { { ManagerialStance::kConfident, "Pure entertainment! That is the {CLUB} way!", 5, "Crowd-pleasing charisma!" },
        { ManagerialStance::kHumble, "Every defender and midfielder helped create those goals too.", 4, "Team-first leadership earns high marks." },
        { ManagerialStance::kTactical, "Switching play quickly stretched {OPP} all afternoon.", 3, "Smart tactical breakdown." },
        { ManagerialStance::kHostile, "Finally you journalists have something positive to write.", -4, "Picking fights even in victory hurts charisma." } } },
    { "BIG_WIN", "Bulent Karpat (Star Spor)",
      "Spectacular {SCORE} win over {OPP}! The terraces are still singing your name!",
      { { ManagerialStance::kConfident, "Sing louder! Together we are building something unforgettable!", 6, "Massive boost with both fans and TV networks!" },
        { ManagerialStance::kHumble, "They carried us from the first whistle. This victory is theirs.", 5, "Heartfelt tribute wins universal praise." },
        { ManagerialStance::kTactical, "Clinical execution of our match plan from minute one.", 3, "Composed and professional." },
        { ManagerialStance::kHostile, "Remember this scoreline next time you doubt my selection.", -3, "Petty dig dims the celebration slightly." } } },

    { "NARROW_WIN", "Barry Davies (Sportsnight)",
      "Hard-fought {SCORE} victory over {OPP}. Winning ugly or winning smart?",
      { { ManagerialStance::kHumble, "Full credit to {OPP}, they pushed us all the way. Proud of our grit.", 4, "Gracious in a tight win." },
        { ManagerialStance::kConfident, "Champions find a way to win even in trenches like that!", 5, "Winning mentality shines through!" },
        { ManagerialStance::kTactical, "We managed the final ten minutes superbly and shut down spaces.", 3, "Pundits praise your game management." },
        { ManagerialStance::kHostile, "Three points are three points. Stop complaining about style.", -3, "Prickly tone dampens the mood." } } },
    { "NARROW_WIN", "Laura Hughes (FourFourTwo)",
      "A tense {SCORE} triumph against {OPP}! Your heart must have been pounding late on!",
      { { ManagerialStance::kHumble, "My heart is still racing! Huge character from the boys.", 4, "Relatable warmth charms the press!" },
        { ManagerialStance::kConfident, "I never doubted them for a second. We know how to close games!", 4, "Cool confidence." },
        { ManagerialStance::kTactical, "Keeping defensive discipline when legs tired won us the points.", 3, "Measured tactical insight." },
        { ManagerialStance::kHostile, "If you want boring matches, go watch chess.", -2, "Blunt quip splits the room." } } },
    { "NARROW_WIN", "Pierre Moreau (L'Equipe 97)",
      "Edge-of-the-seat {SCORE} win against {OPP}. What pleased you most today?",
      { { ManagerialStance::kHumble, "The togetherness. Everyone fought for the teammate beside them.", 4, "Inspiring leadership." },
        { ManagerialStance::kConfident, "The ruthless edge when it mattered most. That is {CLUB}!", 5, "Charismatic winner's aura." },
        { ManagerialStance::kTactical, "Our goalkeeper and back line stayed calm under heavy pressure.", 3, "Fair tactical credit." },
        { ManagerialStance::kHostile, "Leaving this press room with three points in my pocket.", -4, "Rude exit costs charisma points." } } },

    { "DRAW", "Gary Lineker (BBC Studio)",
      "Honours even at {SCORE} against {OPP}. Two points dropped or a fair share?",
      { { ManagerialStance::kHumble, "Both sides fought hard; a draw is probably fair on reflection.", 2, "Fair and sporting assessment." },
        { ManagerialStance::kConfident, "We created enough to win it - next time we bury those chances!", 3, "Positive ambition fans respect." },
        { ManagerialStance::kTactical, "A real tactical chess match; small details decided both ends.", 3, "Thoughtful pundit-friendly take." },
        { ManagerialStance::kHostile, "{OPP} parked the bus for 90 minutes and killed the spectacle.", -4, "Sour grapes draws criticism." } } },
    { "DRAW", "Elena Alvarez (Marca 97)",
      "It finishes {SCORE} against {OPP}. Are you satisfied with the point?",
      { { ManagerialStance::kHumble, "We never love dropping points, but we take the point and move on.", 2, "Sensible perspective." },
        { ManagerialStance::kConfident, "Never satisfied with draws at {CLUB}, but we stay unbeaten!", 3, "High standards admired by supporters." },
        { ManagerialStance::kTactical, "We controlled possession well; now we sharpen the final pass.", 3, "Clear constructive focus." },
        { ManagerialStance::kHostile, "We would have won easily with a competent referee.", -6, "Blaming officials damages your image." } } },
    { "DRAW", "Ahmet Yilmaz (Tribun 97)",
      "A tight {SCORE} stalemate with {OPP}. What was missing in the final third?",
      { { ManagerialStance::kHumble, "A touch of luck and calmer finishing. The effort was faultless.", 2, "Protective of players, calm tone." },
        { ManagerialStance::kConfident, "Only the final inch - the goals are coming very soon!", 3, "Upbeat belief." },
        { ManagerialStance::kTactical, "Better movement in the box and quicker crosses from wide areas.", 3, "Sharp football diagnosis." },
        { ManagerialStance::kHostile, "Maybe ask why {OPP} wasted time from the first half.", -4, "Grumpy deflection." } } },

    { "NARROW_LOSS", "Brian Moore (Matchday Echo)",
      "A heartbreaking {SCORE} defeat to {OPP}. Did {CLUB} deserve more from that match?",
      { { ManagerialStance::kHumble, "Fine margins went {OPP}'s way today. We dust ourselves down.", 3, "Dignity in defeat earns genuine respect." },
        { ManagerialStance::kTactical, "One lapse in concentration cost us dearly. We will fix it.", 2, "Accountable and focused." },
        { ManagerialStance::kConfident, "Play with that spirit and we will win far more than we lose!", 2, "Resilient leadership." },
        { ManagerialStance::kHostile, "Complete daylight robbery. The officials handed {OPP} the match!", -6, "Furious tirade costs heavy charisma." } } },
    { "NARROW_LOSS", "Claire Bennett (World Soccer)",
      "{CLUB} come away empty-handed after losing {SCORE} to {OPP}. How do you lift the squad?",
      { { ManagerialStance::kHumble, "We win together and lose together. Nobody hides at this club.", 3, "Strong, classy backing of your players." },
        { ManagerialStance::kTactical, "Review the video calmly, tighten up set pieces, and go again.", 2, "Professional response." },
        { ManagerialStance::kConfident, "Watch our reaction in the next match - we will come back roaring!", 2, "Defiant optimism." },
        { ManagerialStance::kHostile, "I am not discussing our dressing room with you people.", -5, "Hostility deepens the post-loss gloom." } } },
    { "NARROW_LOSS", "David Coleman (BBC Sport)",
      "Frustration tonight as {OPP} edge {CLUB} {SCORE}. Any regrets on your tactics?",
      { { ManagerialStance::kHumble, "When you lose, the manager looks in the mirror first. Fair play to {OPP}.", 4, "Admirable accountability wins over critics." },
        { ManagerialStance::kTactical, "We had the chances, just lacked clinical composure in front of goal.", 2, "Clear-headed evaluation." },
        { ManagerialStance::kConfident, "Not a single regret - we went for the win and will again!", 1, "Gutsy, though fans wanted points." },
        { ManagerialStance::kHostile, "You have never managed a match in your life. Next!", -6, "Losing your cool sparks negative headlines." } } },

    { "HEAVY_LOSS", "Martin Tyler (Daily Sport)",
      "A chastening {SCORE} defeat against {OPP}. The away fans look stunned. What can you say?",
      { { ManagerialStance::kHumble, "Unacceptable performance and I apologize sincerely to our supporters.", 3, "Owning a heavy defeat prevents a full media meltdown." },
        { ManagerialStance::kTactical, "Our defensive block collapsed far too easily. Urgent work ahead.", 1, "Honest acknowledgement of tactical flaws." },
        { ManagerialStance::kConfident, "Write us off at your peril - {CLUB} will answer back immediately!", -2, "Brave words, though tough to swallow after a thrashing." },
        { ManagerialStance::kHostile, "This interview is over. Write whatever garbage you want!", -8, "Storming out after a heavy defeat destroys press charisma!" } } },
    { "HEAVY_LOSS", "Murat Kaya (Spor Gazetesi)",
      "{OPP} ran away with a {SCORE} victory today. Are alarm bells ringing at {CLUB}?",
      { { ManagerialStance::kHumble, "Full credit to {OPP}. We were second best and owe our fans much better.", 3, "Respectful humility calms the storm." },
        { ManagerialStance::kTactical, "Distance between defence and midfield killed us. We fix it tomorrow.", 1, "Focused on solutions." },
        { ManagerialStance::kConfident, "One freak afternoon changes nothing about our quality!", -3, "Reporters call your reply out of touch." },
        { ManagerialStance::kHostile, "Stop trying to stir up panic! You love kicking a club when it is down!", -7, "Furious outburst dominates the headlines for all the wrong reasons." } } },
    { "HEAVY_LOSS", "John Motson (Match of the Day)",
      "Tough afternoon ending {SCORE} to {OPP}. Does this result dent your season ambitions?",
      { { ManagerialStance::kHumble, "It hurts badly, and we must respond with actions, not excuses.", 3, "Dignified accountability." },
        { ManagerialStance::kTactical, "We made costly mistakes in transition and got punished ruthlessly.", 1, "Objective and calm." },
        { ManagerialStance::kConfident, "Great teams suffer bad days and still lift trophies!", -2, "Mixed reception after a heavy defeat." },
        { ManagerialStance::kHostile, "Ask me again in May if you still have a job.", -8, "Explosive hostility puts your charisma in freefall!" } } },
};

const char *pickOfflineCategory(const PressMatchContext& ctx)
{
    if (ctx.phase == PressPhase::kPreMatch) {
        if (ctx.isDerby)
            return "DERBY";
        if (ctx.leaguePosition > 0 && ctx.opponentPosition > 0) {
            if (ctx.leaguePosition + 4 <= ctx.opponentPosition)
                return "FAVORITE";
            if (ctx.leaguePosition >= ctx.opponentPosition + 4)
                return "UNDERDOG";
        }
        int recentWins = 0, recentLosses = 0;
        for (const auto& f : ctx.recentForm) {
            if (!f.empty() && f[0] == 'W') ++recentWins;
            else if (!f.empty() && f[0] == 'L') ++recentLosses;
        }
        if (recentWins >= 3)
            return "FAVORITE";
        if (recentLosses >= 3)
            return "UNDERDOG";
        return "GENERAL";
    }

    int diff = ctx.clubGoals - ctx.opponentGoals;
    if (ctx.preMatchStance == ManagerialStance::kConfident && diff < 0)
        return "CONFIDENT_BACKFIRE";
    if (ctx.preMatchStance == ManagerialStance::kConfident && diff > 0)
        return "CONFIDENT_VINDICATED";
    if (diff >= 3)
        return "BIG_WIN";
    if (diff <= -3)
        return "HEAVY_LOSS";
    if (diff > 0)
        return "NARROW_WIN";
    if (diff < 0)
        return "NARROW_LOSS";
    return "DRAW";
}

void applySelectedPressOption(int idx)
{
    if (idx < 0 || idx >= 4 || s_showingReaction)
        return;

    const auto& opt = s_activePrompt.options[idx];
    s_selectedOptionIdx = idx;
    s_cpBeforeChoice = s_state.charismaPoints;
    int16_t nextCp = applyCharismaDelta(s_state.charismaPoints, opt.charismaDelta);
    s_lastAppliedDelta = static_cast<int8_t>(nextCp - s_cpBeforeChoice);
    s_state.charismaPoints = nextCp;
    ++s_state.interviewsCompleted;

    if (s_activePrompt.phase == PressPhase::kPreMatch) {
        s_state.lastPreMatchStance = opt.stance;
        s_state.lastPreMatchQuote = opt.text;
    }

    syncPressToHotseatIfActive();

    logInfo("Press conference (%s) choice #%d [%s]: CP %d -> %d (delta %+d)",
        s_activePrompt.phase == PressPhase::kPreMatch ? "PRE" : "POST",
        idx + 1, getManagerialStanceName(opt.stance),
        static_cast<int>(s_cpBeforeChoice), static_cast<int>(s_state.charismaPoints),
        static_cast<int>(s_lastAppliedDelta));

    s_showingReaction = true;
    s_reactionShownTick = SDL_GetTicks();

    // Hide the 4 choice buttons and switch bottom button to CONTINUE
    opt1Entry.hide();
    opt2Entry.hide();
    opt3Entry.hide();
    opt4Entry.hide();
    skipBtnEntry.setBackgroundColor(kGreen);
    skipBtnEntry.copyString("CONTINUE");
    setCurrentEntry(skipBtn);
}

SDL_Color stanceBadgeColor(ManagerialStance s)
{
    switch (s) {
    case ManagerialStance::kHumble:    return { 118, 224, 245, 255 }; // cyan
    case ManagerialStance::kConfident: return { 255, 222, 82, 255 };  // gold
    case ManagerialStance::kTactical:  return { 110, 236, 146, 255 }; // green
    case ManagerialStance::kHostile:   return { 248, 114, 114, 255 }; // red
    default:                           return { 220, 224, 228, 255 };
    }
}

SDL_Color charismaBarColor(int16_t cp)
{
    switch (getCharismaTier(cp)) {
    case CharismaTier::kToxic:       return { 244, 92, 92, 255 };
    case CharismaTier::kUninspiring: return { 236, 168, 76, 255 };
    case CharismaTier::kRespectable: return { 250, 226, 86, 255 };
    case CharismaTier::kCharismatic: return { 104, 234, 142, 255 };
    case CharismaTier::kIconic:      return { 112, 238, 255, 255 };
    default:                         return { 244, 246, 248, 255 };
    }
}

std::string buildPressLlmPrompt(const PressMatchContext& ctx)
{
    std::string p;
    p += "Phase: " + std::string(ctx.phase == PressPhase::kPreMatch ? "PRE_MATCH" : "POST_MATCH") + "\n";
    p += "Manager: " + ctx.managerName + " | Club: " + ctx.clubName + " | Opponent: " + ctx.opponentName + "\n";
    p += "Competition: " + ctx.competition + (ctx.isDerby ? " (BIG DERBY / RIVALRY CLASH!)" : "") + "\n";
    p += "Current Coach Charisma: " + std::to_string(ctx.currentCharisma) + "/100 (" +
         getCharismaTierLabel(getCharismaTier(ctx.currentCharisma)) + " - " +
         getCharismaReputationTitle(getCharismaTier(ctx.currentCharisma)) + ")\n";
    if (!ctx.recentForm.empty())
        p += "Recent Form: " + formatRecentFormLine(ctx.recentForm) + "\n";

    if (ctx.phase == PressPhase::kPostMatch) {
        int diff = ctx.clubGoals - ctx.opponentGoals;
        const char *outcome = diff > 0 ? "WON" : (diff < 0 ? "LOST" : "DREW");
        p += "Final Scoreline: " + ctx.clubName + " " + std::to_string(ctx.clubGoals) + "-" +
             std::to_string(ctx.opponentGoals) + " " + ctx.opponentName + " (" + ctx.clubName + " " + outcome + ")\n";
        if (ctx.clubRedCards > 0 || ctx.opponentRedCards > 0) {
            p += "Red Cards: " + ctx.clubName + " " + std::to_string(ctx.clubRedCards) +
                 ", " + ctx.opponentName + " " + std::to_string(ctx.opponentRedCards) + "\n";
        }
        if (ctx.preMatchStance != ManagerialStance::kNone && !ctx.preMatchQuote.empty()) {
            p += "Manager's Pre-Match Stance: " + std::string(getManagerialStanceName(ctx.preMatchStance)) +
                 " | Pre-Match Quote: \"" + ctx.preMatchQuote + "\"\n";
            p += "IMPORTANT: Hold the manager directly accountable for their pre-match stance/quote vs the actual result!\n";
        }
    }

    p += "\nCreate ONE authentic 1997 football press conference question and 4 distinct managerial reply options "
         "(one HUMBLE, one CONFIDENT, one TACTICAL, one HOSTILE).\n"
         "Output STRICTLY 5 pipe-delimited lines and NOTHING ELSE (no intro, no numbering, no markdown):\n"
         "Line 1: JOURNALIST_NAME_AND_PAPER|QUESTION (max 150 chars)\n"
         "Line 2: HUMBLE|REPLY_TEXT (max 88 chars)|CHARISMA_DELTA (-8 to +8 integer)|MEDIA_REACTION (max 88 chars)\n"
         "Line 3: CONFIDENT|REPLY_TEXT (max 88 chars)|CHARISMA_DELTA (-8 to +8 integer)|MEDIA_REACTION (max 88 chars)\n"
         "Line 4: TACTICAL|REPLY_TEXT (max 88 chars)|CHARISMA_DELTA (-8 to +8 integer)|MEDIA_REACTION (max 88 chars)\n"
         "Line 5: HOSTILE|REPLY_TEXT (max 88 chars)|CHARISMA_DELTA (-8 to +8 integer)|MEDIA_REACTION (max 88 chars)\n"
         "CRITICAL: Keep the STANCE keywords on lines 2-5 strictly in English (HUMBLE, CONFIDENT, TACTICAL, HOSTILE). "
         "Write the journalist name, question, reply texts, and media reactions naturally in the configured language. "
         "Score CHARISMA_DELTA realistically (-8 to +8): e.g., vindicated confidence or gracious humility gains CP, while arrogant excuses after a loss or snapping at reporters loses CP.";
    return p;
}

} // namespace

// --- Native Menu Callbacks (referenced by pressConference.mnu) ---

static void initPressConferenceMenu()
{
    s_showingReaction = false;
    s_selectedOptionIdx = -1;
    s_reactionShownTick = 0;
    std::memset(s_prevKeyDown, 0, sizeof(s_prevKeyDown));

    headerEntry.copyString(s_activePrompt.phase == PressPhase::kPreMatch
        ? "PRE-MATCH PRESS CONFERENCE" : "POST-MATCH PRESS CONFERENCE");

    MenuEntry *opts[4] = { &opt1Entry, &opt2Entry, &opt3Entry, &opt4Entry };
    for (int i = 0; i < 4; ++i) {
        opts[i]->show();
        opts[i]->setBackgroundColor(kDarkBlue);
        opts[i]->copyString("");
    }

    skipBtnEntry.show();
    skipBtnEntry.setBackgroundColor(kRed);
    skipBtnEntry.copyString("SKIP INTERVIEW");
    setCurrentEntry(opt1);
}

static void drawPressConferenceMenu()
{
    // Top Charisma & Journalist bar (y=16..29)
    drawDarkRectangle({ 16.0f, 16.0f, 288.0f, 13.0f }, true);
    // Question card (y=31..68)
    drawDarkRectangle({ 16.0f, 31.0f, 288.0f, 37.0f }, true);

    if (s_showingReaction) {
        drawDarkRectangle({ 16.0f, 72.0f, 288.0f, 104.0f }, true);
    }

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (r && atlas) {
        float gameScale = getGameScale();
        float px = computeCompactPixelScaleForVga(gameScale, 0.44f);
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);

        SDL_Color whiteCol{ 244, 246, 248, 255 };
        SDL_Color yellowCol{ 255, 226, 78, 255 };
        SDL_Color cyanCol{ 126, 232, 244, 255 };
        SDL_Color greenCol{ 96, 236, 134, 255 };
        SDL_Color redCol{ 248, 108, 108, 255 };
        SDL_Color barCol = charismaBarColor(s_state.charismaPoints);

        // Journalist label on top-left
        std::string jLabel = s_activePrompt.journalistName.empty() ? "PRESS CORPS" : s_activePrompt.journalistName;
        if (jLabel.size() > 30) jLabel.resize(30);
        drawCompactText(r, atlas, offX + 21.0f * gameScale, offY + 19.0f * gameScale, px,
            jLabel, cyanCol, 255, true);

        // Charisma readout + compact visual progress bar on top-right
        CharismaTier tier = getCharismaTier(s_state.charismaPoints);
        std::string cpText = "CHARISMA: " + std::to_string(s_state.charismaPoints) + " CP (" +
                             getCharismaTierLabel(tier) + ")";
        drawCompactTextRightAligned(r, atlas, offX + 266.0f * gameScale, offY + 19.0f * gameScale, px,
            cpText, barCol, 255, true);

        // Mini progress bar (26 VGA units wide at x=271..297)
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_FRect barBg{ offX + 271.0f * gameScale, offY + 19.5f * gameScale, 28.0f * gameScale, 6.0f * gameScale };
        SDL_SetRenderDrawColor(r, 24, 28, 32, 220);
        SDL_RenderFillRectF(r, &barBg);
        float fillW = (28.0f * gameScale) * (static_cast<float>(std::clamp<int>(s_state.charismaPoints, 0, 100)) / 100.0f);
        SDL_FRect barFill{ barBg.x, barBg.y, fillW, barBg.h };
        SDL_SetRenderDrawColor(r, barCol.r, barCol.g, barCol.b, 235);
        SDL_RenderFillRectF(r, &barFill);
        SDL_SetRenderDrawColor(r, 180, 190, 196, 160);
        SDL_RenderDrawRectF(r, &barBg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Question text inside Question card
        int qMaxUnits = std::max(80, static_cast<int>((276.0f * gameScale) / px));
        auto qLines = wrapCompactTextUnits(s_activePrompt.question, qMaxUnits);
        float qY = offY + 34.5f * gameScale;
        for (size_t li = 0; li < qLines.size() && li < 4; ++li) {
            drawCompactText(r, atlas, offX + 22.0f * gameScale, qY, px, qLines[li], yellowCol, 255, li == 0);
            qY += 9.5f * px;
        }

        if (!s_showingReaction) {
            // Render the 4 option texts directly over the 4 native clickable option buttons (y=72, 99, 126, 153)
            static constexpr float kOptYs[4] = { 72.0f, 99.0f, 126.0f, 153.0f };
            int optMaxUnits = std::max(80, static_cast<int>((274.0f * gameScale) / px));
            for (int i = 0; i < 4; ++i) {
                const auto& opt = s_activePrompt.options[i];
                std::string prefix = "[" + std::to_string(i + 1) + "] [" + getManagerialStanceName(opt.stance) + "] ";
                SDL_Color badgeCol = stanceBadgeColor(opt.stance);
                float bx = offX + 22.0f * gameScale;
                float by = offY + (kOptYs[i] + 3.0f) * gameScale;

                // Wrap full text with prefix on line 1, or badge + wrapped quote cleanly
                float afterBadgeX = drawCompactText(r, atlas, bx, by, px, prefix, badgeCol, 255, true);
                int badgeUnits = measureCompactTextUnits(prefix) + 2;
                int firstLineUnits = std::max(40, optMaxUnits - badgeUnits);

                // Word-wrap opt.text across line 1 (after badge) and line 2 (full width)
                std::string cleanText = sanitizeCompactMixedCase(opt.text);
                std::istringstream iss(cleanText);
                std::string word, line1, line2;
                bool onLine2 = false;
                while (iss >> word) {
                    if (!onLine2) {
                        std::string cand = line1.empty() ? word : (line1 + " " + word);
                        if (measureCompactTextUnits(cand) <= firstLineUnits) {
                            line1 = std::move(cand);
                        } else {
                            onLine2 = true;
                            line2 = word;
                        }
                    } else {
                        std::string cand = line2.empty() ? word : (line2 + " " + word);
                        if (measureCompactTextUnits(cand) <= optMaxUnits) {
                            line2 = std::move(cand);
                        } else {
                            break;
                        }
                    }
                }
                if (!line1.empty())
                    drawCompactText(r, atlas, afterBadgeX, by, px, line1, whiteCol, 255, false);
                if (!line2.empty())
                    drawCompactText(r, atlas, bx, by + 9.5f * px, px, line2, whiteCol, 255, false);
            }
        } else if (s_selectedOptionIdx >= 0 && s_selectedOptionIdx < 4) {
            const auto& chosen = s_activePrompt.options[s_selectedOptionIdx];
            int panelUnits = std::max(80, static_cast<int>((274.0f * gameScale) / px));
            float rx = offX + 22.0f * gameScale;
            float ry = offY + 78.0f * gameScale;

            std::string stanceHdr = std::string("YOUR RESPONSE [") + getManagerialStanceName(chosen.stance) + "]:";
            drawCompactText(r, atlas, rx, ry, px, stanceHdr, stanceBadgeColor(chosen.stance), 255, true);
            ry += 10.5f * px;

            auto quoteLines = wrapCompactTextUnits(chosen.text, panelUnits);
            for (size_t li = 0; li < quoteLines.size() && li < 2; ++li) {
                drawCompactText(r, atlas, rx, ry, px, quoteLines[li], whiteCol, 255, false);
                ry += 9.5f * px;
            }
            ry += 4.5f * px;

            drawCompactText(r, atlas, rx, ry, px, "MEDIA REACTION:", cyanCol, 255, true);
            ry += 10.5f * px;

            auto reactLines = wrapCompactTextUnits(chosen.mediaReaction, panelUnits);
            for (size_t li = 0; li < reactLines.size() && li < 2; ++li) {
                drawCompactText(r, atlas, rx, ry, px, reactLines[li], whiteCol, 255, false);
                ry += 9.5f * px;
            }
            ry += 6.0f * px;

            SDL_Color deltaCol = (s_lastAppliedDelta > 0) ? greenCol : ((s_lastAppliedDelta < 0) ? redCol : yellowCol);
            char deltaBuf[96];
            std::snprintf(deltaBuf, sizeof(deltaBuf), "CHARISMA IMPACT: %+d CP  ->  %d CP (%s - %s)",
                static_cast<int>(s_lastAppliedDelta), static_cast<int>(s_state.charismaPoints),
                getCharismaTierLabel(tier), getCharismaReputationTitle(tier));
            drawCompactText(r, atlas, rx, ry, px, deltaBuf, deltaCol, 255, true);
            ry += 10.5f * px;

            char multBuf[96];
            std::snprintf(multBuf, sizeof(multBuf), "Gate Receipts: %.2fx  |  End-of-Season TV Bonus Mult: %.2fx",
                getPressAttendanceMultiplier(s_state.charismaPoints),
                getCharismaTvMultiplier(s_state.charismaPoints));
            drawCompactText(r, atlas, rx, ry, px, multBuf, cyanCol, 240, false);
        }

        SDL_RenderSetScale(r, origSX, origSY);
    }

    // Edge-triggered keyboard shortcuts: 1-4 select option, ESC skips, Enter/Space continues reaction
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        bool down[6] = {
            keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1],
            keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2],
            keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3],
            keys[SDL_SCANCODE_4] || keys[SDL_SCANCODE_KP_4],
            keys[SDL_SCANCODE_ESCAPE] != 0,
            keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] || keys[SDL_SCANCODE_SPACE],
        };
        if (!s_showingReaction) {
            for (int i = 0; i < 4; ++i) {
                if (down[i] && !s_prevKeyDown[i]) {
                    applySelectedPressOption(i);
                    break;
                }
            }
            if (down[4] && !s_prevKeyDown[4]) {
                SetExitMenuFlag();
            }
        } else {
            if ((down[4] && !s_prevKeyDown[4]) ||
                (down[5] && !s_prevKeyDown[5] && (SDL_GetTicks() - s_reactionShownTick) > 250)) {
                SetExitMenuFlag();
            }
        }
        for (int i = 0; i < 6; ++i)
            s_prevKeyDown[i] = down[i];
    }
}

static void selectPressOption1() { applySelectedPressOption(0); }
static void selectPressOption2() { applySelectedPressOption(1); }
static void selectPressOption3() { applySelectedPressOption(2); }
static void selectPressOption4() { applySelectedPressOption(3); }
static void skipOrContinuePressConference() { SetExitMenuFlag(); }

// --- Pure Deterministic Logic ---

CharismaTier getCharismaTier(int16_t charismaPoints)
{
    int cp = std::clamp<int>(charismaPoints, 0, 100);
    if (cp < 20) return CharismaTier::kToxic;
    if (cp < 45) return CharismaTier::kUninspiring;
    if (cp < 70) return CharismaTier::kRespectable;
    if (cp < 90) return CharismaTier::kCharismatic;
    return CharismaTier::kIconic;
}

const char *getCharismaTierLabel(CharismaTier tier)
{
    switch (tier) {
    case CharismaTier::kToxic:       return "Toxic";
    case CharismaTier::kUninspiring: return "Uninspiring";
    case CharismaTier::kRespectable: return "Respectable";
    case CharismaTier::kCharismatic: return "Charismatic";
    case CharismaTier::kIconic:      return "Iconic";
    default:                         return "Respectable";
    }
}

const char *getCharismaReputationTitle(CharismaTier tier)
{
    switch (tier) {
    case CharismaTier::kToxic:       return "Media Pariah";
    case CharismaTier::kUninspiring: return "Dull Tactician";
    case CharismaTier::kRespectable: return "Media Friendly";
    case CharismaTier::kCharismatic: return "Box Office Manager";
    case CharismaTier::kIconic:      return "Global Superstar";
    default:                         return "Media Friendly";
    }
}

const char *getManagerialStanceName(ManagerialStance stance)
{
    switch (stance) {
    case ManagerialStance::kHumble:    return "HUMBLE";
    case ManagerialStance::kConfident: return "CONFIDENT";
    case ManagerialStance::kTactical:  return "TACTICAL";
    case ManagerialStance::kHostile:   return "HOSTILE";
    default:                           return "NONE";
    }
}

double getCharismaTvMultiplier(int16_t charismaPoints)
{
    switch (getCharismaTier(charismaPoints)) {
    case CharismaTier::kToxic:       return 0.00;
    case CharismaTier::kUninspiring: return 0.25;
    case CharismaTier::kRespectable: return 0.50;
    case CharismaTier::kCharismatic: return 1.00;
    case CharismaTier::kIconic:      return 1.50;
    default:                         return 0.50;
    }
}

double getPressAttendanceMultiplier(int16_t charismaPoints)
{
    switch (getCharismaTier(charismaPoints)) {
    case CharismaTier::kToxic:       return 0.90;
    case CharismaTier::kUninspiring: return 0.95;
    case CharismaTier::kRespectable: return 1.00;
    case CharismaTier::kCharismatic: return 1.08;
    case CharismaTier::kIconic:      return 1.15;
    default:                         return 1.00;
    }
}

int64_t computeSeasonCharismaBonus(int64_t tvRightsBase, int16_t charismaPoints)
{
    if (tvRightsBase <= 0)
        return 0;
    int cp = std::clamp<int>(charismaPoints, 0, 100);
    double mult = getCharismaTvMultiplier(static_cast<int16_t>(cp));
    if (mult <= 0.0 || cp <= 0)
        return 0;
    return static_cast<int64_t>(std::floor(static_cast<double>(tvRightsBase) * mult * (static_cast<double>(cp) / 100.0)));
}

int16_t applyCharismaDelta(int16_t currentCp, int8_t rawDelta)
{
    int clampedDelta = std::clamp<int>(rawDelta, -8, 8);
    int next = std::clamp<int>(static_cast<int>(currentCp) + clampedDelta, 0, 100);
    return static_cast<int16_t>(next);
}

bool detectClassicDerby(const std::string& clubA, const std::string& clubB, int posA, int posB)
{
    if (posA > 0 && posB > 0 && posA <= 3 && posB <= 3)
        return true;

    std::string a = toUpperStr(clubA);
    std::string b = toUpperStr(clubB);
    static const std::pair<const char *, const char *> kDerbies[] = {
        { "FENERBAHCE", "GALATASARAY" }, { "FENERBAHCE", "BESIKTAS" }, { "GALATASARAY", "BESIKTAS" },
        { "FENERBAHCE", "TRABZONSPOR" }, { "GALATASARAY", "TRABZONSPOR" },
        { "LIVERPOOL", "MANCHESTER UTD" }, { "LIVERPOOL", "EVERTON" },
        { "ARSENAL", "TOTTENHAM" }, { "MANCHESTER UTD", "MANCHESTER CITY" },
        { "NEWCASTLE", "SUNDERLAND" }, { "CELTIC", "RANGERS" },
        { "BARCELONA", "REAL MADRID" }, { "REAL MADRID", "ATLETICO" },
        { "AC MILAN", "INTER" }, { "MILAN", "INTER" }, { "ROMA", "LAZIO" },
        { "JUVENTUS", "TORINO" }, { "JUVENTUS", "INTER" }, { "JUVENTUS", "MILAN" },
        { "AJAX", "FEYENOORD" }, { "AJAX", "PSV" },
        { "BAYERN", "DORTMUND" }, { "SCHALKE", "DORTMUND" }, { "BAYERN", "1860" },
        { "BENFICA", "PORTO" }, { "BENFICA", "SPORTING" },
        { "MARSEILLE", "PARIS" }, { "BOCA", "RIVER" },
        { "OLYMPIAKOS", "PANATHINAIKOS" }, { "RED STAR", "PARTIZAN" },
    };
    for (const auto& pair : kDerbies) {
        bool fwd = (a.find(pair.first) != std::string::npos && b.find(pair.second) != std::string::npos);
        bool rev = (b.find(pair.first) != std::string::npos && a.find(pair.second) != std::string::npos);
        if (fwd || rev)
            return true;
    }
    return false;
}

PressConferencePrompt generateOfflinePressPrompt(const PressMatchContext& ctx, uint32_t seed)
{
    const char *cat = pickOfflineCategory(ctx);
    const OfflinePressTemplate *arr = (ctx.phase == PressPhase::kPreMatch) ? kPreMatchTemplates : kPostMatchTemplates;
    size_t count = (ctx.phase == PressPhase::kPreMatch)
        ? (sizeof(kPreMatchTemplates) / sizeof(kPreMatchTemplates[0]))
        : (sizeof(kPostMatchTemplates) / sizeof(kPostMatchTemplates[0]));

    std::vector<const OfflinePressTemplate *> matches;
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(arr[i].category, cat) == 0)
            matches.push_back(&arr[i]);
    }
    if (matches.empty()) {
        for (size_t i = 0; i < count; ++i)
            matches.push_back(&arr[i]);
    }

    const OfflinePressTemplate *picked = matches[seed % matches.size()];
    std::string scoreStr = std::to_string(ctx.clubGoals) + "-" + std::to_string(ctx.opponentGoals);

    PressConferencePrompt out;
    out.phase = ctx.phase;
    out.journalistName = sanitizePressField(picked->journalist, 48);
    out.question = sanitizePressField(replaceTokens(picked->question, ctx.clubName, ctx.opponentName, scoreStr), 180);
    for (int i = 0; i < 4; ++i) {
        out.options[i].stance = picked->opts[i].stance;
        out.options[i].text = sanitizePressField(replaceTokens(picked->opts[i].text, ctx.clubName, ctx.opponentName, scoreStr), 96);
        out.options[i].charismaDelta = static_cast<int8_t>(std::clamp<int>(picked->opts[i].delta, -8, 8));
        out.options[i].mediaReaction = sanitizePressField(replaceTokens(picked->opts[i].reaction, ctx.clubName, ctx.opponentName, scoreStr), 96);
    }
    out.isReady = true;
    return out;
}

PressConferencePrompt parseLlmPressPrompt(const std::string& rawReply, const PressMatchContext& ctx, uint32_t seed)
{
    PressConferencePrompt fallback = generateOfflinePressPrompt(ctx, seed);
    if (rawReply.empty())
        return fallback;

    std::istringstream stream(rawReply);
    std::string line;
    std::string journalist;
    std::string question;
    std::vector<PressOption> opts;

    while (std::getline(stream, line)) {
        line = trimStr(line);
        if (line.empty() || line.find('|') == std::string::npos)
            continue;

        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos <= line.size()) {
            size_t sep = line.find('|', pos);
            parts.push_back(trimStr(line.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos)));
            if (sep == std::string::npos)
                break;
            pos = sep + 1;
        }

        if (question.empty() && parts.size() >= 2 && parseStanceString(parts[0]) == ManagerialStance::kNone) {
            journalist = sanitizePressField(parts[0], 48);
            question = sanitizePressField(parts[1], 180);
            continue;
        }

        if (parts.size() >= 4 && opts.size() < 4) {
            ManagerialStance st = parseStanceString(parts[0]);
            if (st == ManagerialStance::kNone) {
                static constexpr ManagerialStance kOrder[4] = {
                    ManagerialStance::kHumble, ManagerialStance::kConfident,
                    ManagerialStance::kTactical, ManagerialStance::kHostile
                };
                st = kOrder[opts.size()];
            }
            PressOption opt;
            opt.stance = st;
            opt.text = sanitizePressField(parts[1], 96);
            int rawDelta = std::atoi(parts[2].c_str());
            opt.charismaDelta = static_cast<int8_t>(std::clamp(rawDelta, -8, 8));
            opt.mediaReaction = sanitizePressField(parts[3], 96);
            if (!opt.text.empty()) {
                if (opt.mediaReaction.empty())
                    opt.mediaReaction = "The press room notes your response.";
                opts.push_back(std::move(opt));
            }
        }
    }

    if (question.empty() || opts.size() < 4)
        return fallback;

    PressConferencePrompt out;
    out.phase = ctx.phase;
    out.journalistName = journalist.empty() ? fallback.journalistName : journalist;
    out.question = std::move(question);
    for (int i = 0; i < 4; ++i)
        out.options[i] = std::move(opts[i]);
    out.isReady = true;
    return out;
}

// --- Career Lifecycle & UI Hooks ---

void resetPressCharismaState()
{
    s_state = CareerPressState{};
    s_activePrompt = PressConferencePrompt{};
    s_lastPreContext = PressMatchContext{};
    s_pendingPreRequestId = 0;
    s_pendingPostRequestId = 0;
    s_matchEligible = false;
    s_gateReceiptsPending = false;
    s_lastGateReceiptsDelta = 0;
    s_lastBonusSeasonKey = -1;
}

void beginPreMatchPressConference(const TeamGame *topTeam, const TeamGame *bottomTeam)
{
    s_pendingPreRequestId = 0;
    s_pendingPostRequestId = 0;
    s_matchEligible = false;
    s_gateReceiptsPending = false;
    s_lastGateReceiptsDelta = 0;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    bool humanIsTeam1 = true;
    if (!resolveHumanCareerSideForPress(topTeam, bottomTeam, humanIsTeam1))
        return;

    syncPressFromHotseatIfActive();

    s_matchEligible = true;
    s_humanIsTeam1 = humanIsTeam1;
    s_preMatchGateReceipts = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
    s_gateReceiptsPending = true;

    const TeamGame *humanTeam = humanIsTeam1 ? topTeam : bottomTeam;
    const TeamGame *oppTeam = humanIsTeam1 ? bottomTeam : topTeam;

    PressMatchContext ctx;
    ctx.phase = PressPhase::kPreMatch;
    ctx.managerName = readCurrentManagerName();
    ctx.clubName = humanTeam->teamName[0] ? humanTeam->teamName : "CLUB";
    ctx.opponentName = oppTeam->teamName[0] ? oppTeam->teamName : "OPPONENT";
    ctx.competition = HotseatCareerManager::get().isActive() ? "Hotseat Career" : "Career";
    ctx.isDerby = detectClassicDerby(ctx.clubName, ctx.opponentName, 0, 0);
    ctx.recentForm = s_state.recentForm;
    ctx.currentCharisma = s_state.charismaPoints;

    s_lastPreContext = ctx;
    s_pressSeed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_state.interviewsCompleted * 37u + 13u));
    s_activePrompt = generateOfflinePressPrompt(ctx, s_pressSeed);

    if (!cfg.enabled)
        return;

    s_pendingPreRequestId = enqueueLlmRequest(
        "You are a 1997 football press conference journalist and sports media editor. "
        "Output strictly 5 pipe-delimited lines (Journalist|Question, then 4 options as STANCE|TEXT|DELTA|REACTION). Plain text only.",
        buildPressLlmPrompt(ctx));
}

void showPreMatchPressConferenceIfReady()
{
    if (!s_matchEligible)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    if (s_pendingPreRequestId != 0) {
        std::string reply;
        if (waitForPressLlmWithSpinner(s_pendingPreRequestId, std::min(cfg.timeoutMs, 2500), "INCOMING PRESS QUESTION", reply)) {
            s_activePrompt = parseLlmPressPrompt(reply, s_lastPreContext, s_pressSeed);
        }
        s_pendingPreRequestId = 0;
    }

#ifndef SWOS_TEST
    if (s_activePrompt.isReady) {
        setStandardMenuBackgroundImage();
        menuFadeIn();
        showMenu(pressConferenceMenu);
        menuFadeOut();
    }
#endif
}

void applyPendingMatchdayGateReceiptsCharismaBonus()
{
    if (!s_gateReceiptsPending || swos.g_gameType != kGameTypeCareer || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences) {
        s_gateReceiptsPending = false;
        return;
    }

    syncPressFromHotseatIfActive();
    auto *vmGate = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    int32_t curGate = *vmGate;

    if (curGate > s_preMatchGateReceipts) {
        int32_t matchGate = curGate - s_preMatchGateReceipts;
        double mult = getPressAttendanceMultiplier(s_state.charismaPoints);
        int32_t delta = static_cast<int32_t>(std::llround(static_cast<double>(matchGate) * (mult - 1.0)));
        if (delta != 0) {
            *vmGate += delta;
            *vmBal += delta;
            auto& mgr = HotseatCareerManager::get();
            if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
                auto& p = mgr.activePlayer();
                p.gateReceipts += delta;
                p.newBalance += delta;
            }
            s_lastGateReceiptsDelta = delta;
            logInfo("Press Charisma attendance multiplier (%.2fx at %d CP) adjusted gate receipts by %+d (base match gate %d)",
                mult, static_cast<int>(s_state.charismaPoints), delta, matchGate);
        }
        s_preMatchGateReceipts = *vmGate;
        s_gateReceiptsPending = false;
    }
}

void beginPostMatchPressConference(int topGoals, int botGoals)
{
    s_pendingPostRequestId = 0;
    if (!s_matchEligible || swos.gameCanceled || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    syncPressFromHotseatIfActive();
    applyPendingMatchdayGateReceiptsCharismaBonus();

    const auto stats = getStats();
    PressMatchContext ctx = s_lastPreContext;
    ctx.phase = PressPhase::kPostMatch;
    ctx.clubGoals = s_humanIsTeam1 ? topGoals : botGoals;
    ctx.opponentGoals = s_humanIsTeam1 ? botGoals : topGoals;
    ctx.clubRedCards = s_humanIsTeam1 ? stats.team1.sendingsOff : stats.team2.sendingsOff;
    ctx.opponentRedCards = s_humanIsTeam1 ? stats.team2.sendingsOff : stats.team1.sendingsOff;
    ctx.preMatchStance = s_state.lastPreMatchStance;
    ctx.preMatchQuote = s_state.lastPreMatchQuote;
    ctx.currentCharisma = s_state.charismaPoints;

    pushRecentFormResult(ctx.clubGoals, ctx.opponentGoals);
    ctx.recentForm = s_state.recentForm;

    s_lastPreContext = ctx;
    s_pressSeed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_state.interviewsCompleted * 53u + 29u));
    s_activePrompt = generateOfflinePressPrompt(ctx, s_pressSeed);

    if (!cfg.enabled)
        return;

    s_pendingPostRequestId = enqueueLlmRequest(
        "You are a sharp 1997 post-match football press conference journalist. "
        "React directly to the real scoreline, red cards, and the manager's pre-match promise. "
        "Output strictly 5 pipe-delimited lines (Journalist|Question, then 4 options as STANCE|TEXT|DELTA|REACTION). Plain text only.",
        buildPressLlmPrompt(ctx));
}

void showPostMatchPressConferenceIfReady()
{
    if (!s_matchEligible || swos.gameCanceled || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    if (s_pendingPostRequestId != 0) {
        std::string reply;
        if (waitForPressLlmWithSpinner(s_pendingPostRequestId, std::min(cfg.timeoutMs, 2500), "POST-MATCH PRESS ROOM", reply)) {
            s_activePrompt = parseLlmPressPrompt(reply, s_lastPreContext, s_pressSeed);
        }
        s_pendingPostRequestId = 0;
    }

#ifndef SWOS_TEST
    if (s_activePrompt.isReady) {
        setStandardMenuBackgroundImage();
        menuFadeIn();
        showMenu(pressConferenceMenu);
        menuFadeOut();
    }
#endif
}

void onViewResultMatchPressConference()
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    const auto *t1File = swos.playMatchTeam1Ptr.asAligned();
    const auto *t2File = swos.playMatchTeam2Ptr.asAligned();
    if (!t1File || !t2File)
        return;

    bool t1Human = (t1File->teamControls == kPlayerCoach || t1File->teamControls == kCoach);
    bool t2Human = (t2File->teamControls == kPlayerCoach || t2File->teamControls == kCoach);
    if (t1Human == t2Human)
        return;

    syncPressFromHotseatIfActive();
    applyPendingMatchdayGateReceiptsCharismaBonus();

    int g1 = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379094]));
    int g2 = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379096]));

    PressMatchContext ctx;
    ctx.phase = PressPhase::kPostMatch;
    ctx.managerName = readCurrentManagerName();
    ctx.clubName = t1Human ? t1File->teamName : t2File->teamName;
    ctx.opponentName = t1Human ? t2File->teamName : t1File->teamName;
    ctx.competition = HotseatCareerManager::get().isActive() ? "Hotseat Career" : "Career";
    ctx.isDerby = detectClassicDerby(ctx.clubName, ctx.opponentName, 0, 0);
    ctx.clubGoals = t1Human ? g1 : g2;
    ctx.opponentGoals = t1Human ? g2 : g1;
    ctx.preMatchStance = s_state.lastPreMatchStance;
    ctx.preMatchQuote = s_state.lastPreMatchQuote;
    ctx.currentCharisma = s_state.charismaPoints;

    pushRecentFormResult(ctx.clubGoals, ctx.opponentGoals);
    ctx.recentForm = s_state.recentForm;

    s_lastPreContext = ctx;
    s_pressSeed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_state.interviewsCompleted * 61u + 17u));
    s_activePrompt = generateOfflinePressPrompt(ctx, s_pressSeed);
    s_matchEligible = true;

    if (cfg.enabled) {
        s_pendingPostRequestId = enqueueLlmRequest(
            "You are a sharp 1997 post-match football press conference journalist. "
            "Output strictly 5 pipe-delimited lines (Journalist|Question, then 4 options as STANCE|TEXT|DELTA|REACTION). Plain text only.",
            buildPressLlmPrompt(ctx));
    }
    showPostMatchPressConferenceIfReady();
    s_matchEligible = false;
}

void onEndOfSeasonPressCharismaBonus()
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return;

    syncPressFromHotseatIfActive();

    uint16_t seasonNum = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    int16_t teamId = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int32_t seasonKey = (static_cast<int32_t>(seasonNum) << 16) | static_cast<uint16_t>(teamId);
    if (seasonKey == s_lastBonusSeasonKey)
        return;
    s_lastBonusSeasonKey = seasonKey;

    auto *vmTv = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionBonuses]);
    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);

    int divTier = readDivisionTierLocal();
    int64_t divBaseTv = (divTier <= 0) ? 1000000 : ((divTier == 1) ? 500000 : 250000);
    int64_t tvBase = (*vmTv > 0) ? static_cast<int64_t>(*vmTv) : divBaseTv;

    int64_t bonus = computeSeasonCharismaBonus(tvBase, s_state.charismaPoints);
    s_state.lastSeasonCharismaBonus = bonus;

    if (bonus > 0) {
        int32_t bonus32 = static_cast<int32_t>(std::min<int64_t>(bonus, 50000000));
        *vmBal += bonus32;
        *vmTv += bonus32;
        auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            auto& p = mgr.activePlayer();
            p.newBalance += bonus32;
            p.competitionBonuses += bonus32;
        }
    }
    syncPressToHotseatIfActive();

    logInfo("End-of-season Press Charisma Bonus: CP=%d (%s), TV base=%lld, bonus=%lld",
        static_cast<int>(s_state.charismaPoints),
        getCharismaTierLabel(getCharismaTier(s_state.charismaPoints)),
        static_cast<long long>(tvBase),
        static_cast<long long>(bonus));

#ifndef SWOS_TEST
    CharismaTier tier = getCharismaTier(s_state.charismaPoints);
    char multBuf[32];
    std::snprintf(multBuf, sizeof(multBuf), "%.2fx", getCharismaTvMultiplier(s_state.charismaPoints));
    char attBuf[32];
    std::snprintf(attBuf, sizeof(attBuf), "%.2fx", getPressAttendanceMultiplier(s_state.charismaPoints));

    std::string body = "SEASON FINANCIAL SUMMARY - CHARISMA & MEDIA BONUS\n\n";
    body += "REPUTATION TIER: " + std::string(getCharismaTierLabel(tier)) + " (" +
            getCharismaReputationTitle(tier) + ")\n";
    body += "FINAL CHARISMA RATING: " + std::to_string(s_state.charismaPoints) + " / 100 CP (" +
            std::to_string(s_state.interviewsCompleted) + " interviews)\n";
    body += "TV RIGHTS & PRIZE BASE: " + formatSponsorPounds(tvBase) + "\n";
    body += "TIER TV MULTIPLIER: " + std::string(multBuf) + "  |  GATE RECEIPTS FACTOR: " + std::string(attBuf) + "\n\n";
    body += "CHARISMA & MEDIA BONUS CREDITED: +" + formatSponsorPounds(bonus) + "\n";
    body += "UPDATED CLUB BALANCE: " + formatSponsorPounds(*vmBal);

    setStandardMenuBackgroundImage();
    menuFadeIn();
    showLlmReportDialog("SEASON CHARISMA & MEDIA BONUS", body);
    menuFadeOut();
#endif
}

// --- Live Telemetry & Newspaper Synergy ---

std::string formatPressContextForTelemetry()
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame)
        return {};
    const auto& cfg = getLlmConfig();
    if (!cfg.features.pressConferences)
        return {};

    syncPressFromHotseatIfActive();
    CharismaTier tier = getCharismaTier(s_state.charismaPoints);
    std::string out = "Manager Press Charisma: " + std::to_string(s_state.charismaPoints) + "/100 (" +
                      getCharismaTierLabel(tier) + " - " + getCharismaReputationTitle(tier) + ")";
    if (s_state.lastPreMatchStance != ManagerialStance::kNone && !s_state.lastPreMatchQuote.empty()) {
        out += " | Pre-match press stance [" + std::string(getManagerialStanceName(s_state.lastPreMatchStance)) +
               "]: \"" + s_state.lastPreMatchQuote + "\"";
    }
    return out;
}

std::string formatPressContextForNewspaper()
{
    return formatPressContextForTelemetry();
}

// --- Persistence (single-player .prs sidecar + Hotseat PRSS trailer sync) ---

bool savePressCharismaSidecar(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetLongValue("press", "charismaPoints", static_cast<long>(s_state.charismaPoints));
    ini.SetLongValue("press", "interviewsCompleted", static_cast<long>(s_state.interviewsCompleted));
    ini.SetLongValue("press", "lastPreMatchStance", static_cast<long>(static_cast<uint8_t>(s_state.lastPreMatchStance)));
    ini.SetValue("press", "lastPreMatchQuote", s_state.lastPreMatchQuote.c_str());
    std::string bonusStr = std::to_string(s_state.lastSeasonCharismaBonus);
    ini.SetValue("press", "lastSeasonCharismaBonus", bonusStr.c_str());
    return ini.SaveFile(path.c_str()) >= 0;
}

bool loadPressCharismaSidecar(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadFile(path.c_str()) < 0)
        return false;

    s_state.charismaPoints = static_cast<int16_t>(std::clamp<long>(ini.GetLongValue("press", "charismaPoints", 50), 0, 100));
    s_state.interviewsCompleted = static_cast<uint16_t>(std::max<long>(0L, ini.GetLongValue("press", "interviewsCompleted", 0)));
    long stanceVal = std::clamp<long>(ini.GetLongValue("press", "lastPreMatchStance", 0), 0, static_cast<long>(ManagerialStance::kHostile));
    s_state.lastPreMatchStance = static_cast<ManagerialStance>(stanceVal);
    const char *qVal = ini.GetValue("press", "lastPreMatchQuote", "");
    s_state.lastPreMatchQuote = qVal ? sanitizePressField(qVal, 96) : std::string();
    const char *bVal = ini.GetValue("press", "lastSeasonCharismaBonus", "0");
    s_state.lastSeasonCharismaBonus = bVal ? std::strtoll(bVal, nullptr, 10) : 0;
    return true;
}

void getPressCareerTotals(int16_t& outCharisma, uint16_t& outInterviews, int64_t& outLastSeasonBonus,
                          uint8_t& outLastStance, std::string& outLastQuote)
{
    syncPressFromHotseatIfActive();
    outCharisma = s_state.charismaPoints;
    outInterviews = s_state.interviewsCompleted;
    outLastSeasonBonus = s_state.lastSeasonCharismaBonus;
    outLastStance = static_cast<uint8_t>(s_state.lastPreMatchStance);
    outLastQuote = s_state.lastPreMatchQuote;
}

void setPressCareerTotals(int16_t charisma, uint16_t interviews, int64_t lastSeasonBonus,
                          uint8_t lastStance, const std::string& lastQuote)
{
    s_state.charismaPoints = std::clamp<int16_t>(charisma, 0, 100);
    s_state.interviewsCompleted = interviews;
    s_state.lastSeasonCharismaBonus = lastSeasonBonus;
    s_state.lastPreMatchStance = static_cast<ManagerialStance>(
        std::min<uint8_t>(lastStance, static_cast<uint8_t>(ManagerialStance::kHostile)));
    s_state.lastPreMatchQuote = sanitizePressField(lastQuote, 96);
    syncPressToHotseatIfActive();
}
