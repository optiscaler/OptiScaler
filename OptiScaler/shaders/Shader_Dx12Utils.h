#pragma once

#include <pch.h>
#include <d3dx/d3dx12.h>
#include <vector>
#include <stdexcept>

class FrameDescriptorHeap
{
    ID3D12DescriptorHeap* heapCSU = nullptr; // Cbv + Srv + Uav
    ID3D12DescriptorHeap* heapRtv = nullptr;

    UINT descriptorSizeCSU = 0;
    UINT descriptorSizeRtv = 0;

    UINT totalDescriptorsCSU = 0;
    UINT totalDescriptorsRtv = 0;
    UINT srvOffset = 0;
    UINT uavOffset = 0;
    UINT cbvOffset = 0;

    static inline CD3DX12_CPU_DESCRIPTOR_HANDLE getEmpty()
    {
        LOG_ERROR("Trying to get a handle outside the range");
        static CD3DX12_CPU_DESCRIPTOR_HANDLE empty {};
        return empty;
    }

  public:
    // Initialize the heap based on counts
    bool Initialize(ID3D12Device* device, UINT numSrv, UINT numUav, UINT numCbv, UINT numRtv = 0)
    {
        totalDescriptorsCSU = numSrv + numUav + numCbv;
        totalDescriptorsRtv = numRtv;

        if (totalDescriptorsCSU > 0)
        {
            descriptorSizeCSU = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            srvOffset = 0;
            uavOffset = numSrv;
            cbvOffset = numSrv + numUav;

            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.NumDescriptors = totalDescriptorsCSU;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heapCSU))))
                return false;
        }

        if (totalDescriptorsRtv > 0)
        {
            descriptorSizeRtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.NumDescriptors = totalDescriptorsRtv;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heapRtv))))
                return false;
        }

        return true;
    }

    // Get CPU Handle by specific index (e.g., SRV[0], SRV[1])
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetSrvCPU(UINT index)
    {
        if (srvOffset + index >= uavOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(heapCSU->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(srvOffset + index, descriptorSizeCSU);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetUavCPU(UINT index)
    {
        if (uavOffset + index >= cbvOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(heapCSU->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(uavOffset + index, descriptorSizeCSU);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCbvCPU(UINT index)
    {
        if (cbvOffset + index >= totalDescriptorsCSU)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(heapCSU->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(cbvOffset + index, descriptorSizeCSU);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtvCPU(UINT index)
    {
        if (index >= totalDescriptorsRtv)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(heapRtv->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(index, descriptorSizeRtv);
        return handle;
    }

    // Get the GPU handle for the ENTIRE table (starts at SRV 0), only CSU
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetTableGPUStart()
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(heapCSU->GetGPUDescriptorHandleForHeapStart());
    }

    ID3D12DescriptorHeap* GetHeapCSU() { return heapCSU; }
    ID3D12DescriptorHeap* GetHeapRtv() { return heapRtv; }

    void ReleaseHeaps()
    {
        if (heapCSU)
            heapCSU->Release();

        if (heapRtv)
            heapRtv->Release();
    }

    ~FrameDescriptorHeap() { ReleaseHeaps(); }
};
