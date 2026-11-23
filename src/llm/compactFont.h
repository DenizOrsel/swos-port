#pragma once

#include <SDL.h>
#include <string>
#include <vector>

SDL_Texture *getCompactFontAtlas(SDL_Renderer *r);
int measureCompactTextUnits(const std::string& text);
std::string sanitizeCompactMixedCase(const std::string& in);
std::vector<std::string> wrapCompactTextUnits(const std::string& rawText, int maxUnits);
float computeCompactPixelScaleForVga(float gameScale, float ratio = 0.50f);

float drawCompactText(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha = 255, bool bold = false);

void drawCompactTextCentered(SDL_Renderer *r, SDL_Texture *atlas, float centerX, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha = 255, bool bold = false);

void drawCompactTextRightAligned(SDL_Renderer *r, SDL_Texture *atlas, float rightX, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha = 255, bool bold = false);

void drawCompactTextWithMentions(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color baseColor, SDL_Color mentionColor, Uint8 alpha = 255, bool bold = false);
