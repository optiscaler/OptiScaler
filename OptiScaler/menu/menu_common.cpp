#include "pch.h"
#include "menu_common.h"

#include "input/input_system.h"

#include "font/Hack_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <upscalers/ffx/FFXFeature.h>
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>
#include <framegen/reprojection/Reprojection_Dx12.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <BuildInfo.h>
#include <version_check.h>

#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <cstdarg>

#include <array>
#include <numeric>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>
#include <low_latency/input/input_common.h>

enum class UiTargetMode
{
    SDR,
    LinearHDR,
    ScRGB,
    PQ,
    HLG
};

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

static float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool inputFG = false;
static bool fgHotkeyLeftDefault = false; // FG hotkey temporarily overrode Default (follow game)
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
static bool ffxInitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;
static ImVec2 lastPosition(-1000.0f, -1000.0f);

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { "Cope smarter, not harder",
                                               "Coping is strong with this one...",
                                               "This is where the fun begins...",
                                               "Got any more of them scalers?...",
                                               "Fake pixels and even faker frames...",
                                               "Fake frames, get your fake frames...",
                                               "I'm here to kick pixels and chew frames...",
                                               "I find your lack of supersampling disturbing...",
                                               "Frame by frame, I scale-up!",
                                               "Resistance is futile. Your pixels will be upscaled.",
                                               "I've got 99 problems, but low-res ain't one.",
                                               "It's over, DLSS, I have the higher ground!",
                                               "This isn't the resolution you're looking for",
                                               "To infinity and beyond... with ray tracing off",
                                               "I have a bad feeling about this frame pacing",
                                               "It's Dangerous to Go Alone-Take This Upscaler",
                                               "Upscaled beyond recognition.",
                                               "Trust the process. Ignore the shimmer.",
                                               "Real fake frames. Certified.",
                                               "The illusion of performance",
                                               "This upscaler belongs in a museum!",
                                               "Because native rendering is overrated.",
                                               "The more you upscaler, the more you save",
                                               "It's never too late to buy a better GPU",
                                               "We don't need real pixels where we're going",
                                               "Did you know that Intel released XeFG for everyone?",
                                               "MFG totally works with Nukem's 100%% no scam",
                                               "Some of those pixels might even be real!",
                                               "Just don't look too closely at the image",
                                               "Even supports \"software\" XeSS!",
                                               "It's too blurry to go alone, take RCAS with you",
                                               "Thanks nitec, back to you nitec",
                                               "Tested and approved by By-U",
                                               "0.8 was an inside job",
                                               "FSR4 DP4a wenETA, AMD plz",
                                               "OptiCopers, assemble!",
                                               "The Way It's Meant To Be Upscaled",
                                               "Your game may not even crash today",
                                               "Expanded and Enhanced",
                                               "It's only my 5th crash today",
                                               "Latency with FG? But I have good internet",
                                               "Console peasants can't do that",
                                               "Hope you don't have a good eyesight",
                                               "Such an aggressive upscaling? A bold move",
                                               "I almost don't feel the input lag",
                                               "And that's how you get to 60 FPS",
                                               "Together We Upscale",
                                               "For upscalers, by upscalers",
                                               "Opti Sports, it's in the sampling",
                                               "Render in your world. Upscale in ours",
                                               "All your pixels are belong to us",
                                               "Upscaling for the masses, not the classes",
                                               "Generating discord since 2023",
                                               "Enabling DLSS since 2023",
                                               "[REDACTED] never looked better",
                                               "Free and always free",
                                               "Getting unshackled from green chains in progress...",
                                               "Who's Nukem anyway?",
                                               "Compiling shaders... ETA: 05h:49m",
                                               "Did you really just pay 70 EUR for this game?!",
                                               "Guess who forgot about a nullptr check again",
                                               "AI can't outslop this",
                                               "Guess we're pre-alpha build demos now",
                                               "New app on the block - TH",
                                               "One more stutter and I might lose it",
                                               "Mostly stable, unlike the driver",
                                               "Vul... what? ~AMD",
                                               "My 8 points are floating",
                                               "No floating here - I'm strictly between -128 and 127",
                                               "Fake it til you bake it",
                                               "Worst case just turn it off and on",
                                               "*On a generative damage control mode at geometry level*",
                                               "Deep Learning Slop Sampling 5",
                                               "2D AI filters, now powered by just 2x 5090s",
                                               "Neural Slop Sampling with DLSS5",
                                               "DLSS 5 - the way it's meant to be slopped",
                                               "Just when I think I'm out, they scale me back in",
                                               "Like going in the first gear on the highway",
                                               "Nitec's Bizarre Upscaling",
                                               "\"Framegen really attracts some strange clientelle\"",
                                               "How to remove those corny messages?!",
                                               "<Your funny text goes here>" };

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool capturingKey = false;
static int selectedMenuTab = 0;
static const char* requestedMenuTab = nullptr; // Label of a tab to switch to on the next frame

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::filesystem::path& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache nukemsExists;
static FsExistsCache enablerExists;

struct FlagDefinition
{
    std::string name;
    uint32_t mask;
    std::string description;
};

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

// CARDS -----------------------------
static void DrawCardTitle(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
    ImGui::PopStyleColor();

    // Spacing below is not an item the tooltips should attach to
    const ImRect titleRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImGuiID titleId = ImGui::GetItemID();

    ImGui::Spacing();
    ImGui::Spacing();
    // ImGui::Separator();

    // HoveredRect has to reflect the mouse, IsItemHovered trusts it
    const ImGuiItemStatusFlags hovered =
        ImGui::IsMouseHoveringRect(titleRect.Min, titleRect.Max) ? ImGuiItemStatusFlags_HoveredRect : 0;
    ImGui::SetLastItemData(titleId, 0, hovered, titleRect);
}

// Used instead of ImGui::SeparatorText. The first one in a card becomes the title of the card
static void SectionTitle(const char* label)
{
    if (!ScopedCard::TryTitle(label))
        ImGui::SeparatorText(label);
}

ScopedCard::ScopedCard(const char* title)
{
    // Cards don't nest, an inner one only gives a title to the outer one
    if (_current != nullptr)
    {
        if (title != nullptr)
            SectionTitle(title);

        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();

    _window = ImGui::GetCurrentWindow();
    _pad = ImVec2(style.WindowPadding.x, style.WindowPadding.y * 0.75f);
    _min = _window->DC.CursorPos;
    _width = ImGui::GetContentRegionAvail().x;

    // Content goes to the channel 1, the background is added to the channel 0 once the height is known
    _splitter.Split(_window->DrawList, 2);
    _splitter.SetCurrentChannel(_window->DrawList, 1);

    _window->DC.CursorPos.y += _pad.y;
    ImGui::Indent(_pad.x);

    // GetContentRegionAvail (used by child windows) reads ContentRegionRect, so both need the padding
    _oldWorkRectMaxX = _window->WorkRect.Max.x;
    _oldContentRectMaxX = _window->ContentRegionRect.Max.x;
    _window->WorkRect.Max.x -= _pad.x;
    _window->ContentRegionRect.Max.x -= _pad.x;

    _contentStartY = _window->DC.CursorPos.y;
    _active = true;
    _current = this;

    if (title != nullptr)
    {
        DrawCardTitle(title);
        _hasTitle = true;
    }
}

ScopedCard::~ScopedCard()
{
    if (!_active)
        return;

    _current = nullptr;

    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = _window->DrawList;

    ImGui::Unindent(_pad.x);
    _window->WorkRect.Max.x = _oldWorkRectMaxX;
    _window->ContentRegionRect.Max.x = _oldContentRectMaxX;

    // Nothing was drawn, skip the rectangle
    if (_window->DC.CursorPos.y <= _contentStartY)
    {
        _splitter.Merge(drawList);
        _window->DC.CursorPos.y = _min.y;
        return;
    }

    const float bottom = _window->DC.CursorPos.y - style.ItemSpacing.y + _pad.y;
    const ImVec2 max(_min.x + _width, bottom);

    _splitter.SetCurrentChannel(drawList, 0);
    // drawList->AddRectFilled(_min, max, ImGui::GetColorU32(ImGuiCol_WindowBg), style.ChildRounding);
    drawList->AddRect(_min, max, ImGui::GetColorU32(ImGuiCol_Separator), style.ChildRounding);
    _splitter.Merge(drawList);

    // Registers the size of the card and leaves a gap before the next one
    _window->DC.CursorPos.y = bottom;
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool ScopedCard::TryTitle(const char* label)
{
    if (_current == nullptr || _current->_hasTitle)
        return false;

    // Only when it's the very first thing in the card
    if (ImGui::GetCurrentWindow() != _current->_window || _current->_window->DC.CursorPos.y != _current->_contentStartY)
        return false;

    DrawCardTitle(label);
    _current->_hasTitle = true;

    return true;
}

bool ScopedCard::Shows(const char* id)
{
    // Inside of a card everything belongs to that card
    return _filter == nullptr || _current != nullptr || strcmp(_filter, id) == 0;
}

void ScopedCard::SetFilter(const char* id) { _filter = id; }

void MenuCommon::RenderCard(RenderMenuContext& ctx, void (*render)(RenderMenuContext&))
{
    ScopedCard card;
    render(ctx);
}

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if (inputFlag)
            return;

        if (vk <= 0 || vk >= 256)
            return;

        if (OptiInput::IsKeyReleased(vk))
        {
            lastKey = vk;
            // receivingWmInputs = false;
            inputFlag = true;
            LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    if (!capturingKey && canAcceptInputs)
    {
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, "Menu key pressed, will be switching menu");
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, "Menu key pressed, will be switching FPS");
        CheckShortcut(config->FGShortcutKey.value_or_default(), inputFG, "Menu key pressed, will be switching FG mode");
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      "Menu key pressed, will be switching FPS mode");
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::Text(tip);
        ImGui::EndTooltip();
    }
}

template <typename TOption>
bool MenuCommon::ConfigCheckbox(const char* label, TOption& option, const char* tip, bool needsRestart)
{
    bool value = option.value_or_default();
    const bool changed = ImGui::Checkbox(label, &value);

    if (changed)
        option = value;

    if (tip != nullptr)
    {
        if (needsRestart)
            ShowTooltip((std::string(tip) + "\n\nAfter changing this option, please Save Settings.\n"
                                            "It will be applied on next launch.")
                            .c_str());
        else
            ShowTooltip(tip);
    }

    return changed;
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    SectionTitle(label);
    ShowTooltip(tip);
}

bool MenuCommon::SliderUInt(const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max, const char* format,
                            ImGuiSliderFlags flags)
{
    return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format, flags);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == (USHORT) UnboundKey)
            return "Unbound";

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

    void Render(CustomOptional<int>& configKey)
    {
        ImGui::PushID(id);
        if (ImGui::Button(name.c_str()))
        {
            waitingForKey = true;
            capturingKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            ImGui::Text("Press any key...");

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            capturingKey = false;
            return;
        }

        ImGui::SameLine();
        ImGui::Text(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());

        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

// List of upscalers reported by FFX for a given backend, nullptr if the backend doesn't use FFX
static State::FfxVersionList* GetFfxVersionList(Upscaler upscaler, API api)
{
    if (upscaler == Upscaler::FFX_on12 || (upscaler == Upscaler::FFX && api == API::DX12))
        return &State::Instance().ffxUpscalerVersionsDx12;

    if (upscaler == Upscaler::FFX && api == API::Vulkan)
        return &State::Instance().ffxUpscalerVersionsVk;

    return nullptr;
}

static bool IsOn12(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::XeSS_on12:
    case Upscaler::FSR21_on12:
    case Upscaler::FSR22_on12:
    case Upscaler::FFX_on12:
    case Upscaler::DLSS_on12:
        return true;
    default:
        return false;
    }
}

// Name without the w/Dx12 suffix, that one is shown as a tag in the list instead
static std::string ShortUpscalerName(Upscaler upscaler, API api, int ffxIndex)
{
    if (auto versions = GetFfxVersionList(upscaler, api);
        versions != nullptr && ffxIndex >= 0 && ffxIndex < versions->names.size())
        return StrFmt("FSR %s", versions->names[ffxIndex]);

    switch (upscaler)
    {
    case Upscaler::XeSS_on12:
        return UpscalerDisplayName(Upscaler::XeSS);
    case Upscaler::FSR21_on12:
        return UpscalerDisplayName(Upscaler::FSR21);
    case Upscaler::FSR22_on12:
        return UpscalerDisplayName(Upscaler::FSR22);
    case Upscaler::FFX_on12:
        return UpscalerDisplayName(Upscaler::FFX, API::DX12);
    case Upscaler::DLSS_on12:
        return UpscalerDisplayName(Upscaler::DLSS);
    default:
        return UpscalerDisplayName(upscaler, api);
    }
}

static feature_version UpscalerSortVersion(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::FSR21:
    case Upscaler::FSR21_on12:
        return { 2, 1, 2 };
    case Upscaler::FSR22:
    case Upscaler::FSR22_on12:
        return { 2, 2, 1 };
    case Upscaler::FSR31:
        return { 3, 1, 2 };
    case Upscaler::FFX:
    case Upscaler::FFX_on12:
        return { 3, 1, 0 };
    default:
        return {};
    }
}

// Indices into the FFX version list, newest first
static std::vector<int> FfxVersionsNewestFirst(const State::FfxVersionList& versions)
{
    std::vector<int> order(versions.names.size());
    std::iota(order.begin(), order.end(), 0);

    std::stable_sort(order.begin(), order.end(), [&versions](int a, int b)
                     { return feature_version(versions.names[b]) < feature_version(versions.names[a]); });

    return order;
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    auto config = Config::Instance();
    const int ffxIndex = config->FfxUpscalerIndex.value_or_default();

    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    // Interop is only shown here, the list uses a tag for it
    auto selectedName = ShortUpscalerName(targetBackend, api, ffxIndex);
    if (IsOn12(targetBackend))
        selectedName += " (Dx12)";

    if (!ImGui::BeginCombo("##UpscalerCombo", selectedName.c_str(), ImGuiComboFlags_HeightLargest))
        return;

    struct Entry
    {
        Upscaler upscaler;
        int ffxIndex; // Newest upscaler reported by FFX, -1 when not using FFX
        feature_version version;
    };

    std::vector<Entry> entries;

    for (auto opt : options)
    {
        // Check if GPU is capable of a given backend
        if ((opt == Upscaler::DLSS || opt == Upscaler::DLSS_on12) && !primaryGpu.dlssCapable)
            continue;

        // Not all Intel GPUs support native DX11 XeSS but don't think we have a good way to check exactly
        if (opt == Upscaler::XeSS && api == API::DX11 && primaryGpu.vendorId != VendorId::Intel)
            continue;

        // Only the newest FFX upscaler is listed here, the rest are in the FFX Upscaler combo
        if (auto versions = GetFfxVersionList(opt, api); versions != nullptr)
        {
            if (versions == &State::Instance().ffxUpscalerVersionsVk)
                FFXFeature::EnsureVersionsVulkan();
            else
                FFXFeature::EnsureVersionsDx12();

            // Fall back to a generic entry if FFX couldn't be queried
            if (!versions->names.empty())
            {
                auto newest = FfxVersionsNewestFirst(*versions)[0];
                entries.push_back({ opt, newest, feature_version(versions->names[newest]) });
                continue;
            }
        }

        entries.push_back({ opt, -1, UpscalerSortVersion(opt) });
    }

    // Upscaler of the GPU vendor goes first, FSRs are sorted by version, newest first
    // Stable sort keeps native ahead of w/Dx12
    auto group = [&primaryGpu](Upscaler upscaler)
    {
        int family = IsFsr(upscaler) ? 1 : (upscaler == Upscaler::DLSS || upscaler == Upscaler::DLSS_on12) ? 2 : 0;

        const int vendorFamily = primaryGpu.vendorId == VendorId::AMD      ? 1
                                 : primaryGpu.vendorId == VendorId::Nvidia ? 2
                                                                           : 0;

        return family == vendorFamily ? -1 : family;
    };

    std::stable_sort(entries.begin(), entries.end(),
                     [&group](const Entry& a, const Entry& b)
                     {
                         if (group(a.upscaler) != group(b.upscaler))
                             return group(a.upscaler) < group(b.upscaler);

                         return b.version < a.version;
                     });

    for (size_t i = 0; i < entries.size(); i++)
    {
        const auto& entry = entries[i];

        if (i > 0 && group(entry.upscaler) != group(entries[i - 1].upscaler))
            ImGui::Separator();

        auto label =
            StrFmt("%s##%d", ShortUpscalerName(entry.upscaler, api, entry.ffxIndex).c_str(), (int) entry.upscaler);

        if (ImGui::Selectable(label.c_str(), currentUpscaler == entry.upscaler))
        {
            State::Instance().newBackend = entry.upscaler;

            // Switching to FFX starts with the newest one
            if (entry.ffxIndex >= 0 && currentUpscaler != entry.upscaler)
                config->FfxUpscalerIndex = entry.ffxIndex;
        }

        // Dimmed tags aligned to the right
        const char* tag = nullptr;

        if (entry.ffxIndex >= 0 && IsOn12(entry.upscaler))
            tag = "FFX Dx12";
        else if (entry.ffxIndex >= 0)
            tag = "FFX";
        else if (IsOn12(entry.upscaler))
            tag = "Dx12";

        if (tag != nullptr)
        {
            auto& style = ImGui::GetStyle();

            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - style.WindowPadding.x -
                                                                      ImGui::CalcTextSize(tag).x));
            ImGui::TextDisabled(tag);
        }
    }

    ImGui::EndCombo();
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_on12, Upscaler::FSR21_on12,
                          Upscaler::FSR22_on12, Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::FSR21_on12,
                          Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { 6, "PRESET F", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = "Unknown";
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), preview.c_str()))
    {
        for (const auto& opt : options)
        {
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", opt.tooltip.c_str());

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static UiTargetMode getUiTargetMode()
{
    const auto& state = State::Instance();

    const bool fallback = !Config::Instance()->OverlayMenu.value_or_default();

    if (fallback)
    {
        // We have no reliable swapchain/output color-space information here.
        // Only classify the upscaled working image.
        if (state.currentFeature && state.currentFeature->IsHdr())
            return UiTargetMode::LinearHDR;

        return UiTargetMode::SDR;
    }

    const auto& output = state.outputColorSpace;

    // If SetColorSpace1 has not provided a known/valid color space,
    // fall back conservatively.
    if (!output.valid)
        return UiTargetMode::SDR;

    switch (output.transfer)
    {
    case ColorTransfer::Linear:
        // scRGB: linear Rec.709 RGB.
        //
        // hdrOutputActive is intentionally NOT required here.
        // A scRGB swapchain is still linear even when the physical output
        // is currently SDR. hdrOutputActive only affects the desired
        // reference-white scaling in toneMapColor().
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec709)
        {
            return UiTargetMode::ScRGB;
        }

        break;

    case ColorTransfer::PQ:
        // Direct PQ UI rendering currently assumes RGB PQ / Rec.2020.
        //
        // Do not treat YCbCr PQ as an RGB render target.
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec2020)
        {
            return UiTargetMode::PQ;
        }

        break;

    case ColorTransfer::HLG:
        // Current HLG UI path assumes an RGB render target.
        //
        // DXGI HLG modes are commonly YCbCr, so reject unsupported
        // combinations rather than applying an RGB HLG transform blindly.
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec2020)
        {
            return UiTargetMode::HLG;
        }

        break;

    case ColorTransfer::SRGB:
        return UiTargetMode::SDR;

    case ColorTransfer::Unknown:
    default:
        break;
    }

    // Unsupported model / primaries / transfer combination.
    return UiTargetMode::SDR;
}

static float srgbToLinear(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);

    if (x <= 0.04045f)
        return x / 12.92f;

    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static float linearToPQ(float nits)
{
    // SMPTE ST.2084
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;

    const float y = std::clamp(nits / 10000.0f, 0.0f, 1.0f);
    const float ym1 = std::pow(y, m1);

    return std::pow((c1 + c2 * ym1) / (1.0f + c3 * ym1), m2);
}

static float linearToHLG(float x)
{
    // BT.2100 HLG OETF
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;

    x = std::max(x, 0.0f);

    if (x <= (1.0f / 12.0f))
        return std::sqrt(3.0f * x);

    return a * std::log(12.0f * x - b) + c;
}

static ImVec4 linear709To2020(float r, float g, float b)
{
    return ImVec4(0.6274040f * r + 0.3292820f * g + 0.0433136f * b, 0.0690970f * r + 0.9195400f * g + 0.0113612f * b,
                  0.0163916f * r + 0.0880132f * g + 0.8955950f * b, 0.0f);
}

static ImVec4 legacyHdrToneMap(const ImVec4& color)
{
    constexpr float exposure = 1.0f;
    constexpr float strength = 1.0f;

    const float peak = std::max(color.x, std::max(color.y, color.z));

    if (peak <= 0.0f)
        return color;

    const float exposedPeak = peak * exposure;
    const float mappedPeak = exposedPeak / (1.0f + exposedPeak);

    const float reinhardScale = mappedPeak / peak;
    const float scale = 1.0f + (reinhardScale - 1.0f) * strength;

    return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    const auto mode = getUiTargetMode();

    switch (mode)
    {
    case UiTargetMode::SDR:
        return color;

    case UiTargetMode::LinearHDR:
        return ImVec4(srgbToLinear(color.x), srgbToLinear(color.y), srgbToLinear(color.z), color.w);

    case UiTargetMode::ScRGB:
    {
        constexpr float scRgbReferenceWhiteNits = 80.0f;
        constexpr float hdrUiWhiteNits = 203.0f;

        const float uiWhiteNits = State::Instance().hdrOutputActive ? hdrUiWhiteNits : scRgbReferenceWhiteNits;

        const float scale = uiWhiteNits / scRgbReferenceWhiteNits;

        return ImVec4(srgbToLinear(color.x) * scale, srgbToLinear(color.y) * scale, srgbToLinear(color.z) * scale,
                      color.w);
    }

    case UiTargetMode::PQ:
        // Direct ImGui rendering into a nonlinear PQ target.
        //
        // Proper PQ encoding of vertex colors produces incorrect results
        // with the standard ImGui alpha blend state because blending then
        // happens in PQ space.
        //
        // Keep the known-good legacy compression until PQ rendering is
        // moved to a linear intermediate/composite pass.
        return legacyHdrToneMap(color);

    case UiTargetMode::HLG:
        // Same fundamental nonlinear-blending problem as PQ.
        // Conservative compatibility behavior for now.
        return legacyHdrToneMap(color);

    default:
        return color;
    }
}

static void MenuHdrCheck(ImGuiIO io)
{
    if (!_hdrTonemapApplied)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        const auto mode = getUiTargetMode();

        LOG_INFO("Output HDR: {}, UI Mode: {}", State::Instance().hdrOutputActive, magic_enum::enum_name(mode));

        CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

        // Apply tone mapping to the ImGui style
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            ImVec4 color = style.Colors[i];
            style.Colors[i] = toneMapColor(color);
        }

        _hdrTonemapApplied = true;
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    case 128:
        return "OMUAV";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 0:
        return "-";
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

