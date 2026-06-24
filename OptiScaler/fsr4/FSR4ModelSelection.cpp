#include "pch.h"
#include "FSR4ModelSelection.h"
#include <scanner/scanner.h>
#include <detours/detours.h>

PFN_getModelBlob FSR4ModelSelection::o_getModelBlobSDK = nullptr;
PFN_getModelBlob FSR4ModelSelection::o_getModelBlobDriver = nullptr;
PFN_createModel FSR4ModelSelection::o_createModelSDK = nullptr;
PFN_createModel FSR4ModelSelection::o_createModelDriver = nullptr;
PFN_createModel2 FSR4ModelSelection::o_createModelDriver2 = nullptr;
PFN_createModel2 FSR4ModelSelection::o_createModelSDK2 = nullptr;

uint32_t getCorrectedPreset(uint32_t preset)
{
    auto correctedPreset = preset;
    // Fixup for Quality preset sometimes using model 0, sometimes using model 1
    if (State::Instance().currentFeature)
    {
        auto target = State::Instance().currentFeature->TargetWidth();
        auto render = State::Instance().currentFeature->RenderWidth();

        auto ratio = (float) target / (float) render;

        // Include Ultra Quality in the fix as well
        if (preset == 0 && ratio >= 1.29f)
            correctedPreset = 1;
    }

    if (Config::Instance()->Fsr4Preset.has_value())
    {
        correctedPreset = Config::Instance()->Fsr4Preset.value();
    }

    State::Instance().currentFsr4Preset = correctedPreset;

    return correctedPreset;
}

uint64_t FSR4ModelSelection::hkgetModelBlobSDK(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_getModelBlobSDK(preset, unknown, source, size);

    return result;
}

uint64_t FSR4ModelSelection::hkgetModelBlobDriver(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_getModelBlobDriver(preset, unknown, source, size);

    return result;
}

uint64_t FSR4ModelSelection::hkcreateModelSDK(void* context, uint32_t preset)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_createModelSDK(context, preset);

    return result;
}

uint64_t FSR4ModelSelection::hkcreateModelSDK2(void* context, uint32_t preset, void** model)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_createModelSDK2(context, preset, model);

    return result;
}

uint64_t FSR4ModelSelection::hkcreateModelDriver(void* context, uint32_t preset)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_createModelDriver(context, preset);

    return result;
}

uint64_t FSR4ModelSelection::hkcreateModelDriver2(void* context, uint32_t preset, void** model)
{
    LOG_FUNC();

    preset = getCorrectedPreset(preset);

    auto result = o_createModelDriver2(context, preset, model);

    return result;
}

