#pragma once
#include <windows.h>
#include <detours/detours.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <iostream>
#include <map>

// Typedefs for the original Windows APIs we are hooking
typedef decltype(&GetRawInputData) PFN_GetRawInputData;
typedef decltype(&RegisterRawInputDevices) PFN_RegisterRawInputDevices;

class RawInputHook
{
  private:
    std::thread pollThread;
    std::atomic<bool> isRunning = false;
    HWND hiddenWindow = nullptr;

    // Thread-safe storage for discrete spoofed mouse events
    std::mutex dataMutex;
    std::map<HRAWINPUT, RAWMOUSE> fakeMouseDataMap; // Now an ordered map
    std::atomic<uint64_t> nextFakeHandle { 0x10000 };

    HWND gameWindow = nullptr;

    static inline PFN_GetRawInputData o_GetRawInputData = GetRawInputData;
    static inline PFN_RegisterRawInputDevices o_RegisterRawInputDevices = RegisterRawInputDevices;

    // Singleton instance for the static hooks to access
    static inline RawInputHook* instance = nullptr;

  public:
    static RawInputHook& getInstance()
    {
        if (!instance)
            instance = new RawInputHook();

        return *instance;
    }

    void start()
    {
        if (isRunning)
            return;
        isRunning = true;

        // Start Detours hooking
        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_GetRawInputData, hkGetRawInputData);
        DetourAttach(&(PVOID&) o_RegisterRawInputDevices, hkRegisterRawInputDevices);

        DetourTransactionCommit();

        // Launch the background polling/message thread
        pollThread = std::thread(&RawInputHook::PollingThreadLoop, this);
    }

    void stop()
    {
        if (!isRunning)
            return;
        isRunning = false;

        if (hiddenWindow)
        {
            PostMessage(hiddenWindow, WM_CLOSE, 0, 0);
        }

        if (pollThread.joinable())
        {
            pollThread.join();
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&) o_GetRawInputData, hkGetRawInputData);
        DetourDetach(&(PVOID&) o_RegisterRawInputDevices, hkRegisterRawInputDevices);

        DetourTransactionCommit();
    }

  private:
    HWND FindGameWindow();
    void PollingThreadLoop();

    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    static BOOL WINAPI hkRegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
    static UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize,
                                         UINT cbSizeHeader);
};
