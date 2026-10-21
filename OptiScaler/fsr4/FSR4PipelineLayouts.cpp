#include "pch.h"
#include "FSR4PipelineLayouts.h"

#include <scanner/scanner.h>
#include <detours/detours.h>

#include <unordered_map>

namespace
{
// Offsets matched by the patterns below
constexpr uintptr_t BackendOffset = 0xB8;          // FfxInterface::scratchBuffer, the backend context
constexpr uintptr_t LayoutsOffset = 0x1C8;         // BackendContext_VK::pPipelineLayouts
constexpr uintptr_t EffectContextsOffset = 0x41F8; // BackendContext_VK::pEffectContexts
constexpr uintptr_t EffectContextSize = 0xA0;
constexpr uintptr_t NextLayoutOffset = 0x80; // EffectContext::nextPipelineLayout
constexpr size_t LayoutSize = 0x1E8;

// FfxPipelineState starts with the pipeline layout (rootSignature), the VkPipeline is the third pointer
constexpr size_t StateLayoutIndex = 0;
constexpr size_t StatePipelineIndex = 2;

constexpr uint32_t FfxOk = 0;

// CreatePipeline up to taking the next layout slot of the effect context in r9d:
//     backend = interface->scratchBuffer
//     layout = backend->pPipelineLayouts + backend->pEffectContexts[effectContextId].nextPipelineLayout++
// The output pipeline state is the 5th argument ([rbp + 0x7A0] with this frame).
// This is the only code reading pPipelineLayouts, everything after it uses the layout pointer from the state
constexpr std::string_view CreatePipelinePattern =
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C8 F8 FF FF 48 81 EC 38 08 00 00 48 8B 05 ? ? ? ? 48 33 C4 "
    "48 89 85 20 07 00 00 4C 89 44 24 58 48 8B FA 48 89 4C 24 60 48 8B 85 A0 07 00 00 48 89 44 24 38 4C 8B 91 B8 00 "
    "00 00 4C 89 54 24 50 41 8B C1 4C 8D 3C 80 49 C1 E7 05 4D 8B A2 F8 41 00 00 43 8B 94 27 80 00 00 00 4C 69 F2 E8 "
    "01 00 00 4D 03 B2 C8 01 00 00 8D 42 01 43 89 84 27 80 00 00 00";

// DestroyPipeline(interface, pipelineState) up to reading the VkPipelineLayout of the state's layout
constexpr std::string_view DestroyPipelinePattern =
    "40 53 57 48 83 EC 28 48 8B B9 B8 00 00 00 48 8B DA 48 85 D2 0F 84 ? ? ? ? 48 8B 52 10 48 89 74 24 48 4C 89 64 "
    "24 50 45 33 E4 48 85 D2 74 14 48 8B 87 80 00 00 00 45 33 C0 48 8B 4F 08 FF D0 4C 89 63 10 4C 89 63 08 48 8B 1B "
    "48 85 DB 0F 84 ? ? ? ? 48 8B 93 D0 01 00 00";

typedef uint32_t (*PFN_CreatePipeline)(void* backendInterface, void* arg2, void* arg3, uint32_t effectContextId,
                                       void** pipelineState);
typedef uint32_t (*PFN_DestroyPipeline)(void* backendInterface, void** pipelineState);

PFN_CreatePipeline o_CreatePipeline = nullptr;
PFN_DestroyPipeline o_DestroyPipeline = nullptr;
HMODULE hookedModule = nullptr;

struct alignas(16) Layout
{
    uint8_t data[LayoutSize];
};

std::mutex layoutsMutex;

// Layouts handed to the backend, with the pipeline using them
std::unordered_map<Layout*, void*> usedLayouts;
std::vector<std::unique_ptr<Layout>> allLayouts;
std::vector<Layout*> freeLayouts;

Layout* TakeLayout()
{
    if (freeLayouts.empty())
    {
        allLayouts.push_back(std::make_unique<Layout>());
        return allLayouts.back().get();
    }

    auto layout = freeLayouts.back();
    freeLayouts.pop_back();

    // Same state as the slots the backend clears when it gets created
    memset(layout, 0, sizeof(Layout));
    return layout;
}

uint32_t hkCreatePipeline(void* backendInterface, void* arg2, void* arg3, uint32_t effectContextId,
                          void** pipelineState)
{
    std::scoped_lock lock(layoutsMutex);

    const auto backend = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(backendInterface) + BackendOffset);
    auto& layouts = *reinterpret_cast<uintptr_t*>(backend + LayoutsOffset);
    auto effectContexts = *reinterpret_cast<uintptr_t*>(backend + EffectContextsOffset);
    auto& nextLayout =
        *reinterpret_cast<uint32_t*>(effectContexts + effectContextId * EffectContextSize + NextLayoutOffset);

