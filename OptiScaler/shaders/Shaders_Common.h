#pragma once

#include <d3dx/d3dx12.h>
#include <vector>
#include <stdexcept>

class FrameDescriptorHeap
{
    static inline CD3DX12_CPU_DESCRIPTOR_HANDLE getEmpty()
    {
        LOG_ERROR("Trying to get a handle outside the range");
        static CD3DX12_CPU_DESCRIPTOR_HANDLE empty {};
        return empty;
    }

  public:
    ID3D12DescriptorHeap* Heap = nullptr;
    UINT descriptorSize = 0;

    UINT totalDescriptors = 0;
    UINT srvOffset = 0;
    UINT uavOffset = 0;
    UINT cbvOffset = 0;

    // Initialize the heap based on counts
    bool Initialize(ID3D12Device* device, UINT numSrv, UINT numUav, UINT numCbv)
    {
        totalDescriptors = numSrv + numUav + numCbv;
        descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        srvOffset = 0;
        uavOffset = numSrv;
        cbvOffset = numSrv + numUav;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = totalDescriptors;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Heap))))
            return false;

        return true;
    }

    // Get CPU Handle by specific index (e.g., SRV[0], SRV[1])
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetSrvCPU(UINT index)
    {
        if (srvOffset + index >= uavOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(srvOffset + index, descriptorSize);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetUavCPU(UINT index)
    {
        if (uavOffset + index >= cbvOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(uavOffset + index, descriptorSize);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCbvCPU(UINT index)
    {
        if (cbvOffset + index >= totalDescriptors)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(cbvOffset + index, descriptorSize);
        return handle;
    }

    // Get the GPU handle for the ENTIRE table (starts at SRV 0)
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetTableGPUStart()
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(Heap->GetGPUDescriptorHandleForHeapStart());
    }

    ~FrameDescriptorHeap()
    {
        if (Heap)
            Heap->Release();
    }
};