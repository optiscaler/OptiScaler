#pragma once

// [Hotfix] AuxiliaryDxgiFactoryModules: dlls whose DXGI factories only create auxiliary swapchains, like Social Club's
// overlay in GTA V Enhanced. Dxgi_Hooks marks their factories and DxgiFactory_Hooks creates the swapchains of a marked
// factory natively, so they never replace the game's frame generation swapchain. This is separate from
// skipDxgiWrapping (DllNames.h), which also holds DirectX runtime callers.
//
// Parsed strictly: an entry that matched more than intended (a pattern, a path, a part of a name, the game's exe)
// would keep the game's own swapchain out of frame generation without a sign. Entries are dll file names separated
// by '|', compared whole and case-insensitively, a malformed list keeps the default.
//
// No Windows headers here, so the parser can be tested on its own.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AuxiliaryModules
{
// Social Club's D3D12 renderer, its overlay swapchain can replace the game's FG context in GTA V Enhanced
inline constexpr std::string_view Default = "SocialClubD3D12Renderer.dll";

inline std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// A dll file name: "<name>.dll", printable ASCII without blanks and without the characters Windows file names
// can't hold (so no path, drive or wildcard), nor ',' or ';' (a list typed with another separator would be one name
// that never matches, and the default would be gone)
inline bool IsModuleName(std::string_view name)
{
    if (name.size() <= 4 || name.size() > 255 || Lower(name.substr(name.size() - 4)) != ".dll")
        return false;

    for (char c : name)
    {
        if (c <= ' ' || c > '~' || std::string_view("\\/:*?\"<>|,;").find(c) != std::string_view::npos)
            return false;
    }

    return true;
}

// The entries in lower case, each once, nullopt when an entry isn't a dll file name (rejected then holds it).
// Blanks around an entry are allowed, an empty entry isn't.
inline std::optional<std::vector<std::string>> Parse(std::string_view list, std::string* rejected = nullptr)
{
    std::vector<std::string> names;

    for (size_t start = 0;;)
    {
        const size_t end = list.find('|', start);
        std::string_view entry = list.substr(start, end == std::string_view::npos ? end : end - start);

        while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t'))
            entry.remove_prefix(1);
        while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t'))
            entry.remove_suffix(1);

        if (!IsModuleName(entry))
        {
            if (rejected != nullptr)
                *rejected = std::string(entry);

            return std::nullopt;
        }

        if (auto name = Lower(entry); std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(std::move(name));

        if (end == std::string_view::npos)
            return names;

        start = end + 1;
    }
}

// The list in effect: the configured one, or the default when the setting is auto, empty or malformed (rejected
// then holds the first malformed entry, which can be empty)
inline std::vector<std::string> Resolve(const std::optional<std::string>& configured,
                                        std::optional<std::string>& rejected)
{
    rejected.reset();

    if (configured.has_value() && !configured->empty())
    {
        std::string entry;
        if (auto names = Parse(*configured, &entry))
            return *names;

        rejected = entry;
    }

    return *Parse(Default);
}

// module: the caller's file name (Util::WhoIsTheCaller)
inline bool Contains(const std::vector<std::string>& names, std::string_view module)
{
    return std::find(names.begin(), names.end(), Lower(module)) != names.end();
}
} // namespace AuxiliaryModules
