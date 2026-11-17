#pragma once

#include "SysUtils.h"

#include <State.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <detours/detours.h>

#include <d3d12.h>

class D3d12Proxy
{
  public:
    typedef decltype(&D3D12CreateDevice) PFN_D3D12CreateDevice;
    typedef decltype(&D3D12SerializeRootSignature) PFN_D3D12SerializeRootSignature;
    typedef decltype(&D3D12CreateRootSignatureDeserializer) PFN_D3D12CreateRootSignatureDeserializer;
    typedef decltype(&D3D12SerializeVersionedRootSignature) PFN_D3D12SerializeVersionedRootSignature;
    typedef decltype(&D3D12CreateVersionedRootSignatureDeserializer) PFN_D3D12CreateVersionedRootSignatureDeserializer;
    typedef decltype(&D3D12GetDebugInterface) PFN_D3D12GetDebugInterface;
    typedef decltype(&D3D12EnableExperimentalFeatures) PFN_D3D12EnableExperimentalFeatures;
    typedef decltype(&D3D12GetInterface) PFN_D3D12GetInterface;

    static void Init(HMODULE module = nullptr)
    {
        if (_dll != nullptr)
            return;

        if (module == nullptr)
        {
            _dll = KernelBaseProxy::GetModuleHandleW_()(L"d3d12.dll");

            // Doing this in games with Agility SDK before they are set up will crash
            if (_dll == nullptr)
                _dll = NtdllProxy::LoadLibraryExW_Ldr(L"d3d12.dll", NULL, 0);
        }
        else
        {
            _dll = module;
        }

        if (_dll == nullptr)
            return;

        _D3D12CreateDevice = (PFN_D3D12CreateDevice) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12CreateDevice");
        _D3D12SerializeRootSignature =
            (PFN_D3D12SerializeRootSignature) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12SerializeRootSignature");
        _D3D12CreateRootSignatureDeserializer =
            (PFN_D3D12CreateRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
                _dll, "D3D12CreateRootSignatureDeserializer");
        _D3D12SerializeVersionedRootSignature =
            (PFN_D3D12SerializeVersionedRootSignature) KernelBaseProxy::GetProcAddress_()(
                _dll, "D3D12SerializeVersionedRootSignature");
        _D3D12CreateVersionedRootSignatureDeserializer =
            (PFN_D3D12CreateVersionedRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
                _dll, "D3D12CreateVersionedRootSignatureDeserializer");
        _D3D12GetDebugInterface =
            (PFN_D3D12GetDebugInterface) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12GetDebugInterface");
        _D3D12EnableExperimentalFeatures = (PFN_D3D12EnableExperimentalFeatures) KernelBaseProxy::GetProcAddress_()(
            _dll, "D3D12EnableExperimentalFeatures");
        _D3D12GetInterface = (PFN_D3D12GetInterface) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12GetInterface");
    }

    static HMODULE Module() { return _dll; }

    // Original methods
    static PFN_D3D12CreateDevice D3D12CreateDevice_() { return _D3D12CreateDevice; }
    static PFN_D3D12SerializeRootSignature D3D12SerializeRootSignature_() { return _D3D12SerializeRootSignature; }
    static PFN_D3D12CreateRootSignatureDeserializer D3D12CreateRootSignatureDeserializer_()
    {
        return _D3D12CreateRootSignatureDeserializer;
    }
    static PFN_D3D12SerializeVersionedRootSignature D3D12SerializeVersionedRootSignature_()
    {
        return _D3D12SerializeVersionedRootSignature;
    }
    static PFN_D3D12CreateVersionedRootSignatureDeserializer D3D12CreateVersionedRootSignatureDeserializer_()
    {
        return _D3D12CreateVersionedRootSignatureDeserializer;
    }
    static PFN_D3D12GetDebugInterface D3D12GetDebugInterface_() { return _D3D12GetDebugInterface; }
    static PFN_D3D12EnableExperimentalFeatures D3D12EnableExperimentalFeatures_()
    {
        return _D3D12EnableExperimentalFeatures;
    }
    static PFN_D3D12GetInterface D3D12GetInterface_() { return _D3D12GetInterface; }

