#pragma once

#include <SDL.h>
#include <cstdint>
#include <string>

// Cached club, national-team, and league/competition crest loader & renderer.
// Loads 64x64 transparent PNGs from assets/logos/{clubs,leagues}/ (optional pack;
// all functions fail gracefully and return false/nullptr if a logo is not installed).

SDL_Texture *getClubLogoTexture(SDL_Renderer *r, const char *teamName);
SDL_Texture *getLeagueLogoTexture(SDL_Renderer *r, const char *leagueKey);

// Draws the club/national crest aspect-preserving inside {x, y, size, size}.
// Returns true if a logo texture was found and drawn.
bool drawClubLogo(SDL_Renderer *r, const char *teamName, float x, float y, float size, Uint8 alpha = 255);

// Draws the league/competition logo aspect-preserving inside {x, y, size, size}.
// Returns true if a logo texture was found and drawn.
bool drawLeagueLogo(SDL_Renderer *r, const char *leagueKey, float x, float y, float size, Uint8 alpha = 255);

// Resolves the best league/competition logo key for the current match or active career club
// (e.g. "england_premier", "england_div1", "turkey_superlig", "uefa_champions", "fifa_worldcup").
std::string resolveCurrentMatchLeagueLogoKey();
std::string resolveCurrentCareerLeagueLogoKey();
