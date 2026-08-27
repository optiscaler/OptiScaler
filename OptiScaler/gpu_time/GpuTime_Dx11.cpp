#include "pch.h"
#include "GpuTime_Dx11.h"
#include <State.h>

GpuTime_Dx11::GpuTime_Dx11(ID3D11Device* device)
{
    if (!device)
        return;

    D3D11_QUERY_DESC disjointDesc = {};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

    D3D11_QUERY_DESC timestampDesc = {};
    timestampDesc.Query = D3D11_QUERY_TIMESTAMP;

    for (int i = 0; i < QUERY_BUFFER_COUNT; i++)
    {
        if (FAILED(device->CreateQuery(&disjointDesc, &_disjointQueries[i])))
            LOG_ERROR("CreateQuery DISJOINT error at index {}", i);

        if (FAILED(device->CreateQuery(&timestampDesc, &_startQueries[i])))
            LOG_ERROR("CreateQuery START timestamp error at index {}", i);

        if (FAILED(device->CreateQuery(&timestampDesc, &_endQueries[i])))
            LOG_ERROR("CreateQuery END timestamp error at index {}", i);

        _trigger[i] = false;
    }

    _init = true;
}

GpuTime_Dx11::~GpuTime_Dx11()
{
    for (int i = 0; i < QUERY_BUFFER_COUNT; i++)
    {
        SAFE_RELEASE(_disjointQueries[i]);
        SAFE_RELEASE(_startQueries[i]);
        SAFE_RELEASE(_endQueries[i]);
    }
}

void GpuTime_Dx11::Start(ID3D11DeviceContext* context)
{
    if (_init && context)
    {
        // Record the queries in the current frame
        context->Begin(_disjointQueries[_currentFrameIndex]);
        context->End(_startQueries[_currentFrameIndex]);
    }
}

void GpuTime_Dx11::End(ID3D11DeviceContext* context)
{
    if (_init && context)
    {
        context->End(_endQueries[_currentFrameIndex]);
        context->End(_disjointQueries[_currentFrameIndex]);

        _trigger[_currentFrameIndex] = true;
    }
}

std::optional<double> GpuTime_Dx11::ReadGpuTime(ID3D11DeviceContext* context)
{
    std::optional<double> elapsedTimeMs = std::nullopt;

    if (!_init || !context)
        return elapsedTimeMs;

    // Read the oldest query in the ring buffer to ensure the GPU is finished with it
    int readIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

    if (_trigger[readIndex])
    {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;

        if (context->GetData(_disjointQueries[readIndex], &disjointData, sizeof(disjointData), 0) == S_OK)
        {
            if (!disjointData.Disjoint && disjointData.Frequency > 0)
            {
                UINT64 startTime = 0;
                UINT64 endTime = 0;

                if (context->GetData(_startQueries[readIndex], &startTime, sizeof(UINT64), 0) == S_OK &&
                    context->GetData(_endQueries[readIndex], &endTime, sizeof(UINT64), 0) == S_OK)
                {
                    if (endTime >= startTime)
                    {
                        elapsedTimeMs = (endTime - startTime) / static_cast<double>(disjointData.Frequency) * 1000.0;
                    }
                }
            }
        }

        _trigger[readIndex] = false;
    }

    _currentFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

    return elapsedTimeMs;
}