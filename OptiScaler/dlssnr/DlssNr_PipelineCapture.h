#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

namespace DlssNr
{
// Diagnostic readbacks only. The caller must retain this object with GpuLifetime
// until the recording is closed and every submission has completed.
struct PipelineCaptureFrame
{
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Image
    {
        std::string name;
        ComPtr<ID3D12Resource> readback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout {};
        UINT64 bytes = 0;
    };
    std::vector<Image> images;
    std::ostringstream metadata;
    std::filesystem::path directory;
    ComPtr<ID3D12QueryHeap> query;
    ComPtr<ID3D12Resource> stamp;
    UINT64 totalBytes = 0;
    bool ended = false;

    static bool Readback(ID3D12Device* device, UINT64 bytes, ID3D12Resource** result)
    {
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1;
        desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(result)));
    }
    bool Init(ID3D12Device* device)
    {
        D3D12_QUERY_HEAP_DESC desc {}; desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; desc.Count = 1;
        if (FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&query))) ||
            !Readback(device, sizeof(UINT64), &stamp)) return false;
        void* mapped = nullptr; D3D12_RANGE empty {};
        if (FAILED(stamp->Map(0, &empty, &mapped))) return false;
        std::memset(mapped, 0, sizeof(UINT64));
        D3D12_RANGE written { 0, sizeof(UINT64) }; stamp->Unmap(0, &written);
        return true;
    }
    void Copy(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const char* name,
              ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
    {
        metadata << name << " resource " << resource << " state " << unsigned(state);
        if (!resource) { metadata << " missing\n"; return; }
        auto desc = resource->GetDesc();
        metadata << " width " << desc.Width << " height " << desc.Height << " format " << desc.Format;
        switch (desc.Format)
        {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        case DXGI_FORMAT_R32_TYPELESS: desc.Format = DXGI_FORMAT_R32_FLOAT; break;
        case DXGI_FORMAT_R16G16_TYPELESS: desc.Format = DXGI_FORMAT_R16G16_FLOAT; break;
        default: break;
        }
        // Deliberately narrow: reject planar/depth-stencil and unusual layouts.
        const bool format = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
            desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT || desc.Format == DXGI_FORMAT_R32_FLOAT ||
            desc.Format == DXGI_FORMAT_R16_FLOAT || desc.Format == DXGI_FORMAT_R16G16_FLOAT ||
            desc.Format == DXGI_FORMAT_R32G32_FLOAT || desc.Format == DXGI_FORMAT_R11G11B10_FLOAT;
        if (!format || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.DepthOrArraySize != 1)
        { metadata << " skipped_unsupported\n"; return; }
        Image image; image.name = name;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &image.layout, nullptr, nullptr, &image.bytes);
        if (!image.bytes || image.bytes > 256ull * 1024 * 1024 - totalBytes ||
            !Readback(device, image.bytes, &image.readback))
        { metadata << " skipped_budget_or_allocation\n"; return; }
        totalBytes += image.bytes;
        metadata << " storedFormat " << desc.Format << " rowPitch " << image.layout.Footprint.RowPitch
                 << " bytes " << image.bytes << '\n';
        D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = { resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state, D3D12_RESOURCE_STATE_COPY_SOURCE };
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) cmd->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from {}; from.pResource = resource;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to {}; to.pResource = image.readback.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = image.layout;
        cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) cmd->ResourceBarrier(1, &barrier);
        images.push_back(std::move(image));
    }
    void End(ID3D12GraphicsCommandList* cmd)
    {
        cmd->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        cmd->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, stamp.Get(), 0);
        ended = true;
    }
    bool Write()
    {
        // A discarded, never-submitted recording must not produce uninitialized images.
        if (!ended) return false;
        void* data = nullptr; D3D12_RANGE range { 0, sizeof(UINT64) }, empty {};
        if (FAILED(stamp->Map(0, &range, &data))) return false;
        UINT64 timestamp = 0; std::memcpy(&timestamp, data, sizeof(timestamp)); stamp->Unmap(0, &empty);
        if (!timestamp) return false;
        std::error_code error; std::filesystem::create_directories(directory, error);
        if (error) return false;
        bool success = true;
        for (auto& image : images)
        {
            range = { 0, SIZE_T(image.bytes) };
            if (FAILED(image.readback->Map(0, &range, &data))) { success = false; continue; }
            std::ofstream file(directory / (image.name + ".raw"), std::ios::binary);
            file.write(static_cast<const char*>(data), std::streamsize(image.bytes));
            file.close(); success &= !file.fail();
            image.readback->Unmap(0, &empty);
        }
        std::ofstream manifest(directory / "manifest.txt");
        manifest << "gpu_timestamp " << timestamp << "\n" << metadata.str();
        manifest.close();
        return success && !manifest.fail();
    }
};
}
