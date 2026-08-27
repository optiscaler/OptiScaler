#pragma once

#include "SysUtils.h"
#include <d3d11.h>
#include <optional>
#include <array>

class GpuTime_Dx11
{
    static constexpr int QUERY_BUFFER_COUNT = 3;

    std::array<ID3D11Query*, QUERY_BUFFER_COUNT> _disjointQueries {};
    std::array<ID3D11Query*, QUERY_BUFFER_COUNT> _startQueries {};
    std::array<ID3D11Query*, QUERY_BUFFER_COUNT> _endQueries {};
    std::array<bool, QUERY_BUFFER_COUNT> _trigger {};

    int _currentFrameIndex = 0;
    bool _init = false;

  public:
    GpuTime_Dx11(ID3D11Device* device);
    ~GpuTime_Dx11();

    void Start(ID3D11DeviceContext* context);
    void End(ID3D11DeviceContext* context);

    // Call this once per frame. It will return the elapsed time from (QUERY_BUFFER_COUNT - 1) frames ago.
    std::optional<double> ReadGpuTime(ID3D11DeviceContext* context);
};

class ScopedGpuTime_Dx11
{
    GpuTime_Dx11* _gpuTime;
    ID3D11DeviceContext* _context;

  public:
    ScopedGpuTime_Dx11(GpuTime_Dx11* gpuTime, ID3D11DeviceContext* context) : _gpuTime(gpuTime), _context(context)
    {
        if (_gpuTime && _context)
            _gpuTime->Start(_context);
    }

    ~ScopedGpuTime_Dx11()
    {
        if (_gpuTime && _context)
            _gpuTime->End(_context);
    }
};