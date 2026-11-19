#pragma once

#include <cstddef>
#include <cstdint>

void startMainMenuLoop();
uint32_t getHotseatExtraMemoryOffset();
size_t getHotseatExtraMemorySize();
#ifdef DEBUG
void checkMemory();
#endif
