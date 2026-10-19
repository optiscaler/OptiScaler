#pragma once

#include "hooks/Amdxc64_Hooks.h"

struct AmdExtFfxApi : public IAmdExtFfxApi
{
    FSR4Support realFsr4Support {};

    PFN_UpdateFfxApiProvider o_UpdateFfxApiProvider = nullptr;
    PFN_UpdateFfxApiProviderEx o_UpdateFfxApiProviderEx = nullptr;

    HRESULT STDMETHODCALLTYPE UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};

struct AmdExtD3DShaderIntrinsics : public IAmdExtD3DShaderIntrinsics
{
    HRESULT STDMETHODCALLTYPE GetInfo(void* ShaderIntrinsicsInfo) override;
    HRESULT STDMETHODCALLTYPE CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic) override;
    HRESULT STDMETHODCALLTYPE Enable() override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};

#define STUB(number)                                                                                                   \
    HRESULT STDMETHODCALLTYPE unknown##number()                                                                        \
    {                                                                                                                  \
        LOG_FUNC();                                                                                                    \
        return S_OK;                                                                                                   \
    }

struct AmdExtD3DDevice8 : public IAmdExtD3DDevice8
{
    FSR4Support realFsr4Support {};
    FSR4Support fsr4Support {};

    STUB(1)
    STUB(2)
    STUB(3)
    STUB(4)
    STUB(5)
    STUB(6)
    STUB(7)
    STUB(8)
    STUB(9)
    STUB(10)
    STUB(11)
    STUB(12)
    STUB(13)
    HRESULT STDMETHODCALLTYPE GetWaveMatrixProperties(uint64_t* count,
                                                      AmdExtWaveMatrixProperties* waveMatrixProperties) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};
