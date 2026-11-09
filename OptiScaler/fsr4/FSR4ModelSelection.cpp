#include "pch.h"
#include "FSR4ModelSelection.h"
#include <detours/detours.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <span>
#include <vector>

uint32_t getCorrectedPreset(uint32_t preset)
{
    auto correctedPreset = preset;

    // Fixup for Quality preset sometimes using model 0, sometimes using model 1
    if (State::Instance().currentFeature)
    {
        auto target = State::Instance().currentFeature->TargetWidth();
        auto render = State::Instance().currentFeature->RenderWidth();

        auto ratio = (float) target / (float) render;

        if (preset == 0 && ratio >= 1.49f)
            correctedPreset = 1;
    }

    if (Config::Instance()->Fsr4Preset.has_value())
    {
        correctedPreset = Config::Instance()->Fsr4Preset.value();
    }

    State::Instance().currentFsr4Preset = correctedPreset;

    return correctedPreset;
}

namespace
{
// Every known version takes 2 or 3 arguments, the extra ones are forwarded untouched
typedef uint32_t (*PFN_CreateModel)(void* context, uint32_t preset, void* arg3, void* arg4);

struct HookedModule
{
    HMODULE module = nullptr;
    PFN_CreateModel o_CreateModel = nullptr;
};

// Each hooked module needs a detour of its own to know which original to call,
// the SDK and driver dlls of both APIs, plus spares for dlls the game loads
std::array<HookedModule, 8> hookedModules {};
std::mutex hookMutex;

template <size_t Index> uint32_t hkCreateModel(void* context, uint32_t preset, void* arg3, void* arg4)
{
    auto correctedPreset = getCorrectedPreset(preset);
    LOG_DEBUG("preset: {}, corrected preset: {}", preset, correctedPreset);

    return hookedModules[Index].o_CreateModel(context, correctedPreset, arg3, arg4);
}

template <size_t... Indices> constexpr auto MakeDetours(std::index_sequence<Indices...>)
{
    return std::array<PFN_CreateModel, sizeof...(Indices)> { hkCreateModel<Indices>... };
}

constexpr auto createModelDetours = MakeDetours(std::make_index_sequence<std::tuple_size_v<decltype(hookedModules)>>());

IMAGE_NT_HEADERS64* NtHeaders(HMODULE module)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    return reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
}

// Number of leading vtable entries that point into the module's code
size_t CountVirtualFunctions(HMODULE module, void** vtable)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto nt = NtHeaders(module);
    auto sections = std::span(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections);

    auto isCode = [&](const void* address)
    {
        return std::ranges::any_of(sections,
                                   [&](const IMAGE_SECTION_HEADER& section)
                                   {
                                       auto start = base + section.VirtualAddress;
                                       return (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && address >= start &&
                                              address < start + section.Misc.VirtualSize;
                                   });
    };

    size_t count = 0;
    while (isCode(vtable[count]))
        count++;

    return count;
}

// Unwind info of a chunk that continues another one ends with the RUNTIME_FUNCTION of the previous chunk
DWORD GetFirstChunkRva(const uint8_t* base, const RUNTIME_FUNCTION* chunk)
{
    struct UnwindInfo
    {
        BYTE versionAndFlags;
        BYTE sizeOfProlog;
        BYTE countOfCodes;
        BYTE frameRegisterAndOffset;
        USHORT codes[1];
    };

    while (true)
    {
        auto unwind = reinterpret_cast<const UnwindInfo*>(base + chunk->UnwindData);

        if (((unwind->versionAndFlags >> 3) & UNW_FLAG_CHAININFO) == 0)
            return chunk->BeginAddress;

        // Unwind codes are padded to an even count
        chunk = reinterpret_cast<const RUNTIME_FUNCTION*>(&unwind->codes[(unwind->countOfCodes + 1) & ~1]);
    }
}

// The function body and the cold chunks the compiler split off from it
std::vector<std::span<const uint8_t>> GetFunctionChunks(HMODULE module, const void* function)
{
    std::vector<std::span<const uint8_t>> chunks;

    auto base = reinterpret_cast<const uint8_t*>(module);
    auto& directory = NtHeaders(module)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

    auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(base + directory.VirtualAddress);
    auto count = directory.Size / sizeof(RUNTIME_FUNCTION);
    auto functionRva = static_cast<DWORD>(static_cast<const uint8_t*>(function) - base);

    for (size_t i = 0; i < count; i++)
    {
        if (GetFirstChunkRva(base, &functions[i]) == functionRva)
            chunks.emplace_back(base + functions[i].BeginAddress, functions[i].EndAddress - functions[i].BeginAddress);
    }

    return chunks;
}