void MenuCommon::ApplyThemeStyle()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    ImGuiStyle& style = ImGui::GetStyle();

    auto conf = Config::Instance();
    bool lightTheme = conf->LightTheme.value_or_default();

    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FrameBorderSize = lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = lightTheme ? 1.0f : 0.0f;

    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;

    auto Clamp01 = [](float v) { return std::max(0.0f, std::min(v, 1.0f)); };

    auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
    { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

    auto Luminance = [](const ImVec4& c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };

    auto Saturate = [&](const ImVec4& color, float amount)
    {
        float lum = Luminance(color);

        return ImVec4(Clamp01(lum + (color.x - lum) * amount), Clamp01(lum + (color.y - lum) * amount),
                      Clamp01(lum + (color.z - lum) * amount), color.w);
    };

    ImVec4 accent = ImVec4(conf->MenuAccentColorR.value_or_default(), conf->MenuAccentColorG.value_or_default(),
                           conf->MenuAccentColorB.value_or_default(), 1.0f);

    ImVec4 bgAccent = ImVec4(conf->MenuBGColorR.value_or_default(), conf->MenuBGColorG.value_or_default(),
                             conf->MenuBGColorB.value_or_default(), 1.0f);

    float luminance = Luminance(accent);

    const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

    const ImVec4 textPrimary = lightTheme ? ImVec4(0.05f, 0.06f, 0.08f, 1.00f) : ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDim = lightTheme ? ImVec4(0.22f, 0.25f, 0.31f, 1.00f) : ImVec4(0.54f, 0.58f, 0.62f, 1.00f);

    const ImVec4 borderCol = lightTheme ? ImVec4(0.35f, 0.40f, 0.50f, 1.00f) : ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    const ImVec4 dimBg = lightTheme ? ImVec4(0.30f, 0.33f, 0.38f, 0.20f) : ImVec4(0.09f, 0.10f, 0.13f, 0.20f);
    const ImVec4 modalDimBg = lightTheme ? ImVec4(0.22f, 0.24f, 0.28f, 0.55f) : ImVec4(0.04f, 0.04f, 0.07f, 0.55f);

    // MenuBGColor: only background/surface tint.
    auto BgTint = [&](const ImVec4& base, float strength = 1.0f, float alpha = 1.0f)
    {
        float t = lightTheme ? (0.180f * strength) : (0.120f * strength);
        return Mix(base, bgAccent, t, alpha);
    };

    // MenuAccentColor: all visible interactive accent colors.
    auto AccentSoft = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.14f, alpha) : Mix(bgDark, accent, 0.32f, alpha); };

    auto AccentMed = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha); };

    auto AccentStrong = [&](float alpha = 1.0f) { return ImVec4(accent.x, accent.y, accent.z, alpha); };

    const ImVec4 bgTitle = AccentSoft();

    auto SurfaceHover = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.12f, alpha) : Mix(bgLight, accent, 0.18f, alpha); };

    auto SurfaceActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.20f, alpha) : Mix(bgLight, accent, 0.28f, alpha); };

    auto TitleActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgTitle, accent, 0.18f, alpha) : Mix(bgTitle, accent, 0.16f, alpha); };

    auto PlotAccent = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            // Darken slightly for contrast on light bg — no channel floors
            return Mix(accent, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.20f, alpha);
        }

        // Brighten slightly for visibility on dark bg — no channel floors
        return Mix(accent, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.35f, alpha);
    };

    auto PlotAccentHovered = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            return Mix(PlotAccent(alpha), ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.15f, alpha);
        }

        return Mix(PlotAccent(alpha), ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.25f, alpha);
    };

    auto AccentReadable = [&](float alpha = 1.0f)
    {
        // Apply saturation boost and luminance correction only here,
        // so AccentStrong / AccentMed / AccentSoft stay true to the user's pick.
        ImVec4 a = Saturate(accent, lightTheme ? 1.35f : 1.25f);
        float lum = Luminance(a);

        if (lightTheme && lum > 0.72f)
            a = Mix(a, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f, 1.0f);

        if (!lightTheme && lum < 0.25f)
            a = Mix(a, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.30f, 1.0f);

        return ImVec4(a.x, a.y, a.z, alpha);
    };

    ImVec4* c = ImGui::GetStyle().Colors;

    float minAlpha = Config::Instance()->MenuBGColorA.value_or_default() >= 0.5f
                         ? Config::Instance()->MenuBGColorA.value_or_default()
                         : 0.5f;

    c[ImGuiCol_Text] = textPrimary;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_TextLink] = AccentReadable();

    // MenuBGColor only.
    c[ImGuiCol_WindowBg] = BgTint(bgDark, 1.00f, Config::Instance()->MenuBGColorA.value_or_default());
    c[ImGuiCol_ChildBg] = BgTint(bgMid, 1.10f, minAlpha + 0.1f);
    c[ImGuiCol_PopupBg] =
        lightTheme ? BgTint(bgLight, 0.90f) : BgTint(ImVec4(0.09f, 0.10f, 0.13f, 0.97f), 0.90f, 0.97f);
    c[ImGuiCol_MenuBarBg] = BgTint(bgDark, 0.85f);
    c[ImGuiCol_DockingEmptyBg] = BgTint(bgDark, 0.75f);

    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Neutral background, not MenuBGColor.
    c[ImGuiCol_FrameBg] = BgTint(bgLight, 0.50f, minAlpha + 0.15f);
    c[ImGuiCol_FrameBgHovered] = SurfaceHover();
    c[ImGuiCol_FrameBgActive] = SurfaceActive();

    c[ImGuiCol_TitleBg] = BgTint(bgTitle, 0.40f);
    c[ImGuiCol_TitleBgActive] = TitleActive();
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bgTitle.x, bgTitle.y, bgTitle.z, 0.75f);

    c[ImGuiCol_ScrollbarBg] = BgTint(bgDark, 0.60f, minAlpha + 0.2f);
    c[ImGuiCol_ScrollbarGrab] = AccentSoft();
    c[ImGuiCol_ScrollbarGrabHovered] = AccentMed();
    c[ImGuiCol_ScrollbarGrabActive] = AccentStrong();

    c[ImGuiCol_CheckMark] = AccentReadable();
    c[ImGuiCol_SliderGrab] = AccentMed();
    c[ImGuiCol_SliderGrabActive] = AccentReadable();
    c[ImGuiCol_InputTextCursor] = AccentReadable();

    c[ImGuiCol_Button] = AccentSoft();
    c[ImGuiCol_ButtonHovered] = AccentMed();
    c[ImGuiCol_ButtonActive] = AccentStrong();

    c[ImGuiCol_Header] = AccentSoft(0.90f);
    c[ImGuiCol_HeaderHovered] = AccentMed(0.95f);
    c[ImGuiCol_HeaderActive] = AccentStrong();

    c[ImGuiCol_Separator] = borderCol;
    c[ImGuiCol_SeparatorHovered] = AccentMed(0.85f);
    c[ImGuiCol_SeparatorActive] = AccentStrong();

    c[ImGuiCol_ResizeGrip] = AccentSoft(0.30f);
    c[ImGuiCol_ResizeGripHovered] = AccentStrong(0.70f);
    c[ImGuiCol_ResizeGripActive] = AccentStrong(0.95f);

    c[ImGuiCol_Tab] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabHovered] = AccentMed(0.85f);
    c[ImGuiCol_TabSelected] = AccentMed();
    c[ImGuiCol_TabSelectedOverline] = AccentStrong();
    c[ImGuiCol_TabDimmed] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabDimmedSelected] = AccentSoft(0.75f);
    c[ImGuiCol_TabDimmedSelectedOverline] = borderCol;

    c[ImGuiCol_DockingPreview] = AccentStrong(0.70f);

    c[ImGuiCol_PlotLines] = PlotAccent();
    c[ImGuiCol_PlotLinesHovered] = PlotAccentHovered();
    c[ImGuiCol_PlotHistogram] = PlotAccent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = PlotAccentHovered();

    c[ImGuiCol_TableHeaderBg] = BgTint(bgMid, 0.80f, minAlpha + 0.25f);
    c[ImGuiCol_TableBorderStrong] = borderCol;
    c[ImGuiCol_TableBorderLight] = lightTheme ? ImVec4(0.68f, 0.72f, 0.80f, 1.00f) : AccentSoft();
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = lightTheme ? ImVec4(0.00f, 0.00f, 0.00f, 0.045f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    c[ImGuiCol_TreeLines] = borderCol;
    c[ImGuiCol_TextSelectedBg] = AccentMed(0.38f);
    c[ImGuiCol_DragDropTarget] = AccentStrong(0.90f);
    c[ImGuiCol_NavCursor] = AccentReadable();
    c[ImGuiCol_NavWindowingHighlight] = AccentStrong(0.70f);
    c[ImGuiCol_NavWindowingDimBg] = dimBg;
    c[ImGuiCol_ModalWindowDimBg] = modalDimBg;

    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

static std::string splashMessage;

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        if (state.activeFgInput == FGInput::NoFG || state.activeFgOutput == FGOutput::NoFG)
            MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                if (fgHotkeyLeftDefault)
                {
                    // Second press goes back to following the game
                    fgHotkeyLeftDefault = false;
                    config->FGEnabled = std::optional<bool> {};
                    LOG_DEBUG("FG toggle key pressed, following the game again");
                }
                else if (config->FGFollowsGame())
                {
                    // Volatile so the ini keeps auto
                    fgHotkeyLeftDefault = true;
                    config->FGEnabled.set_volatile_value(!config->IsFGEnabled());
                    LOG_DEBUG("FG toggle key pressed, overriding the game with {}", config->FGEnabled.value());
                }
                else
                {
                    config->FGEnabled = !config->IsFGEnabled();
                    LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value());
                }

                if (config->IsFGEnabled())
                    state.fgChanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value());
                state.artursFgFileAvailable = enablerExists.Get(optiPath / L"dlss-enabler-headless.dll");
                state.nukemsFgFileAvailable = nukemsExists.Get(optiPath / L"dlssg_to_fsr3_amd_is_better.dll");

                if (State::Instance().currentFeature != nullptr)
                {
                    auto upscalerType = State::Instance().currentFeature->GetUpscalerType();

                    if (upscalerType == Upscaler::DLSSD)
                        comboPreset = config->DLSSDRenderPresetForAll.value_or_default();
                    else if (upscalerType == Upscaler::DLSS || upscalerType == Upscaler::DLSS_on12)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                ImGui::CloseCurrentPopup();

                _showMipmapCalcWindow = false;
                _showHudlessWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& versionStatus = ctx.versionStatus;

    constexpr double splashTime = 7000.0;
    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Late Streamline hook detected");
            notification.setContent(
                "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise.",
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        if (state.postCodes & PostCode::TryingFsr4Fp8OnUnsupported)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Silly goose detected");
            notification.setContent("FSR 4 FP8 only works on AMD");
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    // Initialize splash timing and select the splash text once per process.
    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

        std::srand(static_cast<unsigned>(std::time(nullptr)));
        splashMessage = splashText[std::rand() % splashText.size()];
    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;
    auto& newFrame = ctx.newFrame;

    // New frame check
    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0)
    {
        if (!_isUWP)
        {
            ImGui_ImplWin32_NewFrame();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderSplashWindow(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;

    constexpr double fadeTime = 1000.0;

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (!config->OverlaysUseTheme.value_or_default())
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text("OptiScaler - %s for menu",
                            Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            if (!config->OverlaysUseTheme.value_or_default())
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);
        }
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    const UiTargetMode uiTargetMode = getUiTargetMode();
    const bool tonemapRequired = uiTargetMode != UiTargetMode::SDR;

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::gameUsesDxvk())
            {
                if (state.swapchainInteropApi == SwapchainInteropApi::None)
                    api = "DXVK";
                else
                    api = "DXVK w/Dx12";
            }
            else if (IdentifyGpu::gameUsesVkd3dProton())
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
                        api = "D3D11 w/DX12";
                    else
                        api = "D3D12";

                    break;

                default:
                    switch (state.api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? (" (" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            const int fakeFramesCount = state.dlssgDetectedInterpolationCount;
            auto formatFg = [&](std::string_view name, int maxFakeFrames)
            {
                if (fakeFramesCount > maxFakeFrames)
                    return std::format(" ({} Doesn't support more than {}x)", name, maxFakeFrames);

                else if (fakeFramesCount == 0)
                    return std::format(" ({} off)", name);

                return std::format(" ({} x{})", name, fakeFramesCount + 1);
            };

            const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
            if (activeNvngxFg == FGNvngxReplacement::Arturs)
            {
                fgText = formatFg("Enabler", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                fgText = formatFg("Nukems", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::FFX)
            {
                fgText = formatFg("FFX", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Combo)
            {
                fgText = formatFg("Combo", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (state.activeFgOutput == FGOutput::DLSSG && fg)
            {
                fgText = formatFg("DLSSG", fg->GetMaxInterpolationCount());
            }

            const auto overlayType = config->FpsOverlayType.value_or_default();
            const bool hasFeature = currentFeature && !currentFeature->IsFrozen();

            // Prepare Line 1
            std::string featurePart;
            std::string fpsPart;

            if (hasFeature)
            {
                const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

                featurePart = StrFmt(" | %s -> %s %u.%u.%u%s", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                                     currentFeature->ShortName().c_str(), currentFeature->Version().major,
                                     currentFeature->Version().minor, currentFeature->Version().patch,
                                     usesDx12CompatLayer ? " w/Dx12" : "");
            }

            uint32_t interpolatedFrameCount = 0;
            if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
                interpolatedFrameCount = fg->GetInterpolatedFrameCount();

            if (interpolatedFrameCount)
            {
                const double baseFps = frameRate / (double) (interpolatedFrameCount + 1);

                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f/%5.1f ", frameRate, baseFps);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, %7.2f ms", frameRate, baseFps, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, Avg: %6.1f", frameRate, baseFps, 1000.0f / averageFrameTime);
                    break;
                }
            }
            else
            {
                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f ", frameRate);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f, %7.2f ms", frameRate, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f, Avg: %6.1f", frameRate, 1000.0f / averageFrameTime);
                    break;
                }
            }

            if (overlayType == FpsOverlay_JustFPS)
                firstLine = StrFmt("%s", fpsPart.c_str());
            else
                firstLine = StrFmt("%s | %s%s%s", api.c_str(), fpsPart.c_str(), fgText.c_str(), featurePart.c_str());

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::Text(firstLine.c_str());

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

#ifdef LOW_LATENCY_INPUTS
                static TimingData timingData {};

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = InputCommon::get_timing_data(timingData);
                    previousPoll = now;
                }

                if (gotData && timingData.timeRange.has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData.timeRange.value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Low latency timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](const auto& timingOpt, const char* desc, ImVec4 color)
                    {
                        if (!timingOpt.has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        const auto& timing = timingOpt.value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);

                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;

                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);

                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);

                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);

                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(timingData.simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(timingData.renderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(timingData.present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(timingData.driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(timingData.osRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(timingData.gpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#else
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Reflex timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#endif
            }
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

void MenuCommon::RenderMainMenuHeaderMessages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& versionStatus = ctx.versionStatus;
    auto& currentVersionText = ctx.currentVersionText;

    if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        ImGui::SetWindowFocus();

    if (config->MenuScale.has_value())
    {
        _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
    }
    else
    {
        _selectedScale = 0;
    }

    if (versionStatus.completed)
    {
        if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Update available: %s (current %s)",
                               versionStatus.latestTag.c_str(), currentVersionText.c_str());

            if (!versionStatus.latestUrl.empty())
            {
                ImGui::SameLine();
                ImGui::TextLinkOpenURL("Open release page", versionStatus.latestUrl.c_str());
            }

            ImGui::Spacing();
        }
        else if (!versionStatus.error.empty())
        {
            LOG_ERROR("Version check failed: {0}", versionStatus.error);
            versionStatus.error.clear();
        }
        // Disabled error message
        // else if (!versionStatus.error.empty())
        //{
        //    ImGui::Spacing();
        //    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.4f, 0.f, 1.f)), "%s", versionStatus.error.c_str());
        //    ImGui::Spacing();
        //}
    }

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)));
        ImGui::TextWrapped("nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
}

void MenuCommon::RenderUpscalerStateMessage(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    // No active upscaler message
    if (currentFeature == nullptr || !currentFeature->IsInited())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.0f);

        if (state.nvngxExists || state.nvngxReplacement.has_value() ||
            (state.libxessExists || XeSSProxy::Module() != nullptr))
        {
            ImGui::Spacing();

            std::vector<std::string> upscalers;

            if (state.fsrHooks)
                upscalers.push_back("FSR");

            if (state.nvngxExists || state.nvngxReplacement.has_value() || primaryGpu.dlssCapable)
                upscalers.push_back("DLSS");

            if (state.libxessExists || XeSSProxy::Module() != nullptr)
                upscalers.push_back("XeSS");

            auto joined = upscalers | std::views::join_with(std::string { " or " });

            std::string joinedUpscalers(joined.begin(), joined.end());

            ImGui::PushTextWrapPos(0.0f);
            ImGui::Text("Please select %s as upscaler from game options and load a save game to enable Opti settings.\n"
                        "Upscalers don't always work in menus.",
                        joinedUpscalers.c_str());
            ImGui::PopTextWrapPos();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFontSize();
            else
                ImGui::SetWindowFontScale(menuResScale);

            ImGui::Spacing();

            RenderDetectedCard(ctx);

            ImGui::Spacing();
        }
        else
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::Text("Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFont();
            else
                ImGui::SetWindowFontScale(menuResScale);

            RenderDetectedCard(ctx);
        }
    }
    else if (currentFeature->IsFrozen())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.0f);

        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("%s active, but currently not used by the game\nPlease load into the game",
                    currentFeature->Name().c_str());
        ImGui::PopTextWrapPos();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(menuResScale);
    }
}

