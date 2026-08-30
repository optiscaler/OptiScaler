// Measures the frame the model is about to work on, so the codec's white point can be derived from the
// game's actual exposure instead of set by hand.
//
// A fixed value cannot work. In Cyberpunk the upscaler's output measured a mean luminance of about 0.065
// during gameplay, about 1.8 elsewhere, and about 185 in a third scene -- three orders of magnitude
// apart. Any single number is wrong for two of those.
//
// Adapting is safe in a way worth being explicit about: the encode and the resolve use the same white
// point within a frame and are exact inverses, so changing it cannot shift the finished image. It only
// moves the working point the model is shown. There is no flicker to guard against here, only a
// preference for the value to settle rather than chase.
//
// The readback is issued on one frame and mapped several frames later rather than fenced. A measurement
// that stalled the render thread would change the thing it measures, and a torn read costs nothing: the
// statistic is a rough exposure and it is sampled continuously.

#pragma once

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

namespace probe
{
// Half-precision to float. Only the cases a colour buffer produces are handled exactly; denormals are
// close enough to zero for an exposure readout.
inline float halfToFloat(unsigned short h)
{
    const unsigned int sign = (h & 0x8000u) << 16;
    const unsigned int exponent = (h >> 10) & 0x1Fu;
    const unsigned int mantissa = h & 0x3FFu;
    unsigned int bits;

    if (exponent == 0)
        bits = sign;
    else if (exponent == 31)
        bits = sign | 0x7F800000u | (mantissa << 13);
    else
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);

    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

struct Stats
{
    float minLuma = 0.0f;
    float maxLuma = 0.0f;
    float meanLuma = 0.0f;
    bool valid = false;
};

// Averages the whole frame down to 64x64, so what is read back describes the picture rather than
// whichever 64 pixels happen to sit in the middle of it. A centre block first reported a range of 154 to
// 197 -- narrow enough to be actively misleading about a frame that certainly contained shadows too.
class FrameReducer
{
  public:
    static const unsigned int kSide = 64;

    bool ensure(ID3D12Device* device)
    {
        if (pipeline_ != nullptr)
            return true;

        static const char* kSource = R"(
cbuffer Params : register(b0)
{
    uint gTileWidth;
    uint gTileHeight;
    uint gWidth;
    uint gHeight;
};

Texture2D<float4>   gSource : register(t0);
RWTexture2D<float4> gTarget : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 64 || id.y >= 64)
        return;

    // Every source pixel belongs to exactly one tile, so no part of the frame goes unrepresented.
    uint2 start = uint2(id.x * gTileWidth, id.y * gTileHeight);
    float3 sum = float3(0.0, 0.0, 0.0);
    uint count = 0;

    for (uint y = 0; y < gTileHeight; ++y)
    {
        for (uint x = 0; x < gTileWidth; ++x)
        {
            uint2 p = start + uint2(x, y);

            if (p.x < gWidth && p.y < gHeight)
            {
                sum += max(gSource.Load(int3(p, 0)).rgb, float3(0.0, 0.0, 0.0));
                ++count;
            }
        }
    }

    gTarget[id.xy] = float4(count > 0 ? sum / count : sum, 1.0);
}
)";

        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;

        if (FAILED(D3DCompile(kSource, strlen(kSource), nullptr, nullptr, nullptr, "main", "cs_5_1", 0, 0,
                              &code, &errors)))
        {
            if (errors != nullptr)
                errors->Release();

            return false;
        }

        if (errors != nullptr)
            errors->Release();

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 4;

        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = 2;
        rootDesc.pParameters = params;

        ID3DBlob* serialized = nullptr;

        if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr)))
        {
            code->Release();
            return false;
        }

        HRESULT hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                 IID_PPV_ARGS(&root_));
        serialized->Release();

        if (FAILED(hr))
        {
            code->Release();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = root_;
        pso.CS.pShaderBytecode = code->GetBufferPointer();
        pso.CS.BytecodeLength = code->GetBufferSize();
        hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline_));
        code->Release();

        if (FAILED(hr))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_))))
            return false;

        stride_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC reducedDesc = {};
        reducedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        reducedDesc.Width = kSide;
        reducedDesc.Height = kSide;
        reducedDesc.DepthOrArraySize = 1;
        reducedDesc.MipLevels = 1;
        reducedDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        reducedDesc.SampleDesc.Count = 1;
        reducedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &reducedDesc,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&reduced_))))
            return false;

        device_ = device;
        return true;
    }

    // Source must already be readable as a shader resource. The result is left in UNORDERED_ACCESS.
    ID3D12Resource* dispatch(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source, unsigned int width,
                             unsigned int height)
    {
        if (pipeline_ == nullptr)
            return nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap_->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = source->GetDesc().Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(source, &srv, cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = cpu;
        uavHandle.ptr += stride_;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(reduced_, nullptr, &uav, uavHandle);

        // Rounded up so the tiles cover the frame; threads landing outside it drop out in the shader.
        const unsigned int constants[4] = { (width + kSide - 1) / kSide, (height + kSide - 1) / kSide, width,
                                            height };

        ID3D12DescriptorHeap* heaps[] = { heap_ };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(root_);
        cmd->SetPipelineState(pipeline_);
        cmd->SetComputeRootDescriptorTable(0, gpu);
        cmd->SetComputeRoot32BitConstants(1, 4, constants, 0);
        cmd->Dispatch(kSide / 8, kSide / 8, 1);
        return reduced_;
    }

    void destroy()
    {
        if (pipeline_ != nullptr)
        {
            pipeline_->Release();
            pipeline_ = nullptr;
        }

        if (root_ != nullptr)
        {
            root_->Release();
            root_ = nullptr;
        }

        if (heap_ != nullptr)
        {
            heap_->Release();
            heap_ = nullptr;
        }

        if (reduced_ != nullptr)
        {
            reduced_->Release();
            reduced_ = nullptr;
        }

        device_ = nullptr;
    }

  private:
    ID3D12Device* device_ = nullptr;
    ID3D12RootSignature* root_ = nullptr;
    ID3D12PipelineState* pipeline_ = nullptr;
    ID3D12DescriptorHeap* heap_ = nullptr;
    ID3D12Resource* reduced_ = nullptr;
    unsigned int stride_ = 0;
};