struct Instruction
{
    const uint8_t* bytes;
    size_t length;
};

std::vector<Instruction> Disassemble(std::span<const uint8_t> code)
{
    std::vector<Instruction> instructions;

    auto current = code.data();
    auto end = code.data() + code.size();

    while (current < end)
    {
        auto next = static_cast<const uint8_t*>(
            DetourCopyInstruction(nullptr, nullptr, const_cast<uint8_t*>(current), nullptr, nullptr));

        if (next == nullptr || next <= current)
            break;

        instructions.push_back({ current, static_cast<size_t>(next - current) });
        current = next;
    }

    return instructions;
}

uint8_t Rex(const Instruction& instruction) { return (instruction.bytes[0] & 0xF0) == 0x40 ? instruction.bytes[0] : 0; }

// cmp r/m32, 4 - opcode 83 with 7 in the ModRM reg field, optionally after a REX prefix
bool IsCompareWith4(const Instruction& instruction)
{
    auto bytes = instruction.bytes;
    size_t opcode = Rex(instruction) != 0 ? 1 : 0;

    return instruction.length > opcode + 2 && bytes[opcode] == 0x83 && ((bytes[opcode + 1] >> 3) & 7) == 7 &&
           bytes[instruction.length - 1] == 4;
}

// mov r32, imm32 - opcode B8+r, REX.B extends the register number
bool IsMoveImmediate(const Instruction& instruction, uint8_t& reg, uint32_t& value)
{
    auto rex = Rex(instruction);
    auto opcode = instruction.bytes + (rex != 0 ? 1 : 0);

    if (instruction.length != (rex != 0 ? 6 : 5) || (opcode[0] & 0xF8) != 0xB8)
        return false;

    reg = (opcode[0] & 7) | ((rex & 1) << 3);
    memcpy(&value, opcode + 1, sizeof(value));
    return true;
}

// cmp r/m32, r32 or cmp r32, r/m32 (opcodes 39 and 3B) where either register is in the mask
bool IsCompareWithRegister(const Instruction& instruction, uint16_t registers)
{
    auto rex = Rex(instruction);
    auto opcode = instruction.bytes + (rex != 0 ? 1 : 0);

    if (instruction.length < (size_t) (rex != 0 ? 3 : 2) || (opcode[0] != 0x39 && opcode[0] != 0x3B))
        return false;

    auto modRM = opcode[1];
    auto reg = ((modRM >> 3) & 7) | ((rex & 4) << 1);
    auto rm = (modRM & 7) | ((rex & 1) << 3);

    return (registers >> reg) & 1 || ((modRM >> 6) == 3 && (registers >> rm) & 1);
}

bool IsJumpIfEqual(const Instruction& instruction)
{
    auto bytes = instruction.bytes;
    return bytes[0] == 0x74 || (bytes[0] == 0x0F && bytes[1] == 0x84);
}

bool IsDirectCall(const Instruction& instruction) { return instruction.length == 5 && instruction.bytes[0] == 0xE8; }

// call rel32 or call r/m64 (FF with 2 in the ModRM reg field)
bool IsCall(const Instruction& instruction)
{
    auto opcode = instruction.bytes + (Rex(instruction) != 0 ? 1 : 0);
    return IsDirectCall(instruction) || (instruction.length >= 2 && opcode[0] == 0xFF && ((opcode[1] >> 3) & 7) == 2);
}

// rax, rcx, rdx, r8-r11, a call can change any of them
constexpr uint16_t VolatileRegisters = 0x0F07;

void* CallTarget(const Instruction& call)
{
    int32_t displacement;
    memcpy(&displacement, call.bytes + 1, sizeof(displacement));
    return const_cast<uint8_t*>(call.bytes + call.length + displacement);
}

