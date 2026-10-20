#include "pch.h"
#include "FSR4Presets.h"

#include <scanner/rtti.h>
#include <scanner/scanner.h>
#include <detours/detours.h>

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
// one for the driver dll and the rest for FFX SDK dlls the game and Opti load
std::array<HookedModule, 4> hookedModules {};
std::mutex hookMutex;

uint32_t CorrectedPreset(uint32_t preset)
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
        correctedPreset = Config::Instance()->Fsr4Preset.value();

    State::Instance().currentFsr4Preset = correctedPreset;

    return correctedPreset;
}

template <size_t Index> uint32_t hkCreateModel(void* context, uint32_t preset, void* arg3, void* arg4)
{
    auto correctedPreset = CorrectedPreset(preset);
    LOG_DEBUG("preset: {}, corrected preset: {}", preset, correctedPreset);

    return hookedModules[Index].o_CreateModel(context, correctedPreset, arg3, arg4);
}

template <size_t... Indices> constexpr auto MakeDetours(std::index_sequence<Indices...>)
{
    return std::array<PFN_CreateModel, sizeof...(Indices)> { hkCreateModel<Indices>... };
}

constexpr auto createModelDetours = MakeDetours(std::make_index_sequence<std::tuple_size_v<decltype(hookedModules)>>());

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

// cmp r/m32, 4 - opcode 83 with 7 in the ModRM reg field, optionally after a REX prefix
bool IsCompareWith4(const Instruction& instruction)
{
    auto bytes = instruction.bytes;
    size_t opcode = (bytes[0] & 0xF0) == 0x40 ? 1 : 0;

    return instruction.length > opcode + 2 && bytes[opcode] == 0x83 && ((bytes[opcode + 1] >> 3) & 7) == 7 &&
           bytes[instruction.length - 1] == 4;
}

bool IsJumpIfEqual(const Instruction& instruction)
{
    auto bytes = instruction.bytes;
    return bytes[0] == 0x74 || (bytes[0] == 0x0F && bytes[1] == 0x84);
}

bool IsDirectCall(const Instruction& instruction) { return instruction.length == 5 && instruction.bytes[0] == 0xE8; }

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
// Every version so far compiles the last check into `cmp reg, 4` + `jz`, then sets up the arguments and calls it
void* FindCreateModel(HMODULE module)
{
    constexpr size_t MaxArgumentSetupInstructions = 6;

    auto vtable = rtti::FindVtable(module, "ffxProvider_FSR4");
    auto virtualFunctions = rtti::CountVirtualFunctions(module, vtable);

    if (virtualFunctions == 0)
    {
        LOG_DEBUG("No FSR 4 provider in the module");
        return nullptr;
    }

    // Dispatch is the last virtual function of FFX providers
    auto dispatch = vtable[virtualFunctions - 1];

    std::vector<void*> candidates;

    for (auto chunk : scanner::GetFunctionChunks(module, dispatch))
    {
        auto instructions = Disassemble(chunk);

        for (size_t i = 0; i + 2 < instructions.size(); i++)
        {
            if (!IsCompareWith4(instructions[i]) || !IsJumpIfEqual(instructions[i + 1]))
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

} // namespace

void FSR4Presets::Hook(HMODULE module)
{
    if (module == nullptr)
        return;

    std::scoped_lock lock(hookMutex);

    if (std::ranges::any_of(hookedModules, [module](const HookedModule& hooked) { return hooked.module == module; }))
        return;

    auto freeSlot = std::ranges::find(hookedModules, HMODULE(nullptr), &HookedModule::module);
    if (freeSlot == hookedModules.end())
    {
        LOG_ERROR("No free slot left for hooking FSR 4 presets");
        return;
    }

    auto moduleName = wstring_to_string(Util::ModulePath(module).filename().wstring());

    auto createModel = FindCreateModel(module);
    if (createModel == nullptr)
    {
        LOG_ERROR("Couldn't find FSR 4 preset selection in {}", moduleName);
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
    LOG_INFO("Hooked FSR 4 preset selection of {} at {:X}", moduleName, (uintptr_t) createModel);
}

bool FSR4Presets::IsAnyModuleHooked()
{
    std::scoped_lock lock(hookMutex);
    return std::ranges::any_of(hookedModules, [](const HookedModule& hooked) { return hooked.module != nullptr; });
}
