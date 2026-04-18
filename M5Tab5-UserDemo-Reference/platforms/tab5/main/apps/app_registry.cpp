#include "app_registry.h"
#include "app_template/app_template.h"
#include "app_system_info/app_system_info.h"
#include "app_spotify/app_spotify.h"

const std::vector<AppEntry>& GetAppRegistry()
{
    static const std::vector<AppEntry> registry = {
        {
            "System Info",
            0xFF4488FF,
            []() { return std::make_unique<AppSystemInfo>(); },
        },
        {
            "Template",
            0xFF44CC88,
            []() { return std::make_unique<AppTemplate>(); },
        },
        {
            "Spotify Connect",
            0xFF1DB954,
            []() { return std::make_unique<AppSpotify>(); },
        },
    };
    return registry;
}
