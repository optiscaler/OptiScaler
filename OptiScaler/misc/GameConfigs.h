#pragma once

#include "Quirks.h"

#include <functional>
#include <string>
#include <vector>

struct GameConfigContext
{
    bool isNvidia = false;
};

// A single "Setting = value" line of a game entry
struct ConfigDefault
{
    std::string text;
    std::function<bool()> apply; // Returns false when the user's config already sets this option
    bool (*condition)(const GameConfigContext&) = nullptr;
};

struct GameConfig
{
    std::vector<std::string> exes;   // Lowercase exe names
    std::vector<std::string> ueExes; // UE project names, matches <name>-win64-shipping.exe & -wingdk-shipping.exe
    std::vector<ConfigDefault> defaults;
    std::vector<GameQuirk> quirks;
};

// Applies the config defaults of every entry matching exeName and returns their quirks
flag_set<GameQuirk> ApplyGameConfigs(std::string exeName, const GameConfigContext& context);