    // Hooked methods
    static PFN_D3D12CreateDevice D3D12CreateDevice_Hooked()
    {
        return (PFN_D3D12CreateDevice) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12CreateDevice");
    }
    static PFN_D3D12SerializeRootSignature D3D12SerializeRootSignature_Hooked()
    {
        return (PFN_D3D12SerializeRootSignature) KernelBaseProxy::GetProcAddress_()(_dll,
                                                                                    "D3D12SerializeRootSignature");
    }
    static PFN_D3D12CreateRootSignatureDeserializer D3D12CreateRootSignatureDeserializer_Hooked()
    {
        return (PFN_D3D12CreateRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
            _dll, "D3D12CreateRootSignatureDeserializer");
    }
    static PFN_D3D12SerializeVersionedRootSignature D3D12SerializeVersionedRootSignature_Hooked()
    {
        return (PFN_D3D12SerializeVersionedRootSignature) KernelBaseProxy::GetProcAddress_()(
            _dll, "D3D12SerializeVersionedRootSignature");
    }
    static PFN_D3D12CreateVersionedRootSignatureDeserializer D3D12CreateVersionedRootSignatureDeserializer_Hooked()
    {
        return (PFN_D3D12CreateVersionedRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
            _dll, "D3D12CreateVersionedRootSignatureDeserializer");
    }
    static PFN_D3D12GetDebugInterface D3D12GetDebugInterface_Hooked()
    {
        return (PFN_D3D12GetDebugInterface) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12GetDebugInterface");
    }
    static PFN_D3D12EnableExperimentalFeatures D3D12EnableExperimentalFeatures_Hooked()
    {
        return (PFN_D3D12EnableExperimentalFeatures) KernelBaseProxy::GetProcAddress_()(
            _dll, "D3D12EnableExperimentalFeatures");
    }
    static PFN_D3D12GetInterface D3D12GetInterface_Hooked()
    {
        return (PFN_D3D12GetInterface) KernelBaseProxy::GetProcAddress_()(_dll, "D3D12GetInterface");
    }

    // Hook methods
    static PFN_D3D12CreateDevice Hook_D3D12CreateDevice(PVOID method)
    {
        auto addr = D3D12CreateDevice_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12CreateDevice = addr;

        return addr;
    }

    static PFN_D3D12SerializeRootSignature Hook_D3D12SerializeRootSignature(PVOID method)
    {
        auto addr = D3D12SerializeRootSignature_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12SerializeRootSignature = addr;

        return addr;
    }

    static PFN_D3D12CreateRootSignatureDeserializer Hook_D3D12CreateRootSignatureDeserializer(PVOID method)
    {
        auto addr = D3D12CreateRootSignatureDeserializer_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12CreateRootSignatureDeserializer = addr;

        return addr;
    }

    static PFN_D3D12SerializeVersionedRootSignature Hook_D3D12SerializeVersionedRootSignature(PVOID method)
    {
        auto addr = D3D12SerializeVersionedRootSignature_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12SerializeVersionedRootSignature = addr;

        return addr;
    }

    static PFN_D3D12CreateVersionedRootSignatureDeserializer
    Hook_D3D12CreateVersionedRootSignatureDeserializer(PVOID method)
    {
        auto addr = D3D12CreateVersionedRootSignatureDeserializer_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12CreateVersionedRootSignatureDeserializer = addr;

        return addr;
    }

    static PFN_D3D12GetDebugInterface Hook_D3D12GetDebugInterface(PVOID method)
    {
        auto addr = D3D12GetDebugInterface_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12GetDebugInterface = addr;

        return addr;
    }

