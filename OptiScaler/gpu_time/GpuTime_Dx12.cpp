#include "pch.h"
#include "GpuTime_Dx12.h"

#include <State.h>

#include <include/d3dx/d3dx12.h>

GpuTime_Dx12::GpuTime_Dx12(ID3D12Device* device)
{
    // Create query heap for timestamp queries
    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Count = 2; // Start and End timestamps
    queryHeapDesc.NodeMask = 0;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    auto result = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&_queryHeap));

    if (result != S_OK)
    {
        LOG_ERROR("CreateQueryHeap error: {:X}", (UINT) result);
        return;
    }

    // Create a readback buffer to retrieve timestamp data
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(2 * sizeof(UINT64));
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    result = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_readbackBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("CreateCommittedResource error: {:X}", (UINT) result);
        return;
    }

    _init = true;
}

GpuTime_Dx12::~GpuTime_Dx12()
{
    SAFE_RELEASE(_queryHeap);
    SAFE_RELEASE(_readbackBuffer);
}

void GpuTime_Dx12::Start(ID3D12GraphicsCommandList* cmdList)
{
    if (_init && _queryHeap != nullptr)
        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void GpuTime_Dx12::End(ID3D12GraphicsCommandList* cmdList)
{
    if (_init && _queryHeap != nullptr)
    {
        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 1);

        // Resolve the queries to the readback buffer
        cmdList->ResolveQueryData(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, _readbackBuffer, 0);

        _trigger = true;
    }
}

std::optional<double> GpuTime_Dx12::ReadGpuTime(ID3D12CommandQueue* commandQueue)
{
    std::optional<double> elapsedTimeMs = std::nullopt;

    if (!_init || _queryHeap == nullptr || !_trigger || _readbackBuffer == nullptr)
        return elapsedTimeMs;

    _trigger = false;

    UINT64* timestampData {};
    _readbackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&timestampData));

    if (timestampData != nullptr)
    {
        // Get the GPU timestamp frequency (ticks per second)
        UINT64 gpuFrequency;
        commandQueue->GetTimestampFrequency(&gpuFrequency);

        // Calculate elapsed time in milliseconds
        UINT64 startTime = timestampData[0];
        UINT64 endTime = timestampData[1];

        if (endTime < startTime)
            return elapsedTimeMs;

        elapsedTimeMs = (endTime - startTime) / static_cast<double>(gpuFrequency) * 1000.0;
    }
    else
    {
        LOG_WARN("timestampData is null!");
    }

    // Unmap the buffer
    _readbackBuffer->Unmap(0, nullptr);

    return elapsedTimeMs;
}