void MenuCommon::RenderActiveUpscalerSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    // UPSCALERS -----------------------------
    SectionTitle("Upscalers");

    GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

    ImGui::PushItemWidth(150.0f * menuResScale);

    const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;

    if (!usesDlssd)
    {
        switch (state.api)
        {
        case DX11:
            AddDx11Backends(currentBackend);
            break;

        case DX12:
            AddDx12Backends(currentBackend);
            break;

        default:
            AddVulkanBackends(currentBackend);
        }
    }

    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::TextDisabled("%dx%d -> %dx%d (%.2fx)", currentFeature->RenderWidth(), currentFeature->RenderHeight(),
                        currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                        (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth());

    if (!usesDlssd)
    {
        if (state.newBackend != Upscaler::Reset && state.newBackend != currentBackend)
        {
            if (state.newBackend == Upscaler::XeSS)
            {
                // Reseting them for xess
                config->DisableReactiveMask.reset();
                config->DlssReactiveMaskBias.reset();
            }

            MARK_ALL_BACKENDS_CHANGED();
        }
    }

    if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
    {
        ImGui::Spacing();
        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "nvngx_dlss.dll not found, DLSS disabled!");
    }

    // Dx11 with Dx12
    if (state.api == DX11 && currentFeature->IsWithDx12())
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Dx11 with Dx12 Settings"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            if (bool dontUseNTShared = config->DontUseNTShared.value_or_default();
                ImGui::Checkbox("Don't Use NTShared", &dontUseNTShared))
                config->DontUseNTShared = dontUseNTShared;

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    if (state.api == Vulkan && currentFeature->IsWithDx12())
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Vulkan with Dx12 Settings"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            if (bool inputsUseCopy = config->VulkanUseCopyForInputs.value_or_default();
                ImGui::Checkbox("Use CopyResource for Inputs", &inputsUseCopy))
                config->VulkanUseCopyForInputs = inputsUseCopy;

            if (bool outputUseCopy = config->VulkanUseCopyForOutput.value_or_default();
                ImGui::Checkbox("Use CopyResource for Output", &outputUseCopy))
                config->VulkanUseCopyForOutput = outputUseCopy;

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // UPSCALER SPECIFIC -----------------------------

    // XeSS -----------------------------
    if (currentBackend == Upscaler::XeSS && !usesDlssd)
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("XeSS Settings"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
            auto configModes = config->NetworkModel.value_or_default();

            if (configModes < 0 || configModes > 5)
                configModes = 0;

            const char* selectedModel = models[configModes];

            if (ImGui::BeginCombo("Network Models", selectedModel))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(models[n], (config->NetworkModel.value_or_default() == n)))
                    {
                        config->NetworkModel = n;
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                }

                ImGui::EndCombo();
            }
            ShowTooltip("Likely doesn't do much");

            if (bool dbg = state.xessDebug; ImGui::Checkbox("Dump (Shift+Del)", &dbg))
                state.xessDebug = dbg;

            ImGui::SameLine(0.0f, 6.0f);
            int dbgCount = state.xessDebugFrames;

            ImGui::PushItemWidth(95.0f * menuResScale);
            if (ImGui::InputInt("frames", &dbgCount))
            {
                if (dbgCount < 4)
                    dbgCount = 4;
                else if (dbgCount > 999)
                    dbgCount = 999;

                state.xessDebugFrames = dbgCount;
            }

            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // FFX -----------------
    if (!usesDlssd && (currentBackend == Upscaler::FFX || currentBackend == Upscaler::FFX_on12))
    {
        if (auto versions = GetFfxVersionList(currentBackend, state.api);
            versions != nullptr && !versions->names.empty())
        {
            ImGui::Spacing();

            const int ffxIndex = config->FfxUpscalerIndex.value_or_default();
            auto currentName =
                ffxIndex >= 0 && ffxIndex < versions->names.size() ? StrFmt("FSR %s", versions->names[ffxIndex]) : "";

            ImGui::PushItemWidth(150.0f * menuResScale);

            if (ImGui::BeginCombo("FFX Upscaler", currentName.c_str()))
            {
                for (auto n : FfxVersionsNewestFirst(*versions))
                {
                    if (ImGui::Selectable(StrFmt("FSR %s##%d", versions->names[n], n).c_str(), ffxIndex == n) &&
                        ffxIndex != n)
                    {
                        config->FfxUpscalerIndex = n;
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowTooltip("List of upscalers reported by FFX SDK");
        }

        auto majorFsrVersion = currentFeature->Version().major;

        if (majorFsrVersion >= 4)
        {
            ImGui::Spacing();

            // Colorspaces
            const char* colorSpaces[] = { "Linear (Default)", "Non-Linear", "Non-Linear sRGB", "Non-Linear PQ" };
            int currentColorSpace = 0;
            if (config->FsrNonLinearPQ.value_or_default())
                currentColorSpace = 3;
            else if (config->FsrNonLinearSRGB.value_or_default())
                currentColorSpace = 2;
            else if (config->FsrNonLinearColorSpace.value_or_default())
                currentColorSpace = 1;

            ImGui::SetNextItemWidth(150.0f * menuResScale);
            if (ImGui::Combo("Input Color Space", &currentColorSpace, colorSpaces, IM_ARRAYSIZE(colorSpaces)))
            {
                bool isSrgb = (currentColorSpace == 2);
                bool isPq = (currentColorSpace == 3);

                config->FsrNonLinearSRGB = isSrgb;
                config->FsrNonLinearPQ = isPq;

                if (isSrgb || isPq)
                {
                    config->FsrNonLinearColorSpace.set_volatile_value(true);
                }
                else if (currentColorSpace == 1) // Just non-Linear
                {
                    config->FsrNonLinearColorSpace = true;
                }
                else // Linear
                {
                    config->FsrNonLinearColorSpace = false;
                }

                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
            ShowTooltip("Select the input color space that the game uses.\n"
                        "Non-Linear / sRGB: Might improve FSR4 upscaling quality, might increase ghosting.\n"
                        "PQ: Rarest, might increase ghosting and break lights.");

            // FSR 4 Presets
            const char* presets[] = {
                "Default", "Preset 0", "Preset 1", "Preset 2", "Preset 3", "Preset 4", "Preset 5"
            };
            int currentPresetIdx = config->Fsr4Preset.has_value() ? config->Fsr4Preset.value() + 1 : 0;

            if (currentPresetIdx < 0 || currentPresetIdx >= IM_ARRAYSIZE(presets))
                currentPresetIdx = 0;

            ImGui::SetNextItemWidth(150.0f * menuResScale);
            if (ImGui::Combo("FSR4 Preset", &currentPresetIdx, presets, IM_ARRAYSIZE(presets)))
            {
                if (currentPresetIdx == 0)
                    config->Fsr4Preset.reset();
                else
                    config->Fsr4Preset = currentPresetIdx - 1;

                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
            ShowTooltip("Each internal FSR4 preset is tuned for a specific resolution.\n"
                        "Selecting an FSR4 preset won't change the in-game\nupscaler preset!!!\n\n"
                        "Preset 0 is meant for FSR Native AA\n"
                        "Preset 1 is meant for Quality/Ultra Quality\n"
                        "Preset 2 is meant for Balanced\n"
                        "Preset 3 is meant for Performance\n"
                        "Preset 4 is meant for DRS\n"
                        "Preset 5 is meant for Ultra Performance");

            // Display the active preset right next to the combo box instead of using a table
            ImGui::SameLine();
            if (state.currentFsr4Preset.has_value())
                ImGui::TextDisabled("(Active: %d)", state.currentFsr4Preset.value());
            else if (FSR4ModelSelection::IsInt8FsrHooked())
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "(Potential FSR3 fallback)");
            else
                ImGui::TextDisabled("(Failed to hook)");
        }

        if (majorFsrVersion >= 3)
        {
            ImGui::Spacing();

            bool debugView = config->FsrDebugView.value_or_default();
            if (ImGui::Checkbox("Upscaler Debug View", &debugView))
            {
                config->FsrDebugView = debugView;

                // FSR 4's debug view requires backend reinit
                if (majorFsrVersion > 3)
                {
                    state.newBackend = currentBackend;
                    MARK_ALL_BACKENDS_CHANGED();
                }
            }

            if (majorFsrVersion > 3)
            {
                ShowTooltip("Top left: Dilated Motion Vectors\n"
                            "Top right: Predicted Blend Factor");
            }
            else
            {
                ShowTooltip("Top left: Dilated Motion Vectors\n"
                            "Top middle: Protected Areas\n"
                            "Top right: Dilated Depth\n"
                            "Middle: Upscaled frame\n"
                            "Bottom left: Disocclusion mask\n"
                            "Bottom middle: Reactiveness\n"
                            "Bottom right: Detail Protection Takedown");
            }

            if (majorFsrVersion > 3)
            {
                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                bool fsr4wm = config->Fsr4EnableWatermark.value_or_default();
                if (ImGui::Checkbox("Watermark", &fsr4wm))
                {
                    LOG_DEBUG("FSR4 Watermark set to {}", fsr4wm);
                    config->Fsr4EnableWatermark = fsr4wm;
                }

                ShowTooltip("After changing this option, please Save Settings.\n"
                            "It will be applied on next launch.");
            }
        }

        if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
            currentFeature->Version() < feature_version { 4, 0, 0 })
        {
            ImGui::Spacing();

            if (currentFeature != nullptr)
            {
                ImGui::Text("FSR 3.1 Presets:");

                ImGui::SameLine(0.0f, 6.0f);

                // This will be applied by default
                if (ImGui::Button("Stability"))
                {
                    auto const scaleRatioX =
                        (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                    auto const scaleRatioY =
                        (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                    auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                    config->FsrVelocity = 0.5f;
                    config->FsrReactiveScale = 0.25f;

                    config->FsrShadingScale.reset();
                    config->FsrAccAddPerFrame.reset();
                    config->FsrMinDisOccAcc.reset();
                    config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);
                    config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                    config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Motion"))
                {
                    auto const scaleRatioX =
                        (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                    auto const scaleRatioY =
                        (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                    auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                    config->FsrVelocity = 1.0f;
                    config->FsrReactiveScale = 0.5f;

                    config->FsrShadingScale.reset();
                    config->FsrAccAddPerFrame.reset();
                    config->FsrMinDisOccAcc.reset();
                    config->FsrShadingScale.set_volatile_value(1.0f / scaleRatio);
                    config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                    config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Default"))
                {
                    config->FsrVelocity = 1.0f;
                    config->FsrReactiveScale = 1.0f;
                    config->FsrShadingScale = 1.0f;
                    config->FsrAccAddPerFrame = 0.333f;
                    config->FsrMinDisOccAcc = -0.333f;
                }
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("FSR 3 Upscaler Manual Tuning"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();
                ImGui::Spacing();

                ImGui::PushItemWidth(220.0f * menuResScale);

                float velocity = config->FsrVelocity.value_or_default();
                if (ImGui::SliderFloat("Velocity Factor", &velocity, 0.00f, 1.0f, "%.2f"))
                    config->FsrVelocity = velocity;

                ShowTooltip("Value of 0.0f can improve temporal stability of bright pixels\n"
                            "Lower values are more stable with ghosting\n"
                            "Higher values are more pixelly, but less ghosting");

                if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                {
                    // Reactive Scale
                    float reactiveScale = config->FsrReactiveScale.value_or_default();
                    if (ImGui::SliderFloat("Reactive Scale", &reactiveScale, 0.0f, 1.0f, "%.3f"))
                        config->FsrReactiveScale = reactiveScale;

                    ShowTooltip("Meant for development purpose to test if\n"
                                "writing a larger value to reactive mask, reduces ghosting.");

                    // Shading Scale
                    float shadingScale = config->FsrShadingScale.value_or_default();
                    if (ImGui::SliderFloat("Shading Scale", &shadingScale, 0.0f, 1.0f, "%.3f"))
                        config->FsrShadingScale = shadingScale;

                    ShowTooltip("Increasing this scales FSR3.1 computed shading\n"
                                "change value at read to have higher reactiveness.");

                    // Accumulation Added Per Frame
                    float accAddPerFrame = config->FsrAccAddPerFrame.value_or_default();
                    if (ImGui::SliderFloat("Acc. Added Per Frame", &accAddPerFrame, 0.0f, 1.0f, "%.3f"))
                        config->FsrAccAddPerFrame = accAddPerFrame;

                    ShowTooltip("Corresponds to amount of accumulation added per frame\n"
                                "at pixel coordinate where disocclusion occured or when\n"
                                "reactive mask value is > 0.0f. Decreasing this and \n"
                                "drawing the ghosting object (IE no mv) to reactive mask \n"
                                "with value close to 1.0f can decrease temporal ghosting.\n"
                                "Decreasing this could result in more thin feature pixels flickering.");

                    // Min Disocclusion Accumulation
                    float minDisOccAcc = config->FsrMinDisOccAcc.value_or_default();
                    if (ImGui::SliderFloat("Min. Disocclusion Acc.", &minDisOccAcc, -1.0f, 1.0f, "%.3f"))
                        config->FsrMinDisOccAcc = minDisOccAcc;

                    ShowTooltip("Increasing this value may reduce white pixel temporal\n"
                                "flickering around swaying thin objects that are disoccluding \n"
                                "one another often. Too high value may increase ghosting.");
                }

                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // DLSS -----------------
    if ((config->DLSSEnabled.value_or_default() &&
         (currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12) &&
         currentFeature->Version().major > 2) ||
        usesDlssd)
    {
        ImGui::Spacing();

        auto overridden = usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

        if (overridden)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Presets are overridden externally");
            ShowTooltip("This usually happens due to using tools\n"
                        "such as Nvidia App or Nvidia Inspector");
            // ImGui::Text("Selecting setting below will disable that external override\n"
            //             "but you need to Save Settings and restart the game");

            ImGui::Spacing();
        }

        if (usesDlssd)
        {
            if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                ImGui::Checkbox("Render Presets Override", &pOverride))
                config->DLSSDRenderPresetOverride = pOverride;

            ShowTooltip("Each render preset has it strengths and weaknesses\n"
                        "Override to potentially improve image quality\n"
                        "Press apply after enable/disable");

            /*
            auto currentPresetIndex = GetPresetIndex(currentFeature, true);

            if (currentPresetIndex == 0)
                ImGui::Text("Current Preset: Default");
            else
                ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
            */

            ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() /*|| overridden*/);
            ImGui::PushItemWidth(135.0f * menuResScale);

            AddDLSSDRenderPreset("Override Preset", &comboPreset);

            ImGui::PopItemWidth();
            ImGui::EndDisabled();
        }
        else
        {
            if (bool pOverride = config->RenderPresetOverride.value_or_default();
                ImGui::Checkbox("Render Presets Override", &pOverride))
                config->RenderPresetOverride = pOverride;

            ShowTooltip("Each render preset has it strengths and weaknesses\n"
                        "Override to potentially improve image quality\n"
                        "Press Apply after enable/disable");

            /*
            auto currentPresetIndex = GetPresetIndex(currentFeature, false);

            if (currentPresetIndex == 0)
                ImGui::Text("Current Preset: Default");
            else
                ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
            */

            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() /*|| overridden*/);

            ImGui::PushItemWidth(135.0f * menuResScale);

            AddDLSSRenderPreset("Override Preset", &comboPreset);

            ImGui::PopItemWidth();
            ImGui::EndDisabled();
        }

        ImGui::SameLine(0.0f, 6.0f);

        if (ImGui::Button("Apply Changes"))
        {
            LOG_DEBUG("Applying DLSS/DLSSD preset override changes, preset index: {}", comboPreset.value_or_default());

            if (usesDlssd)
            {
                config->DLSSDRenderPresetForAll = comboPreset.value_or_default();
                state.newBackend = Upscaler::DLSSD;
            }
            else
            {
                config->RenderPresetForAll = comboPreset.value_or_default();
                state.newBackend = currentBackend;
            }

            MARK_ALL_BACKENDS_CHANGED();
        }

        ImGui::Spacing();

        if (auto ch = ScopedCollapsingHeader(usesDlssd ? "Advanced DLSSD Settings" : "Advanced DLSS Settings");
            ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            bool appIdOverride = config->UseGenericAppIdWithDlss.value_or_default();
            if (ImGui::Checkbox("Use Generic App Id with DLSS", &appIdOverride))
                config->UseGenericAppIdWithDlss = appIdOverride;

            ShowTooltip("Use generic appid with NGX\n"
                        "Fixes OptiScaler preset override not working with certain games\n"
                        "Requires a game restart");

            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);
            ImGui::Spacing();
            ImGui::PushItemWidth(135.0f * menuResScale);

            if (usesDlssd)
            {
                AddDLSSDRenderPreset("DLAA Preset", &config->DLSSDRenderPresetDLAA);
                AddDLSSDRenderPreset("UltraQ Preset", &config->DLSSDRenderPresetUltraQuality);
                AddDLSSDRenderPreset("Quality Preset", &config->DLSSDRenderPresetQuality);
                AddDLSSDRenderPreset("Balanced Preset", &config->DLSSDRenderPresetBalanced);
                AddDLSSDRenderPreset("Perf Preset", &config->DLSSDRenderPresetPerformance);
                AddDLSSDRenderPreset("UltraP Preset", &config->DLSSDRenderPresetUltraPerformance);
            }
            else
            {
                AddDLSSRenderPreset("DLAA Preset", &config->RenderPresetDLAA);
                AddDLSSRenderPreset("UltraQ Preset", &config->RenderPresetUltraQuality);
                AddDLSSRenderPreset("Quality Preset", &config->RenderPresetQuality);
                AddDLSSRenderPreset("Balanced Preset", &config->RenderPresetBalanced);
                AddDLSSRenderPreset("Perf Preset", &config->RenderPresetPerformance);
                AddDLSSRenderPreset("UltraP Preset", &config->RenderPresetUltraPerformance);
            }
            ImGui::PopItemWidth();
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }
}

void MenuCommon::RenderFrameGenerationSelection(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    /// FG INPUTS

    static std::vector<MenuOption<FGInput>> inputOptions;
    inputOptions.clear();

    // clang-format off

    inputOptions = {
        { FGInput::NoFG, "None" },
        { FGInput::Upscaler, "OptiFG (Upscaler)",
            "Upscaler must be enabled\n\nCan be used with any FG Output, but might be imperfect with some\nTo prevent UI glitching, HUDfix required" },
        { FGInput::DLSSG, "DLSSG via Streamline",
            "Can be used with any FG Output\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\n\nLimited to games that use Streamline" },
        { FGInput::NvngxFG, "DLSSG via Nvngx",
            "Limited to variants of FSR FG\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\nUses Streamline swapchain for pacing" },
        { FGInput::FSRFG, "FSR 3.1 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::FSRFG30, "FSR 3.0 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::XeFG, "XeFG" }
    };

    // clang-format on

    auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;

    // XeFG input requirements
    auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
    inputOptions[xefgInputIndex].set_disabled(true, "Support not implemented, they meant FG Output");

    // OptiFG requirements
    auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
    inputOptions[optiFgIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady() &&
        !ffxInitTried)
    {
        ffxInitTried = true;
        FfxApiProxy::InitFfxDx12();
        inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(), "amd_fidelityfx_dx12.dll is missing");
    }
    else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG && !xefgInitTried &&
             XeFGProxy::Module() == nullptr)
    {
        xefgInitTried = true;
        XeFGProxy::InitXeFG();
        inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, "libxess_fg.dll is missing");
    }

    // DLSSG inputs requirements
    auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
    // inputOptions[dlssgInputIndex].set_disabled(state.streamlineVersion.major == 0, "Game doesn't use streamline");
    inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, "Unsupported API");

    // FSRFG inputs requirements
    auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
    inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    // FSRFG30 inputs requirements
    auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
    inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    if (!config->FGInput.has_value())
        config->FGInput = config->FGInput.value_or_default(); // need to have a value before combo

    /// FG OUTPUTS

    static std::vector<MenuOption<FGOutput>> outputOptions;
    outputOptions.clear();

    // clang-format off

    outputOptions = {
        { FGOutput::NoFG, "None" },
        { FGOutput::FSRFG, "FSR FG", "FSR3/4-FG, RDNA4 autoupgrades to FSR4-FG\n\nFSR4-FG sometimes better/worse than XeFG" },
        { FGOutput::DLSSG, "DLSSG", "DLSSG output\nCan be used in conjuction with Nukem's for example" },
        { FGOutput::XeFG, "XeFG", "XeFG - heaviest, but best universal FG\n\nXeFG 3 overall deals best with HUD\n\nEnable UI Composition if HUD ghosting" },
        { FGOutput::Reprojection, "Reprojection (WIP)", "Reprojects the game image using new mouse data\nKinda like Reflex 2, perceived latency improvement\n\n- REQUIRES DLSSG VIA STREAMLINE AS INPUT\n- Only works with first person perspective games\n- Only mouse, no controller\n- If possible, disable any mouse/camera smoothing in the game\n" },
    };

    // clang-format on

    // DLSSG output requirements
    auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
    const bool maySupportDlssg = primaryGpu.vendorId == VendorId::Nvidia;
    const bool hasDlssgReplacement =
        state.nukemsFgFileAvailable || state.artursFgFileAvailable || FfxApiProxy::IsFGReady(false);

    if (!maySupportDlssg && hasDlssgReplacement)
    {
        outputOptions[dlssgOutputIndex].tooltip =
            "No real DLSSG, unsupported hardware\nOnly Nvngx FG replacements available";
    }

    outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    outputOptions[dlssgOutputIndex].set_disabled(!maySupportDlssg && !hasDlssgReplacement,
                                                 "Unsupported hardware and no replacements");

    // For that one case of DX11 DLSSG
    const auto streamlineVersion = state.streamlineVersion;
    const bool nukemsUnsupportedApi =
        state.swapchainApi == API::DX11 &&
        (streamlineVersion == feature_version { 0, 0, 0 } || streamlineVersion > feature_version { 2, 0, 1 });
    inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");

    // FSR FG output requirements
    auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
    outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    // XeFG output requirements
    auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
    outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    // Unsupported FG input selected
    const auto currentInputIndex = (uint32_t) state.activeFgInput;
    if (config->FGInput != FGInput::NoFG && inputOptions.size() > currentInputIndex &&
        inputOptions[currentInputIndex].disabled && state.activeFgInput == config->FGInput)
    {
        LOG_WARN("Resetting FGInput to NoFG: {}", inputOptions[currentInputIndex].label);
        config->FGInput = FGInput::NoFG;

        // Changing active can be dangerous but we are talking about an unsupported mode
        // which shouldn't even actually have taken affect
        state.activeFgInput = FGInput::NoFG;
    }

    // Unsupported FG output selected
    const auto currentOutputIndex = (uint32_t) state.activeFgOutput;
    if (config->FGOutput != FGOutput::NoFG && outputOptions.size() > currentOutputIndex &&
        outputOptions[currentOutputIndex].disabled && state.activeFgOutput == config->FGOutput)
    {
        LOG_WARN("Resetting FGOutput to NoFG: {}", outputOptions[currentOutputIndex].label);
        config->FGOutput = FGOutput::NoFG;
        state.activeFgOutput = FGOutput::NoFG;
    }

    if (!config->FGOutput.has_value())
        config->FGOutput = config->FGOutput.value_or_default(); // need to have a value before combo

    /// FG NVNGX REPLACEMENT

    static std::vector<MenuOption<FGNvngxReplacement>> nvngxOptions;
    nvngxOptions.clear();

    // clang-format off

    nvngxOptions = {
        { FGNvngxReplacement::None, "None (Real DLSSG)", "Real DLSSG, For RTX 40xx and above"},
        { FGNvngxReplacement::Nukems, "Nukem's", "FSR 3 FG" },
        { FGNvngxReplacement::Arturs, "Enabler", "FSR 3 MFG mod" },
        { FGNvngxReplacement::FFX, "FSR 3/4 FG", "FSR 3/4 FG using the FFX upgrade\n\n"
                                                 "Partially based on Nukems, uses SL swapchain\n"
                                                 "Possibly better performance and frame pacing compared to FSR-FG output"},
        { FGNvngxReplacement::Combo, "FFX + Enabler", "Use if FSR4-FG is supported, otherwise stick to Enabler\n\n"
                                                      "FFX used for the middle fake frame, Enabler for the rest\n\n"
                                                      "2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler\n\n"
                                                      "Due to pacing, only odd number of fake frames are able to use FFX"},
    };

    // clang-format on

    bool replaceFgOutputWithNvngx = false;
    bool showNvngxFgDowndown = false;

    if (config->FGInput == FGInput::NvngxFG)
    {
        config->FGOutput = FGOutput::NoFG;
        replaceFgOutputWithNvngx = true;
    }
    else if (config->FGOutput == FGOutput::DLSSG)
    {
        showNvngxFgDowndown = true;
    }

    auto constexpr fgNvngxNoneIndex = (uint32_t) FGNvngxReplacement::None;
    nvngxOptions[fgNvngxNoneIndex].set_disabled(!maySupportDlssg, "Unsupported hardware");

    if (replaceFgOutputWithNvngx)
    {
        nvngxOptions[fgNvngxNoneIndex].label = "None";
        nvngxOptions[fgNvngxNoneIndex].set_hidden(true);
    }

    auto constexpr fgNvngxNukemsIndex = (uint32_t) FGNvngxReplacement::Nukems;
    nvngxOptions[fgNvngxNukemsIndex].set_disabled(!state.nukemsFgFileAvailable,
                                                  "Missing dlssg_to_fsr3_amd_is_better.dll");

    auto constexpr fgNvngxArtursIndex = (uint32_t) FGNvngxReplacement::Arturs;
    nvngxOptions[fgNvngxArtursIndex].set_disabled(!state.artursFgFileAvailable, "Missing dlss-enabler-headless.dll");

    auto constexpr fgNvngxFfxIndex = (uint32_t) FGNvngxReplacement::FFX;
    nvngxOptions[fgNvngxFfxIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    nvngxOptions[fgNvngxFfxIndex].set_disabled(!FfxApiProxy::IsFGReady(false),
                                               "Missing amd_fidelityfx_framegeneration_dx12.dll");

    auto constexpr fgNvngxComboIndex = (uint32_t) FGNvngxReplacement::Combo;
    nvngxOptions[fgNvngxComboIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    nvngxOptions[fgNvngxComboIndex].set_disabled(
        !FfxApiProxy::IsFGReady(false) || !state.artursFgFileAvailable,
        "Missing amd_fidelityfx_framegeneration_dx12.dll\nor missing dlss-enabler-headless.dll");

    // TODO: Automatically switch to any other option

    if (!config->FGNvngxReplacement.has_value())
        config->FGNvngxReplacement = config->FGNvngxReplacement.value_or_default(); // need to have a value before combo

    if (state.activeFgInput != FGInput::ForceXeLL)
    {
        SectionTitle("Frame Generation");
        ShowTooltip("Which copium do you choose?");

        if (ImGui::BeginTable("fgSelection", 1, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();

            PopulateCombo("FG Input", config->FGInput, inputOptions);
            ShowTooltip("The data source to be used for FG\n"
                        "The native FG which the game supports");

            ImGui::TableNextColumn();

            if (replaceFgOutputWithNvngx)
            {
                // Disable None?
                PopulateCombo("FG Nvngx", config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("What backend to use instead of the real DLSSG");
            }
            else
            {
                PopulateCombo("FG Output", config->FGOutput, outputOptions);
                ShowTooltip("The FG that you will actually be using");
            }

            if (showNvngxFgDowndown)
            {
                ImGui::TableNextColumn();

                PopulateCombo("FG Nvngx Replacement", config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("What backend to use instead of the real DLSSG");
            }

            ImGui::EndTable();
        }

        // Try to avoid having None selected when the gpu doesn't support DLSSG + some fallbacks
        if (!maySupportDlssg && (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
            config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::None)
        {
            if (state.nukemsFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Nukems);

            else if (state.artursFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Arturs);

            else if (FfxApiProxy::IsFGReady(false))
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::FFX);
        }

        const bool nvngxFgChanged = (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
                                    state.activeFgNvngx != config->FGNvngxReplacement.value_or_default();
        state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                  state.activeFgInput != config->FGInput.value_or_default() || nvngxFgChanged;

        if (state.fgSettingsChanged)
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)),
                               "Save Settings and restart to apply the changes");
            ImGui::Spacing();
        }

        const bool dlssgInputOrOutput =
            state.activeFgOutput == FGOutput::DLSSG || state.activeFgInput == FGInput::DLSSG;

        ImGui::BeginDisabled(state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default());
        if (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1 && !dlssgInputOrOutput)
        {
            auto maxInterpolationCount = state.dlssgMfgMax.value();

            if (maxInterpolationCount >= 1)
            {
                // Map config value to UI index
                int currentSet = 0;
                if (config->FGDLSSGOverrideInterpolationCount.has_value())
                {
                    currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
                }

                std::string currentIntCountStr;
                if (currentSet == 0)
                    currentIntCountStr = "Default";
                else if (currentSet == 1)
                    currentIntCountStr = "Off";
                else
                    currentIntCountStr = std::to_string(currentSet) + "X";

                ImGui::PushItemWidth(95.0f * menuResScale);

                if (ImGui::BeginCombo("Override DLSSG Ratio", currentIntCountStr.c_str()))
                {
                    for (int i = 0; i <= maxInterpolationCount + 1; i++)
                    {
                        std::string modeStr;
                        if (i == 0)
                            modeStr = "Default";
                        else if (i == 1)
                            modeStr = "Off";
                        else
                            modeStr = std::to_string(i) + "X";

                        if (ImGui::Selectable(modeStr.c_str(), (currentSet == i)))
                        {
                            if (i == 0)
                            {
                                // Default, no override
                                config->FGDLSSGOverrideInterpolationCount.reset();
                            }
                            else
                            {
                                // UI index, store value
                                int framesToGenerate = i - 1;

                                LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                                config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                            }

                            StreamlineHooks::updateDlssgOptions();
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();
            }
        }

        ImGui::EndDisabled();

        if (state.dlssgGameDMFGSupported && !dlssgInputOrOutput)
        {
            ImGui::SameLine(0.0f, 16.0f);

            if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
                ImGui::Checkbox("Force Dynamic MFG", &dynamicMFG))
            {
                config->FGDLSSGOverrideForceDMFG = dynamicMFG;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::BeginDisabled(state.dlssgLastSetMode != sl::DLSSGMode::eDynamic);
            static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
            ImGui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, 200, "%.0f");

            ShowTooltip("An active limit of 0 means auto-detect the display refresh rate");

            if (ImGui::Button("Apply Target"))
            {
                config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Reset Target"))
            {
                fpsTarget = 0.0f;
                config->FGDLSSGFramerateTargetDMFG.reset();
            }

            ImGui::EndDisabled();
        }

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if ((state.activeFgOutput != FGOutput::Reprojection && state.activeFgOutput != FGOutput::NoFG &&
             state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
            fgOutput)
        {
            ImGui::Checkbox("Show Detected UI", &state.fgHudlessCompare);
            ShowTooltip("Needs HUDless texture to compare with final image.\n"
                        "UI elements and ONLY UI elements should have a pink tint!");

            const auto isUsingUIAny = fgOutput->IsUsingUIAny();

            ImGui::BeginDisabled(!isUsingUIAny);

            if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                ImGui::Checkbox("Draw UI over", &drawUIOverFG))
            {
                config->FGDrawUIOverFG = drawUIOverFG;
            }
            ShowTooltip("Draws UI resource over the final image\n"
                        "If no UI visible, enable this!");

            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

            if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                ImGui::Checkbox("UI Premult. alpha", &uiPremultipliedAlpha))
            {
                config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
            }
            ShowTooltip("If UI is too faint, disable this option");

            ImGui::EndDisabled();
        }

        const bool showOutputSpecificFGSettings = state.activeFgInput == FGInput::DLSSG ||
                                                  state.activeFgInput == FGInput::FSRFG ||
                                                  state.activeFgInput == FGInput::FSRFG30;

        const bool showHudCutoff = state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::FSRFG;

        if (showOutputSpecificFGSettings || showHudCutoff)
        {
            if (auto ch = ScopedCollapsingHeader("Advanced FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (showOutputSpecificFGSettings)
                {
                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput)
                    {
                        ImGui::BeginDisabled(!fgOutput->IsActive());

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                        const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                        bool disableUI = config->FGDisableUI.value_or_default();
                        ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                        if (ImGui::Checkbox("Disable UI texture", &disableUI))
                        {
                            config->FGDisableUI = disableUI;
                            fgOutput->UpdateTarget();
                        }

                        ShowTooltip("For when the game sends a UI texture, but you want to disable it");

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);

                        bool disableHudless = config->FGDisableHudless.value_or_default();
                        ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                        if (ImGui::Checkbox("Disable HUDless", &disableHudless))
                        {
                            config->FGDisableHudless = disableHudless;
                        }

                        ShowTooltip("For when the game sends HUDless, but you want to disable it");

                        ImGui::EndDisabled();

                        bool depthValidNow = config->FGDepthValidNow.value_or_default();
                        if (ImGui::Checkbox("Depth as ValidNow", &depthValidNow))
                            config->FGDepthValidNow = depthValidNow;

                        ShowTooltip("Will use more VRAM, but Uniscaler needs this\n"
                                    "Maybe some other games might need too");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                        if (ImGui::Checkbox("Velocity as ValidNow", &velocityValidNow))
                            config->FGVelocityValidNow = velocityValidNow;

                        ShowTooltip("Will use more VRAM, but Uniscaler needs this\n"
                                    "Maybe some other games might need too");

                        bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                        if (ImGui::Checkbox("HUDless as ValidNow", &hudlessValidNow))
                            config->FGHudlessValidNow = hudlessValidNow;

                        ShowTooltip("Will use more VRAM, but some games might need this");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                        if (ImGui::Checkbox("Accept First HUDless", &firstHudless))
                            config->FGOnlyAcceptFirstHudless = firstHudless;

                        ShowTooltip("If source tags more than one HUDless, only use the first one");

                        if (bool skipReset = config->FGSkipReset.value_or_default();
                            ImGui::Checkbox("Skip Reset", &skipReset))
                        {
                            config->FGSkipReset = skipReset;
                        }

                        ShowTooltip("Don't use reset signals from FG Inputs");

                        ImGui::EndDisabled();

                        ImGui::PushItemWidth(80.0f * menuResScale);

                        auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                        if (ImGui::InputInt("Frame Ahead", &frameAhead, 1, 1) && frameAhead > 0 && frameAhead < 4)
                        {
                            config->FGAllowedFrameAhead = frameAhead;
                        }

                        ShowTooltip("Number of frames the FG is allowed to be ahead of the game\n"
                                    "Might prevent FG on/off switching, but also might cause issues");

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 16.0f);

                        const char* ftSources[] = { "Input", "Opti", "Zero" };
                        const char* ftSourceInfos[] = { "Uses frametimes provided by\nDLSSG or FSR-FG ",
                                                        "Uses frametimes calculated by Opti",
                                                        "Let XeFG to handle frametimes" };

                        auto currentSet = (int) config->FTInput.value_or_default();
                        auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                        ImGui::PushItemWidth(95.0f * menuResScale);

                        if (ImGui::BeginCombo("FT Input", ftSources[currentSet]))
                        {
                            for (size_t i = 0; i < currentSourceCount; i++)
                            {

                                if (ImGui::Selectable(ftSources[i], currentSet == i))
                                {
                                    LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet], ftSources[i]);
                                    config->FTInput = (FrameTimeSource) i;
                                }

                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    ImGui::SetTooltip(ftSourceInfos[i]);
                            }

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ShowTooltip("Select source for frametime\n"
                                    "Might help frame pacing and stutter issues");
                    }
                }

                if (showHudCutoff)
                {
                    float fgHudCutoff = config->FGHudCutoff.value_or_default();
                    if (ImGui::SliderFloat("Hud Cutoff", &fgHudCutoff, 0.00f, 1.0f, "%.2f"))
                        config->FGHudCutoff = fgHudCutoff;

                    ShowTooltip("Cutoffs transparency from UI to help with interpolation\n"
                                "You can use Show Detected UI to see the difference\n0.0 is auto");
                }
            }
        }
    }
}

// Replaces the Active checkbox + MFG combo of FG outputs
// maxInterpolationCount of 0 means the output doesn't generate frames, "On" is shown instead of "2X"
void MenuCommon::RenderFGModeCombo(RenderMenuContext& ctx, const char* label, int maxInterpolationCount,
                                   CustomOptional<int>* countConfig)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // -1 = Default (follow game), 0 = Off, >0 = interpolated frame count
    auto modeName = [&](int mode) -> std::string
    {
        if (mode < 0)
            return "Default";

        if (mode == 0)
            return "Off";

        if (maxInterpolationCount == 0)
            return "On";

        return std::format("{}X", mode + 1);
    };

    const int maxMode = std::max(maxInterpolationCount, 1);

    int current = 0;
    if (config->FGFollowsGame())
        current = -1;
    else if (config->FGEnabled.value_or_default())
        current = countConfig != nullptr ? std::clamp(countConfig->value_or_default(), 1, maxMode) : 1;

    std::string preview = modeName(current);
    if (current < 0)
    {
        const int gameMode =
            state.dlssgInputGameEnabled ? std::min(state.dlssgInputGameInterpolationCount, maxMode) : 0;
        preview += std::format(" ({})", modeName(gameMode));
    }

    ImGui::PushItemWidth(115.0f * ctx.menuResScale);

    if (ImGui::BeginCombo(label, preview.c_str()))
    {
        const int firstMode = Config::FGCanFollowGame() ? -1 : 0;

        for (int mode = firstMode; mode <= maxMode; mode++)
        {
            if (!ImGui::Selectable(modeName(mode).c_str(), current == mode) || current == mode)
                continue;

            fgHotkeyLeftDefault = false;

            if (mode < 0)
            {
                config->FGEnabled = std::optional<bool> {};
            }
            else
            {
                config->FGEnabled = mode > 0;

                if (mode > 0 && countConfig != nullptr)
                    *countConfig = mode;
            }

            LOG_DEBUG("FG mode set to: {}", modeName(mode));

            if (config->IsFGEnabled())
                state.fgChanged = true;
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();

    if (Config::FGCanFollowGame())
        ShowTooltip("Frame Generation mode\nDefault follows the game's DLSS FG setting");
    else
        ShowTooltip("Frame Generation mode");
}

void MenuCommon::RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;
    auto fgOutput = state.currentFG;

    const UiTargetMode uiTargetMode = getUiTargetMode();
    const bool outputIsHdr = uiTargetMode != UiTargetMode::SDR;

    // FSR FG controls
    if (ScopedCard::Shows("fg_fsrfg") && state.activeFgOutput == FGOutput::FSRFG &&
        state.activeFgInput != FGInput::NoFG && state.currentFGSwapchain != nullptr)
    {
        if (state.activeFgInput != FGInput::Upscaler ||
            (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
        {
            ScopedCard card { "FSR FG Settings" };

            if (_ffxFGIndex < 0)
                _ffxFGIndex = config->FfxFGIndex.value_or_default();

            if (state.ffxFGVersionNames.size() > 0)
            {
                ImGui::PushItemWidth(115.0f * menuResScale);

                auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                if (ImGui::BeginCombo("FFX FG Version", currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                        if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                        {
                            _ffxFGIndex = n;
                            config->FfxFGIndex = _ffxFGIndex;
                            state.fgChanged = true;
                            state.scChanged = true;
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowTooltip("List of FGs reported by FFX SDK");
            }

            RenderFGModeCombo(ctx, "Frame Gen##fsrfg", 1, nullptr);

            bool fgAsync = config->FGAsync.value_or_default();
            if (ImGui::Checkbox("Allow Async", &fgAsync))
            {
                config->FGAsync = fgAsync;

                if (config->IsFGEnabled())
                {
                    state.fgChanged = true;
                    state.scChanged = true;
                    LOG_DEBUG("Async set FGChanged");
                }
            }
            ShowTooltip("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

            ImGui::SameLine(0.0f, 16.0f);

            bool fgDV = config->FGDebugView.value_or_default();
            if (ImGui::Checkbox("Debug View##2", &fgDV))
            {
                config->FGDebugView = fgDV;

                if (config->IsFGEnabled())
                {
                    state.fgChanged = true;
                    LOG_DEBUG("DebugView set FGChanged");
                }
            }
            ShowTooltip("Enable FSR3.1-FG Debug view\n\n"
                        "Top left: Game Motion Vectors\n"
                        "Top middle: GMV Depth\n"
                        "Top right: Optical Flow MV\n"
                        "Middle: Interpolated frame only\n"
                        "Bottom left: Disocclusion mask\n"
                        "Bottom middle: Interpolation source (w/o UI)\n"
                        "Bottom right: HUDless resource");

            ImGui::SameLine(0.0f, 16.0f);

            if (state.currentFG && state.currentFG->Version().major > 3)
            {
                if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                    ImGui::Checkbox("Enable Watermark", &fgwm))
                {
                    LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                    config->FSRFGEnableWatermark = fgwm;
                }

                ShowTooltip("After changing this option, please Save Settings\n"
                            "It will be applied on next launch.");
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Extended FSR FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::Checkbox("FG Only Generated", &state.fgOnlyGenerated);
                ShowTooltip("Display only FSR 3.1 Generated frames");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugResetLines = config->FGDebugResetLines.value_or_default();
                if (ImGui::Checkbox("Debug Reset Lines", &debugResetLines))
                {
                    config->FGDebugResetLines = debugResetLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                }
                ShowTooltip("Enables drawing of Interpolation skip lines");

                auto debugTearLines = config->FGDebugTearLines.value_or_default();
                if (ImGui::Checkbox("Debug Tear Lines", &debugTearLines))
                {
                    config->FGDebugTearLines = debugTearLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                }
                ShowTooltip("Enables drawing of Tear and Interpolation skip lines");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                if (ImGui::Checkbox("Debug Pacing Lines", &debugPacingLines))
                {
                    config->FGDebugPacingLines = debugPacingLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                }
                ShowTooltip("Enables drawing of Pacing lines");

                ImGui::Spacing();
                if (ImGui::TreeNode("FG Rectangle Settings"))
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    int rectLeft = config->FGRectLeft.value_or(0);
                    if (ImGui::InputInt("Rect Left", &rectLeft))
                        config->FGRectLeft = rectLeft;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectTop = config->FGRectTop.value_or(0);
                    if (ImGui::InputInt("Rect Top", &rectTop))
                        config->FGRectTop = rectTop;

                    int rectWidth = config->FGRectWidth.value_or(0);
                    if (ImGui::InputInt("Rect Width", &rectWidth))
                        config->FGRectWidth = rectWidth;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectHeight = config->FGRectHeight.value_or(0);
                    if (ImGui::InputInt("Rect Height", &rectHeight))
                        config->FGRectHeight = rectHeight;

                    ImGui::PopItemWidth();
                    ShowTooltip("Frame generation rectangle, adjust for letterboxed content");

                    ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                         !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                    if (ImGui::Button("Reset FG Rect"))
                    {
                        config->FGRectLeft.reset();
                        config->FGRectTop.reset();
                        config->FGRectWidth.reset();
                        config->FGRectHeight.reset();
                    }

                    ShowTooltip("Resets Frame generation rectangle");

                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }

                auto fg = state.currentFG;
                if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                    FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                {
                    ImGui::Spacing();

                    if (ImGui::TreeNode("Frame Pacing Tuning"))
                    {
                        auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                        if (ImGui::Checkbox("Enable Tuning", &fptEnabled))
                        {
                            config->FGFramePacingTuning = fptEnabled;
                            state.fsrfgFramePaceTuningChanged = true;
                        }

                        ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                        if (ImGui::InputFloat("Safety Margins in ms", &fptSafetyMargin, 0.01f, 0.1f, "%.2f"))
                            config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                        ShowTooltip("Safety margins in millisecons\n"
                                    "FSR default value: 0.1ms\n"
                                    "Opti default value: 0.01ms");

                        auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                        if (ImGui::SliderFloat("Variance Factor", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                            config->FGFPTVarianceFactor = fptVarianceFactor;
                        ShowTooltip("Variance factor\n"
                                    "FSR default value: 0.1\n"
                                    "Opti default value: 0.3");
                        ImGui::PopItemWidth();

                        auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                        if (ImGui::Checkbox("Enable Hybrid Spin", &fpHybridSpin))
                            config->FGFPTAllowHybridSpin = fpHybridSpin;
                        ShowTooltip("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                    "Might cause slow ramp up of FPS");

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                        if (ImGui::SliderInt("Hybrid Spin Time", &fptHybridSpinTime, 0, 100))
                            config->FGFPTHybridSpinTime = fptHybridSpinTime;
                        ShowTooltip("How long to spin if FPTHybridSpin is true. Measured in timer "
                                    "resolution units.\n"
                                    "Not recommended to go below 2. Will result in frequent overshoots");
                        ImGui::PopItemWidth();

                        auto fpWaitForSingleObjectOnFence =
                            config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                        if (ImGui::Checkbox("Enable WaitForSingleObjectOnFence", &fpWaitForSingleObjectOnFence))
                        {
                            config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                        }
                        ShowTooltip("Allows WaitForSingleObject instead of spinning for fence value");

                        if (ImGui::Button("Apply Timing Changes"))
                            state.fsrfgFramePaceTuningChanged = true;

                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // XeFG controls
    if (ScopedCard::Shows("fg_xefg") && state.activeFgOutput == FGOutput::XeFG &&
        state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::ForceXeLL &&
        state.currentFGSwapchain != nullptr && XeFGProxy::InitXeFG() && fgOutput)
    {
        ScopedCard card { "XeFG Settings" };

        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

        bool nativeAA = false;
        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

        const bool correctMVs = fgOutput->IsLowResMV() || nativeAA ||
                                (State::Instance().gameQuirks & GameQuirk::ForceFGRenderSizeMVs) || ignoreChecks;

        if (!correctMVs || state.realExclusiveFullscreen)
        {
            config->FGEnabled.set_volatile_value(false);
            config->FGXeFGDebugView.reset();
        }

        const bool restartNeeded = config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                                   config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                                   config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV();

        bool cantActivate = false;
        if (restartNeeded)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Restart the game to apply correct XeFG settings!");
        }
        else
        {
            if (!correctMVs)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Requires disabling dilated motion vectors");

            if (!ignoreChecks && state.realExclusiveFullscreen)
            {
                cantActivate = true;
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Borderless display mode required!");
            }

            if (!ignoreChecks && outputIsHdr)
            {
                if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                    state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
                {
                    cantActivate = true;
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "XeFG only supports HDR10");
                }
            }
        }

        if (!correctMVs || cantActivate || ignoreChecks)
        {
            if (ImGui::Checkbox("Ignore Init Checks", &ignoreChecks))
                config->FGXeFGIgnoreInitChecks = ignoreChecks;

            ShowTooltip("Ignores all prechecks for XeFG\n"
                        "Don't use this option to skip MV size warning for UE games!\n"
                        "It might cause crashes and bad IQ!");
        }

        ImGui::BeginDisabled(!correctMVs || cantActivate);

        RenderFGModeCombo(ctx, "Frame Gen##xefg", fgOutput->GetMaxInterpolationCount(),
                          &config->FGXeFGInterpolationCount);

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() || XeFGProxy::SetUiCompositionState() == nullptr);
        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
        if (ImGui::Checkbox("UI Composition", &fgCompositeUI))
            config->FGXeFGUIComposition = fgCompositeUI;

        ShowTooltip("Disable HUD/UI interpolation\n"
                    "Reverts back to previous XeFG 2 behaviour\n\n"
                    "Fixes artifacting transparent HUD/UI");
        ImGui::EndDisabled();

        bool fgDV = config->FGXeFGDebugView.value_or_default();
        if (ImGui::Checkbox("Debug View##2", &fgDV))
        {
            config->FGXeFGDebugView = fgDV;

            if (config->FGXeFGDebugView.value_or_default())
            {
                state.fgChanged = true;
                LOG_DEBUG("DebugView set FGChanged");
            }
        }
        ShowTooltip("Enable XeFG Debug view");

        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 16.0f);
        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
        if (ImGui::Checkbox("Force Borderless", &fgBorderless))
            config->FGXeFGForceBorderless = fgBorderless;

        ShowTooltip("Forces Borderless display mode\n\n"
                    "For best results, set fullscreen \n"
                    "resolution to your display resolution\n"
                    "Might cause some instability issues.\n\n"
                    "NEEDS GAME RESTART TO BE ACTIVE!");

        // Disable this for now
        // ImGui::SameLine(0.0f, 16.0f);
        // ImGui::Checkbox("Only Generated##2", &state.fgOnlyGenerated);
        // ShowTooltip("Display only XeFG generated frames");

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Extended XeFG Settings"); ch.IsHeaderOpen())
        {
            ImGui::Spacing();
            if (ImGui::TreeNode("Rectangle Settings"))
            {
                ImGui::PushItemWidth(95.0f * menuResScale);
                int rectLeft = config->FGRectLeft.value_or(0);
                if (ImGui::InputInt("Rect Left##2", &rectLeft))
                    config->FGRectLeft = rectLeft;

                ImGui::SameLine(0.0f, 16.0f);
                int rectTop = config->FGRectTop.value_or(0);
                if (ImGui::InputInt("Rect Top##2", &rectTop))
                    config->FGRectTop = rectTop;

                int rectWidth = config->FGRectWidth.value_or(0);
                if (ImGui::InputInt("Rect Width##2", &rectWidth))
                    config->FGRectWidth = rectWidth;

                ImGui::SameLine(0.0f, 16.0f);
                int rectHeight = config->FGRectHeight.value_or(0);
                if (ImGui::InputInt("Rect Height##2", &rectHeight))
                    config->FGRectHeight = rectHeight;

                ImGui::PopItemWidth();
                ShowTooltip("Frame generation rectangle, adjust for letterboxed content##2");

                ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                     !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                if (ImGui::Button("Reset FG Rect##2"))
                {
                    config->FGRectLeft.reset();
                    config->FGRectTop.reset();
                    config->FGRectWidth.reset();
                    config->FGRectHeight.reset();
                }

                ShowTooltip("Resets Frame generation rectangle##2");

                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // DLSSG controls
    if (ScopedCard::Shows("fg_dlssg") && state.activeFgOutput == FGOutput::DLSSG &&
        state.activeFgInput != FGInput::NoFG && state.currentFGSwapchain != nullptr &&
        StreamlineProxy::LoadStreamline() && fgOutput)
    {
        ScopedCard card { "DLSSG Settings" };

        if (state.activeFgNvngx == FGNvngxReplacement::None && (state.hdrOutputActive && outputIsHdr))
        {
            if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "DLSSG only supports HDR10");
            }
        }

        ImGui::Text("Current DLSSG state:");
        ImGui::SameLine();
        if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
        {
            ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), std::format("ON {}x", count + 1).c_str());
        }
        else
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
        }

        RenderFGModeCombo(ctx, "Frame Gen##dlssg", fgOutput->GetMaxInterpolationCount(),
                          &config->FGDLSSGInterpolationCount);

        if (fgOutput->GetMaxInterpolationCount() > 1)
        {
            if (fgOutput->GetDMFGSupport())
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGForceDMFG.value_or_default();
                    ImGui::Checkbox("Force Dynamic MFG", &dynamicMFG))
                {
                    config->FGDLSSGForceDMFG = dynamicMFG;
                    state.fgChanged = true;
                }

                ImGui::BeginDisabled(!config->FGDLSSGForceDMFG.value_or_default());
                static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
                float fpsSliderMax = (float) std::max(200, refreshRate);
                ImGui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, fpsSliderMax, "%.0f");

                ShowTooltip("An active limit of 0 means auto-detect the display refresh rate");

                if (ImGui::Button("Apply Target"))
                {
                    config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                }

                ImGui::SameLine(0.0f, 16.0f);

                if (ImGui::Button("Reset Target"))
                {
                    fpsTarget = 0.0f;
                    config->FGDLSSGFramerateTargetDMFG.reset();
                }

                ImGui::EndDisabled();
            }
        }

        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
        if (ImGui::Checkbox("Use Game's Reflex Markers", &useGamesMarkers))
        {
            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
        }
        ImGui::EndDisabled();
    }

    if (ScopedCard::Shows("fg_reprojection") && fgOutput && fgOutput->HasReprojection())
    {
        ScopedCard card { "Reprojection Settings" };

        if (fgOutput->IsActive() && fgOutput->IsReprojectionActive())
        {
            ImGui::Text("Updated camera rotation by: %.1fms",
                        (float) fgOutput->GetLastTimeSinceSimStartNs() / 1'000'000.f);
        }
        else
        {
            ImGui::TextDisabled("Not updating camera rotation");
        }

        if (state.activeFgOutput == FGOutput::Reprojection)
        {
            RenderFGModeCombo(ctx, "Reprojection##mode", 0, nullptr);
        }
        else
        {
            bool reprojectionActive = config->FGReprojectionEnabled.value_or_default();
            if (ImGui::Checkbox("Active##5", &reprojectionActive))
            {
                config->FGReprojectionEnabled = reprojectionActive;
                LOG_DEBUG("Reprojection enabled: {}", reprojectionActive);
            }
            ShowTooltip("Enable reprojection");
        }

        ImGui::SameLine();

        ImGui::Checkbox("Show static elements", &state.fgHudlessCompare);
        ShowTooltip("For fine tuning the depth cutoff\n"
                    "Shows UI and depth cutoff areas\n"
                    "Adjust depth cutoff so that only stuff like your gun and hands are marked");

        ImGui::Spacing();

        // clang-format off
        static std::vector<MenuOption<ReprojectionFill>> fillModes = {
            { ReprojectionFill::StrechEdge, "Strech edge" },
            { ReprojectionFill::Dithering, "Dithering" },
            { ReprojectionFill::Noise, "Noise" },
            { ReprojectionFill::Debug, "Debug" }
        };
        // clang-format on

        // need to have a value before combo
        if (!config->ReprojectionFillMode.has_value())
            config->ReprojectionFillMode = config->ReprojectionFillMode.value_or_default();

        PopulateCombo("Edge fill mode", config->ReprojectionFillMode, fillModes);
        ShowTooltip("You want either dither or noise\n"
                    "Those two use the unprojected image as fill\n"
                    "and then some blending on the edges to fool the eye");

        float cutoff = config->ReprojectionDepthCutoff.value_or_default();
        if (ImGui::SliderFloat("Depth cutoff", &cutoff, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
            config->ReprojectionDepthCutoff = cutoff;
        ShowTooltip("Selects how many elements close to the camera\n"
                    "should be shown over the reprojected image.\n"
                    "This prevents your gun from being moved in weird ways.\n\n"
                    "Use \"Show static elements\" to help you adjust it\n"
                    "Unreal Engine games are usually around 0.10\n"
                    "Cyberpunk is around 0.02");

        uint32_t cutoffExpandPx = config->ReprojectionCutoffExpand.value_or_default();
        if (SliderUInt("Cutoff expand", &cutoffExpandPx, 0, 8))
            config->ReprojectionCutoffExpand = cutoffExpandPx;
        ShowTooltip("Expands the cutoff area by this many depth pixels\n"
                    "Use only when you see an outline left by the cutoff process");

        if (bool lateLatch = config->ReprojectionLateLatch.value_or_default();
            ImGui::Checkbox("Late latch mouse", &lateLatch))
        {
            config->ReprojectionLateLatch = lateLatch;
        }
        ShowTooltip("Reads the mouse right when the GPU starts the reprojection\n"
                    "instead of when the frame is presented on the CPU.\n"
                    "Lowers latency, especially when GPU bound");
    }

    // OptiFG
    if (ScopedCard::Shows("fg_optifg") && state.api != API::Vulkan && state.currentFGSwapchain != nullptr &&
        state.activeFgInput == FGInput::Upscaler)
    {
        ScopedCard card { "OptiFG Settings" };

        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
             (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
             (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr) ||
             state.activeFgOutput == FGOutput::Reprojection))
        {
            const bool dx11HudfixTracking = state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12;
            const bool hudfixTrackingSupported =
                !Config::Instance()->FGDisableHUDFix.value_or_default() &&
                (state.swapchainInteropApi == SwapchainInteropApi::None || dx11HudfixTracking);

            if (hudfixTrackingSupported)
            {
                bool fgHudfix = config->FGHUDFix.value_or_default();

                if (ImGui::Checkbox("HUDFix", &fgHudfix))
                {
                    config->FGHUDFix = fgHudfix;
                    LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                    state.clearCapturedHudlesses = true;
                    state.fgChanged = true;
                }

                ShowTooltip("Enable HUD stability fix, might cause crashes!");

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                ImGui::SameLine(0.0f, 16.0f);
                ImGui::PushItemWidth(95.0f * menuResScale);
                int hudFixLimit = config->FGHUDLimit.value_or_default();
                if (ImGui::InputInt("Limit", &hudFixLimit))
                {
                    if (hudFixLimit < 1)
                        hudFixLimit = 1;
                    else if (hudFixLimit > 999)
                        hudFixLimit = 999;

                    config->FGHUDLimit = hudFixLimit;
                    LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                }
                ShowTooltip("Delay HUDless capture, high values might cause crash!");

                ImGui::SameLine(0.0f, 16.0f);
                if (ImGui::Button("Res##2"))
                    _showHudlessWindow = !_showHudlessWindow;

                ImGui::EndDisabled();

                auto hudExtended = config->FGHUDFixExtended.value_or_default();
                if (ImGui::Checkbox("Extended", &hudExtended))
                {
                    LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                    config->FGHUDFixExtended = hudExtended;
                }
                ShowTooltip("Extended format checks for possible HUDless\nMight cause crashes and slowdowns!");
                ImGui::SameLine(0.0f, 16.0f);

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                auto immediate = config->FGImmediateCapture.value_or_default();
                if (ImGui::Checkbox("Immediate Capture", &immediate))
                {
                    LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                    config->FGImmediateCapture = immediate;
                }
                ShowTooltip("Enables capturing of resources before shader execution.\nIncrease HUDless "
                            "capture chances, but might cause capturing of unnecessary resources.");

                ImGui::PopItemWidth();

                ImGui::EndDisabled();
            }

            bool depthScale = config->FGEnableDepthScale.value_or_default();
            if (ImGui::Checkbox("Scale Depth to fix DLSS RR", &depthScale))
                config->FGEnableDepthScale = depthScale;
            ShowTooltip("Fix for DLSS-D wrong depth inputs");

            bool resourceFlip = config->FGResourceFlip.value_or_default();
            if (ImGui::Checkbox("Flip (Unity)", &resourceFlip))
                config->FGResourceFlip = resourceFlip;
            ShowTooltip("Flip Velocity & Depth resources of Unity games");

            ImGui::SameLine(0.0f, 16.0f);

            bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
            if (ImGui::Checkbox("Flip Use Offset", &resourceFlipOffset))
                config->FGResourceFlipOffset = resourceFlipOffset;
            ShowTooltip("Use height difference as offset");

            if (auto ch = ScopedCollapsingHeader("Advanced OptiFG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                ImGui::Spacing();

                auto disablehudfix = config->FGDisableHUDFix.value_or_default();
                {
                    if (ImGui::Checkbox("Disable HUDFix Resource Tracking", &disablehudfix))
                    {
                        config->FGDisableHUDFix = disablehudfix;
                        LOG_DEBUG("Enabled FGDisableHUDFix: {}", disablehudfix);
                    }
                    ShowTooltip("Disables HUDFix resource tracking and\n"
                                "reduces CPU overhead.\n"
                                "Useful for MLFG which deals well with the HUD.\n\n"
                                "After changing, please Save Settings and restart.");
                }

                if (hudfixTrackingSupported)
                {
                    ImGui::Spacing();

                    auto rb = config->FGResourceBlocking.value_or_default();
                    if (ImGui::Checkbox("Resource Blocking", &rb))
                    {
                        config->FGResourceBlocking = rb;
                        LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                    }
                    ShowTooltip("Block rarely used resources from using as HUDless \n"
                                "to prevent flickers and other issues\n\n"
                                "HUDfix enable/disable will reset the block list!");

                    ImGui::SameLine(0.0f, 16.0f);

                    auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                    if (ImGui::Checkbox("Relaxed Resource Check", &rrc))
                    {
                        config->FGRelaxedResolutionCheck = rrc;
                        LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                    }
                    ShowTooltip("Relax resolution checks for HUDless by 32 pixels \n"
                                "Helps games which use black borders for some \n"
                                "resolutions and screen ratios (e.g. Witcher 3)");

                    ImGui::BeginDisabled(state.fgResetCapturedResources);
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    if (ImGui::Checkbox("FG Create List", &state.fgCaptureResources))
                    {
                        if (!state.fgCaptureResources)
                            config->FGHUDLimit = 1;
                        else
                            state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::SameLine(0.0f, 16.0f);
                    if (ImGui::Checkbox("FG Use List", &state.fgOnlyUseCapturedResources))
                    {
                        if (state.fgCaptureResources)
                        {
                            state.fgCaptureResources = false;
                            config->FGHUDLimit = 1;
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("(%d)", state.fgCapturedResourceCount);

                    ImGui::PopItemWidth();

                    ImGui::SameLine(0.0f, 16.0f);

                    if (ImGui::Button("Reset List"))
                    {
                        LOG_DEBUG("Resetting captured resource list");

                        state.fgResetCapturedResources = true;
                        state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (ImGui::TreeNode("Tracking Settings"))
                    {
                        ImGui::BeginDisabled(dx11HudfixTracking);

                        auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                        if (ImGui::Checkbox("Always Track Heaps", &ath))
                        {
                            config->FGAlwaysTrackHeaps = ath;
                            LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                        }
                        ImGui::EndDisabled();

                        ShowTooltip(dx11HudfixTracking
                                        ? "D3D12 only; not applicable to DX11."
                                        : "Always track resources, might cause performance issues\n, but also might "
                                          "fix HUDFix related crashes!");

                        auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                        if (ImGui::Checkbox("Disable RTV Tracking", &disableRTV))
                            config->FGHudfixDisableRTV = disableRTV;
                        ShowTooltip("Disable tracking of CreateRenderTargetView\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                        if (ImGui::Checkbox("Disable SRV Tracking", &disableSRV))
                            config->FGHudfixDisableSRV = disableSRV;
                        ShowTooltip("Disable tracking of CreateShaderResourceView\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                        if (ImGui::Checkbox("Disable UAV Tracking", &disableUAV))
                            config->FGHudfixDisableUAV = disableUAV;
                        ShowTooltip("Disable tracking of CreateUnorderedAccessView\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableOM = config->FGHudfixDisableOM.value_or_default();
                        if (ImGui::Checkbox("Disable OM Tracking", &disableOM))
                            config->FGHudfixDisableOM = disableOM;
                        ShowTooltip("Disable tracking of OMSetRenderTargets\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                        if (ImGui::Checkbox("Disable SCR Tracking", &disableSCR))
                            config->FGHudfixDisableSCR = disableSCR;
                        ShowTooltip("Disable tracking of SetComputeRootDescriptorTable\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                        if (ImGui::Checkbox("Disable SGR Tracking", &disableSGR))
                            config->FGHudfixDisableSGR = disableSGR;
                        ShowTooltip("Disable tracking of SetGraphicsRootDescriptorTable\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::Spacing();

                        auto disableDI = config->FGHudfixDisableDI.value_or_default();
                        if (ImGui::Checkbox("Disable DI Tracking", &disableDI))
                            config->FGHudfixDisableDI = disableDI;
                        ShowTooltip("Disable tracking of DrawInstanced\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableDII = config->FGHudfixDisableDII.value_or_default();
                        if (ImGui::Checkbox("Disable DII Tracking", &disableDII))
                            config->FGHudfixDisableDII = disableDII;
                        ShowTooltip("Disable tracking of DrawIndexedInstanced\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                        if (ImGui::Checkbox("Disable Dispatch Tracking", &disableDispatch))
                            config->FGHudfixDisableDispatch = disableDispatch;
                        ShowTooltip("Disable tracking of Dispatch\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                if (ImGui::TreeNode("Resource Settings"))
                {
                    bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                    if (ImGui::Checkbox("FG Make MV Copies", &makeMVCopies))
                        config->FGMakeMVCopy = makeMVCopies;
                    ShowTooltip("Make a copy of motion vectors to use with OptiFG\n"
                                "For preventing corruptions that might happen");

                    bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                    if (ImGui::Checkbox("FG Make Depth Copies", &makeDepthCopies))
                        config->FGMakeDepthCopy = makeDepthCopies;
                    ShowTooltip("Make a copy of depth to use with OptiFG\n"
                                "For preventing corruptions that might happen");

                    ImGui::PushItemWidth(115.0f * menuResScale);
                    float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                    if (ImGui::InputFloat("FG Scale Depth Max", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                        config->FGDepthScaleMax = depthScaleMax;
                    ShowTooltip("Depth values will be divided to this value");
                    ImGui::PopItemWidth();

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                if (ImGui::TreeNode("Syncing Settings"))
                {
                    bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                    if (ImGui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                        config->FGUseMutexForSwapchain = useMutexForPresent;
                    ShowTooltip("Use mutex to prevent desync of FG and crashes\n"
                                "Disabling might improve the perf but decrease stability");

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
        else if (currentFeature == nullptr || currentFeature->IsFrozen())
        {
            ImGui::Text("Upscaler is not active"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "amd_fidelityfx_dx12.dll is missing!"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "libxess_fg.dll is missing!"); // Probably never will be visible
        }
    }

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (ScopedCard::Shows("fg_nvngx") && activeNvngxFg != FGNvngxReplacement::None)
    {
        ScopedCard card;

        if (activeNvngxFg == FGNvngxReplacement::Nukems)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-FG via Nukem's DLSSG)",
                                    "Requires Nukem's dlssg_to_fsr3 dll");

            if (!state.nukemsFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlssg_to_fsr3_amd_is_better.dll into OptiScaler folder");
            }
        }
        else if (activeNvngxFg == FGNvngxReplacement::Arturs)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-MFG via DLSS Enabler)",
                                    "DLSS Enabler as dlss-enabler-headless.dll");

            if (!state.artursFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlss-enabler-headless.dll into OptiScaler folder");
            }

            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Using a subset of features from DLSS Enabler");
        }
        else if (activeNvngxFg == FGNvngxReplacement::FFX)
        {
            SeparatorWithHelpMarker("Frame Generation (FSRFG via FFX)", "FFX using the DLSSG swapchain");
        }
        else if (activeNvngxFg == FGNvngxReplacement::Combo)
        {
            SeparatorWithHelpMarker("Frame Generation (Enabler + FFX)",
                                    "FFX for middle fake frames, and Enabler for the rest\n\n2x - FFX\n"
                                    "3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler");
        }

        if (state.activeFgInput == FGInput::NvngxFG)
        {

            bool dmfgActive = state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

            if (!ReflexHooks::isReflexHooked())
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
                ImGui::Text("If you are using an AMD/Intel GPU, then make sure you have Fakenvapi");
            }
            else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
            {
                ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }

            if (state.swapchainApi == DX12)
            {
                ImGui::Text("Current DLSSG state:");
                ImGui::SameLine();
                if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
                {
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)),
                                       std::format("ON {}x", count + 1).c_str());
                }
                else
                {
                    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                }

                // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                // Hide to reduce confusion, config is still read
                const bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                            State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                const bool isDllProxyNvngxType =
                    activeNvngxFg == FGNvngxReplacement::Nukems || activeNvngxFg == FGNvngxReplacement::Arturs;
                if (isDllProxyNvngxType && !primaryGpu.dlssCapable && primaryGpu.fsr4Support == FSR4Support::None &&
                    !primaryGpu.d3d12IsVkd3dProton && !isUnrealEngine)
                {
                    if (bool makeDepthCopy = config->NvngxFGMakeDepthCopy.value_or_default();
                        ImGui::Checkbox("Fix broken visuals", &makeDepthCopy))
                    {
                        config->NvngxFGMakeDepthCopy = makeDepthCopy;
                    }
                    ShowTooltip("Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                "GPUs under Windows\nCan cause stutters, so best to use only when necessary");
                }
            }
            else if (state.swapchainApi == Vulkan)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   "DLSSG is purposefully disabled when this menu is visible");
                ImGui::Spacing();
            }
        }

        bool isLoaded = false;
        if (state.swapchainApi == Vulkan)
            isLoaded = Nvngx_FG::isVulkanAvailable();
        if (state.swapchainApi == DX12)
            isLoaded = Nvngx_FG::isDx12Available();

        if (isLoaded)
        {
            if (activeNvngxFg == FGNvngxReplacement::Arturs || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                auto featureVer = Nvngx_FG::version();
                auto antighostingVer = Nvngx_FG::extraVersion();
                ImGui::Text("DE Ver: %d.%d.%d.%d   GB Ver: %d.%d", featureVer.major, featureVer.minor, featureVer.patch,
                            featureVer.reserved, antighostingVer.major, antighostingVer.minor);

                static std::vector<FlagDefinition> common_flags = {
                    { "Antighosting (GB)", 0x00100000, "Enable anti-ghosting correction" },
                    { "Temporal HUD pin", 0x04000000, "Enable temporal HUD pinning (present-backbuffer stability)" }
                };

                static std::vector<FlagDefinition> uncommon_flags = {
                    //{ "Hudless UI mask", 0x02000000, "Use HUD-less as UI mask (DL2 inverted semantics)" },
                    { "HUD interpolation", 0x08000000, "HUD OF interpolation (0=legacy pin-present, 1=OF warp)" },
                    { "Ignore UI texture", 0x10000000, "Ignore dedicated DLSSG.UI texture (force legacy HUD path)" },
                    //{ "Dp4a active", 0x20000000, "OF pipeline using dp4a-accelerated SSD (SM 6.4+)" },
                    { "Pin backbuffer", 0x40000000, "Pin DLSSG.Backbuffer to subframe-1 snapshot across MFG frame" }
                };

                static std::vector<FlagDefinition> debug_flags = {
                    { "Antighosting red tint", 0x00200000, "Debug: red tint on corrected pixels" },
                    { "Antighosting split screen", 0x00400000, "Debug: split screen comparison" },
                    { "Frame index line", 0x00010000, "" },
                    { "HUD detection", 0x00020000, "" },
                    { "Disocclusion tint", 0x00040000, "" },
                    { "Artifacts detection", 0x00080000, "" },
                    { "Camera MV debug", 0x00800000, "Debug: blue tint where camera MV fallback is used" },
                    { "Generic visualization", 0x01000000, "Debug: trapezoid zone visualization" }
                };

                uint32_t temp_flags = config->NvngxFGDispatchFlags.value_or_default();
                bool changed = false;

                ImGui::Text("Raw DispatchFlags:");
                changed |= ImGui::InputScalar("##RawFlags", ImGuiDataType_U32, &temp_flags, NULL, NULL, "%08X",
                                              ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                if (bool showDebug = config->NvngxFGShowDebug.value_or_default();
                    ImGui::Checkbox("Show Debug", &showDebug))
                {
                    config->NvngxFGShowDebug = showDebug;
                }
                ShowTooltip("Required for Debug flags to work correctly");

                ImGui::Spacing();

                if (auto ch = ScopedCollapsingHeader("Active DispatchFlags"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};

                    auto render_flags = [&](const std::vector<FlagDefinition>& flags)
                    {
                        for (const auto& flag : flags)
                        {
                            changed |= ImGui::CheckboxFlags(flag.name.c_str(), &temp_flags, flag.mask);

                            if (ImGui::IsItemHovered() && !flag.description.empty())
                            {
                                ImGui::SetTooltip("%s", flag.description.c_str());
                            }
                        }
                    };

                    ImGui::TextDisabled("Common");
                    render_flags(common_flags);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Uncommon");
                    render_flags(uncommon_flags);

                    if (config->NvngxFGShowDebug.value_or_default())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Debug");
                        render_flags(debug_flags);
                    }
                }

                if (changed)
                {
                    config->NvngxFGDispatchFlags = temp_flags;
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                if (ImGui::Checkbox("Enable Debug View", &state.dlssgDebugView))
                {
                    Nvngx_FG::setDebugView(state.dlssgDebugView);
                }
                if (ImGui::Checkbox("Interpolated frames only", &state.dlssgInterpolatedOnly))
                {
                    Nvngx_FG::setInterpolatedOnly(state.dlssgInterpolatedOnly);
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::FFX || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                if (_ffxFGIndex < 0)
                    _ffxFGIndex = config->FfxFGIndex.value_or_default();

                if (state.ffxFGVersionNames.size() > 0)
                {
                    ImGui::PushItemWidth(135.0f * menuResScale);

                    auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                    if (ImGui::BeginCombo("FFX FG", currentName.c_str()))
                    {
                        for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                        {
                            auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                            if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                _ffxFGIndex = n;
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ShowTooltip("List of FGs reported by FFX SDK");

                    ImGui::SameLine(0.0f, 6.0f);

                    if (ImGui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                    {
                        config->FfxFGIndex = _ffxFGIndex;
                        state.fgChanged = true;
                    }
                }

                bool fgAsync = config->FGAsync.value_or_default();
                if (ImGui::Checkbox("Allow Async##2", &fgAsync))
                {
                    config->FGAsync = fgAsync;

                    if (config->IsFGEnabled())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("Async set FGChanged");
                    }
                }
                ShowTooltip("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                bool fgDV = config->FGDebugView.value_or_default();
                if (ImGui::Checkbox("Debug View##3", &fgDV))
                {
                    config->FGDebugView = fgDV;

                    if (config->IsFGEnabled())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("DebugView set FGChanged");
                    }
                }
                ShowTooltip("Enable FSR3.1-FG Debug view\n\n"
                            "Top left: Game Motion Vectors\n"
                            "Top middle: GMV Depth\n"
                            "Top right: Optical Flow MV\n"
                            "Middle: Interpolated frame only\n"
                            "Bottom left: Disocclusion mask\n"
                            "Bottom middle: Interpolation source (w/o UI)\n"
                            "Bottom right: HUDless resource");

                if (Nvngx_FG::version().major > 3)
                {
                    ImGui::SameLine(0.0f, 20.0f * menuResScale);
                    if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                        ImGui::Checkbox("Enable Watermark", &fgwm))
                    {
                        LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                        config->FSRFGEnableWatermark = fgwm;
                    }

                    ShowTooltip("After changing this option, please Save Settings\n"
                                "It will be applied on next launch.");
                }
            }

            if (bool disableHudless = config->NvngxFGDisableHudless.value_or_default();
                ImGui::Checkbox("Disable HUDless", &disableHudless))
            {
                config->NvngxFGDisableHudless = disableHudless;
            }
            ShowTooltip("Might be required for some sets of DispatchFlags");
        }
    }

    // FSR-FG Inputs
    if (ScopedCard::Shows("fg_fsrfg_inputs") && state.currentFGSwapchain != nullptr &&
        (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
    {
        ScopedCard card;

        SeparatorWithHelpMarker("Frame Generation (FSR-FG Inputs)", "Select FSR-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (fgOutput != nullptr)
        {
            ImGui::Text("Current FSR-FG state:");
            ImGui::SameLine();
            if (state.fsrfgInputActive)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                ImGui::Text("Please select FSR Frame Generation in the game options\n"
                            "You might need to select FSR first");
            }
        }

        bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
        if (ImGui::Checkbox("Skip Config for HUDless", &skipConfig))
            config->FSRFGSkipConfigForHudless = skipConfig;

        ShowTooltip("Do not use HUDless set at ffxConfig");

        ImGui::SameLine(0.0f, 6.0f);

        bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
        if (ImGui::Checkbox("Skip Dispatch for HUDless", &skipDispatch))
            config->FSRFGSkipDispatchForHudless = skipDispatch;

        ShowTooltip("Do not use HUDless set at ffxDispatch");
    }

    // Streamline FG Inputs
    if (ScopedCard::Shows("fg_sl_inputs") && state.currentFGSwapchain != nullptr &&
        state.activeFgInput == FGInput::DLSSG)
    {
        ScopedCard card;

        SeparatorWithHelpMarker("Frame Generation (Streamline FG Inputs)", "Select DLSS-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

        if (!ReflexHooks::isReflexHooked())
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
            ImGui::Text("If you are using an AMD/Intel GPU, then make sure you have fakenvapi");
        }
        else if (fgOutput != nullptr)
        {
            ImGui::Text("Current Streamline FG state:");
            ImGui::SameLine();
            if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }
        }
    }
}

void MenuCommon::RenderFsrCommonSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // FSR Common -----------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            (state.activeFgOutput == FGOutput::FSRFG || IsFsr(currentBackend)))
        {
            SeparatorWithHelpMarker("FSR Common Settings", "Affects both FSR-FG & Upscalers");

            bool useFsrVales = config->FsrUseFsrInputValues.value_or_default();
            if (ImGui::Checkbox("Use FSR Input Values", &useFsrVales))
                config->FsrUseFsrInputValues = useFsrVales;

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("FoV & Camera Values"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool useVFov = config->FsrVerticalFov.has_value() || !config->FsrHorizontalFov.has_value();

                float vfov = config->FsrVerticalFov.value_or_default();
                float hfov = config->FsrHorizontalFov.value_or(90.0f);

                if (useVFov && !config->FsrVerticalFov.has_value())
                    config->FsrVerticalFov = vfov;
                else if (!useVFov && !config->FsrHorizontalFov.has_value())
                    config->FsrHorizontalFov = hfov;

                if (ImGui::RadioButton("Use Vert. Fov", useVFov))
                {
                    config->FsrHorizontalFov.reset();
                    config->FsrVerticalFov = vfov;
                    useVFov = true;
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::RadioButton("Use Horz. Fov", !useVFov))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov = hfov;
                    useVFov = false;
                }

                if (useVFov)
                {
                    if (ImGui::SliderFloat("Vert. FOV", &vfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrVerticalFov = vfov;

                    ShowTooltip("Might help achieve better image quality");
                }
                else
                {
                    if (ImGui::SliderFloat("Horz. FOV", &hfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrHorizontalFov = hfov;

                    ShowTooltip("Might help achieve better image quality");
                }

                float cameraNear;
                float cameraFar;

                cameraNear = config->FsrCameraNear.value_or_default();
                cameraFar = config->FsrCameraFar.value_or_default();

                if (ImGui::SliderFloat("Camera Near", &cameraNear, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraNear = cameraNear;
                ShowTooltip("Might help achieve better image quality\n"
                            "And potentially less ghosting");

                if (ImGui::SliderFloat("Camera Far", &cameraFar, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraFar = cameraFar;
                ShowTooltip("Might help achieve better image quality\n"
                            "And potentially less ghosting");

                if (ImGui::Button("Reset Camera Values"))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov.reset();
                    config->FsrCameraNear.reset();
                    config->FsrCameraFar.reset();
                }

                ImGui::SameLine(0.0f, 6.0f);
                ImGui::Text("Near: %.1f Far: %.1f",
                            state.lastFsrCameraNear < 500000.0f ? state.lastFsrCameraNear : 500000.0f,
                            state.lastFsrCameraFar < 500000.0f ? state.lastFsrCameraFar : 500000.0f);

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFramerateSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    // Framerate ---------------------
    if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
    {
        SeparatorWithHelpMarker(
            "Framerate", "Uses Reflex when possible\nOn AMD/Intel cards, you can use Fakenvapi to substitute Reflex");

        static std::string currentMethod {};
        LowLatencyMode fakenvapiMode = {};
        if (state.reflexLimitsFps)
        {
            fakenvapiMode = fakenvapi::getCurrentMode();

            if (fakenvapiMode == LowLatencyMode::AntiLag2)
                currentMethod = "FSR Anti-Lag 2.0";
            else if (fakenvapiMode == LowLatencyMode::LatencyFlex)
                currentMethod = "LatencyFlex";
            else if (fakenvapiMode == LowLatencyMode::XeLL)
                currentMethod = "XeLL";
            else if (fakenvapiMode == LowLatencyMode::AntiLagVk)
                currentMethod = "Vulkan AntiLag";
            else if (fakenvapiMode == LowLatencyMode::None)
            {
                if (fakenvapi::isUsingAsMainNvapi())
                    currentMethod = "None";
                else
                    currentMethod = "Reflex";
            }

            if (state.rtssReflexInjection && fakenvapiMode == LowLatencyMode::AntiLag2 &&
                config->FGOutput.value_or_default() == FGOutput::FSRFG)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   "Using RTSS Reflex injection with FSR Anti-Lag 2.0 and FSR FG "
                                   "might cause issues");
        }
        else
        {
            if (XellHooks::canLimit())
                currentMethod = "Game's XeLL";
            else
                currentMethod = "Fallback";
        }

        if (state.rtssReflexInjection)
            currentMethod.append(" (RTSS)");

        const bool fakenvapiInactive = (fakenvapi::isUsingAsMainNvapi() || fakenvapiMode == LowLatencyMode::XeLL) &&
                                       !fakenvapi::isLowLatencyActive() && state.reflexLimitsFps;

        if (fakenvapiInactive)
            currentMethod.append(" (inactive)");

        ImGui::Text("Current method: %s", currentMethod.c_str());

        if (fakenvapiMode == LowLatencyMode::AntiLag2)
            ShowTooltip("FSR Anti-Lag 2.0 is the new name for AntiLag 2\nDon't ask me why");

        if (state.reflexShowWarning)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                               "Using Reflex's limit with FSR FG has performance overhead");

            ImGui::Spacing();
        }

        // set initial value
        if (std::isinf(_limitFps))
            _limitFps = config->FramerateLimit.value_or_default();

        float fpsSliderMax = (float) std::max(200, refreshRate);
        ImGui::SliderFloat("FPS Limit", &_limitFps, 0, fpsSliderMax, "%.0f");

        if (ImGui::Button("Apply Limit"))
        {
            config->FramerateLimit = _limitFps;
        }

        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button("Reset Limit"))
        {
            _limitFps = 0.0f;
            config->FramerateLimit = _limitFps;
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("VRR Frame Cap Calculator"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            ImGui::PushItemWidth(105.0f * menuResScale);
            ImGui::InputInt("Refresh Rate", &refreshRate, 1, 1, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();

            float refreshRateF = static_cast<float>(refreshRate);
            // it's fine to use with real reflex, we only care about antilag
            auto fpsLimitTech = fakenvapi::getCurrentMode();
            constexpr float margin = 0.3f; // in ms
            float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

            if (fpsLimitTech == LowLatencyMode::AntiLag2 || fpsLimitTech == LowLatencyMode::AntiLagVk)
                frameCap = std::round(frameCap);

            ImGui::Text("Calculated Cap: %.1f", frameCap);

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Set as FPS Limit"))
            {
                _limitFps = frameCap;
                config->FramerateLimit = _limitFps;
            }
        }
    }
}

void MenuCommon::RenderFakenvapiSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Using state.reflexLimitsFps as a detection for Reflex being used on Nvidia
    const bool showLatencyFlex =
        fakenvapi::isUsingAsMainNvapi() || (state.activeFgOutput == FGOutput::XeFG && state.reflexLimitsFps);
    const bool showForceXell = state.swapchainApi != API::Vulkan;

    if (!showLatencyFlex && !showForceXell)
        return;

    // FAKENVAPI ---------------------------
    SectionTitle("fakenvapi");

    if (showLatencyFlex)
    {
        ImGui::BeginDisabled(state.activeFgOutput == FGOutput::XeFG || state.activeFgInput == FGInput::ForceXeLL);
        if (bool forceLFX = config->FN_ForceLatencyFlex.value_or_default();
            ImGui::Checkbox("Force LatencyFlex", &forceLFX))
        {
            config->FN_ForceLatencyFlex = forceLFX;
        }
        ShowTooltip("By default, FSR Anti-Lag 2.0/XeLL is used when available.\n"
                    "This setting lets you force LatencyFlex instead");
        ImGui::EndDisabled();
    }

    if (showForceXell)
    {
        if (showLatencyFlex)
            ImGui::SameLine(0.0f, 16.0f);

        bool forceXell = config->ForceXeLL.value_or_default();
        static bool activeForceXeLL = forceXell;

        if (ImGui::Checkbox("Force XeLL", &forceXell))
        {
            config->ForceXeLL = forceXell;
        }
        ShowTooltip("Allows XeLL to work without FG on non-Intel cards.\n\nDisables FG "
                    "options\n\nRequires a restart");

        if (activeForceXeLL != forceXell)
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)), "Save INI and restart to apply the changes");
            ImGui::Spacing();
        }
    }

    if (showLatencyFlex)
    {
        // clang-format off
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative",
                "The safest, but might not reduce latency well" },
            { LFXMode::Aggressive, "Aggressive",
                "Improves latency, but in some cases will lower FPS more than expected" },
            { LFXMode::ReflexIDs, "Reflex ID",
                "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
                "and will fallback to Aggressive" }
        };

        bool usingLFX = fakenvapi::getCurrentMode() == LowLatencyMode::LatencyFlex;

        ImGui::BeginDisabled(!usingLFX);
        PopulateCombo("LatencyFlex mode", config->FN_LatencyFlexMode, lfx_modes);
        ImGui::EndDisabled();

        static std::vector<MenuOption<ForceReflex>> reflex_modes = { { ForceReflex::InGame, "Follow in-game" },
                                                                { ForceReflex::ForceDisable, "Force Disable" },
                                                                { ForceReflex::ForceEnable, "Force Enable" } };

        PopulateCombo("Force Reflex", config->FN_ForceReflex, reflex_modes);
        // clang-format on
    }
}

template <typename T> std::string GetMenuOptionLabel(const std::vector<MenuOption<T>>& options, T targetValue)
{
    auto it = std::find_if(options.begin(), options.end(),
                           [targetValue](const MenuOption<T>& option) { return option.value == targetValue; });

    if (it != options.end())
    {
        return it->label;
    }

    return "Unknown";
}

void MenuCommon::RenderLowLatencySettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Low Latency ---------------------------
    SectionTitle("Low Latency");

    static std::vector<MenuOption<LowLatencyInput>> lowLatencyInput = {
        { LowLatencyInput::None, "None (Off)" },    { LowLatencyInput::Auto, "Auto" },
        { LowLatencyInput::AntiLag2, "AntiLag 2" }, { LowLatencyInput::Reflex, "Reflex" },
        { LowLatencyInput::XeLL, "XeLL" },          { LowLatencyInput::UeLowLatency, "UeLowLatency" },
    };

    static std::vector<MenuOption<LowLatencyMode>> lowLatencyOutput = {
        { LowLatencyMode::None, "None (Off)" },
        { LowLatencyMode::Auto, "Auto" },
        { LowLatencyMode::LatencyFlex, "LatencyFlex" },
        { LowLatencyMode::AntiLag2, "AntiLag 2" },
        { LowLatencyMode::XeLL, "XeLL" },
        { LowLatencyMode::AntiLagVk, "AntiLag Vk" },
        { LowLatencyMode::Reflex, "Reflex" },
    };

    LowLatencyInput activeInput {};
    LowLatencyMode activeOutput {};

    if (ImGui::BeginTable("lowLatencyActive", 2, ImGuiTableFlags_SizingStretchSame))
    {
        InputCommon::get_currently_active(activeInput, activeOutput);

        ImGui::TableNextColumn();

        ImGui::Text("Active input: %s", GetMenuOptionLabel(lowLatencyInput, activeInput).c_str());

        ImGui::TableNextColumn();

        ImGui::Text("Active output: %s", GetMenuOptionLabel(lowLatencyOutput, activeOutput).c_str());

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("lowLatencySelection", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        auto avalibleInputs = InputCommon::get_avaliable_inputs();

        lowLatencyInput[(uint32_t) LowLatencyInput::AntiLag2].set_disabled(!avalibleInputs[LowLatencyInput::AntiLag2]);
        lowLatencyInput[(uint32_t) LowLatencyInput::Reflex].set_disabled(!avalibleInputs[LowLatencyInput::Reflex]);
        lowLatencyInput[(uint32_t) LowLatencyInput::XeLL].set_disabled(!avalibleInputs[LowLatencyInput::XeLL]);
        lowLatencyInput[(uint32_t) LowLatencyInput::UeLowLatency].set_disabled(
            !avalibleInputs[LowLatencyInput::UeLowLatency]);

        // need to have a value before combo
        if (!config->LowLatencyInput.has_value())
            config->LowLatencyInput = config->LowLatencyInput.value_or_default();

        PopulateCombo("Input", config->LowLatencyInput, lowLatencyInput);

        ImGui::TableNextColumn();

        lowLatencyOutput[(uint32_t) LowLatencyMode::AntiLagVk].set_disabled(true, "No support");
        lowLatencyOutput[(uint32_t) LowLatencyMode::Reflex].set_disabled(true, "No support");

        // need to have a value before combo
        if (!config->LowLatencyOutput.has_value())
            config->LowLatencyOutput = config->LowLatencyOutput.value_or_default();

        PopulateCombo("Output", config->LowLatencyOutput, lowLatencyOutput);

        ImGui::EndTable();
    }

    if (activeOutput == LowLatencyMode::LatencyFlex)
    {
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative", "The safest, but might not reduce latency well" },
            { LFXMode::Aggressive, "Aggressive",
              "Improves latency, but in some cases will lower FPS more than expected" },
            { LFXMode::ReflexIDs, "Reflex ID",
              "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
              "and will fallback to Aggressive" }
        };

        PopulateCombo("LatencyFlex mode", config->FN_LatencyFlexMode, lfx_modes);
    }

    static std::vector<MenuOption<ForceReflex>> lowlatency_states = { { ForceReflex::InGame, "Follow in-game" },
                                                                      { ForceReflex::ForceDisable, "Force Disable" },
                                                                      { ForceReflex::ForceEnable, "Force Enable" } };

    ImGui::SetNextItemWidth(150.0f * ctx.menuResScale);
    PopulateCombo("Force State", config->FN_ForceReflex, lowlatency_states);
}

void MenuCommon::RenderActiveImageSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    bool rcasEnabled = false;

    // SHARPNESS -----------------------------
    SectionTitle("Sharpness");

    if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
        ImGui::Checkbox("Override", &overrideSharpness))
    {
        config->OverrideSharpness = overrideSharpness;

        if ((currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12) &&
            currentFeature->Version().major < 3)
        {
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }
    }
    ShowTooltip("Ignores the value sent by the game\n"
                "and uses the value set below");

    ImGui::SameLine(0.0f, 16.0f * menuResScale);

    float featuresCurrentSharpness = currentFeature->Sharpness();
    if (featuresCurrentSharpness > 0.0f)
        ImGui::TextDisabled("(Current: %.3f)", featuresCurrentSharpness);
    else
        ImGui::TextDisabled("(Current: disabled)");

    ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

    float sharpness = config->Sharpness.value_or_default();

    if (ImGui::SliderFloat("Strength", &sharpness, 0.0f, 1.0f))
        config->Sharpness = sharpness;

    ImGui::EndDisabled();

    // RCAS
    {
        // xess or dlss version >= 2.5.1
        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
        const bool isDlss = currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12;
        rcasEnabled = currentBackend == Upscaler::XeSS || (isDlss && currentFeature->Version() >= requiredDlssVersion);

        // Determine current mode for the dropdown
        bool currentRcasEnabled = config->RcasEnabled.value_or(rcasEnabled);
        auto currentShader = Config::Instance()->SharpnessShader.value_or_default();

        int selectedMode = 0; // 0 = Upscaler's Default
        if (currentRcasEnabled)
        {
            if (currentShader == SharpenShader::RCAS)
                selectedMode = 1;
            else if (currentShader == SharpenShader::DepthAware)
                selectedMode = 2;
            else if (currentShader == SharpenShader::LocalContrastDepthAware)
                selectedMode = 3;
        }

        const char* sharpenModes[] = { "Upscaler's default", "RCAS", "Depth Aware (RCAS)", "Depth Aware (DAS)" };

        if (ImGui::Combo("Method", &selectedMode, sharpenModes, IM_ARRAYSIZE(sharpenModes)))
        {
            if (selectedMode == 0)
            {
                config->RcasEnabled = false;
            }
            else
            {
                config->RcasEnabled = true;
                if (selectedMode == 1)
                    Config::Instance()->SharpnessShader = SharpenShader::RCAS;
                else if (selectedMode == 2)
                    Config::Instance()->SharpnessShader = SharpenShader::DepthAware;
                else if (selectedMode == 3)
                    Config::Instance()->SharpnessShader = SharpenShader::LocalContrastDepthAware;
            }
        }

        ShowTooltip("Select the sharpening filter to apply.\n\n"
                    "Upscaler's default: Uses the sharpening provided by the upscaler.\n"
                    "RCAS: AMD's RCAS, modified for Contrast and MAS.\n"
                    "Depth Aware: Smarter sharpening with fewer artifacts (heavier).\n\n"
                    "By default uses a sharpening value provided by the game.\n"
                    "Select 'Override' under 'Sharpness' and adjust the slider to change it.");

        // Only show MAS and Advanced parameters if a custom sharpness mode is active
        if (selectedMode != 0)
        {
            ImGui::Spacing();

            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                ImGui::Checkbox("Enable Motion Adaptive Sharpness", &overrideMotionSharpness))
                config->MotionSharpnessEnabled = overrideMotionSharpness;
            ShowTooltip("Enables sharpness adjustments according to the motion");

            if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
            {
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    ImGui::Checkbox("DA + MAS Debug", &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;

                ShowTooltip("Enable DA + MAS debug views\n"
                            "Blue tint for DA detected edges\n\n"
                            "More red areas will have more sharpness applied\n"
                            "Green areas will get reduced sharpness");

                if (auto ch = ScopedCollapsingHeader("Advanced DA Parameters"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool clamp = config->DAClampOutput.value_or(false); ImGui::Checkbox("Clamp Output", &clamp))
                    {
                        if (clamp)
                            config->DAClampOutput = true;
                        else
                            config->DAClampOutput.reset();
                    }

                    ShowTooltip("Clamps the final image to the [0, 1] range.\n\n"
                                "Prevents overshoot artifacts such as bright halos or negative colors.\n"
                                "Recommended for LDR pipelines; optional for HDR depending on tone-mapping.\n\n"
                                "When not set OptiScaler controls it via upscalers HDR flag");

                    if (currentFeature->DepthLinear())
                    {
                        float depthBias = config->DADepthBias.value_or(0.0015f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.005f, 0.03f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowTooltip("Ignores small depth differences before edge detection.\n\n"
                                    "Higher values reduce flickering and noise from minor depth changes, but may "
                                    "soften real geometry edges.\n"
                                    "Lower values preserve fine detail but can cause unstable or noisy edge "
                                    "detection.");

                        float depthScale = config->DADepthScale.value_or(250.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 100.0f, 600.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowTooltip("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                    "Higher values more aggressively prevent sharpening across object boundaries "
                                    "(reduces halos).\n"
                                    "Lower values allow more sharpening to pass across edges (sharper but "
                                    "riskier).");
                    }
                    else
                    {
                        float depthBias = config->DADepthBias.value_or(0.001f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.0001f, 0.003f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowTooltip("Ignores small depth differences before edge detection.\n\n"
                                    "Higher values reduce flickering and noise from minor depth changes, but may "
                                    "soften real geometry edges.\n"
                                    "Lower values preserve fine detail but can cause unstable or noisy edge "
                                    "detection.");

                        float depthScale = config->DADepthScale.value_or(35.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 25.0f, 400.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowTooltip("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                    "Higher values more aggressively prevent sharpening across object boundaries "
                                    "(reduces halos).\n"
                                    "Lower values allow more sharpening to pass across edges (sharper but "
                                    "riskier).");
                    }

                    if (ImGui::Button("Reset Depth Values"))
                    {
                        config->DADepthBias.reset();
                        config->DADepthScale.reset();
                    }
                }
            }
            else
            {
                if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                    ImGui::Checkbox("Contrast Enabled", &contrastEnabled))
                    config->ContrastEnabled = contrastEnabled;

                ShowTooltip("Controls sharpness at high contrast areas.");

                ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                float contrast = config->Contrast.value_or_default();
                if (ImGui::SliderFloat("Contrast", &contrast, -2.0f, 2.0f, "%.2f"))
                    config->Contrast = contrast;

                ShowTooltip("Positive values decrease sharpness at high contrast areas.\n"
                            "Negative values increase sharpness at high contrast areas.");

                ImGui::EndDisabled();
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Motion Adaptive Sharpness##2"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

                if (Config::Instance()->SharpnessShader.value_or_default() == SharpenShader::RCAS)
                {
                    if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                        ImGui::Checkbox("MAS Debug", &overrideMSDebug))
                        config->MotionSharpnessDebug = overrideMSDebug;
                    ShowTooltip("Areas that are more red will have more sharpness applied\n"
                                "Green areas will get reduced sharpness");
                }

                float motionSharpness = config->MotionSharpness.value_or_default();
                if (ImGui::SliderFloat("MotionSharpness", &motionSharpness, -1.0f, 1.0f, "%.3f"))
                    config->MotionSharpness = motionSharpness;

                ShowTooltip("Maximum amount of sharpness that motion can add or remove.\n\n"
                            "Negative values reduce sharpening in motion (recommended).\n"
                            "Positive values increase sharpening in motion.\n\n"
                            "The final adjustment scales with motion and is capped at this value.");

                float motionThreshod = config->MotionThreshold.value_or_default();
                if (ImGui::SliderFloat("MotionThreshod", &motionThreshod, 0.0f, 100.0f, "%.2f"))
                    config->MotionThreshold = motionThreshod;

                ShowTooltip("Minimum motion required before motion-based sharpening adjustment begins.\n\n"
                            "Higher values ignore small movements (more stable).\n"
                            "Lower values react to subtle motion (more sensitive).");

                float motionScale = config->MotionScaleLimit.value_or_default();
                if (ImGui::SliderFloat("MotionRange", &motionScale, 0.01f, 100.0f, "%.2f"))
                    config->MotionScaleLimit = motionScale;

                ShowTooltip("Defines the motion range over which the effect ramps from zero to full strength.\n\n"
                            "Values above the threshold are mapped into this range.\n"
                            "Larger values make the response smoother and more gradual.\n"
                            "Smaller values make the effect react more quickly and aggressively.");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderUpscaleRatioSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // UPSCALE RATIO OVERRIDE -----------------

    auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
    auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

    SectionTitle("Upscale Ratio Override");

    if (ImGui::BeginTable("ratioOverride", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableNextColumn();

        if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override all", &upOverride))
        {
            config->UpscaleRatioOverrideEnabled = upOverride;

            if (upOverride)
                config->QualityRatioOverrideEnabled = false;
        }
        ShowTooltip("Overrides every upscaler preset with the set value\n\n"
                    "1.5x on a 1080p screen means an internal res of 720p\n"
                    "1080 / 1.5 = 720");

        ImGui::TableNextColumn();

        if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override per quality preset", &qOverride))
        {
            config->QualityRatioOverrideEnabled = qOverride;

            if (qOverride)
                config->UpscaleRatioOverrideEnabled = false;
        }

        ShowTooltip("Lets you override each preset's ratio individually\n"
                    "Note that not every game supports every quality preset\n\n"
                    "1.5x on a 1080p screen means internal resolution of 720p\n"
                    "1080 / 1.5 = 720");

        ImGui::EndTable();
    }

    if (config->UpscaleRatioOverrideEnabled.value_or_default())
    {
        float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
        ImGui::SliderFloat("All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
        config->UpscaleRatioOverrideValue = urOverride;
    }

    if (config->QualityRatioOverrideEnabled.value_or_default())
    {
        float qDlaa = config->QualityRatio_DLAA.value_or_default();
        if (ImGui::SliderFloat("DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_DLAA = qDlaa;

        float qUq = config->QualityRatio_UltraQuality.value_or_default();
        if (ImGui::SliderFloat("Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_UltraQuality = qUq;

        float qQ = config->QualityRatio_Quality.value_or_default();
        if (ImGui::SliderFloat("Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Quality = qQ;

        float qB = config->QualityRatio_Balanced.value_or_default();
        if (ImGui::SliderFloat("Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Balanced = qB;

        float qP = config->QualityRatio_Performance.value_or_default();
        if (ImGui::SliderFloat("Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Performance = qP;

        float qUp = config->QualityRatio_UltraPerformance.value_or_default();
        if (ImGui::SliderFloat("Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_UltraPerformance = qUp;
    }
}

void MenuCommon::RenderOutputScalingSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // OUTPUT SCALING -----------------------------
    // if (state.api == DX12 || state.api == DX11)
    {
        // if motion vectors are not display size
        ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                             currentFeature->RenderWidth() != currentFeature->DisplayWidth());

        SectionTitle("Output Scaling");

        float defaultRatio = 1.5f;

        if (_ssRatio == 0.0f)
        {
            _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
            _ssEnabled = config->OutputScalingEnabled.value_or_default();
            _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
        }

        ImGui::BeginDisabled((currentBackend == Upscaler::XeSS || currentBackend == Upscaler::DLSS ||
                              currentBackend == Upscaler::DLSS_on12) &&
                             currentFeature->RenderWidth() > currentFeature->DisplayWidth());
        ImGui::Checkbox("Enable", &_ssEnabled);
        ImGui::EndDisabled();

        ShowTooltip("Upscales the image internally to a higher output resolution\n"
                    "then downscales it back to your display resolution\n\n"
                    "Values <1.0 make the upscaler cheaper\n"
                    "Values >1.0 make image sharper at the cost of performance\n\n"
                    "If greyed out, please check Git Wiki - Unreal Engine tweaks\n\n"
                    "Target res and total ratio at the bottom (max. total 3.0!)");

        ImGui::SameLine(0.0f, 6.0f);

        ImGui::BeginDisabled(!_ssEnabled);
        {
            ImGui::PushItemWidth(105.0f * menuResScale);

            // clang-format off
                    std::vector<MenuOption<Scaler>> ds_options = {
                        { Scaler::FSR1, "FSR1",
                            "Default option.\nGood enough image quality and very fast." },
                        { Scaler::Bicubic, "Bicubic",
                            "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling." },
                        { Scaler::CatmullRom, "Catmull-Rom",
                            "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos." },
                        { Scaler::Lanczos2, "Lanczos2",
                            "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier." },
                        { Scaler::Lanczos3, "Lanczos3",
                            "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing.\nConsidered the best along with Kaiser3." },
                        { Scaler::Kaiser2, "Kaiser2",
                            "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier." },
                        { Scaler::Kaiser3, "Kaiser3",
                            "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU.\nConsidered the best along with Lanczos3." },
                        { Scaler::Magic, "MAGIC",
                            "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft." }
                    };
            // clang-format on

            const bool isUpsampleRatio = _ssRatio < 1.0f;
            const std::string disabledReason = "Only FSR1 and Bicubic are supported when Ratio is below 1.0.";

            for (auto& opt : ds_options)
            {
                if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                    opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
            }

            if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                _ssDownsampler = Scaler::FSR1;

            PopulateCombo("Downscaler", _ssDownsampler, ds_options);

            ImGui::PopItemWidth();
        }
        ImGui::EndDisabled();

        bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                            _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                            _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

        ImGui::BeginDisabled(!applyEnabled);
        if (ImGui::Button("Apply Change"))
        {
            config->OutputScalingEnabled = _ssEnabled;
            config->OutputScalingMultiplier = _ssRatio;

            if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                _ssDownsampler = Scaler::FSR1;

            config->OutputScalingDownscaler = _ssDownsampler;

            const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
            if (usesDlssd)
                state.newBackend = Upscaler::DLSSD;
            else
                state.newBackend = currentBackend;

            MARK_ALL_BACKENDS_CHANGED();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!_ssEnabled || currentFeature->RenderWidth() > currentFeature->DisplayWidth());
        ImGui::SliderFloat("Ratio", &_ssRatio, 0.5f, 3.0f, "%.2f");
        ImGui::EndDisabled();

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            ImGui::Text("Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d",
                        config->OutputScalingEnabled.value_or_default() ? "ENABLED" : "DISABLED",
                        (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                        (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                        ((float) currentFeature->DisplayWidth() * _ssRatio) / (float) currentFeature->RenderWidth(),
                        currentFeature->JitterCount());
        }

        ImGui::EndDisabled();
    }
}

void MenuCommon::RenderInitFlagsSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature == nullptr || currentFeature->IsFrozen())
        return;

    // INIT -----------------------------
    SectionTitle("Init Flags");

    if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        // AutoExposure is always enabled for XeSS with native Dx11
        bool autoExposureDisabled = state.api == API::DX11 && currentBackend == Upscaler::XeSS;
        ImGui::BeginDisabled(autoExposureDisabled);

        if (bool autoExposure = currentFeature->AutoExposure(); ImGui::Checkbox("Auto Exposure", &autoExposure))
        {
            config->AutoExposure = autoExposure;
            ReInitUpscaler();
        }
        ShowTooltip("Some Unreal Engine games need this\n\n"
                    "Try using if colours flickering or\n"
                    "objects have ghosting trails");
        ShowResetButton(&config->AutoExposure, "R");

        ImGui::EndDisabled();

        ImGui::TableNextColumn();
        if (bool depth = currentFeature->DepthInverted(); ImGui::Checkbox("Depth Inverted", &depth))
        {
            config->DepthInverted = depth;
            ReInitUpscaler();
        }
        ShowTooltip("You shouldn't need to change it");
        ShowResetButton(&config->DepthInverted, "R##2");

        ImGui::TableNextColumn();
        if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("HDR", &hdr))
        {
            config->HDR = hdr;
            ReInitUpscaler();
        }
        ShowTooltip("Might help with purple hue in some games");
        ShowResetButton(&config->HDR, "R##1");

        ImGui::TableNextColumn();
        if (bool jitter = currentFeature->JitteredMV(); ImGui::Checkbox("Jitter Cancellation", &jitter))
        {
            config->JitterCancellation = jitter;
            ReInitUpscaler();
        }
        ShowTooltip("Fix for games that send motion data with preapplied jitter");
        ShowResetButton(&config->JitterCancellation, "R##3");

        ImGui::TableNextColumn();
        if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("Display Res. MV", &mv))
        {
            config->DisplayResolution = mv;

            // Disable output scaling when
            // Display res MV is active
            if (mv)
            {
                config->OutputScalingEnabled = false;
                _ssEnabled = false;
            }

            ReInitUpscaler();
        }
        ShowTooltip("Mostly a fix for Unreal Engine games\n"
                    "Top left part of the screen will be blurry");
        ShowResetButton(&config->DisplayResolution, "R##4");

        ImGui::TableNextColumn();
        auto accessToReactiveMask = currentFeature->AccessToReactiveMask();
        ImGui::BeginDisabled(!accessToReactiveMask);

        bool canUseReactiveMask =
            accessToReactiveMask && currentBackend != Upscaler::DLSS && currentBackend != Upscaler::DLSS_on12 &&
            (currentBackend != Upscaler::XeSS || currentFeature->Version() >= feature_version { 2, 0, 1 });

        bool disableReactiveMask = config->DisableReactiveMask.value_or(!canUseReactiveMask);

        if (ImGui::Checkbox("Disable Reactive Mask", &disableReactiveMask))
        {
            config->DisableReactiveMask = disableReactiveMask;

            if (currentBackend == Upscaler::XeSS)
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
        }

        ImGui::EndDisabled();

        if (accessToReactiveMask)
            ShowTooltip("Allows the use of a Reactive mask\n"
                        "Keep in mind that a Reactive mask sent to DLSS\n"
                        "will not produce a good image in combination with FSR/XeSS");
        else
            ShowTooltip("Option disabled because the game doesn't provide a Reactive mask");

        ImGui::EndTable();
    }

    if (currentFeature->AccessToReactiveMask())
    {
        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(false));

        auto useAsTransparency = config->FsrUseMaskForTransparency.value_or_default();
        if (ImGui::Checkbox("Use Reactive Mask as Transparency Mask", &useAsTransparency))
            config->FsrUseMaskForTransparency = useAsTransparency;

        ImGui::EndDisabled();
    }

    if (currentFeature->AccessToReactiveMask() && currentBackend != Upscaler::DLSS &&
        currentBackend != Upscaler::DLSS_on12)
    {
        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(currentBackend == Upscaler::XeSS));

        bool binaryMask = state.api == Vulkan || currentBackend == Upscaler::XeSS;
        auto defaultBias = binaryMask ? 0.0f : 0.45f;
        auto maskBias = config->DlssReactiveMaskBias.value_or(defaultBias);

        if (!binaryMask)
        {
            if (ImGui::SliderFloat("React. Mask Bias", &maskBias, 0.0f, 0.9f, "%.2f"))
                config->DlssReactiveMaskBias = maskBias;

            ShowTooltip("Values above 0 activate usage of Reactive mask");
        }
        else
        {
            bool useRM = maskBias > 0.0f;
            if (ImGui::Checkbox("Use Binary Reactive Mask", &useRM))
            {
                if (useRM)
                    config->DlssReactiveMaskBias = 0.45f;
                else
                    config->DlssReactiveMaskBias.reset();
            }
        }

        ImGui::EndDisabled();
    }
}

void MenuCommon::RenderMagnifierSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Magnifier -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Magnifier"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool magnifierEnabled = config->MagnifierEnabled.value_or_default();
        if (ImGui::Checkbox("Enable Magnifier", &magnifierEnabled))
            config->MagnifierEnabled = magnifierEnabled;

        ImGui::BeginDisabled(!magnifierEnabled);

        float magnifierSize = config->MagnifierSize.value_or_default();
        if (ImGui::SliderFloat("Size", &magnifierSize, 5.0f, 50.0f, "%.1f%% of screen"))
            config->MagnifierSize = magnifierSize;

        int zoomFactor = config->MagnifierZoomFactor.value_or_default();
        if (ImGui::SliderInt("Zoom Factor", &zoomFactor, 2, 20, "%dx"))
            config->MagnifierZoomFactor = zoomFactor;

        float borderSize = config->MagnifierBorderSize.value_or_default();
        if (ImGui::SliderFloat("Border Size", &borderSize, 0.0f, 2.0f, "%.2f%% of screen"))
            config->MagnifierBorderSize = borderSize;

        ImGui::Separator();
        ImGui::Text("Positioning");

        bool staticMode = config->MagnifierStaticPosX.has_value() && config->MagnifierStaticPosY.has_value();
        if (staticMode)
        {
            float staticX = config->MagnifierStaticPosX.value();
            if (ImGui::SliderFloat("Static Pos X", &staticX, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosX = staticX;

            float staticY = config->MagnifierStaticPosY.value();
            if (ImGui::SliderFloat("Static Pos Y", &staticY, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosY = staticY;

            if (ImGui::Button("Reset Static Position (Follow Cursor)"))
            {
                config->MagnifierStaticPosX.reset();
                config->MagnifierStaticPosY.reset();
            }
        }
        else
        {
            // Button to initialize static position mode
            if (ImGui::Button("Set Static Position"))
            {
                config->MagnifierStaticPosX = 50.0f;
                config->MagnifierStaticPosY = 50.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Currently following cursor)");

            float offsetX = config->MagnifierCursorOffsetX.value_or_default();
            if (ImGui::SliderFloat("Cursor Offset X", &offsetX, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetX = offsetX;

            float offsetY = config->MagnifierCursorOffsetY.value_or_default();
            if (ImGui::SliderFloat("Cursor Offset Y", &offsetY, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetY = offsetY;
        }

        ImGui::EndDisabled();
        ImGui::Spacing();
    }
}
void MenuCommon::RenderQuirksSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;

    // QUIRKS -----------------------------
    if (state.detectedQuirks.size() > 0)
    {
        SectionTitle("Active Quirks");

        for (const auto& quirk : state.detectedQuirks)
        {
            ImGui::TextWrapped("%s", quirk.c_str());
        }
    }
}

void MenuCommon::RenderAdvancedSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    const bool upscalerActive = currentFeature != nullptr && !currentFeature->IsFrozen();
    const bool nonDlssActive =
        upscalerActive && currentBackend != Upscaler::DLSS && currentBackend != Upscaler::DLSS_on12;

    // SPOOFING/HOOKING -----------------------------
    if (ScopedCard::Shows("spoofing") && nonDlssActive)
    {
        ScopedCard card { "Spoofing / Hooking" };

        if (ImGui::BeginTable("hooking", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("DXGI Spoofing", config->DxgiSpoofing,
                           "Enable Nvidia GPU spoofing for DXGI adapter\n"
                           "Detailed modifications available in the INI",
                           true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Ntdll Hooks", config->UseNtdllHooks,
                           "Only hook ntdll.dll methods\n"
                           "Disable for switching back to kernel hooks",
                           true);

            if (state.api == Vulkan)
            {
                ImGui::TableNextColumn();
                ConfigCheckbox("VLK Spoofing", config->VulkanSpoofing,
                               "Enable Nvidia GPU spoofing for Vulkan\n"
                               "Detailed modifications available in the INI",
                               true);

                ImGui::TableNextColumn();
                ConfigCheckbox("VLK Extension Spoofing", config->VulkanExtensionSpoofing,
                               "Enable Nvidia Extension spoofing for Vulkan\n"
                               "Detailed modifications available in the INI",
                               true);
            }

            ImGui::TableNextColumn();
            ConfigCheckbox("Disable Overlays", config->DisableOverlays,
                           "Disable supported overlays (autoenabled with OptiFG)\n"
                           "Including Steam Input (controller issues)",
                           true);

            ImGui::EndTable();
        }
    }

    // PLUGINS/MISC -----------------------------
    if (ScopedCard::Shows("plugins"))
    {
        ScopedCard card { "Plugins / Startup" };

        if (ImGui::BeginTable("plugins", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("Load ASI plugins", config->LoadAsiPlugins,
                           "Let OptiScaler load *.asi files from plugins folder", true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Disable Splash message", config->DisableSplash,
                           "Disables Startup Splash message (bottom left corner)", true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Check for Update", config->CheckForUpdate,
                           "Enable checking Github for latest version\n"
                           "Only works on stable/final releases",
                           true);

            ImGui::EndTable();
        }
    }

    // SHADERS, LIMITS AND DRS -----------------------------
    if (ScopedCard::Shows("shaders_limits"))
    {
        ScopedCard card { "Shaders / Limits" };

        if (ImGui::BeginTable("limits", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            if (ConfigCheckbox("Use Precompiled Shaders", config->UsePrecompiledShaders))
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }

            // DRS
            ImGui::TableNextColumn();
            ConfigCheckbox("DRS Override Minimum", config->DrsMinOverrideEnabled,
                           "Dynamic Resolution Scaling\nFix for games ignoring official DRS limits");

            ImGui::TableNextColumn();
            if (upscalerActive)
                ConfigCheckbox("Enable Extended Limits", config->ExtendedLimits,
                               "Extended sliders limit for quality presets\n\n"
                               "Using this option changes resolution detection logic\n"
                               "and might cause issues and crashes!");

            ImGui::TableNextColumn();
            ConfigCheckbox("DRS Override Maximum", config->DrsMaxOverrideEnabled,
                           "Dynamic Resolution Scaling\nFix for games ignoring official DRS limits");

            ImGui::EndTable();
        }
    }

    if (!nonDlssActive)
        return;

    // BARRIERS -----------------------------
    if (ScopedCard::Shows("barriers"))
    {
        ScopedCard card { "Resource Barriers" };

        if (ImGui::BeginTable("barriers", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            AddResourceBarrier("Color", &config->ColorResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Depth", &config->DepthResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Motion", &config->MVResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Exposure", &config->ExposureResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Mask", &config->MaskResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Output", &config->OutputResourceBarrier);

            ImGui::EndTable();
        }
    }

    // HOTFIXES -----------------------------
    if (ScopedCard::Shows("root_signatures") && state.api == DX12)
    {
        ScopedCard card { "Root Signatures" };

        if (ImGui::BeginTable("rootSignatures", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("Restore Compute Root Signature", config->RestoreComputeSignature);

            ImGui::TableNextColumn();
            ConfigCheckbox("Restore Graphic Root Signature", config->RestoreGraphicSignature);

            ImGui::EndTable();
        }
    }
}

void MenuCommon::RenderLoggingSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // LOGGING -----------------------------
    SectionTitle("Logging");

    if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
        config->LogToNGX.value_or_default())
        spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
    else
        spdlog::default_logger()->set_level(spdlog::level::off);

    if (bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile))
    {
        config->LogToFile = toFile;
        PrepareLogger();
    }

    ImGui::SameLine(0.0f, 6.0f);
    if (bool toConsole = config->LogToConsole.value_or_default(); ImGui::Checkbox("To Console", &toConsole))
    {
        config->LogToConsole = toConsole;
        PrepareLogger();
    }

    ImGui::SameLine(0.0f, 6.0f);
    if (auto SingleFile = config->LogSingleFile.value_or_default(); ImGui::Checkbox("Single File", &SingleFile))
    {
        config->LogSingleFile = SingleFile;
        PrepareLogger();
    }

    const char* logLevels[] = { "Trace", "Debug", "Information", "Warning", "Error" };
    const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

    if (ImGui::BeginCombo("Log Level", selectedLevel))
    {
        for (int n = 0; n < 5; n++)
        {
            if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
            {
                config->LogLevel = n;
                spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
            }
        }

        ImGui::EndCombo();
    }
}

void MenuCommon::RenderThemeSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // THEME -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Menu Theme and Color"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool lightTheme = config->LightTheme.value_or_default();

        const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

        auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
        { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

        auto AccentSoft = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.24f, alpha) : Mix(bgDark, accent, 0.32f, alpha)); };

        auto AccentMed = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha)); };

        auto AccentStrong = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(ImVec4(accent.x, accent.y, accent.z, alpha)); };

        if (ImGui::Checkbox("Light Theme", &lightTheme))
        {
            config->LightTheme = lightTheme;
            ApplyThemeStyle();
        }

        SectionTitle("Accent Colour");

        ImGui::Text("Presets:");
        ImGui::SameLine(0.0f, 6.0f);

        ImVec4 colorBlue = { 0.00f, 0.40f, 0.77f, 1.0f };
        ImVec4 colorTeal = { 0.00f, 1.00f, 0.91f, 1.0f };
        ImVec4 colorGray = { 0.54f, 0.54f, 0.54f, 1.0f };
        ImVec4 colorYellow = { 1.00f, 0.89f, 0.00f, 1.0f };
        ImVec4 colorGreen = { 0.25f, 1.00f, 0.00f, 1.0f };
        ImVec4 colorRed = { 1.00f, 0.00f, 0.00f, 1.0f };
        ImVec4 colorOrange = { 1.00f, 0.52f, 0.00f, 1.0f };
        ImVec4 colorPurple = { 0.576f, 0.00f, 1.00f, 1.0f };

        ImVec4 color = {};

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Blue"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Teal"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Gray"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Yellow"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Green"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Red"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Orange"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Purple"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float accentColor[3] = { config->MenuAccentColorR.value_or_default(),
                                 config->MenuAccentColorG.value_or_default(),
                                 config->MenuAccentColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom Accent Color", accentColor))
        {
            config->MenuAccentColorR = accentColor[0];
            config->MenuAccentColorG = accentColor[1];
            config->MenuAccentColorB = accentColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset Accent Color"))
        {
            config->MenuAccentColorR.reset();
            config->MenuAccentColorG.reset();
            config->MenuAccentColorB.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        SectionTitle("Background Colour");

        ImGui::Text("Presets:");
        ImGui::SameLine(0.0f, 6.0f);

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Blue##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Teal##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Gray##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Yellow##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Green##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Red##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Orange##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Purple##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float bgColor[3] = { config->MenuBGColorR.value_or_default(), config->MenuBGColorG.value_or_default(),
                             config->MenuBGColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom BG Colour", bgColor))
        {
            config->MenuBGColorR = bgColor[0];
            config->MenuBGColorG = bgColor[1];
            config->MenuBGColorB = bgColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        auto alpha = config->MenuBGColorA.value_or_default();
        if (ImGui::SliderFloat("Background Alpha", &alpha, 0.0f, 1.0f))
        {
            config->MenuBGColorA = alpha;
            ApplyThemeStyle();
        }

        if (_bgBlurTexture != ImTextureID_Invalid)
        {
            ConfigCheckbox("Background Blur", config->MenuBlur,
                           "Blur the game behind the menu\nOnly visible when Background Alpha is below 1");

            ImGui::BeginDisabled(!config->MenuBlur.value_or_default());

            auto blurStrength = config->MenuBlurStrength.value_or_default();
            if (ImGui::SliderFloat("Blur Strength", &blurStrength, 0.0f, 4.0f, "%.2f"))
                config->MenuBlurStrength = blurStrength;

            ImGui::EndDisabled();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset BG Colour"))
        {
            config->MenuBGColorR.reset();
            config->MenuBGColorG.reset();
            config->MenuBGColorB.reset();
            config->MenuBGColorA.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();
    }
}

void MenuCommon::RenderFpsOverlaySettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // FPS OVERLAY -----------------------------
    SectionTitle("FPS Overlay");
    {
        ScopedID sectionId { "FPS Overlay" };

        bool fpsEnabled = config->ShowFps.value_or_default();
        if (ImGui::Checkbox("FPS Overlay Enabled", &fpsEnabled))
            config->ShowFps = fpsEnabled;

        ImGui::SameLine(0.0f, 6.0f);

        bool fpsHorizontal = config->FpsOverlayHorizontal.value_or_default();
        if (ImGui::Checkbox("Horizontal", &fpsHorizontal))
            config->FpsOverlayHorizontal = fpsHorizontal;

        const char* fpsPosition[] = { "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
        const char* selectedPosition = fpsPosition[config->FpsOverlayPosition.value_or_default()];

        if (ImGui::BeginCombo("Overlay Position", selectedPosition))
        {
            for (int n = 0; n < std::size(fpsPosition); n++)
            {
                if (ImGui::Selectable(fpsPosition[n], (config->FpsOverlayPosition.value_or_default() == n)))
                    config->FpsOverlayPosition = (FpsOverlayPos) n;
            }

            ImGui::EndCombo();
        }

        const char* fpsType[] = { "Just FPS", "Simple",       "Detailed",      "Detailed + Graph",
                                  "Full",     "Full + Graph", "Reflex timings" };
        const char* selectedType = fpsType[config->FpsOverlayType.value_or_default()];

        if (ImGui::BeginCombo("Overlay Type", selectedType))
        {
            for (int n = 0; n < std::size(fpsType); n++)
            {
                if (ImGui::Selectable(fpsType[n], (config->FpsOverlayType.value_or_default() == n)))
                    config->FpsOverlayType = (FpsOverlay) n;
            }

            ImGui::EndCombo();
        }

        float fpsAlpha = config->FpsOverlayAlpha.value_or_default();
        if (ImGui::SliderFloat("Background Alpha", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
            config->FpsOverlayAlpha = fpsAlpha;

        const char* options[] = { "Same as menu", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                  "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
        int currentIndex = std::max(((int) (config->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
        float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                           1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

        if (ImGui::SliderInt("Scale", &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                             ImGuiSliderFlags_ClampOnInput))
        {
            if (currentIndex == 0)
                config->FpsScale.reset();
            else
                config->FpsScale = values[currentIndex];
        }

        bool useTheme = config->OverlaysUseTheme.value_or_default();
        if (ImGui::Checkbox("Use Theme Colors", &useTheme))
            config->OverlaysUseTheme = useTheme;
    }
}

void MenuCommon::RenderUpscalerInputsSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // UPSCALER INPUTS -----------------------------
    SectionTitle("Upscaler Inputs");
    {
        ScopedID sectionId { "Upscaler Inputs" };

        // Values when the menu was first drawn, enabling and pattern matching apply on next launch
        static const bool startFsr2 = config->EnableFsr2Inputs.value_or_default();
        static const bool startFsr3 = config->EnableFsr3Inputs.value_or_default();
        static const bool startFfx = config->EnableFfxInputs.value_or_default();
        static const bool startFsr2Pattern = config->Fsr2Pattern.value_or_default();
        static const bool startFsr3Pattern = config->Fsr3Pattern.value_or_default();

        if (ImGui::BeginTable("upscalerInputs", 4, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("Enable", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Pattern", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow();
            for (int i = 0; i < 4; i++)
            {
                ImGui::TableSetColumnIndex(i);
                ImGui::TextDisabled("%s", ImGui::TableGetColumnName(i));

                if (i == 1)
                    ShowTooltip("Hook the inputs of this API, needed before they can be used");
                else if (i == 2)
                    ShowTooltip("Take the upscaler inputs from this API");
                else if (i == 3)
                    ShowTooltip("Find the inputs by pattern matching");
            }

            auto row = [&](const char* name, auto& enable, auto& use, auto pattern)
            {
                ScopedID rowId { name };

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name);

                ImGui::TableNextColumn();
                ConfigCheckbox("##enable", enable, "Hook the inputs of this API", true);

                ImGui::BeginDisabled(!enable.value_or_default());

                ImGui::TableNextColumn();
                ConfigCheckbox("##use", use, "Take the upscaler inputs from this API");

                ImGui::TableNextColumn();
                if constexpr (!std::is_null_pointer_v<decltype(pattern)>)
                    ConfigCheckbox("##pattern", *pattern, "Find the inputs by pattern matching", true);

                ImGui::EndDisabled();
            };

            row("FSR2", config->EnableFsr2Inputs, config->UseFsr2Inputs, &config->Fsr2Pattern);
            row("FSR3", config->EnableFsr3Inputs, config->UseFsr3Inputs, &config->Fsr3Pattern);
            row("FFX", config->EnableFfxInputs, config->UseFfxInputs, nullptr);

            ImGui::EndTable();
        }

        const bool restartNeeded = startFsr2 != config->EnableFsr2Inputs.value_or_default() ||
                                   startFsr3 != config->EnableFsr3Inputs.value_or_default() ||
                                   startFfx != config->EnableFfxInputs.value_or_default() ||
                                   startFsr2Pattern != config->Fsr2Pattern.value_or_default() ||
                                   startFsr3Pattern != config->Fsr3Pattern.value_or_default();

        if (restartNeeded)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)));
            ImGui::TextWrapped("Save Settings and restart the game for Enable and Pattern changes to apply");
            ImGui::PopStyleColor();
        }
    }
}

void MenuCommon::RenderVsyncSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // DX11 & DX12 -----------------------------
    if (state.swapchainApi != Vulkan)
    {
        // V-SYNC -----------------------------
        SectionTitle("V-Sync Settings");
        {
            ScopedID sectionId { "V-Sync Settings" };

            auto OverrideVsync = config->OverrideVsync.value_or_default();
            {
                if (ImGui::Checkbox("Override Vsync", &OverrideVsync))
                {
                    config->OverrideVsync = OverrideVsync;
                    LOG_DEBUG("Enabled OverrideVsync: {}", OverrideVsync);
                }
                ShowTooltip("Force override the game's V-Sync settings.\n\n"
                            "Useful for example when XeFG is locked to V-sync\n"
                            "frame cap despite toggling Vsync off (e.g. Nioh 3)\n\n"
                            "After changing, please Save Settings and restart.");
            }

            auto forceVsyncOn = config->ForceVsync.has_value() && config->ForceVsync.value();
            auto forceVsyncOff = config->ForceVsync.has_value() && !config->ForceVsync.value();
            bool vsyncChanged = false;

            if (ImGui::Checkbox("V-Sync On", &forceVsyncOn))
            {
                if (forceVsyncOn)
                {
                    config->ForceVsync = true;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Checkbox("V-Sync Off", &forceVsyncOff))
            {
                if (forceVsyncOff)
                {
                    config->ForceVsync = false;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!forceVsyncOn);

            ImGui::PushItemWidth(50.0f * menuResScale);

            auto vsyncBuf = StrFmt("%d", config->VsyncInterval.value_or_default());
            if (ImGui::BeginCombo("Sync Int.", vsyncBuf.c_str()))
            {
                if (ImGui::Selectable("0", config->VsyncInterval.value_or_default() == 0))
                {
                    config->VsyncInterval = 0;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("1", config->VsyncInterval.value_or_default() == 1))
                {
                    config->VsyncInterval = 1;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("2", config->VsyncInterval.value_or_default() == 2))
                {
                    config->VsyncInterval = 2;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("3", config->VsyncInterval.value_or_default() == 3))
                {
                    config->VsyncInterval = 3;
                    vsyncChanged = true;
                }

                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ShowTooltip("Controls the DXGI Present sync interval, which determines how\n"
                        "the swap chain waits for vertical refresh.\n\n"
                        "0  = Present immediately, no VSync wait.\n"
                        "1  = Sync to every refresh, normal VSync.\n"
                        "2+ = Present every N refreshes, reducing effective frame rate.\n\n"
                        "Higher values can reduce tearing but may increase latency and cap FPS.\n"
                        "For most games, use 0 for lowest latency or 1 for normal VSync.");

            ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Reset##10"))
            {
                config->ForceVsync.reset();
                vsyncChanged = true;
            }

            ShowTooltip("Force V-Sync On/Off & Sync Interval options");

            if (vsyncChanged && state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr)
            {
                // To prevent XeLL issues
                LOG_DEBUG("V-Sync change detected, forcing XeFG reset");
                state.WAR_xefgRequestFGToggle = true;
            }
        }
    }
}

void MenuCommon::RenderApiAndTextureSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // DX11 & DX12 -----------------------------
    if (state.swapchainApi != Vulkan)
    {
        // MIPMAP BIAS & Anisotropy -----------------------------
        if (ScopedCard::Shows("mipmap"))
        {
            ScopedCard card { "Mipmap Bias" };
            ScopedID sectionId { "Mipmap Bias" };
            if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                _mipBias = config->MipmapBiasOverride.value();

            ImGui::SliderFloat("Mipmap Bias##2", &_mipBias, -15.0f, 15.0f, "%.6f");
            ShowTooltip("Can help with blurry textures in broken games\n"
                        "Negative values will make textures sharper\n"
                        "Positive values will make textures more blurry\n\n"
                        "Has a small performance impact");

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                     config->MipmapBiasScaleOverride.value());
                {
                    bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                    if (ImGui::Checkbox("MB Fixed Override", &mbFixed))
                    {
                        config->MipmapBiasScaleOverride.reset();
                        config->MipmapBiasFixedOverride = mbFixed;
                    }

                    ShowTooltip("Apply same override value to all textures");
                }
                ImGui::EndDisabled();

                ImGui::SameLine(0.0f, 6.0f);

                ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                     config->MipmapBiasFixedOverride.value());
                {
                    bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                    if (ImGui::Checkbox("MB Scale Override", &mbScale))
                    {
                        config->MipmapBiasFixedOverride.reset();
                        config->MipmapBiasScaleOverride = mbScale;
                    }

                    ShowTooltip("Apply override value as scale multiplier\n"
                                "When using scale mode, please use positive\n"
                                "override values to increase sharpness!");
                }
                ImGui::EndDisabled();

                bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
                if (ImGui::Checkbox("MB Override All Textures", &mbAll))
                    config->MipmapBiasOverrideAll = mbAll;

                ShowTooltip("Override all textures mipmap values\n"
                            "Normally OptiScaler only overrides\n"
                            "below zero mipmap values!");
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() &&
                                 config->MipmapBiasOverride.value() == _mipBias);
            {
                if (ImGui::Button("Set"))
                {
                    config->MipmapBiasOverride = _mipBias;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 6.0f);

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                if (ImGui::Button("Reset"))
                {
                    config->MipmapBiasOverride.reset();
                    _mipBias = 0.0f;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            if (currentFeature != nullptr && !currentFeature->IsFrozen())
            {
                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Calculate Mipmap Bias"))
                    _showMipmapCalcWindow = true;
            }

            if (config->MipmapBiasOverride.has_value())
            {
                if (config->MipmapBiasFixedOverride.value_or_default())
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else if (config->MipmapBiasScaleOverride.value_or_default())
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: Base * %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: Base + %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
            }
            else
            {
                ImGui::Text("Current : %.3f / %.3f", state.lastMipBias, state.lastMipBiasMax);
            }

            ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
        }

        if (ScopedCard::Shows("anisotropy"))
        {
            ScopedCard card { "Anisotropic Filtering" };
            ScopedID sectionId { "Anisotropic Filtering" };
            ImGui::PushItemWidth(65.0f * menuResScale);

            auto selectedAF =
                config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto";
            if (ImGui::BeginCombo("Force Anisotropic Filtering", selectedAF.c_str()))
            {
                if (ImGui::Selectable("Auto", !config->AnisotropyOverride.has_value()))
                    config->AnisotropyOverride.reset();

                if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                    config->AnisotropyOverride = 1;

                if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                    config->AnisotropyOverride = 2;

                if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                    config->AnisotropyOverride = 4;

                if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                    config->AnisotropyOverride = 8;

                if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                    config->AnisotropyOverride = 16;

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            bool afComp = config->AnisotropyModifyComp.value_or_default();
            if (ImGui::Checkbox("Modify Compare", &afComp))
                config->AnisotropyModifyComp = afComp;

            ShowTooltip("Update comparison filters");

            ImGui::SameLine(0.0f, 6.0f);

            bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
            if (ImGui::Checkbox("Modify Min/Max", &afMinMax))
                config->AnisotropyModifyMinMax = afMinMax;

            ShowTooltip("Update min/max filters");

            bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
            if (ImGui::Checkbox("Skip Point Filters", &afSkipPoint))
                config->AnisotropySkipPointFilter = afSkipPoint;

            ShowTooltip("Skip updating of point filters");

            ImGui::Text("Will might be applied after RESOLUTION/PRESET change !!!");
        }
    }
}

void MenuCommon::RenderKeybindSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Keybinds");
    {
        ScopedID sectionId { "Keybinds" };

        ImGui::Text("Key combinations are currently NOT supported!");
        ImGui::Text("Escape to cancel, Backspace to unbind");
        ImGui::Spacing();

        static auto menu = Keybind("Menu", 10);
        static auto fpsOverlay = Keybind("FPS Overlay", 11);
        static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);
        static auto fgEnable = Keybind("Frame Generation", 13);

        menu.Render(config->ShortcutKey);
        fpsOverlay.Render(config->FpsShortcutKey);
        fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
        fgEnable.Render(config->FGShortcutKey);
    }
}

void MenuCommon::RenderMenuScaleSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    SectionTitle("Menu");

    ImGui::PushItemWidth(100.0f * menuResScale);

    auto autoText = config->MenuScale.has_value() ? "Auto" : StrFmt("Auto (%3.1f)", menuResScale);
    // clang-format off
    const char* uiScales[] = { autoText.c_str(), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1",
                               "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
    // clang-format on

    const char* selectedScaleName = uiScales[_selectedScale];

    if (ImGui::BeginCombo("Menu Scale", selectedScaleName))
    {
        for (int n = 0; n < std::size(uiScales); n++)
        {
            if (ImGui::Selectable(uiScales[n], (_selectedScale == n)))
            {
                _selectedScale = n;

                if (n == 0)
                    config->MenuScale.reset();
                else
                    config->MenuScale = 0.4f + (float) n / 10.0f;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();
}

static const char* ApiName(API api)
{
    if (api == DX11)
        return "D3D11";

    if (api == DX12)
        return "D3D12";

    if (api == Vulkan)
        return "Vulkan";

    return "Unknown";
}

// Filled dot for on / found, hollow dot for off / missing. Goes into the next table cell
static void StatusFlag(const char* label, bool on, bool warnWhenOn = false)
{
    const ImVec4 onColor = toneMapColor(ImVec4(0.35f, 0.85f, 0.45f, 1.f));
    const ImVec4 warnColor = toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f));

    ImGui::TableNextColumn();

    const float lineHeight = ImGui::GetTextLineHeight();
    const float radius = lineHeight * 0.28f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + lineHeight * 0.5f, pos.y + lineHeight * 0.5f);
    auto drawList = ImGui::GetWindowDrawList();

    if (on)
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(warnWhenOn ? warnColor : onColor));
    else
        drawList->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 1.5f);

    ImGui::Dummy(ImVec2(lineHeight, lineHeight));
    ImGui::SameLine(0.0f, 0.0f);

    if (on)
        ImGui::TextUnformatted(label);
    else
        ImGui::TextDisabled("%s", label);
}

// Which libraries and GPU features were found. Used by the Status tab and when no upscaler is available
void MenuCommon::RenderDetectedInfo(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& primaryGpu = *ctx.primaryGpu;

    if (!ScopedCard::Shows("status_detected"))
        return;

    ScopedCard card { "Detected" };

    if (ImGui::BeginTable("statusDetected", 4, ImGuiTableFlags_SizingStretchProp))
    {
        StatusFlag("nvngx.dll", state.nvngxExists);
        StatusFlag("nvngx_dlss", state.NVNGX_DLSS_Path.has_value());
        StatusFlag("nvngx_dlssd", state.NVNGX_DLSSD_Path.has_value());
        StatusFlag("nvngx replacement", state.nvngxReplacement.has_value());
        StatusFlag("libxess", state.libxessExists || XeSSProxy::Module() != nullptr);
        StatusFlag("FSR hooks", state.fsrHooks);
        StatusFlag("FFX 3.1", FfxApiProxy::Dx12Module() != nullptr);
        StatusFlag("FFX SR", FfxApiProxy::Dx12Module_SR() != nullptr);
        StatusFlag("FFX FG", FfxApiProxy::Dx12Module_FG() != nullptr);
        StatusFlag("OptiPatcher", state.isOptiPatcherSucceed);
        StatusFlag("DLSS capable GPU", primaryGpu.dlssCapable);
        StatusFlag("FSR4 capable GPU", primaryGpu.fsr4Support != FSR4Support::None,
                   primaryGpu.fsr4Support == FSR4Support::INT8);

        ImGui::EndTable();
    }
}

// Same dense look as the Status tab
void MenuCommon::RenderDetectedCard(RenderMenuContext& ctx)
{
    const auto& style = ImGui::GetStyle();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 1.0f * ctx.menuResScale));

    RenderDetectedInfo(ctx);

    ImGui::PopStyleVar(2);
}

void MenuCommon::RenderStatusInfo(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    const bool active = currentFeature != nullptr && !currentFeature->IsFrozen();

    // Dense on purpose, the whole page is meant to fit in a single screenshot
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 1.0f * menuResScale));

    // Label and value, two pairs per row
    auto pair = [&](const char* label, const std::string& value, bool dim = false)
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);

        ImGui::TableNextColumn();
        if (dim)
            ImGui::TextDisabled("%s", value.c_str());
        else
            ImGui::TextUnformatted(value.c_str());
    };

    auto flag = [](const char* label, bool on, bool warnWhenOn = false) { StatusFlag(label, on, warnWhenOn); };

    auto setupPairs = []()
    {
        ImGui::TableSetupColumn("##l1", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##v1", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("##l2", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##v2", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    };

    // SYSTEM -----------------------------
    if (ScopedCard::Shows("status_system"))
    {
        ScopedCard card { "System" };

        // Long values get the whole row
        if (ImGui::BeginTable("statusSystem", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("##label");
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

            auto row = [&](const char* label, const std::string& value)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            };

            row("GPU", primaryGpu.name);
            row("Game", state.gameExe + (state.gameName.empty() ? "" : " (" + state.gameName + ")"));

            if (state.detectedQuirks.size() > 0)
            {
                std::string quirks;
                for (const auto& quirk : state.detectedQuirks)
                    quirks += quirks.empty() ? quirk : ", " + quirk;

                ImGui::TableNextColumn();
                ImGui::TextDisabled("Quirks");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", quirks.c_str());
            }

            ImGui::EndTable();
        }

        std::string ratioOverride = "Off";
        if (config->UpscaleRatioOverrideEnabled.value_or_default())
            ratioOverride = StrFmt("All %.2f", config->UpscaleRatioOverrideValue.value_or_default());
        else if (config->QualityRatioOverrideEnabled.value_or_default())
            ratioOverride = "Per preset";

        if (ImGui::BeginTable("statusRuntime", 4, ImGuiTableFlags_SizingStretchProp))
        {
            setupPairs();

            pair("API", StrFmt("%s%s", ApiName(state.api),
                               IdentifyGpu::gameUsesDxvk()          ? " (DXVK)"
                               : IdentifyGpu::gameUsesVkd3dProton() ? " (VKD3D)"
                                                                    : ""));
            pair("Swapchain", ApiName(state.swapchainApi));
            pair("Input", ApiUpscalerInputName(state.currentInputApiName));

            if (active)
            {
                pair("Upscaler",
                     StrFmt("%s %d.%d.%d%s", currentFeature->ShortName().c_str(), currentFeature->Version().major,
                            currentFeature->Version().minor, currentFeature->Version().patch,
                            (currentFeature->IsWithDx12() && state.api != DX12) ? " w/Dx12" : ""));
                pair("Render", StrFmt("%dx%d", currentFeature->RenderWidth(), currentFeature->RenderHeight()));
                pair("Output", StrFmt("%dx%d (%.2fx)", currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                                      (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth()));
                pair("Display", StrFmt("%dx%d (%.2fx)", currentFeature->DisplayWidth(), currentFeature->DisplayHeight(),
                                       (float) currentFeature->DisplayWidth() / (float) currentFeature->RenderWidth()));
                pair("Frames", std::to_string(currentFeature->FrameCount()));
            }
            else
            {
                pair("Upscaler", "None", true);
                pair("Render", "-", true);
                pair("Output", "-", true);
                pair("Display", "-", true);
                pair("Frames", "-", true);
            }

            auto fgInput = state.activeFgInput;
            if (fgInput != FGInput::NoFG)
                pair("FG Input", EnumToCode(fgInput));
            else
                pair("FG Input", "Off", true);

            auto fgOutput = state.activeFgOutput;
            if (fgOutput != FGOutput::NoFG)
                pair("FG Output", EnumToCode(fgOutput));
            else
                pair("FG Output", "Off", true);

            pair("Ratio override", ratioOverride, ratioOverride == "Off");
            pair("Mipmap bias",
                 config->MipmapBiasOverride.has_value() ? StrFmt("%.3f", config->MipmapBiasOverride.value()) : "Off",
                 !config->MipmapBiasOverride.has_value());
            pair("Anisotropy",
                 config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto",
                 !config->AnisotropyOverride.has_value());

            ImGui::EndTable();
        }
    }

    // DETECTED -----------------------------
    RenderDetectedInfo(ctx);

    // HOOKS AND CONFIG -----------------------------
    if (ScopedCard::Shows("status_hooks"))
    {
        ScopedCard card { "Hooks / Config" };

        if (ImGui::BeginTable("statusConfig", 4, ImGuiTableFlags_SizingStretchSame))
        {
            flag("DXGI spoofing", config->DxgiSpoofing.value_or_default());
            flag("VLK spoofing", config->VulkanSpoofing.value_or_default());
            flag("VLK ext. spoofing", config->VulkanExtensionSpoofing.value_or_default());
            flag("Ntdll hooks", config->UseNtdllHooks.value_or_default());
            flag("Disable overlays", config->DisableOverlays.value_or_default(), true);
            flag("ASI plugins", config->LoadAsiPlugins.value_or_default());
            flag("Precomp. shaders", config->UsePrecompiledShaders.value_or_default());
            flag("Extended limits", config->ExtendedLimits.value_or_default(), true);
            flag("DRS min override", config->DrsMinOverrideEnabled.value_or_default());
            flag("DRS max override", config->DrsMaxOverrideEnabled.value_or_default());
            flag("Compute root sig.", config->RestoreComputeSignature.value_or_default());
            flag("Graphic root sig.", config->RestoreGraphicSignature.value_or_default());

            ImGui::EndTable();
        }

        // Only shown when something was overridden
        std::string barriers;
        auto addBarrier = [&](const char* name, bool set)
        {
            if (!set)
                return;

            barriers += barriers.empty() ? name : std::string(", ") + name;
        };
        addBarrier("Color", config->ColorResourceBarrier.has_value());
        addBarrier("Depth", config->DepthResourceBarrier.has_value());
        addBarrier("Motion", config->MVResourceBarrier.has_value());
        addBarrier("Exposure", config->ExposureResourceBarrier.has_value());
        addBarrier("Mask", config->MaskResourceBarrier.has_value());
        addBarrier("Output", config->OutputResourceBarrier.has_value());

        if (!barriers.empty())
        {
            ImGui::TextDisabled("Barriers overridden");
            ImGui::SameLine();
            ImGui::TextUnformatted(barriers.c_str());
        }
    }

    // INIT FLAGS -----------------------------
    if (ScopedCard::Shows("status_init") && active)
    {
        ScopedCard card { "Init Flags" };

        if (ImGui::BeginTable("statusInit", 4, ImGuiTableFlags_SizingStretchSame))
        {
            flag("Auto exposure", currentFeature->AutoExposure());
            flag("Depth inverted", currentFeature->DepthInverted());
            flag("HDR", currentFeature->IsHdr());
            flag("Display res. MV", !currentFeature->LowResMV());
            flag("Jitter cancel.", currentFeature->JitteredMV());
            flag("Output scaling", config->OutputScalingEnabled.value_or_default());
            flag("Sharp. override", config->OverrideSharpness.value_or_default());
            flag("V-Sync override", config->OverrideVsync.value_or_default());

            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar(2);
}

static bool IsUpscalerActive(IFeature* feature) { return feature != nullptr && !feature->IsFrozen(); }

static float FooterPlotHeight() { return ImGui::GetFrameHeight() * 1.0f; }

// Everything RenderMainMenuFooter draws, every item is followed by the item spacing
static float FooterHeight()
{
    const auto& style = ImGui::GetStyle();

    return 1.0f + style.ItemSpacing.y * 5.0f + ImGui::GetTextLineHeight() + FooterPlotHeight() +
           ImGui::GetFrameHeight();
}

// CUSTOM TAB -----------------------------
struct MenuCommon::MenuBox
{
    const char* id;   // Saved to the config
    const char* tab;  // Label of the tab it belongs to
    const char* name; // Shown in the Custom tab picker
    void (*draw)(RenderMenuContext&);
    // Function that draws several boxes, ScopedCard::Shows(id) selects the one to draw
    void (*source)(RenderMenuContext&) = nullptr;
    bool pinnable = true;
};

// Every box of every tab in the order they are drawn
const MenuCommon::MenuBox* MenuCommon::GetMenuBoxes(size_t& count)
{
    // clang-format off
    static const MenuBox boxes[] = {
        { "upscaler", "Upscaling", "Upscalers",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderActiveUpscalerSettings); } },
        { "sharpness", "Upscaling", "Sharpness",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderActiveImageSettings); } },
        { "upscale_ratio", "Upscaling", "Upscale Ratio Override",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderUpscaleRatioSettings); } },
        { "output_scaling", "Upscaling", "Output Scaling",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderOutputScalingSettings); } },
        { "shader_times", "Upscaling", "Per shader GPU times",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderShaderTimes); } },
        { "magnifier", "Upscaling", "Magnifier",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderMagnifierSettings); } },

        { "fg_selection", "Frame Gen", "Frame Generation", [](RenderMenuContext& c) { RenderCard(c, RenderFrameGenerationSelection); } },
        { "fg_fsrfg", "Frame Gen", "FSR FG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_xefg", "Frame Gen", "XeFG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_dlssg", "Frame Gen", "DLSSG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_reprojection", "Frame Gen", "Reprojection Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_optifg", "Frame Gen", "OptiFG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_nvngx", "Frame Gen", "Nvngx FG replacement", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_fsrfg_inputs", "Frame Gen", "FSR-FG Inputs", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_sl_inputs", "Frame Gen", "Streamline FG Inputs", nullptr, RenderFrameGenerationRuntimeSettings },

        { "framerate", "Latency & FPS", "Framerate", [](RenderMenuContext& c) { RenderCard(c, RenderFramerateSettings); } },
#ifdef LOW_LATENCY_INPUTS
        { "low_latency", "Latency & FPS", "Low Latency", [](RenderMenuContext& c) { RenderCard(c, RenderLowLatencySettings); } },
#else
        { "low_latency", "Latency & FPS", "fakenvapi", [](RenderMenuContext& c) { RenderCard(c, RenderFakenvapiSettings); } },
#endif
        { "vsync", "Latency & FPS", "V-Sync", [](RenderMenuContext& c) { RenderCard(c, RenderVsyncSettings); } },

        { "mipmap", "Textures", "Mipmap Bias", nullptr, RenderApiAndTextureSettings },
        { "anisotropy", "Textures", "Anisotropic Filtering", nullptr, RenderApiAndTextureSettings },

        { "init_flags", "Compatibility", "Init Flags", [](RenderMenuContext& c) { RenderCard(c, RenderInitFlagsSettings); } },
        { "fsr_common", "Compatibility", "FSR Common Settings", [](RenderMenuContext& c) { RenderCard(c, RenderFsrCommonSettings); } },
        { "upscaler_inputs", "Compatibility", "Upscaler Inputs", [](RenderMenuContext& c) { RenderCard(c, RenderUpscalerInputsSettings); } },
        { "spoofing", "Compatibility", "Spoofing / Hooking", nullptr, RenderAdvancedSettings },
        { "plugins", "Compatibility", "Plugins / Startup", nullptr, RenderAdvancedSettings },
        { "shaders_limits", "Compatibility", "Shaders / Limits", nullptr, RenderAdvancedSettings },
        { "barriers", "Compatibility", "Resource Barriers", nullptr, RenderAdvancedSettings },
        { "root_signatures", "Compatibility", "Root Signatures", nullptr, RenderAdvancedSettings },

        { "menu_scale", "Menu", "Menu Scale", [](RenderMenuContext& c) { RenderCard(c, RenderMenuScaleSettings); } },
        { "theme", "Menu", "Theme and Color", [](RenderMenuContext& c) { RenderCard(c, RenderThemeSettings); } },
        { "fps_overlay", "Menu", "FPS Overlay", [](RenderMenuContext& c) { RenderCard(c, RenderFpsOverlaySettings); } },
        { "keybinds", "Menu", "Keybinds", [](RenderMenuContext& c) { RenderCard(c, RenderKeybindSettings); } },
        { "custom_tab", "Menu", "Custom Tab", [](RenderMenuContext& c) { RenderCard(c, RenderCustomTabSettings); }, nullptr, false },

        { "status_system", "Help / Status", "System", nullptr, RenderStatusInfo },
        { "status_detected", "Help / Status", "Detected", nullptr, RenderStatusInfo },
        { "status_hooks", "Help / Status", "Hooks / Config", nullptr, RenderStatusInfo },
        { "status_init", "Help / Status", "Init Flags (active)", nullptr, RenderStatusInfo },
        { "logging", "Help / Status", "Logging", [](RenderMenuContext& c) { RenderCard(c, RenderLoggingSettings); } },
    };
    // clang-format on

    count = std::size(boxes);
    return boxes;
}

// Draws a single box, even when it's one of many drawn by the same function
void MenuCommon::RenderMenuBox(RenderMenuContext& ctx, const MenuBox& box)
{
    if (box.draw != nullptr)
    {
        box.draw(ctx);
        return;
    }

    ScopedCard::SetFilter(box.id);
    box.source(ctx);
    ScopedCard::SetFilter(nullptr);
}

// Ids are kept in a comma separated list
static bool IsBoxPinned(const std::string& pinned, const char* id)
{
    const size_t length = strlen(id);

    for (size_t pos = pinned.find(id); pos != std::string::npos; pos = pinned.find(id, pos + 1))
    {
        const bool startOk = pos == 0 || pinned[pos - 1] == ',';
        const bool endOk = pos + length == pinned.size() || pinned[pos + length] == ',';

        if (startOk && endOk)
            return true;
    }

    return false;
}

void MenuCommon::RenderCustomTab(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    const std::string pinned = config->CustomTabCards.value_or_default();

    size_t count = 0;
    const MenuBox* boxes = GetMenuBoxes(count);

    bool anyPinned = false;
    const float startY = ImGui::GetCursorPosY();

    for (size_t i = 0; i < count; i++)
    {
        if (!boxes[i].pinnable || !IsBoxPinned(pinned, boxes[i].id))
            continue;

        anyPinned = true;

        ImGui::PushID(boxes[i].id);

        // Explains why the upscaler settings are missing, the Upscaling tab draws it above its boxes
        if (strcmp(boxes[i].id, "upscaler") == 0)
        {
            auto feature = ctx.currentFeature;
            if (feature == nullptr || !feature->IsInited() || feature->IsFrozen())
                RenderCard(ctx, RenderUpscalerStateMessage);
        }

        RenderMenuBox(ctx, boxes[i]);
        ImGui::PopID();
    }

    // Boxes only show up when their feature is in use
    if (!anyPinned)
        ImGui::TextDisabled("Nothing picked yet, choose the boxes in Menu > Custom Tab");
    else if (ImGui::GetCursorPosY() == startY)
        ImGui::TextDisabled("None of the picked boxes are available right now");

    // Hint pinned to the bottom of the tab
    const float hintHeight = ImGui::GetFrameHeight();
    const float gap = ImGui::GetContentRegionAvail().y - hintHeight - ImGui::GetStyle().ItemSpacing.y;
    if (gap > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, gap));
    else
        ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::SmallButton("Customise this tab"))
        requestedMenuTab = "Menu";
    ImGui::PopStyleColor(2);
}

void MenuCommon::RenderCustomTabSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Custom Tab");

    ConfigCheckbox("Show Custom tab", config->CustomTabEnabled,
                   "Adds a tab on top of the list that shows only the picked boxes");

    if (!config->CustomTabEnabled.value_or_default())
        return;

    ImGui::Spacing();

    if (auto ch = ScopedCollapsingHeader("Boxes to show"); ch.IsHeaderOpen())
    {
        const std::string pinned = config->CustomTabCards.value_or_default();
        std::string newPinned;
        bool changed = false;

        size_t count = 0;
        const MenuBox* boxes = GetMenuBoxes(count);

        const char* lastTab = nullptr;
        bool tableOpen = false;

        for (size_t i = 0; i < count; i++)
        {
            const MenuBox& box = boxes[i];

            if (!box.pinnable)
                continue;

            if (lastTab == nullptr || strcmp(lastTab, box.tab) != 0)
            {
                if (tableOpen)
                    ImGui::EndTable();

                ImGui::TextDisabled("%s", box.tab);
                tableOpen = ImGui::BeginTable(box.tab, 2, ImGuiTableFlags_SizingStretchSame);
                lastTab = box.tab;
            }

            bool on = IsBoxPinned(pinned, box.id);

            if (tableOpen)
            {
                ImGui::TableNextColumn();

                ImGui::PushID(box.id);
                changed |= ImGui::Checkbox(box.name, &on);
                ImGui::PopID();
            }

            if (on)
            {
                if (!newPinned.empty())
                    newPinned += ',';

                newPinned += box.id;
            }
        }

        if (tableOpen)
            ImGui::EndTable();

        if (changed)
            config->CustomTabCards = newPinned;
    }
}

void MenuCommon::RenderMainMenuTabs(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    using DrawFn = void (*)(RenderMenuContext&);

    struct Tab
    {
        const char* group;
        const char* label;
        const char* description;
        DrawFn draw; // Drawn above the boxes of the tab
        bool hideLabelInMenu;
        bool (*visible)(RenderMenuContext&) = nullptr; // Tab hidden when returned false
    };

    // Boxes of a tab come from GetMenuBoxes, by the label of the tab
    // clang-format off
    static const Tab tabs[] = {
        { "MAIN", "Custom", "Boxes picked in Menu > Custom Tab.", RenderCustomTab },
        { "MAIN", "Upscaling", "Backend, sharpness and output scaling.", RenderUpscalerStateMessage },
        { "MAIN", "Frame Gen", "Frame generation source, output and pacing." },
        { "MAIN", "Latency & FPS", "Frame limiter, low latency and V-Sync." },
        { "SYSTEM", "Textures", "Mipmap bias and anisotropic filtering." },
        { "SYSTEM", "Compatibility", "Init flags, hooks, plugins and game specific workarounds." },
        { "ADVANCED", "Menu", "Menu scale, theme, FPS overlay and keybinds." },
        { "ADVANCED", "Quirks", "Game specific quirks applied by OptiScaler.",
+          [](RenderMenuContext& c) { RenderCard(c, RenderQuirksSettings); }, false,
+          [](RenderMenuContext& c) { return !c.state.detectedQuirks.empty(); } },
        { "ADVANCED", "Help / Status", "Support info. Filled dot: on or found, hollow dot: off or missing.", nullptr, true },
    };
    // clang-format on

    constexpr int customTab = 0;
    const bool customTabEnabled = config->CustomTabEnabled.value_or_default();

    auto isTabVisible = [&](int i)
    {
        if (i == customTab)
            return customTabEnabled;
        return tabs[i].visible == nullptr || tabs[i].visible(ctx);
    };

    if (requestedMenuTab != nullptr)
    {
        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (strcmp(tabs[i].label, requestedMenuTab) == 0)
                selectedMenuTab = i;
        }

        requestedMenuTab = nullptr;
    }

    selectedMenuTab = std::clamp(selectedMenuTab, 0, (int) std::size(tabs) - 1);

    // Selected tab can disappear, pick the first one that's visible
    if (!isTabVisible(selectedMenuTab))
    {
        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (isTabVisible(i))
            {
                selectedMenuTab = i;
                break;
            }
        }
    }

    const auto& style = ImGui::GetStyle();

    // Space kept free below for the frametime graph and the buttons
    const float footerHeight = FooterHeight();
    const float sidebarWidth = 120.0f * menuResScale;
    const float tabHeight = ImGui::GetFrameHeight() * 1.35f;

    // Sidebar -----------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##MenuSidebar", ImVec2(sidebarWidth, -footerHeight),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    {
        const char* lastGroup = nullptr;

        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (!isTabVisible(i))
                continue;

            if (lastGroup == nullptr || strcmp(lastGroup, tabs[i].group) != 0)
            {
                if (lastGroup != nullptr)
                    ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));

                ImGui::TextDisabled("%s", tabs[i].group);
                lastGroup = tabs[i].group;
            }

            const bool selected = selectedMenuTab == i;

            ImGui::PushID(i);
            if (ImGui::Selectable("##Tab", selected, 0, ImVec2(0.0f, tabHeight)))
                selectedMenuTab = i;
            ImGui::PopID();

            // Label is drawn manually to center it vertically and to leave room for the accent bar
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();
            auto drawList = ImGui::GetWindowDrawList();

            if (selected)
                drawList->AddRectFilled(rectMin, ImVec2(rectMin.x + 3.0f * menuResScale, rectMax.y),
                                        ImGui::GetColorU32(ImGuiCol_CheckMark));

            drawList->AddText(
                ImVec2(rectMin.x + 10.0f * menuResScale, rectMin.y + (tabHeight - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), tabs[i].label);
        }

        // Compact state of the upscaler and FG pinned to the bottom of the sidebar
        const float statusHeight = 1.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f + style.ItemSpacing.y;
        const float gap = ImGui::GetContentRegionAvail().y - statusHeight - style.ItemSpacing.y;
        if (gap > 0.0f)
            ImGui::Dummy(ImVec2(0.0f, gap));

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("SR");
        ImGui::SameLine(0.0f, 8.0f * menuResScale);
        if (currentFeature == nullptr || !currentFeature->IsInited())
            ImGui::TextDisabled("None");
        else if (currentFeature->IsFrozen())
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Idle");
        else
            ImGui::TextColored(toneMapColor(ImVec4(0.35f, 0.85f, 0.45f, 1.f)), "%s",
                               currentFeature->ShortName().c_str());

        ImGui::TextDisabled("FG");
        ImGui::SameLine(0.0f, 8.0f * menuResScale);
        auto fg = state.currentFG;
        if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
            ImGui::TextColored(toneMapColor(ImVec4(0.35f, 0.85f, 0.45f, 1.f)), "%s", std::string(fg->Name()).c_str());
        else
            ImGui::TextDisabled("Off");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Tab title, short explanation and the scrollable content -----------------------------
    ImGui::BeginChild("##MenuContent", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const Tab& tab = tabs[selectedMenuTab];

        // if (!tab.hideLabelInMenu)
        //{
        //     if (config->UseHQFont.value_or_default())
        //         ImGui::PushFontSize(std::round(fontSize * menuResScale * 1.3f));

        //    ImGui::TextUnformatted(tab.label);

        //    if (config->UseHQFont.value_or_default())
        //        ImGui::PopFontSize();
        //}

        // ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        // ImGui::TextWrapped("%s", tab.description);
        // ImGui::PopStyleColor();

        // ImGui::Separator();

        // Own ID per tab, so every tab remembers its scroll position
        ImGui::PushID(selectedMenuTab);
        ImGui::BeginChild("##MenuTabBody", ImVec2(0.0f, 0.0f));

        if (tab.draw != nullptr)
            tab.draw(ctx);

        size_t boxCount = 0;
        const MenuBox* boxes = GetMenuBoxes(boxCount);
        DrawFn lastSource = nullptr;

        for (size_t i = 0; i < boxCount; i++)
        {
            if (strcmp(boxes[i].tab, tab.label) != 0)
                continue;

            // A function with several boxes is drawn once for all of them
            if (boxes[i].draw == nullptr)
            {
                if (boxes[i].source != lastSource)
                    boxes[i].source(ctx);
            }
            else
            {
                boxes[i].draw(ctx);
            }

            lastSource = boxes[i].source;
        }

        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MenuCommon::PlotLinesWithRange(const char* label, float (*values_getter)(void* data, int idx), void* data,
                                    int values_count, int values_offset, const char* overlay_text, ImVec2 graph_size,
                                    float padding)
{
    float v_min = FLT_MAX;
    float v_max = -FLT_MAX;
    for (int i = 0; i < plotWidth; i++)
    {
        const float v = values_getter(data, i);
        if (v != v) // Ignore NaN values
            continue;
        v_min = ImMin(v_min, v);
        v_max = ImMax(v_max, v);
    }

    ImGui::PlotEx(ImGuiPlotType_Lines, label, values_getter, data, values_count, values_offset, overlay_text, v_min,
                  v_max, graph_size);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Range: %.2f ms - %.2f ms", v_min, v_max);
}

void MenuCommon::RenderFrameTimeGraph(RenderMenuContext& ctx)
{
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& averageFrameTime = ctx.averageFrameTime;

    ImGui::Text("Frametime: %.2fms / %.1ffps (avg: %.1ffps)", frameTime, frameRate, 1000.f / averageFrameTime);

    PlotLinesWithRange(
        "##FrameTime",
        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
        &gFrameTimes, plotWidth, 0, nullptr, ImVec2(0.0f, FooterPlotHeight()), 4.0f * menuResScale);
}

void MenuCommon::RenderUpscalerTimeGraph(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    if (!IsUpscalerActive(currentFeature))
        return;

    ImGui::Text("Upscaler GPU time: %.2fms (avg: %.2fms)", state.upscaleTimes.back(), averageUpscalerFT);

    PlotLinesWithRange(
        "##UpscalerTime",
        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
        &gUpscalerTimes, plotWidth, 0, nullptr, ImVec2(0.0f, FooterPlotHeight()), 4.0f * menuResScale);
}

void MenuCommon::RenderMainMenuFooter(RenderMenuContext& ctx)
{
    ImGui::Separator();
    ImGui::Spacing();

    // Placed by hand instead of using a table or SameLine. Both of them carry the text baseline of the
    // first graph over to the second one, which made it sit a few pixels lower
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float graphWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    const ImVec2 startPos = ImGui::GetCursorPos();

    ImGui::PushItemWidth(graphWidth);

    RenderFrameTimeGraph(ctx);
    const float endY = ImGui::GetCursorPosY();

    // The indent is needed because the cursor goes back to the left edge after every item
    ImGui::SetCursorPosY(startPos.y);
    ImGui::Indent(graphWidth + spacing);
    RenderUpscalerTimeGraph(ctx);
    ImGui::Unindent(graphWidth + spacing);

    ImGui::PopItemWidth();

    ImGui::SetCursorPosY(endY);

    RenderMainMenuBottomBar(ctx);
}

void MenuCommon::RenderShaderTimes(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;

    if (!IsUpscalerActive(currentFeature) || state.detailedGpuTimes.empty())
        return;

    SectionTitle("Per shader GPU times");

    if (ImGui::BeginTable("ShaderTimes", 2, ImGuiTableFlags_SizingFixedSame))
    {
        bool hasExtra = false;

        for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
        {
            if (!includedInUpscalerTime)
            {
                hasExtra = true;
                continue;
            }

            auto formattedTime = StrFmt("%7.2f ms", time);

            ImGui::TableNextColumn();
            ImGui::Text("%s", name.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", formattedTime.c_str());
        }

        if (hasExtra)
        {
            ImGui::TableNextRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Extra shaders:");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("");
            for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
            {
                if (includedInUpscalerTime)
                    continue;

                auto formattedTime = StrFmt("%7.2f ms", time);

                ImGui::TableNextColumn();
                ImGui::Text("%s", name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", formattedTime.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuBottomBar(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // BOTTOM LINE ---------------
    if (ImGui::Button("Open Wiki"))
    {
        auto pIO = &ImGui::GetPlatformIO();
        auto ctx = ImGui::GetCurrentContext();
        pIO->Platform_OpenInShellFn(ctx, "https://github.com/optiscaler/OptiScaler/wiki");
    }
    ShowTooltip("Click to open the OptiScaler Wiki page\nin your default browser\n\n"
                "Compatibility list with known game issues\nand workarounds, FG options explained\n"
                "and other useful info");

    ImGui::SameLine();

    auto& style = ImGui::GetStyle();
    float rightButtonsWidth =
        ImGui::CalcTextSize("Save Settings").x + ImGui::CalcTextSize("Close").x + style.FramePadding.x * 4.0f + 6.0f;

    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - rightButtonsWidth);

    if (ImGui::Button("Save Settings"))
        config->SaveIni();

    ImGui::SameLine(0.0f, 6.0f);

    if (ImGui::Button("Close"))
    {
        _isVisible = false;
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

        _showMipmapCalcWindow = false;
        _showHudlessWindow = false;
        io.MouseDrawCursor = false;
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
    }
}

void MenuCommon::RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    // Metrics window (for debug)
    // ImGui::ShowMetricsWindow();

    // Mipmap calculation window
    if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() && currentFeature->IsInited())
    {
        auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

        if (_displayWidth == 0)
        {
            if (config->OutputScalingEnabled.value_or_default())
            {
                _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                      config->OutputScalingMultiplier.value_or_default());
            }
            else
            {
                _displayWidth = currentFeature->DisplayWidth();
            }

            _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
            _mipmapUpscalerQuality = 0;
            _mipmapUpscalerRatio = 3.0f;
            _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
        }

        if (ImGui::Begin("Mipmap Bias", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (ImGui::InputScalar("Display Width", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
            {
                if (_displayWidth <= 0)
                {
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }
                }

                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            const char* q[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Ultra Quality", "DLAA" };
            float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
            auto configQ = _mipmapUpscalerQuality;

            const char* selectedQ = q[configQ];

            ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

            if (ImGui::BeginCombo("Upscaler Quality", selectedQ))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                    {
                        _mipmapUpscalerQuality = n;

                        float ov = -1.0f;

                        if (config->QualityRatioOverrideEnabled.value_or_default())
                        {
                            switch (n)
                            {
                            case 0:
                                ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                break;

                            case 1:
                                ov = config->QualityRatio_Performance.value_or(-1.0f);
                                break;

                            case 2:
                                ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                break;

                            case 3:
                                ov = config->QualityRatio_Quality.value_or(-1.0f);
                                break;

                            case 4:
                                ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                break;
                            }
                        }

                        if (ov > 0.0f)
                            _mipmapUpscalerRatio = ov;
                        else
                            _mipmapUpscalerRatio = fr[n];

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();

            auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
            if (ImGui::SliderFloat("Upscaler Ratio", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
            {
                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

            ImGui::SliderFloat("Mipmap Bias", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

            // BOTTOM LINE
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SameLine();
            ImGui::Spacing();

            constexpr float spacing = 6.0f;
            auto textSize = ImGui::CalcTextSize("Use Value");
            textSize += ImGui::CalcTextSize("Close");
            textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

            if (ImGui::Button("Use Value"))
            {
                _mipBias = _mipBiasCalculated;
                _showMipmapCalcWindow = false;
            }

            ImGui::SameLine(0.0f, spacing);

            if (ImGui::Button("Close"))
                _showMipmapCalcWindow = false;

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::End();
        }
    }
}

void MenuCommon::RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    auto fg = state.currentFG;
    if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
    {
        auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

        if (ImGui::Begin("HUDless Resources", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            int btnCount = 100;

            if (ImGui::BeginTable("HUDlessTable", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                for (it = state.capturedHudlesses.begin(); it != state.capturedHudlesses.end(); it++)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::Text("%08x, %s->%s, Count: %llu, %s", (size_t) it->first,
                                GetSourceString(it->second.captureInfo & 0xFF).c_str(),
                                GetDispatchString(it->second.captureInfo & 0xFF00).c_str(), it->second.usageCount,
                                it->second.enabled ? "Active" : "Passive");

                    ImGui::TableSetColumnIndex(1);

                    btnCount++;
                    std::string text;

                    if (it->second.enabled)
                        text = StrFmt("Disable##%d", btnCount);
                    else
                        text = StrFmt("Enable##%d", btnCount);

                    if (ImGui::Button(text.c_str()))
                    {
                        LOG_DEBUG("HUDless {:X}: {}", (size_t) it->first,
                                  it->second.enabled ? "Disabling" : "Enabling");
                        it->second.enabled = !it->second.enabled;
                    }
                }

                ImGui::EndTable();
            }

            if (ImGui::Button("Clear##4"))
            {
                LOG_DEBUG("Clearing captured HUDless resources");
                state.clearCapturedHudlesses = true;
            }

            ImGui::SameLine(0.0f, 8.0f);

            if (ImGui::Button("Close##4"))
                _showHudlessWindow = false;

            ImGui::End();
        }
    }
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& io = ctx.io;

    if (!_isVisible)
        return;

    // Check for GPU support once and reuse the result in all menu sections.
    // DXVK might call Vulkan device creation, which would destroy our objects.
    State::Instance().vulkanSkipHooks = true;
    ctx.primaryGpu =
        std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(IdentifyGpu::getPrimaryGpu());
    State::Instance().vulkanSkipHooks = false;

    // Overlay font
    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(menuResScale * fontSize));

    // If overlay is not visible frame needs to be inited
    if (!frameTimesCalculated)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
    }

    ImGuiWindowFlags flags = 0;
    flags |= ImGuiWindowFlags_NoSavedSettings;
    flags |= ImGuiWindowFlags_NoCollapse;
    flags |= ImGuiWindowFlags_AlwaysAutoResize;

    if (lastMenuScale != menuResScale)
    {
        lastMenuScale = menuResScale;

        // if UI scale is changed rescale the style
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiStyle styleold = style; // Backup colors
        style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                     // so we should reset all style config

        ApplyThemeStyle();

        style.ScaleAllSizes(menuResScale);
        style.MouseCursorScale = 1.0f;
        CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors
    }

    // Fixed width, only the tab content scrolls. Keeps the menu from growing or shrinking when tabs change
    // Height can be changed by the user with the resize grip and is stored unscaled in the config
    ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    const float menuWidth = std::min(680.0f * menuResScale, io.DisplaySize.x - 20.0f);
    const float maxMenuHeight = io.DisplaySize.y - 20.0f;
    const float minMenuHeight = std::min(300.0f * menuResScale, maxMenuHeight);
    const float menuHeight =
        std::clamp(config->MenuHeight.value_or(815.0f) * menuResScale, minMenuHeight, maxMenuHeight);

    ImGui::SetNextWindowSize(ImVec2(menuWidth, menuHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(menuWidth, minMenuHeight), ImVec2(menuWidth, maxMenuHeight));

    // Main menu window
    if (windowTitle.empty())
    {
        windowTitle = StrFmt("%s - %s %s", BuildInfo::ProductName(), state.gameExe.c_str(),
                             state.gameName.empty() ? "" : StrFmt("- %s", state.gameName.c_str()).c_str());
    }

    if (ImGui::Begin(windowTitle.c_str(), NULL, mainFlags))
    {
        // Update and config notices shown above the tabs.
        RenderMainMenuHeaderMessages(ctx);

        // Tabs on the left, tab content on the right.
        RenderMainMenuTabs(ctx);

        // Graphs and buttons, always visible regardless of the selected tab.
        RenderMainMenuFooter(ctx);

        auto winSize = ImGui::GetWindowSize();
        auto winPos = ImGui::GetWindowPos();

        // Menu was resized by the user
        if (std::abs(winSize.y - menuHeight) > 0.5f)
            config->MenuHeight = std::round(winSize.y / menuResScale);

        if (lastPosition.x < -900.0f || (lastPosition.x >= winPos.x - 1.0f && lastPosition.y >= winPos.y - 1.0f &&
                                         lastPosition.x <= winPos.x + 1.0f && lastPosition.y <= winPos.y + 1.0f))
        {
            float posX;
            float posY;

            posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
            posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;

            // don't position menu outside of screen
            if (posX < 0.0 || posY < 0.0)
            {
                posX = 50;
                posY = 50;
            }

            ImGui::SetWindowPos(ImVec2 { posX, posY });
            lastPosition.x = posX;
            lastPosition.y = posY;
        }

        // Blurred game image behind the window, the semi transparent window background tints it
        if (_bgBlurTexture != ImTextureID_Invalid && config->MenuBlur.value_or_default() &&
            ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w < 1.0f)
        {
            const ImVec2 blurMin = ImGui::GetWindowPos();
            const ImVec2 blurMax = ImVec2(blurMin.x + ImGui::GetWindowWidth(), blurMin.y + ImGui::GetWindowHeight());
            const ImVec2 uvMul = ImVec2(_bgBlurUVScale.x / io.DisplaySize.x, _bgBlurUVScale.y / io.DisplaySize.y);

            ImGui::GetBackgroundDrawList()->AddImageRounded(
                _bgBlurTexture, blurMin, blurMax, ImVec2(blurMin.x * uvMul.x, blurMin.y * uvMul.y),
                ImVec2(blurMax.x * uvMul.x, blurMax.y * uvMul.y), IM_COL32_WHITE, ImGui::GetStyle().WindowRounding);

            _bgBlurUsed = true;
        }

        ImGui::End();
    }

    // Detached utility windows owned by the main menu.
    RenderMipmapBiasWindow(ctx, flags);
    RenderHudlessResourcesWindow(ctx, flags);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void KeyUp(UINT vKey)
{
    inputMenu = vKey == Config::Instance()->ShortcutKey.value_or_default();
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFG = vKey == Config::Instance()->FGShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();
    ctx.currentFeature = ctx.state.currentFeature;

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    OptiInput::EndFrame(_isVisible);

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    RenderSplashWindow(ctx);
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    _isVisible = false;
    _isUWP = isUWP;
    lastPosition = { -1000.0f, -1000.0f };

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                         fontSize, &fontConfig);
        }
    }

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::Initialize(_handle, isUWP);

    ApplyThemeStyle();
    _isInited = true;
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    _showMipmapCalcWindow = false;
    _showHudlessWindow = false;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