// Copies the reduced frame back to the CPU and reports its luminance.
class BlockReader
{
  public:
    static const unsigned int kSide = 64;

    // Safe to call every frame; it only issues a copy when the previous one has been collected.
    void capture(ID3D12GraphicsCommandList* cmd, ID3D12Resource* tex, D3D12_RESOURCE_STATES state)
    {
        if (countdown_ >= 0 || tex == nullptr)
            return;

        D3D12_RESOURCE_DESC desc = tex->GetDesc();

        if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Width < kSide || desc.Height < kSide)
            return;

        ID3D12Device* device = nullptr;

        if (FAILED(tex->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
            return;

        if (readback_ == nullptr)
        {
            D3D12_RESOURCE_DESC block = desc;
            block.Width = kSide;
            block.Height = kSide;
            unsigned long long total = 0;
            device->GetCopyableFootprints(&block, 0, 1, 0, &layout_, nullptr, nullptr, &total);

            D3D12_HEAP_PROPERTIES heap = {};
            heap.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC buf = {};
            buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buf.Width = total;
            buf.Height = 1;
            buf.DepthOrArraySize = 1;
            buf.MipLevels = 1;
            buf.Format = DXGI_FORMAT_UNKNOWN;
            buf.SampleDesc.Count = 1;
            buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                       IID_PPV_ARGS(&readback_))))
            {
                device->Release();
                return;
            }
        }

        device->Release();

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback_;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = layout_;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = tex;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_BOX box = {};
        box.left = 0;
        box.top = 0;
        box.right = kSide;
        box.bottom = kSide;
        box.back = 1;

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = state;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd->ResourceBarrier(1, &b);
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = state;
        cmd->ResourceBarrier(1, &b);

        // Deep enough that the copy has certainly executed; the render thread never waits on it.
        countdown_ = 6;
    }

    // Returns a reading once one is ready, and nothing on every other frame.
    // lumas, when given, receives every tile's luminance (kSide * kSide values) -- the material a
    // tone curve is fitted from.
    Stats collect(float* lumas = nullptr, float* rgb = nullptr)
    {
        Stats out;

        if (countdown_ < 0)
            return out;

        if (--countdown_ >= 0)
            return out;

        if (readback_ == nullptr)
            return out;

        void* mapped = nullptr;
        D3D12_RANGE range = { 0, (SIZE_T) layout_.Footprint.RowPitch * kSide };

        if (FAILED(readback_->Map(0, &range, &mapped)) || mapped == nullptr)
            return out;

        float lo = 1e30f;
        float hi = -1e30f;
        double sum = 0.0;

        for (unsigned int y = 0; y < kSide; ++y)
        {
            const unsigned short* row =
                (const unsigned short*) ((const unsigned char*) mapped + (size_t) y * layout_.Footprint.RowPitch);

            for (unsigned int x = 0; x < kSide; ++x)
            {
                const float r = halfToFloat(row[x * 4 + 0]);
                const float g = halfToFloat(row[x * 4 + 1]);
                const float b = halfToFloat(row[x * 4 + 2]);
                const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;

                if (lumas != nullptr)
                    lumas[y * kSide + x] = luma;

                if (rgb != nullptr)
                {
                    rgb[(y * kSide + x) * 3 + 0] = r;
                    rgb[(y * kSide + x) * 3 + 1] = g;
                    rgb[(y * kSide + x) * 3 + 2] = b;
                }

                lo = luma < lo ? luma : lo;
                hi = luma > hi ? luma : hi;
                sum += luma;
            }
        }

        D3D12_RANGE written = { 0, 0 };
        readback_->Unmap(0, &written);

        out.minLuma = lo;
        out.maxLuma = hi;
        out.meanLuma = (float) (sum / (kSide * kSide));
        out.valid = true;
        return out;
    }

    void destroy()
    {
        if (readback_ != nullptr)
        {
            readback_->Release();
            readback_ = nullptr;
        }

        countdown_ = -1;
    }

  private:
    ID3D12Resource* readback_ = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout_ = {};
    int countdown_ = -1;
};
} // namespace probe