    // The backend takes pPipelineLayouts[nextPipelineLayout], moving the array start makes that our layout.
    // Neither gets to change, so the slots of the effect context never run out
    auto layout = TakeLayout();
    const auto originalLayouts = layouts;
    const auto originalNext = nextLayout;
    layouts = reinterpret_cast<uintptr_t>(layout) - static_cast<uintptr_t>(originalNext) * LayoutSize;

    const auto result = o_CreatePipeline(backendInterface, arg2, arg3, effectContextId, pipelineState);

    layouts = originalLayouts;
    nextLayout = originalNext;

    if (result == FfxOk && pipelineState != nullptr && pipelineState[StateLayoutIndex] == layout)
        usedLayouts[layout] = pipelineState[StatePipelineIndex];
    else
        freeLayouts.push_back(layout);

    return result;
}

uint32_t hkDestroyPipeline(void* backendInterface, void** pipelineState)
{
    std::scoped_lock lock(layoutsMutex);

    if (pipelineState == nullptr)
        return o_DestroyPipeline(backendInterface, pipelineState);

    const auto layout = static_cast<Layout*>(pipelineState[StateLayoutIndex]);
    const auto used = usedLayouts.find(layout);
    const bool ownLayout = used != usedLayouts.end() || std::ranges::any_of(allLayouts, [layout](const auto& owned)
                                                                            { return owned.get() == layout; });

    // Layouts in the backend's own array, from pipelines created before the hooks
    if (!ownLayout)
        return o_DestroyPipeline(backendInterface, pipelineState);

    // A copy of a state that got destroyed already, the dispatch queues the same states again when it retries
    // a failed network build. Its pipeline is gone and its layout may belong to another pipeline by now
    if (used == usedLayouts.end() || used->second != pipelineState[StatePipelineIndex])
    {
        LOG_DEBUG("Skipping destruction of an already destroyed pipeline");
        pipelineState[StateLayoutIndex] = nullptr;
        pipelineState[1] = nullptr;
        pipelineState[StatePipelineIndex] = nullptr;
        return FfxOk;
    }

    const auto result = o_DestroyPipeline(backendInterface, pipelineState);

    usedLayouts.erase(used);
    freeLayouts.push_back(layout);

    return result;
}
} // namespace

void FSR4PipelineLayouts::Hook(HMODULE module)
{
    if (module == nullptr)
        return;

    std::scoped_lock lock(layoutsMutex);

    if (hookedModule != nullptr)
        return;

    auto createPipeline = scanner::GetAddress(module, CreatePipelinePattern);
    auto destroyPipeline = scanner::GetAddress(module, DestroyPipelinePattern);

    if (createPipeline == 0 || destroyPipeline == 0)
    {
        LOG_INFO("Pipeline layout fix not applied, backend doesn't match (create: {}, destroy: {})",
                 createPipeline != 0, destroyPipeline != 0);
        return;
    }

    o_CreatePipeline = reinterpret_cast<PFN_CreatePipeline>(createPipeline);
    o_DestroyPipeline = reinterpret_cast<PFN_DestroyPipeline>(destroyPipeline);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_CreatePipeline, hkCreatePipeline);
    DetourAttach(&(PVOID&) o_DestroyPipeline, hkDestroyPipeline);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach detours: {:X}", detourResult);
        o_CreatePipeline = nullptr;
        o_DestroyPipeline = nullptr;
        return;
    }

    hookedModule = module;
    LOG_INFO("Hooked pipeline creation of the Vulkan backend at {:X}", createPipeline);
}

bool FSR4PipelineLayouts::IsHooked()
{
    std::scoped_lock lock(layoutsMutex);
    return hookedModule != nullptr;
}