void FSR4ModelSelection::Hook(HMODULE module, FSR4Source source)
{
    if (module == nullptr)
        return;

    if (source == FSR4Source::SDK && (o_getModelBlobSDK || o_createModelSDK))
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_getModelBlobSDK)
            DetourDetach(&(PVOID&) o_getModelBlobSDK, hkgetModelBlobSDK);

        if (o_createModelSDK)
            DetourDetach(&(PVOID&) o_createModelSDK, hkcreateModelSDK);

        if (DetourTransactionCommit() == NO_ERROR)
        {
            LOG_DEBUG("Unhooked old model selection hooks for SDK");

            o_createModelSDK = nullptr;
            o_getModelBlobSDK = nullptr;
        }
    }
    else if (source == FSR4Source::DriverDll && (o_getModelBlobDriver || o_createModelDriver))
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_getModelBlobDriver)
            DetourDetach(&(PVOID&) o_getModelBlobDriver, hkgetModelBlobDriver);

        else if (o_createModelDriver)
            DetourDetach(&(PVOID&) o_createModelDriver, hkcreateModelDriver);

        if (DetourTransactionCommit() == NO_ERROR)
        {
            LOG_DEBUG("Unhooked old model selection hooks for the driver dll");

            o_createModelDriver = nullptr;
            o_getModelBlobDriver = nullptr;
        }
    }

    /// Hooks for getModelBlob

    // Older SDK and Driver use this
    const char* modelBlobPattern = "83 F9 05 0F 87";
    if (!o_getModelBlobSDK && source == FSR4Source::SDK)
    {
        o_getModelBlobSDK = (PFN_getModelBlob) scanner::GetAddress(module, modelBlobPattern);

        if (o_getModelBlobSDK)
        {
            LOG_DEBUG("Hooking model selection o_getModelBlobSDK: {:X}", (uintptr_t) o_getModelBlobSDK);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_getModelBlobSDK, hkgetModelBlobSDK);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_getModelBlobSDK = nullptr;
            }
        }
    }
    else if (!o_getModelBlobDriver && source == FSR4Source::DriverDll)
    {
        o_getModelBlobDriver = (PFN_getModelBlob) scanner::GetAddress(module, modelBlobPattern);

        if (o_getModelBlobDriver)
        {
            LOG_DEBUG("Hooking model selection o_getModelBlobDriver: {:X}", (uintptr_t) o_getModelBlobDriver);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_getModelBlobDriver, hkgetModelBlobDriver);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_getModelBlobDriver = nullptr;
            }
        }
    }

    /// Hooks for createModel

    // From amd_fidelityfx_upscaler_dx12 4.0.3.604 from FFX 2.1 SDK
    // Used by some versions of SDK and Driver
    const char* pattern403 =
        "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 0F 29 B4 24 "
        "? ? ? ? 0F 29 BC 24 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 8B F2";

    if (!o_createModelSDK && source == FSR4Source::SDK)
    {
        o_createModelSDK = (PFN_createModel) scanner::GetAddress(module, pattern403);

        if (!o_createModelSDK)
        {
            // From amd_fidelityfx_upscaler_dx12 4.1.0 from FFX 2.2 SDK
            const char* pattern410 =
                "48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 F2 FF FF 48 81 EC A0 "
                "0E 00 00 0F 29 70 B8 0F 29 78 A8 48 8B ? ? ? ? ? 48 33 C4 48 89 85 78 0D 00 00 44 8B F2";
            o_createModelSDK = (PFN_createModel) scanner::GetAddress(module, pattern410);
        }

        LOG_DEBUG("Hooking model selection, o_createModelSDK: {:X}", (uintptr_t) o_createModelSDK);

        if (o_createModelSDK)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_createModelSDK, hkcreateModelSDK);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_createModelSDK = nullptr;
            }
        }
    }

    if (!o_createModelSDK && !o_createModelSDK2 && source == FSR4Source::SDK)
    {
        // From amd_fidelityfx_upscaler_dx12 4.1.1.2740 from FFX 2.3 SDK
        const char* pattern411 =
            "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 F2 FF FF 48 81 EC C0 0E 00 00 0F 29 70 "
            "B8 0F 29 78 A8 48 8B ? ? ? ? ? 48 33 C4 48 89 85 90 0D 00 00 4D 8B E8 8B FA 48 8B F1";
        o_createModelSDK2 = (PFN_createModel2) scanner::GetAddress(module, pattern411);

        if (o_createModelSDK2)
        {
            LOG_DEBUG("Hooking model selection, o_createModelSDK2: {:X}", (uintptr_t) o_createModelSDK2);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_createModelSDK2, hkcreateModelSDK2);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_createModelSDK2 = nullptr;
            }
        }
    }

    else if (!o_createModelDriver && source == FSR4Source::DriverDll)
    {
        o_createModelDriver = (PFN_createModel) scanner::GetAddress(module, pattern403);

        if (!o_createModelDriver)
        {
            // From amdxcffx64 2.1.0.968/2.2.0.1328
            const char* pattern =
                "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F "
                "29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 8B F2 48 8B F1 E8";
            o_createModelDriver = (PFN_createModel) scanner::GetAddress(module, pattern);
        }

        if (o_createModelDriver)
        {
            LOG_DEBUG("Hooking model selection, o_createModelDriver: {:X}", (uintptr_t) o_createModelDriver);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_createModelDriver, hkcreateModelDriver);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_createModelDriver = nullptr;
            }
        }
    }

    if (!o_createModelDriver && !o_createModelDriver2 && source == FSR4Source::DriverDll)
    {
        // From amdxcffx64 2.3.0.2740
        const char* pattern = "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? "
                              "0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4D 8B E8 8B FA 48 8B";
        o_createModelDriver2 = (PFN_createModel2) scanner::GetAddress(module, pattern);

        if (o_createModelDriver2)
        {
            LOG_DEBUG("Hooking model selection, o_createModelDriver2: {:X}", (uintptr_t) o_createModelDriver2);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_createModelDriver2, hkcreateModelDriver2);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_createModelDriver2 = nullptr;
            }
        }
    }

    if (!o_createModelDriver && !o_getModelBlobDriver && !o_createModelDriver2 && source == FSR4Source::DriverDll)
        LOG_ERROR("Couldn't hook model selection from the driver dll");
    else if (!o_createModelSDK && !o_getModelBlobSDK && !o_createModelSDK2 && source == FSR4Source::SDK)
        LOG_ERROR("Couldn't hook model selection from the SDK dll");
}
