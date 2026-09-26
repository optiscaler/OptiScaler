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

    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = hiddenWindow;

    o_RegisterRawInputDevices(rid, 1, sizeof(rid[0]));

    MSG msg;

    while (isRunning && GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyWindow(hiddenWindow);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
}

LRESULT RawInputHook::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
                    HRAWINPUT fakeHandle = (HRAWINPUT) instance->nextFakeHandle.fetch_add(1);

                    {
                        std::lock_guard<std::mutex> lock(instance->dataMutex);
                        instance->fakeMouseDataMap[fakeHandle] = raw->data.mouse;

                        while (instance->fakeMouseDataMap.size() > 64)
                        {
                            instance->fakeMouseDataMap.erase(instance->fakeMouseDataMap.begin());
                        }

                        InputCollection::getInstance().addNewDelta({ raw->data.mouse.lLastX, raw->data.mouse.lLastY });
                    }

                    // Only post if we have a valid target window
                    if (instance->gameWindow != NULL)
                    {
                        PostMessage(instance->gameWindow, WM_INPUT, RIM_INPUT, (LPARAM) fakeHandle);
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
        if (!(pRawInputDevices[i].usUsagePage == 0x01 && pRawInputDevices[i].usUsage == 0x02))
        {
            nonMouseCount++;
        }
        else if (pRawInputDevices[i].usUsage == 0x02 && pRawInputDevices[i].hwndTarget)
        {
            instance->gameWindow = pRawInputDevices[i].hwndTarget;
        }
    }

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

    {
        std::lock_guard<std::mutex> lock(instance->dataMutex);
        auto it = instance->fakeMouseDataMap.find(hRawInput);
        if (it != instance->fakeMouseDataMap.end())
        {
            isSpoofed = true;
            mouseData = it->second;
            // Notice: We NO LONGER erase the data here. The game can read it as many times as it needs.
        }
    }

    if (isSpoofed)
    {
        UINT requiredSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);

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
