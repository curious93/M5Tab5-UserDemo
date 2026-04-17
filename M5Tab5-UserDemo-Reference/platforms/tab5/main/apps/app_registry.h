/*
 * Static list of apps shown in the launcher grid.
 * Each entry holds a factory that constructs the app when tapped.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "mooncake.h"

struct AppEntry {
    std::string name;
    uint32_t    icon_color;  // ARGB hex used as icon background until we have bitmaps
    std::function<std::unique_ptr<mooncake::AppAbility>()> factory;
};

const std::vector<AppEntry>& GetAppRegistry();