// The dispatch only recreates the network when needed, preset 4 is the one used for dynamic resolution:
//
//     if (!created || flagsChanged || (preset != context->preset && context->preset != 4))
//         CreateModel(context, preset, ...);
//
// Every version so far compiles the last check into `cmp reg, 4` + `jz`, then sets up the arguments and calls it.
// Newer ones keep the 4 in a register instead, `mov r15d, 4` ... `cmp eax, r15d` + `jz`
void* FindCreateModel(HMODULE module)
{
    constexpr size_t MaxArgumentSetupInstructions = 6;

    auto vtable = Util::FindVTableByRtti(module, ".?AVffxProvider_FSR4@@");
    auto virtualFunctions = vtable != nullptr ? CountVirtualFunctions(module, vtable) : 0;

    if (virtualFunctions == 0)
    {
        LOG_DEBUG("No FSR 4 provider in the module");
        return nullptr;
    }

    // Dispatch is the last virtual function of FFX providers, 4.0.x has more of them than newer versions
    auto dispatch = vtable[virtualFunctions - 1];

    std::vector<void*> candidates;

    // Bit per register last loaded with 4
    uint16_t registersWith4 = 0;

    for (auto chunk : GetFunctionChunks(module, dispatch))
    {
        auto instructions = Disassemble(chunk);

        for (size_t i = 0; i + 2 < instructions.size(); i++)
        {
            uint8_t reg;
            uint32_t value;

            if (IsMoveImmediate(instructions[i], reg, value))
            {
                if (value == 4)
                    registersWith4 |= 1 << reg;
                else
                    registersWith4 &= ~(1 << reg);

                continue;
            }

            if (IsCall(instructions[i]))
            {
                registersWith4 &= ~VolatileRegisters;
                continue;
            }

            auto isCompareWith4 =
                IsCompareWith4(instructions[i]) || IsCompareWithRegister(instructions[i], registersWith4);

            if (!isCompareWith4 || !IsJumpIfEqual(instructions[i + 1]))
                continue;

            auto last = std::min(i + 2 + MaxArgumentSetupInstructions, instructions.size());
            auto call = std::find_if(instructions.begin() + i + 2, instructions.begin() + last, IsDirectCall);

            if (call != instructions.begin() + last)
                candidates.push_back(CallTarget(*call));
        }
    }

    if (candidates.size() != 1)
    {
        LOG_ERROR("Expected one model creation call in FSR 4 dispatch, found {}", candidates.size());
        return nullptr;
    }

    return candidates.front();
}

void HookCreateModel(HMODULE module)
{
    std::scoped_lock lock(hookMutex);

    if (std::ranges::any_of(hookedModules, [module](const HookedModule& hooked) { return hooked.module == module; }))
        return;

    auto freeSlot = std::ranges::find(hookedModules, HMODULE(nullptr), &HookedModule::module);
    if (freeSlot == hookedModules.end())
    {
        LOG_ERROR("No free slot left for hooking FSR 4 model selection");
        return;
    }

    auto createModel = FindCreateModel(module);
    if (createModel == nullptr)
    {
        LOG_ERROR("Couldn't find FSR 4 model selection in {:X}", (uintptr_t) module);
        return;
    }

    auto index = std::distance(hookedModules.begin(), freeSlot);
    freeSlot->o_CreateModel = reinterpret_cast<PFN_CreateModel>(createModel);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) freeSlot->o_CreateModel, createModelDetours[index]);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach detour: {:X}", detourResult);
        freeSlot->o_CreateModel = nullptr;
        return;
    }

    freeSlot->module = module;
    LOG_INFO("Hooked FSR 4 model selection of {:X} at {:X}", (uintptr_t) module, (uintptr_t) createModel);
}

// In the order FFX checks them, the first supported one runs. The index is Fsr4ForceEnableInt8's value
constexpr std::pair<const char*, const char*> modelProviders[] = {
    { "FP8", ".?AVffxProvider_FSR4@@" },
    { "INT8", ".?AVffxProvider_FSR4_Int8@@" },
    { "INT8 RDNA2", ".?AVffxProvider_FSR4_Int8_Navi2@@" },
    { "INT8 Lite", ".?AVffxProvider_FSR4Lite_Int8@@" },
};

// ffxProvider::IsSupported(void* device), the vtable layout of FFX SDK 2.0+ providers
constexpr size_t isSupportedIndex = 2;

typedef bool (*PFN_IsSupported)(void* provider, void* device);

struct PatchedProvider
{
    HMODULE module;
    void** vtable;
    size_t model;
    API api;
    PFN_IsSupported o_IsSupported;
    bool lastSupported;
};

std::vector<PatchedProvider> patchedProviders;
HMODULE lastCheckedModule = nullptr;

