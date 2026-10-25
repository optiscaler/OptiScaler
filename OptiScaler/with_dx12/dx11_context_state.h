#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace Dx11Bridge
{
// Captures the immediate-context state the DX11->DX12 bridge evaluation path
// disturbs and restores it on scope exit (or explicit, idempotent Restore()).
class ScopedContextState
{
  public:
    explicit ScopedContextState(ID3D11DeviceContext* context) : _context(context)
    {
        if (_context == nullptr)
            return;

        ComPtr<ID3D11Device> device;
        _context->GetDevice(device.GetAddressOf());

        const D3D11_FEATURE_LEVEL fl = device != nullptr
            ? (D3D11_FEATURE_LEVEL) device->GetFeatureLevel()
            : (D3D11_FEATURE_LEVEL) D3D_FEATURE_LEVEL_11_0;

        // D3D11_1_UAV_SLOT_COUNT (64) is only addressable on FL11_1+ devices.
        _uavSlotCount = fl >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : 8;

        _context->QueryInterface(IID_PPV_ARGS(&_context1));

        ComPtr<ID3D11ClassInstance> classInstances[D3D11_SHADER_MAX_INTERFACES] = {};
        UINT classInstanceCount = 0;
        ID3D11ClassInstance* rawClassInstances[D3D11_SHADER_MAX_INTERFACES] = {};
        _context->CSGetShader(_shader.ReleaseAndGetAddressOf(), rawClassInstances, &classInstanceCount);

        if (classInstanceCount > D3D11_SHADER_MAX_INTERFACES)
            classInstanceCount = D3D11_SHADER_MAX_INTERFACES;

        _classInstanceCount = classInstanceCount;

        for (UINT i = 0; i < classInstanceCount; i++)
            _classInstances[i].Attach(rawClassInstances[i]);

        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
            _context->CSGetShaderResources(i, 1, _srvs[i].GetAddressOf());

        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
            _context->CSGetSamplers(i, 1, _samplers[i].GetAddressOf());

        if (_context1 != nullptr)
        {
            ID3D11Buffer* rawCbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            _context1->CSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, rawCbs,
                                             _cbOffsets, _cbCounts);

            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                _cbs[i].Attach(rawCbs[i]);

            _hasCbRanges = true;
        }
        else
        {
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                _context->CSGetConstantBuffers(i, 1, _cbs[i].GetAddressOf());
        }

        for (UINT i = 0; i < _uavSlotCount; i++)
            _context->CSGetUnorderedAccessViews(i, 1, _uavs[i].GetAddressOf());

        ID3D11RenderTargetView* rawRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        _context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRtvs, _dsv.GetAddressOf());

        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            _rtvs[i].Attach(rawRtvs[i]);

        // Unbind render targets for the DX12 work below.
        ID3D11RenderTargetView* nullRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        _context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRtvs, nullptr);
    }

    ~ScopedContextState()
    {
        Restore();
    }

    void Restore()
    {
        if (_restored || _context == nullptr)
            return;

        _restored = true;

        ID3D11ClassInstance* rawClassInstances[D3D11_SHADER_MAX_INTERFACES] = {};
        for (UINT i = 0; i < _classInstanceCount; i++)
            rawClassInstances[i] = _classInstances[i].Get();

        _context->CSSetShader(_shader.Get(), _classInstanceCount > 0 ? rawClassInstances : nullptr, _classInstanceCount);

        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
        {
            auto raw = _srvs[i].Get();
            _context->CSSetShaderResources(i, 1, &raw);
        }

        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
        {
            auto raw = _samplers[i].Get();
            _context->CSSetSamplers(i, 1, &raw);
        }

        if (_hasCbRanges && _context1 != nullptr)
        {
            ID3D11Buffer* rawCbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                rawCbs[i] = _cbs[i].Get();

            _context1->CSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, rawCbs,
                                             _cbOffsets, _cbCounts);
        }
        else
        {
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            {
                auto raw = _cbs[i].Get();
                _context->CSSetConstantBuffers(i, 1, &raw);
            }
        }

        const UINT keepUavCount = D3D11_KEEP_UNORDERED_ACCESS_VIEW_COUNT;
        for (UINT i = 0; i < _uavSlotCount; i++)
        {
            auto raw = _uavs[i].Get();
            // D3D11_KEEP_UNORDERED_ACCESS_VIEW_COUNT preserves the current append counter.
            _context->CSSetUnorderedAccessViews(i, 1, &raw, &keepUavCount);
        }

        ID3D11RenderTargetView* rawRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            rawRtvs[i] = _rtvs[i].Get();

        _context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRtvs, _dsv.Get());
    }

    ScopedContextState(const ScopedContextState&) = delete;
    ScopedContextState& operator=(const ScopedContextState&) = delete;

  private:
    ID3D11DeviceContext* _context = nullptr;
    ComPtr<ID3D11DeviceContext1> _context1;

    ComPtr<ID3D11ComputeShader> _shader;
    ComPtr<ID3D11ClassInstance> _classInstances[D3D11_SHADER_MAX_INTERFACES];
    UINT _classInstanceCount = 0;

    ComPtr<ID3D11ShaderResourceView> _srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ComPtr<ID3D11SamplerState> _samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    ComPtr<ID3D11Buffer> _cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT _cbOffsets[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    UINT _cbCounts[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    bool _hasCbRanges = false;

    ComPtr<ID3D11UnorderedAccessView> _uavs[D3D11_1_UAV_SLOT_COUNT];
    UINT _uavSlotCount = 8;

    ComPtr<ID3D11RenderTargetView> _rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ComPtr<ID3D11DepthStencilView> _dsv;

    bool _restored = false;
};
}
