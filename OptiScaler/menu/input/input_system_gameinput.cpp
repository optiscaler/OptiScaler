#include "pch.h"
#include "input_system_internal.h"

#include <hooks/Kernel_Hooks.h>

#include <detours/detours.h>

namespace OptiInput
{
namespace
{
constexpr wchar_t GameInputModuleName[] = L"GameInput.dll";
constexpr wchar_t WindowsGamingInputModuleName[] = L"Windows.Gaming.Input.dll";
constexpr char GameInputCreateExportName[] = "GameInputCreate";

HMODULE GetAlreadyLoadedModule(const wchar_t* moduleName) { return GetModuleHandleW(moduleName); }
} // namespace

void UpdateGameInputIntegration()
{
    // A module may be mapped before its DllMain has completed
    HMODULE gameInputModule = GetAlreadyLoadedModule(GameInputModuleName);
    HMODULE windowsGamingInputModule = GetAlreadyLoadedModule(WindowsGamingInputModuleName);
    FARPROC gameInputCreate = nullptr;

    if (gameInputModule != nullptr)
        gameInputCreate = KernelBaseProxy::GetProcAddress_()(gameInputModule, GameInputCreateExportName);

    {
        std::unique_lock lock(_state.Mutex);

        _state.GameInputModule = gameInputModule;
        _state.WindowsGamingInputModule = windowsGamingInputModule;
        _state.GameInputModuleLoaded = gameInputModule != nullptr;
        _state.WindowsGamingInputModuleLoaded = windowsGamingInputModule != nullptr;
        _state.GameInputCreateExportFound = gameInputCreate != nullptr;

        if (_state.GameInputCreateHookInstalled || _state.GameInputCreateHookAttempted || gameInputCreate == nullptr)
        {
            return;
        }

        _state.GameInputCreateHookAttempted = true;
        o_GameInputCreate = reinterpret_cast<GameInputCreate_t>(gameInputCreate);
    }

    LONG result = NO_ERROR;

    {
        std::scoped_lock detourLock(GetDetourTransactionMutex());
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&o_GameInputCreate), hkGameInputCreate);
        result = DetourTransactionCommit();
    }

    std::unique_lock lock(_state.Mutex);
    _state.GameInputCreateHookInstalled = result == NO_ERROR;

    if (!_state.GameInputCreateHookInstalled)
    {
        LOG_WARN("GameInputCreate hook installation failed result:{}", result);
        o_GameInputCreate = nullptr;
    }
}

bool RemoveGameInputHooksLocked()
{
    if (!_state.GameInputCreateHookInstalled || o_GameInputCreate == nullptr)
    {
        _state.GameInputCreateHookInstalled = false;
        o_GameInputCreate = nullptr;
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&o_GameInputCreate), hkGameInputCreate);

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_WARN("GameInputCreate hook removal failed result:{}; retaining trampoline for a safe retry", result);
        return false;
    }

    _state.GameInputCreateHookInstalled = false;
    o_GameInputCreate = nullptr;
    return true;
}

HRESULT WINAPI hkGameInputCreate(void** gameInput)
{
    {
        std::unique_lock lock(_state.Mutex);
        _state.GameInputCreateCallCount++;
    }

    HRESULT result = E_NOTIMPL;

    if (o_GameInputCreate != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_GameInputCreate(gameInput);
    }

    {
        std::unique_lock lock(_state.Mutex);

        _state.GameInputLastCreateResult = result;

        if (SUCCEEDED(result))
        {
            _state.GameInputCreateSucceededCount++;

            if (gameInput != nullptr && *gameInput != nullptr)
                _state.GameInputInterfaceSeen = true;
        }
        else
        {
            _state.GameInputCreateFailedCount++;
        }
    }

    return result;
}

} // namespace OptiInput