bool HasModel(HMODULE module, size_t model)
{
    return std::ranges::any_of(patchedProviders, [&](const PatchedProvider& patched)
                               { return patched.module == module && patched.model == model; });
}

size_t ForcedModel() { return Config::Instance()->Fsr4ForceEnableInt8.value_or_default(); }

bool hkIsSupported(void* provider, void* device)
{
    auto vtable = *static_cast<void***>(provider);
    auto forced = ForcedModel();
    PatchedProvider patched {};
    bool useForced = false;

    {
        std::scoped_lock lock(hookMutex);

        auto found = std::ranges::find(patchedProviders, vtable, &PatchedProvider::vtable);
        if (found == patchedProviders.end())
            return false;

        patched = *found;
        useForced = forced > 0 && HasModel(patched.module, forced);
    }

    // The forced model wins even where FFX doesn't allow it, the ones before it (FP8 on RDNA 4) have to fail
    auto supported = useForced ? patched.model == forced : patched.o_IsSupported(provider, device);

    LOG_DEBUG("{}: {}", modelProviders[patched.model].first, supported);

    if (device == nullptr)
        return supported;

    std::scoped_lock lock(hookMutex);

    if (auto found = std::ranges::find(patchedProviders, vtable, &PatchedProvider::vtable);
        found != patchedProviders.end())
    {
        found->lastSupported = supported;
        lastCheckedModule = found->module;
    }

    return supported;
}

bool PatchVtableEntry(void** entry, void* value)
{
    DWORD oldProtection = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtection))
        return false;

    *entry = value;

    VirtualProtect(entry, sizeof(*entry), oldProtection, &oldProtection);
    return true;
}

void HookModelProviders(HMODULE module, API api)
{
    std::scoped_lock lock(hookMutex);

    if (std::ranges::find(patchedProviders, module, &PatchedProvider::module) != patchedProviders.end())
        return;

    std::vector<PatchedProvider> found;

    for (size_t i = 0; i < std::size(modelProviders); i++)
    {
        if (auto vtable = Util::FindVTableByRtti(module, modelProviders[i].second))
            found.push_back({ module, vtable, i, api });
    }

    // Older modules have a single model and a different vtable layout
    if (found.size() < 2)
        return;

    for (auto& provider : found)
    {
        auto entry = &provider.vtable[isSupportedIndex];
        provider.o_IsSupported = reinterpret_cast<PFN_IsSupported>(*entry);

        if (!PatchVtableEntry(entry, reinterpret_cast<void*>(&hkIsSupported)))
        {
            LOG_ERROR("Failed to patch the support check of {} in {:X}", modelProviders[provider.model].first,
                      (uintptr_t) module);
            continue;
        }

        LOG_INFO("Hooked the support check of {} in {:X}", modelProviders[provider.model].first, (uintptr_t) module);
        patchedProviders.push_back(provider);
    }
}
} // namespace

void FSR4ModelSelection::Hook(HMODULE module, FSR4Source source, API api)
{
    if (module == nullptr)
        return;

    HookCreateModel(module);
    HookModelProviders(module, api);
}

bool FSR4ModelSelection::IsInt8FsrHookedDx12()
{
    auto forced = ForcedModel();

    if (forced == 0)
        return false;

    std::scoped_lock lock(hookMutex);

    return std::ranges::any_of(patchedProviders, [&](const PatchedProvider& patched)
                               { return patched.api == API::DX12 && patched.model == forced; });
}

const char* FSR4ModelSelection::ActiveModel()
{
    std::scoped_lock lock(hookMutex);

    // The providers are in the order FFX checks them, it stops at the first supported one
    for (auto& patched : patchedProviders)
    {
        if (patched.module == lastCheckedModule && patched.lastSupported)
            return modelProviders[patched.model].first;
    }

    return nullptr;
}

std::vector<uint32_t> FSR4ModelSelection::SelectableModels()
{
    std::scoped_lock lock(hookMutex);

    std::vector<uint32_t> models;

    // FP8 is left to auto, FFX picks it wherever it runs
    for (auto& patched : patchedProviders)
    {
        if (patched.module == lastCheckedModule && patched.model > 0)
            models.push_back((uint32_t) patched.model);
    }

    return models;
}

const char* FSR4ModelSelection::ModelName(uint32_t model)
{
    return model < std::size(modelProviders) ? modelProviders[model].first : "Unknown";
}
