#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TabloidKitColors {
    uint8_t shirtCol = 4;    // SWOS color index 0..9
    uint8_t stripesCol = 1;
    uint8_t shortsCol = 1;
    uint8_t socksCol = 4;
};

struct TabloidMotmCard {
    bool valid = false;
    std::string name;
    std::string team;
    std::string pos;
    int goals = 0;
    int assists = 0;
    int gkSaves = 0;
    double rating = 0.0;
    TabloidKitColors kit;
};

struct TabloidNewspaperEdition {
    std::string headline;
    std::string subhead;
    std::string photoCaption;
    std::string story;
    std::string chairmanQuote;

    std::string competition;
    std::string team1;
    std::string team2;
    int goals1 = 0;
    int goals2 = 0;
    bool playingPenalties = false;
    int penGoals1 = 0;
    int penGoals2 = 0;
    bool team1Human = false;
    bool team2Human = false;

    std::string scorers1;
    std::string scorers2;
    std::string statsLine;
    std::vector<std::string> briefingBullets;

    TabloidKitColors kit1;
    TabloidKitColors kit2;
    TabloidMotmCard motm;
    bool isPlayedMatch = true;
};

// --- Live Match Action Photo Capture (clean pitch frame before HUD/stream overlays) ---
void resetTabloidMatchPhoto();
void requestTabloidMatchPhoto(int priority, const std::string& caption, int delayFrames = 8);
void captureTabloidMatchPhotoIfPending(float cameraXOffset, float cameraYOffset);
bool hasTabloidMatchPhoto();
std::string getTabloidMatchPhotoCaption();

// --- Pure Deterministic Helpers (directly unit tested) ---
void apply90sTabloidColorPrintFilter(std::vector<uint8_t>& rgbaPixels, int width, int height);
TabloidNewspaperEdition buildOfflineTabloidEdition(const TabloidNewspaperEdition& baseMeta);
TabloidNewspaperEdition parseTabloidEditionReply(
    const std::string& rawReply, const TabloidNewspaperEdition& baseMeta);

// --- Interactive 1996/97 Color Tabloid Modal (with [S] Save Edition PNG) ---
void showTabloidNewspaperModal(const TabloidNewspaperEdition& edition);
