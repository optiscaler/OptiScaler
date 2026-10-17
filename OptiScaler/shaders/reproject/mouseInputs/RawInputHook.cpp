#include "pch.h"
#include "RawInputHook.h"
#include "InputCollection.h"

HWND RawInputHook::FindGameWindow()
{
    DWORD foregroundPid = 0;
    HWND fgWnd = GetForegroundWindow();
    GetWindowThreadProcessId(fgWnd, &foregroundPid);
    if (foregroundPid == GetCurrentProcessId())
    {
        return fgWnd;
    }

    struct EnumData
    {
        DWORD pid;
        HWND hwnd;
    } data = { GetCurrentProcessId(), NULL };

    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL
        {
            EnumData* pData = reinterpret_cast<EnumData*>(lParam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);

            // Find the top-level main window (no owner, visible)
            if (pid == pData->pid && GetWindow(hWnd, GW_OWNER) == NULL && IsWindowVisible(hWnd))
            {
                pData->hwnd = hWnd;
                return FALSE; // Stop enumeration
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));

    return data.hwnd;
}

void RawInputHook::PollingThreadLoop()
{
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"HiddenRawInputWnd";

    RegisterClassEx(&wc);

    hiddenWindow =
        CreateWindowEx(0, wc.lpszClassName, L"RawInputPoller", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);

    RegisterHiddenWindow();

    MSG msg;

    while (isRunning && GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyWindow(hiddenWindow);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
}

void RawInputHook::ReadGameRegistration()
{
    UINT count = 0;
    GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));

    std::vector<RAWINPUTDEVICE> devices(count);
    if (count == 0 || GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == (UINT) -1)
        return;

    for (const auto& device : devices)
    {
        if (device.usUsagePage == 0x01 && device.usUsage == 0x02)
        {
            gameMouseRegistered = true;
            gameMouseFlags = device.dwFlags;

            if (device.hwndTarget)
                gameWindow = device.hwndTarget;
        }
    }
}

// Must run on the hidden window's thread
void RawInputHook::RegisterHiddenWindow()
{
    DWORD flags = RIDEV_INPUTSINK;
    if (gameMouseRegistered)
        flags |= gameMouseFlags & (RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE);

    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = flags;
    rid[0].hwndTarget = hiddenWindow;

    if (!o_RegisterRawInputDevices(rid, 1, sizeof(rid[0])))
        LOG_WARN("RegisterRawInputDevices failed flags: {:#x} error: {}", flags, GetLastError());
}

void RawInputHook::RestoreGameRegistration()
{
    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;

    if (gameMouseRegistered)
    {
        rid[0].dwFlags = gameMouseFlags;
        rid[0].hwndTarget = gameWindow;
    }
    else
    {
        rid[0].dwFlags = RIDEV_REMOVE;
        rid[0].hwndTarget = nullptr;
    }

    o_RegisterRawInputDevices(rid, 1, sizeof(rid[0]));
}

// The game only gets WM_INPUT the way its own registration would have given it
bool RawInputHook::ShouldDeliverToGame(bool& foreground)
{
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    foreground = foregroundPid == GetCurrentProcessId();

    if (!gameMouseRegistered || gameWindow == NULL)
        return false;

    return foreground || (gameMouseFlags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) != 0;
}

