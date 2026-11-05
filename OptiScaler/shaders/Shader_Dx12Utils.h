#pragma once
#include <d3dx/d3dx12.h>
#include <vector>
#include <stdexcept>
#include <misc/DeferredRelease.h>

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

    // Own constants per heap, the GPU can still read the previous ones
    ID3D12Resource* constantBuffer = nullptr;

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

    ID3D12Resource* GetConstantBuffer(ID3D12Device* device, UINT64 size)
    {
        if (constantBuffer != nullptr && constantBuffer->GetDesc().Width >= size)
            return constantBuffer;

        DeferredRelease::Release(constantBuffer);

        auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&constantBuffer))))
        {
            constantBuffer = nullptr;
        }

        return constantBuffer;
    }

    ID3D12DescriptorHeap* GetHeapCSU() { return heapCSU; }
    ID3D12DescriptorHeap* GetHeapRtv() { return heapRtv; }

    void ReleaseHeaps()
    {
        // Command lists in flight can still reference these
        DeferredRelease::Release(heapCSU);
        DeferredRelease::Release(heapRtv);
        DeferredRelease::Release(constantBuffer);
    }

    ~FrameDescriptorHeap() { ReleaseHeaps(); }
};

template <typename T>
bool CreateConstantsBuffer(ID3D12Device* device, ID3D12Resource* constantBuffer, const T& constants,
                           D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
    // Copy the updated constant buffer data to the constant buffer resource
    UINT8* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
    auto result = constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        if (result == DXGI_ERROR_DEVICE_REMOVED && device != nullptr)
            Util::GetDeviceRemovedReason(device);

        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);

    constexpr UINT alignTo = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1;
    UINT alignedSize = (sizeof(constants) + alignTo) & ~alignTo;

    // Create CBV for Constants
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = alignedSize;

    device->CreateConstantBufferView(&cbvDesc, destDescriptor);

    return true;
}

template <typename T>
bool CreateConstantsBuffer(ID3D12Device* device, FrameDescriptorHeap& heap, const T& constants,
                           D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
    constexpr UINT alignTo = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1;
    auto constantBuffer = heap.GetConstantBuffer(device, (sizeof(constants) + alignTo) & ~alignTo);

    if (constantBuffer == nullptr)
        return false;

    return CreateConstantsBuffer(device, constantBuffer, constants, destDescriptor);
}