    static PFN_D3D12EnableExperimentalFeatures Hook_D3D12EnableExperimentalFeatures(PVOID method)
    {
        auto addr = D3D12EnableExperimentalFeatures_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12EnableExperimentalFeatures = addr;

        return addr;
    }

    static PFN_D3D12GetInterface Hook_D3D12GetInterface(PVOID method)
    {
        auto addr = D3D12GetInterface_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        // As the proxy dll we hooked our own export, its trampoline would call back into the proxy
        if (HookModule() == _dll)
            _D3D12GetInterface = addr;

        return addr;
    }

  private:
    inline static HMODULE _dll = nullptr;

    inline static PFN_D3D12CreateDevice _D3D12CreateDevice = nullptr;
    inline static PFN_D3D12SerializeRootSignature _D3D12SerializeRootSignature = nullptr;
    inline static PFN_D3D12CreateRootSignatureDeserializer _D3D12CreateRootSignatureDeserializer = nullptr;
    inline static PFN_D3D12SerializeVersionedRootSignature _D3D12SerializeVersionedRootSignature = nullptr;
    inline static PFN_D3D12CreateVersionedRootSignatureDeserializer _D3D12CreateVersionedRootSignatureDeserializer =
        nullptr;
    inline static PFN_D3D12GetDebugInterface _D3D12GetDebugInterface = nullptr;
    inline static PFN_D3D12EnableExperimentalFeatures _D3D12EnableExperimentalFeatures = nullptr;
    inline static PFN_D3D12GetInterface _D3D12GetInterface = nullptr;

    inline static HMODULE HookModule()
    {
        if (State::Instance().workingMode == WorkingMode::D3d12)
            return dllModule;

        return _dll;
    }

    static PFN_D3D12CreateDevice D3D12CreateDevice_ForHook()
    {
        return (PFN_D3D12CreateDevice) KernelBaseProxy::GetProcAddress_()(HookModule(), "D3D12CreateDevice");
    }
    static PFN_D3D12SerializeRootSignature D3D12SerializeRootSignature_ForHook()
    {
        return (PFN_D3D12SerializeRootSignature) KernelBaseProxy::GetProcAddress_()(HookModule(),
                                                                                    "D3D12SerializeRootSignature");
    }
    static PFN_D3D12CreateRootSignatureDeserializer D3D12CreateRootSignatureDeserializer_ForHook()
    {
        return (PFN_D3D12CreateRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
            HookModule(), "D3D12CreateRootSignatureDeserializer");
    }
    static PFN_D3D12SerializeVersionedRootSignature D3D12SerializeVersionedRootSignature_ForHook()
    {
        return (PFN_D3D12SerializeVersionedRootSignature) KernelBaseProxy::GetProcAddress_()(
            HookModule(), "D3D12SerializeVersionedRootSignature");
    }
    static PFN_D3D12CreateVersionedRootSignatureDeserializer D3D12CreateVersionedRootSignatureDeserializer_ForHook()
    {
        return (PFN_D3D12CreateVersionedRootSignatureDeserializer) KernelBaseProxy::GetProcAddress_()(
            HookModule(), "D3D12CreateVersionedRootSignatureDeserializer");
    }
    static PFN_D3D12GetDebugInterface D3D12GetDebugInterface_ForHook()
    {
        return (PFN_D3D12GetDebugInterface) KernelBaseProxy::GetProcAddress_()(HookModule(), "D3D12GetDebugInterface");
    }
    static PFN_D3D12EnableExperimentalFeatures D3D12EnableExperimentalFeatures_ForHook()
    {
        return (PFN_D3D12EnableExperimentalFeatures) KernelBaseProxy::GetProcAddress_()(
            HookModule(), "D3D12EnableExperimentalFeatures");
    }
    static PFN_D3D12GetInterface D3D12GetInterface_ForHook()
    {
        return (PFN_D3D12GetInterface) KernelBaseProxy::GetProcAddress_()(HookModule(), "D3D12GetInterface");
    }
};
