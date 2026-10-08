#pragma once
#include "SysUtils.h"

// XeFG input: the game's libxess_fg calls feed the selected FG output
namespace XeFGInputs
{
void Hook(HMODULE libxessFg);

// XeFG output: the game's own XeFG runs; Override XeFG Ratio sets its count
bool Passthrough();
uint32_t MaxInterpolations();
} // namespace XeFGInputs
