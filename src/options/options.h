#pragma once

enum GameStyle {
    kPcGameStyle,
    kAmigaGameStyle,
};

void loadOptions();
void saveOptions();
bool ensureSwosDataDirectory();
std::vector<LogItem> parseCommandLine(int argc, char **argv);
void normalizeOptions();

bool showPreMatchMenus();
void setPreMatchMenus(bool enabled);
int getGameStyle();
void setGameStyle(int gameStyle);
