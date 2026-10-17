#pragma once
#include <windows.h>
#include <detours/detours.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <iostream>
#include <map>
#include <vector>

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
    std::map<HRAWINPUT, RAWMOUSE> fakeMouseDataMap; // ordered, the oldest ones get dropped
    std::atomic<uint64_t> nextFakeHandle { 0x10000 };

    // The newest posted event the game hasn't read yet. Mouse movement is added to it instead of posting more,
    // otherwise a stalled game finds the events it reads afterwards already dropped and stops reading the mouse.
    // Movement isn't added to one left unread for too long, the game might have dropped it.
    HRAWINPUT unreadHandle = nullptr;
    ULONGLONG unreadPostedMs = 0;

    HWND gameWindow = nullptr;

    // The game's own mouse registration. Raw input allows one target per device in a process, so the hidden window
    // takes its place and has to carry its flags, otherwise legacy mouse messages come back and the cursor shows up.
    std::atomic<bool> gameMouseRegistered = false;
    std::atomic<DWORD> gameMouseFlags = 0;

    static constexpr UINT WM_UPDATE_REGISTRATION = WM_APP + 1;

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

        ReadGameRegistration();

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

        RestoreGameRegistration();
    }

  private:
    HWND FindGameWindow();
    void PollingThreadLoop();
    void ReadGameRegistration();
    void RegisterHiddenWindow();
    void RestoreGameRegistration();
    bool ShouldDeliverToGame(bool& foreground);

    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    static BOOL WINAPI hkRegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
    static UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize,
                                         UINT cbSizeHeader);
};