LRESULT RawInputHook::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_UPDATE_REGISTRATION && instance)
    {
        instance->RegisterHiddenWindow();
        return 0;
    }

    if (msg == WM_INPUT && instance)
    {
        // Resolve the game window if it hasn't been set yet
        if (instance->gameWindow == NULL)
        {
            instance->gameWindow = instance->FindGameWindow();
        }

        UINT dwSize = 0;
        o_GetRawInputData((HRAWINPUT) lp, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

        if (dwSize > 0)
        {
            LPBYTE lpb = new BYTE[dwSize];
            if (o_GetRawInputData((HRAWINPUT) lp, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize)
            {
                RAWINPUT* raw = (RAWINPUT*) lpb;
                if (raw->header.dwType == RIM_TYPEMOUSE)
                {
                    const RAWMOUSE& mouse = raw->data.mouse;
                    HRAWINPUT fakeHandle = nullptr;
                    bool foreground = false;
                    const bool deliver = instance->ShouldDeliverToGame(foreground);

                    {
                        std::lock_guard<std::mutex> lock(instance->dataMutex);
                        auto& events = instance->fakeMouseDataMap;

                        // Only plain relative movement can be added up, buttons and the wheel keep their own event
                        auto unread = events.find(instance->unreadHandle);
                        const bool merge = unread != events.end() && unread->second.usFlags == mouse.usFlags &&
                                           unread->second.usButtonFlags == 0 && mouse.usButtonFlags == 0 &&
                                           mouse.usFlags == MOUSE_MOVE_RELATIVE &&
                                           GetTickCount64() - instance->unreadPostedMs < 100;

                        if (!deliver)
                        {
                            instance->unreadHandle = nullptr;
                        }
                        else if (merge)
                        {
                            unread->second.lLastX += mouse.lLastX;
                            unread->second.lLastY += mouse.lLastY;
                        }
                        else
                        {
                            fakeHandle = (HRAWINPUT) instance->nextFakeHandle.fetch_add(1);
                            events[fakeHandle] = mouse;
                            instance->unreadHandle = fakeHandle;
                            instance->unreadPostedMs = GetTickCount64();

                            while (events.size() > 64)
                                events.erase(events.begin());
                        }

                        InputCollection::getInstance().addNewDelta({ mouse.lLastX, mouse.lLastY });
                    }

                    if (fakeHandle != nullptr)
                    {
                        PostMessage(instance->gameWindow, WM_INPUT, foreground ? RIM_INPUT : RIM_INPUTSINK,
                                    (LPARAM) fakeHandle);
                    }
                }
            }
            delete[] lpb;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

BOOL RawInputHook::hkRegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize)
{
    if (uiNumDevices == 0 || pRawInputDevices == nullptr)
    {
        return o_RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
    }

    UINT nonMouseCount = 0;
    for (UINT i = 0; i < uiNumDevices; ++i)
    {
        const RAWINPUTDEVICE& device = pRawInputDevices[i];

        if (!(device.usUsagePage == 0x01 && device.usUsage == 0x02))
        {
            nonMouseCount++;
            continue;
        }

        // Games commonly drop the mouse when losing focus and take it back with NOLEGACY when regaining it
        instance->gameMouseRegistered = (device.dwFlags & RIDEV_REMOVE) == 0;
        instance->gameMouseFlags = (device.dwFlags & RIDEV_REMOVE) ? 0 : device.dwFlags;

        if (device.hwndTarget)
            instance->gameWindow = device.hwndTarget;
    }

    if (nonMouseCount != uiNumDevices && instance->hiddenWindow)
        PostMessage(instance->hiddenWindow, WM_UPDATE_REGISTRATION, 0, 0);

    if (nonMouseCount == 0)
    {
        return TRUE;
    }

    if (nonMouseCount == uiNumDevices)
    {
        return o_RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
    }

    RAWINPUTDEVICE* filteredDevices = new RAWINPUTDEVICE[nonMouseCount];
    UINT currentIndex = 0;

    for (UINT i = 0; i < uiNumDevices; ++i)
    {
        if (!(pRawInputDevices[i].usUsagePage == 0x01 && pRawInputDevices[i].usUsage == 0x02))
        {
            filteredDevices[currentIndex] = pRawInputDevices[i];
            currentIndex++;
        }
    }

    BOOL result = o_RegisterRawInputDevices(filteredDevices, nonMouseCount, cbSize);
    delete[] filteredDevices;

    return result;
}

UINT RawInputHook::hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize,
                                     UINT cbSizeHeader)
{
    if (!instance)
        return o_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    bool isSpoofed = false;
    RAWMOUSE mouseData;
    const UINT requiredSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);

    {
        std::lock_guard<std::mutex> lock(instance->dataMutex);
        auto it = instance->fakeMouseDataMap.find(hRawInput);
        if (it != instance->fakeMouseDataMap.end())
        {
            isSpoofed = true;
            mouseData = it->second;

            // Kept, the game can read it as many times as it needs, but once it got the data nothing more can be
            // added to it
            if (uiCommand == RID_INPUT && pData != nullptr && pcbSize != nullptr && *pcbSize >= requiredSize &&
                hRawInput == instance->unreadHandle)
            {
                instance->unreadHandle = nullptr;
            }
        }
    }

    if (isSpoofed)
    {

        // Handle header-only requests explicitly
        if (uiCommand == RID_HEADER)
        {
            if (pData == nullptr)
            {
                *pcbSize = sizeof(RAWINPUTHEADER);
                return 0;
            }
            if (*pcbSize < sizeof(RAWINPUTHEADER))
            {
                *pcbSize = sizeof(RAWINPUTHEADER);
                return (UINT) -1;
            }

            RAWINPUTHEADER* header = (RAWINPUTHEADER*) pData;
            header->dwType = RIM_TYPEMOUSE;
            header->dwSize = requiredSize;
            header->hDevice = NULL;
            header->wParam = RIM_INPUT;

            return sizeof(RAWINPUTHEADER);
        }

        // Handle full data requests
        if (uiCommand == RID_INPUT)
        {
            if (pData == nullptr)
            {
                *pcbSize = requiredSize;
                return 0;
            }

            if (*pcbSize < requiredSize)
            {
                *pcbSize = requiredSize;
                return (UINT) -1;
            }

            RAWINPUT* spoofedInput = (RAWINPUT*) pData;

            spoofedInput->header.dwType = RIM_TYPEMOUSE;
            spoofedInput->header.dwSize = requiredSize;
            spoofedInput->header.hDevice = NULL;
            spoofedInput->header.wParam = RIM_INPUT;

            spoofedInput->data.mouse = mouseData;

            return requiredSize;
        }

        return (UINT) -1;
    }

    return o_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}
