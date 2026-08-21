#include "pch.h"
#include "D3D12_Hooks.h"

#include <Util.h>
#include <Config.h>

#include <magic_enum.hpp>

#include <resource_tracking/ResTrack_Dx12.h>

#include <proxies/D3D12_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/XeSS_Proxy.h>
#include <proxies/IGDExt_Proxy.h>
#include <proxies/Streamline_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <detours/detours.h>

#include <dxgi1_6.h>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"

#pragma intrinsic(_ReturnAddress)

using PFN_CheckFeatureSupport = rewrite_signature<decltype(&ID3D12Device::CheckFeatureSupport)>::type;
using PFN_CreateSampler = rewrite_signature<decltype(&ID3D12Device::CreateSampler)>::type;
using PFN_CreateCommittedResource = rewrite_signature<decltype(&ID3D12Device::CreateCommittedResource)>::type;
using PFN_CreatePlacedResource = rewrite_signature<decltype(&ID3D12Device::CreatePlacedResource)>::type;
using PFN_SetResidencyPriority = rewrite_signature<decltype(&ID3D12Device1::SetResidencyPriority)>::type;
using PFN_CreateRootSignature = rewrite_signature<decltype(&ID3D12Device::CreateRootSignature)>::type;

// GetResourceAllocationInfo is a special case because of the struct return,
// see comment on hkGetResourceAllocationInfo for details
typedef void(STDMETHODCALLTYPE* PFN_GetResourceAllocationInfo)(ID3D12Device* device,
                                                               D3D12_RESOURCE_ALLOCATION_INFO* pResult,
                                                               UINT visibleMask, UINT numResourceDescs,
                                                               D3D12_RESOURCE_DESC* pResourceDescs);

typedef decltype(&D3D12GetInterface) PFN_D3D12GetInterface;

using PFN_CreateDevice = rewrite_signature<decltype(&ID3D12DeviceFactory::CreateDevice)>::type;

using PFN_Release = rewrite_signature<decltype(&IUnknown::Release)>::type;

static PFN_CreateSampler o_CreateSampler = nullptr;
static PFN_CheckFeatureSupport o_CheckFeatureSupport = nullptr;
static PFN_CreateCommittedResource o_CreateCommittedResource = nullptr;
static PFN_CreatePlacedResource o_CreatePlacedResource = nullptr;
static PFN_SetResidencyPriority o_SetResidencyPriority = nullptr;
static PFN_GetResourceAllocationInfo o_GetResourceAllocationInfo = nullptr;
static PFN_CreateRootSignature o_CreateRootSignature = nullptr;
static PFN_D3D12GetInterface o_D3D12GetInterface = nullptr;
static PFN_CreateDevice o_CreateDevice = nullptr;

static D3d12Proxy::PFN_D3D12CreateDevice o_D3D12CreateDevice = nullptr;
static D3d12Proxy::PFN_D3D12SerializeRootSignature o_D3D12SerializeRootSignature = nullptr;
static D3d12Proxy::PFN_D3D12SerializeVersionedRootSignature o_D3D12SerializeVersionedRootSignature = nullptr;
static PFN_Release o_D3D12DeviceRelease = nullptr;

static bool _creatingD3D12Device = false;
static bool _d3d12Captured = false;
static LUID _lastAdapterLuid = {};

// Common
using PFN_SetDescriptorHeaps = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetDescriptorHeaps)>::type;
using PFN_SetPipelineState = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetPipelineState)>::type;

// ComputeRoot
using PFN_SetComputeRootSignature =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRootSignature)>::type;
using PFN_SetComputeRootDescriptorTable =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRootDescriptorTable)>::type;
using PFN_SetComputeRoot32BitConstant =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRoot32BitConstant)>::type;
using PFN_SetComputeRoot32BitConstants =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRoot32BitConstants)>::type;
using PFN_SetComputeRootConstantBufferView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRootConstantBufferView)>::type;
using PFN_SetComputeRootShaderResourceView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRootShaderResourceView)>::type;
using PFN_SetComputeRootUnorderedAccessView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView)>::type;

// GraphicsRoot
using PFN_SetGraphicsRootSignature =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRootSignature)>::type;
using PFN_SetGraphicsRootDescriptorTable =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable)>::type;
using PFN_SetGraphicsRoot32BitConstant =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant)>::type;
using PFN_SetGraphicsRoot32BitConstants =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants)>::type;
using PFN_SetGraphicsRootConstantBufferView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView)>::type;
using PFN_SetGraphicsRootShaderResourceView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView)>::type;
using PFN_SetGraphicsRootUnorderedAccessView =
    rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView)>::type;

template <typename T> struct RootRestoreHook
{
    T o_earlyHook = nullptr;
    T o_lateHook = nullptr;

    T GetHook() const { return o_lateHook ? o_lateHook : o_earlyHook; };
};

struct DescriptorHeap
{
    UINT NumDescriptorHeaps {};
    ID3D12DescriptorHeap* Heaps[2] = { nullptr, nullptr }; // apparently 2 is max
};

enum class RootEntryType
{
    Invalid,
    Table,
    Constant,
    Constants,
    CBV,
    SRV,
    UAV,
};

struct RootState
{
    RootEntryType type = RootEntryType::Invalid;

    // Table
    D3D12_GPU_DESCRIPTOR_HANDLE rootDescriptorTable {};

    // CBV / SRV / UAV
    D3D12_GPU_VIRTUAL_ADDRESS bufferLocation {};

    // Constants; Constant uses Data[0] and DestOffset
    UINT Num32BitValues = 0;
    UINT DestOffset = 0;
    std::vector<uint32_t> Data;
};

enum class SignatureEntryType
{
    Invalid,
    Compute,
    Graphics,
};

struct SignatureEntry
{
    SignatureEntryType type = SignatureEntryType::Invalid;
    ID3D12RootSignature* ptr {};
};

static std::shared_mutex descriptorHeapsMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, DescriptorHeap> descriptorHeaps;
static RootRestoreHook<PFN_SetDescriptorHeaps> s_SetDescriptorHeaps {};

static std::shared_mutex pipelineStatesMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> pipelineStates;
static RootRestoreHook<PFN_SetPipelineState> s_SetPipelineState {};

// Those use a common rootSignatureMutex mutex
static std::shared_mutex rootSignatureMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, SignatureEntry> signatures;
static RootRestoreHook<PFN_SetGraphicsRootSignature> s_SetGraphicsRootSignature {};
static RootRestoreHook<PFN_SetComputeRootSignature> s_SetComputeRootSignature {};

// Those use a common rootStatesMutex mutex
static std::shared_mutex rootStatesMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, std::vector<RootState>> rootStates;
static RootRestoreHook<PFN_SetComputeRootDescriptorTable> s_SetComputeRootDescriptorTable {};
static RootRestoreHook<PFN_SetComputeRoot32BitConstant> s_SetComputeRoot32BitConstant {};
static RootRestoreHook<PFN_SetComputeRoot32BitConstants> s_SetComputeRoot32BitConstants {};
static RootRestoreHook<PFN_SetComputeRootConstantBufferView> s_SetComputeRootConstantBufferView {};
static RootRestoreHook<PFN_SetComputeRootShaderResourceView> s_SetComputeRootShaderResourceView {};
static RootRestoreHook<PFN_SetComputeRootUnorderedAccessView> s_SetComputeRootUnorderedAccessView {};

static RootRestoreHook<PFN_SetGraphicsRootDescriptorTable> s_SetGraphicsRootDescriptorTable {};
static RootRestoreHook<PFN_SetGraphicsRoot32BitConstant> s_SetGraphicsRoot32BitConstant {};
static RootRestoreHook<PFN_SetGraphicsRoot32BitConstants> s_SetGraphicsRoot32BitConstants {};
static RootRestoreHook<PFN_SetGraphicsRootConstantBufferView> s_SetGraphicsRootConstantBufferView {};
static RootRestoreHook<PFN_SetGraphicsRootShaderResourceView> s_SetGraphicsRootShaderResourceView {};
static RootRestoreHook<PFN_SetGraphicsRootUnorderedAccessView> s_SetGraphicsRootUnorderedAccessView {};

static thread_local bool lateInProgressSetDescriptorHeaps = false;
static thread_local bool lateInProgressSetPipelineState = false;

static thread_local bool lateInProgressSetComputeRootSignature = false;
static thread_local bool lateInProgressSetComputeRootDescriptorTable = false;
static thread_local bool lateInProgressSetComputeRoot32BitConstants = false;
static thread_local bool lateInProgressSetComputeRoot32BitConstant = false;
static thread_local bool lateInProgressSetComputeRootConstantBufferView = false;
static thread_local bool lateInProgressSetComputeRootShaderResourceView = false;
static thread_local bool lateInProgressSetComputeRootUnorderedAccessView = false;

static thread_local bool lateInProgressSetGraphicsRootSignature = false;
static thread_local bool lateInProgressSetGraphicsRootDescriptorTable = false;
static thread_local bool lateInProgressSetGraphicsRoot32BitConstants = false;
static thread_local bool lateInProgressSetGraphicsRoot32BitConstant = false;
static thread_local bool lateInProgressSetGraphicsRootConstantBufferView = false;
static thread_local bool lateInProgressSetGraphicsRootShaderResourceView = false;
static thread_local bool lateInProgressSetGraphicsRootUnorderedAccessView = false;

static std::shared_mutex rootSigParameterCountMutex;
static ankerl::unordered_dense::map<ID3D12RootSignature*, UINT> rootSigParameterCount;

static bool isUpscalerActive = false;

// Intel Atomic Extension
struct UE_D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;

    // UE Part
    uint8_t PixelFormat { 0 };
    uint8_t UAVPixelFormat { 0 };
    bool bRequires64BitAtomicSupport : 1 = false;
    bool bReservedResource : 1 = false;
    bool bBackBuffer : 1 = false;
    bool bExternal : 1 = false;
};

static ID3D12Device* _intelD3D12Device = nullptr;
static ULONG _intelD3D12DeviceRefTarget = 0;
static bool _skipCommitedResource = false;
static bool _skipGetResourceAllocationInfo = false;

#ifdef ENABLE_DEBUG_LAYER_DX12
static ID3D12Debug3* debugController = nullptr;
static ID3D12InfoQueue* infoQueue = nullptr;
static ID3D12InfoQueue1* infoQueue1 = nullptr;

static void CALLBACK D3D12DebugCallback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity,
                                        D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext)
{
    LOG_DEBUG("[{}] [{}] [{}]: {}", magic_enum::enum_name(Category), magic_enum::enum_name(Severity),
              magic_enum::enum_name(ID), pDescription);
}
#endif

static void HookToDevice(ID3D12Device* InDevice);
static void UnhookDevice();

static inline D3D12_FILTER UpgradeToAF(D3D12_FILTER f)
{
    // Skip point filter
    const auto minF = D3D12_DECODE_MIN_FILTER(f);
    const auto magF = D3D12_DECODE_MAG_FILTER(f);
    const auto mipF = D3D12_DECODE_MIP_FILTER(f);
    if (Config::Instance()->AnisotropySkipPointFilter.value_or_default() &&
        ((mipF == D3D12_FILTER_TYPE_POINT) || (minF == D3D12_FILTER_TYPE_POINT && magF == D3D12_FILTER_TYPE_POINT)))
    {
        return f;
    }

    const auto reduction = D3D12_DECODE_FILTER_REDUCTION(f);

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_COMPARISON)
    {
        if (Config::Instance()->AnisotropyModifyComp.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_COMPARISON);

        return f;
    }

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_MINIMUM)
    {
        if (Config::Instance()->AnisotropyModifyMinMax.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MINIMUM);

        return f;
    }

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_MAXIMUM)
    {
        if (Config::Instance()->AnisotropyModifyMinMax.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MAXIMUM);

        return f;
    }

    return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

static void ApplySamplerOverrides(D3D12_STATIC_SAMPLER_DESC& samplerDesc)
{
    if (Config::Instance()->MipmapBiasOverride.has_value())
    {
        auto isMipmapped = samplerDesc.MinLOD != samplerDesc.MaxLOD;
        auto isAnisotropic = (samplerDesc.Filter == D3D12_FILTER_ANISOTROPIC) || (samplerDesc.MaxAnisotropy > 1);
        auto isAlreadyBiased = samplerDesc.MipLODBias < 0.0f;

        if ((isMipmapped && (isAnisotropic || isAlreadyBiased)) ||
            Config::Instance()->MipmapBiasOverrideAll.value_or_default())
        {
            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc.MipLODBias,
                          Config::Instance()->MipmapBiasOverride.value());

                if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                    samplerDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                    samplerDesc.MipLODBias = samplerDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                else
                    samplerDesc.MipLODBias = samplerDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();

                samplerDesc.MipLODBias = std::clamp(samplerDesc.MipLODBias, -16.0f, 15.99f);
            }

            if (State::Instance().lastMipBiasMax < samplerDesc.MipLODBias)
                State::Instance().lastMipBiasMax = samplerDesc.MipLODBias;

            if (State::Instance().lastMipBias > samplerDesc.MipLODBias)
                State::Instance().lastMipBias = samplerDesc.MipLODBias;
        }
    }

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc.MaxAnisotropy,
                  Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc.Filter);

        samplerDesc.Filter = UpgradeToAF(samplerDesc.Filter);
        samplerDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
    }
}

static void ApplySamplerOverrides(D3D12_STATIC_SAMPLER_DESC1& samplerDesc)
{
    if (Config::Instance()->MipmapBiasOverride.has_value())
    {
        if ((samplerDesc.MipLODBias < 0.0f && samplerDesc.MinLOD != samplerDesc.MaxLOD) ||
            Config::Instance()->MipmapBiasOverrideAll.value_or_default())
        {
            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc.MipLODBias,
                          Config::Instance()->MipmapBiasOverride.value());

                if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                    samplerDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                    samplerDesc.MipLODBias = samplerDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                else
                    samplerDesc.MipLODBias = samplerDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
            }

            if (State::Instance().lastMipBiasMax < samplerDesc.MipLODBias)
                State::Instance().lastMipBiasMax = samplerDesc.MipLODBias;

            if (State::Instance().lastMipBias > samplerDesc.MipLODBias)
                State::Instance().lastMipBias = samplerDesc.MipLODBias;
        }
    }

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc.MaxAnisotropy,
                  Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc.Filter);

        samplerDesc.Filter = UpgradeToAF(samplerDesc.Filter);
        samplerDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
    }
}

// Early hooks, from Opti's own cmdlist
VALIDATE_HOOK(hkSetPipelineState, PFN_SetPipelineState)
static void hkSetPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pPipelineState)
{
    if (!lateInProgressSetPipelineState && !isUpscalerActive && commandList != nullptr && pPipelineState != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(pipelineStatesMutex);
        pipelineStates.insert_or_assign(commandList, pPipelineState);
    }

    s_SetPipelineState.o_earlyHook(commandList, pPipelineState);
}

VALIDATE_HOOK(hkSetDescriptorHeaps, PFN_SetDescriptorHeaps)
static void hkSetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT NumDescriptorHeaps,
                                 ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    if (!lateInProgressSetDescriptorHeaps && !isUpscalerActive && commandList != nullptr &&
        ppDescriptorHeaps != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(descriptorHeapsMutex);
        DescriptorHeap temp {};
        temp.NumDescriptorHeaps = NumDescriptorHeaps;
        for (UINT i = 0; i < NumDescriptorHeaps; ++i)
        {
            temp.Heaps[i] = ppDescriptorHeaps[i];
        }
        descriptorHeaps.insert_or_assign(commandList, std::move(temp));
    }

    s_SetDescriptorHeaps.o_earlyHook(commandList, NumDescriptorHeaps, ppDescriptorHeaps);
}

UINT GetRootParameterCount(ID3D12RootSignature* pRootSignature)
{
    std::unique_lock<std::shared_mutex> lock(rootSigParameterCountMutex);
    auto it = rootSigParameterCount.find(pRootSignature);
    return (it != rootSigParameterCount.end()) ? it->second : 0;
}

VALIDATE_HOOK(hkSetComputeRootSignature, PFN_SetComputeRootSignature)
static void hkSetComputeRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    if (!lateInProgressSetComputeRootSignature && Config::Instance()->RestoreComputeSignature.value_or_default() &&
        !isUpscalerActive && commandList != nullptr && pRootSignature != nullptr)
    {
        {
            auto paramCount = GetRootParameterCount(pRootSignature);
            std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
            auto& table = rootStates[commandList];
            table.resize(paramCount);
        }

        std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);
        signatures.insert_or_assign(commandList, SignatureEntry { SignatureEntryType::Compute, pRootSignature });
    }

    s_SetComputeRootSignature.o_earlyHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetComputeRootDescriptorTable, PFN_SetComputeRootDescriptorTable)
static void hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                            D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (!lateInProgressSetComputeRootDescriptorTable && !isUpscalerActive && commandList != nullptr &&
        BaseDescriptor.ptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Table;
            table[RootParameterIndex].rootDescriptorTable = BaseDescriptor;
        }
    }

    s_SetComputeRootDescriptorTable.o_earlyHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstants, PFN_SetComputeRoot32BitConstants)
static void hkSetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                           UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues)
{
    if (!lateInProgressSetComputeRoot32BitConstants && !isUpscalerActive && commandList != nullptr && pSrcData)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constants;
            table[RootParameterIndex].Num32BitValues = Num32BitValuesToSet;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            auto* src = static_cast<const uint32_t*>(pSrcData);
            table[RootParameterIndex].Data.assign(src, src + Num32BitValuesToSet);
        }
    }

    s_SetComputeRoot32BitConstants.o_earlyHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                               DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstant, PFN_SetComputeRoot32BitConstant)
static void hkSetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, UINT SrcData,
                                          UINT DestOffsetIn32BitValues)
{
    if (!lateInProgressSetComputeRoot32BitConstant && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constant;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            table[RootParameterIndex].Data.assign(1, SrcData);
        }
    }

    s_SetComputeRoot32BitConstant.o_earlyHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRootConstantBufferView, PFN_SetComputeRootConstantBufferView)
static void hkSetComputeRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!lateInProgressSetComputeRootConstantBufferView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::CBV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootConstantBufferView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetComputeRootShaderResourceView, PFN_SetComputeRootShaderResourceView)
static void hkSetComputeRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!lateInProgressSetComputeRootShaderResourceView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::SRV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootShaderResourceView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetComputeRootShaderResourceView = false;
}

VALIDATE_HOOK(hkSetComputeRootUnorderedAccessView, PFN_SetComputeRootUnorderedAccessView)
static void hkSetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (lateInProgressSetComputeRootUnorderedAccessView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::UAV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootUnorderedAccessView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootSignature, PFN_SetGraphicsRootSignature)
static void hkSetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    if (!lateInProgressSetGraphicsRootSignature && Config::Instance()->RestoreGraphicSignature.value_or_default() &&
        !isUpscalerActive && commandList != nullptr && pRootSignature != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);
        signatures.insert_or_assign(commandList, SignatureEntry { SignatureEntryType::Graphics, pRootSignature });
    }

    s_SetGraphicsRootSignature.o_earlyHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetGraphicsRootDescriptorTable, PFN_SetGraphicsRootDescriptorTable)
static void hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                             D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (!lateInProgressSetGraphicsRootDescriptorTable && !isUpscalerActive && commandList != nullptr &&
        BaseDescriptor.ptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Table;
            table[RootParameterIndex].rootDescriptorTable = BaseDescriptor;
        }
    }

    s_SetGraphicsRootDescriptorTable.o_earlyHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstants, PFN_SetGraphicsRoot32BitConstants)
static void hkSetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                            UINT Num32BitValuesToSet, const void* pSrcData,
                                            UINT DestOffsetIn32BitValues)
{
    if (!lateInProgressSetGraphicsRoot32BitConstants && !isUpscalerActive && commandList != nullptr && pSrcData)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constants;
            table[RootParameterIndex].Num32BitValues = Num32BitValuesToSet;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            auto* src = static_cast<const uint32_t*>(pSrcData);
            table[RootParameterIndex].Data.assign(src, src + Num32BitValuesToSet);
        }
    }

    s_SetGraphicsRoot32BitConstants.o_earlyHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                                DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstant, PFN_SetGraphicsRoot32BitConstant)
static void hkSetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                           UINT SrcData, UINT DestOffsetIn32BitValues)
{
    if (!lateInProgressSetGraphicsRoot32BitConstant && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constant;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            table[RootParameterIndex].Data.assign(1, SrcData);
        }
    }

    s_SetGraphicsRoot32BitConstant.o_earlyHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRootConstantBufferView, PFN_SetGraphicsRootConstantBufferView)
static void hkSetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!lateInProgressSetGraphicsRootConstantBufferView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::CBV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootConstantBufferView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootShaderResourceView, PFN_SetGraphicsRootShaderResourceView)
static void hkSetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (!lateInProgressSetGraphicsRootShaderResourceView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::SRV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootShaderResourceView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetGraphicsRootShaderResourceView = false;
}

VALIDATE_HOOK(hkSetGraphicsRootUnorderedAccessView, PFN_SetGraphicsRootUnorderedAccessView)
static void hkSetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                 D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (lateInProgressSetGraphicsRootUnorderedAccessView && !isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::UAV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootUnorderedAccessView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

// Late hooks, from upscaler eval
VALIDATE_HOOK(hkSetPipelineStateLate, PFN_SetPipelineState)
static void hkSetPipelineStateLate(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pPipelineState)
{
    lateInProgressSetPipelineState = true;

    if (!isUpscalerActive && commandList != nullptr && pPipelineState != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(pipelineStatesMutex);
        pipelineStates.insert_or_assign(commandList, pPipelineState);
    }

    s_SetPipelineState.o_lateHook(commandList, pPipelineState);

    lateInProgressSetPipelineState = false;
}

VALIDATE_HOOK(hkSetDescriptorHeapsLate, PFN_SetDescriptorHeaps)
static void hkSetDescriptorHeapsLate(ID3D12GraphicsCommandList* commandList, UINT NumDescriptorHeaps,
                                     ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    lateInProgressSetDescriptorHeaps = true;

    if (!isUpscalerActive && commandList != nullptr && ppDescriptorHeaps != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(descriptorHeapsMutex);
        DescriptorHeap temp {};
        temp.NumDescriptorHeaps = NumDescriptorHeaps;
        for (UINT i = 0; i < NumDescriptorHeaps; ++i)
        {
            temp.Heaps[i] = ppDescriptorHeaps[i];
        }
        descriptorHeaps.insert_or_assign(commandList, std::move(temp));
    }

    s_SetDescriptorHeaps.o_lateHook(commandList, NumDescriptorHeaps, ppDescriptorHeaps);

    lateInProgressSetDescriptorHeaps = false;
}

VALIDATE_HOOK(hkSetComputeRootSignatureLate, PFN_SetComputeRootSignature)
static void hkSetComputeRootSignatureLate(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    lateInProgressSetComputeRootSignature = true;

    if (Config::Instance()->RestoreComputeSignature.value_or_default() && !isUpscalerActive && commandList != nullptr &&
        pRootSignature != nullptr)
    {
        {
            auto paramCount = GetRootParameterCount(pRootSignature);
            std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
            auto& table = rootStates[commandList];
            table.resize(paramCount);
        }

        std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);
        signatures.insert_or_assign(commandList, SignatureEntry { SignatureEntryType::Compute, pRootSignature });
    }

    s_SetComputeRootSignature.o_lateHook(commandList, pRootSignature);

    lateInProgressSetComputeRootSignature = false;
}

VALIDATE_HOOK(hkSetComputeRootDescriptorTableLate, PFN_SetComputeRootDescriptorTable)
static void hkSetComputeRootDescriptorTableLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    lateInProgressSetComputeRootDescriptorTable = true;

    if (!isUpscalerActive && commandList != nullptr && BaseDescriptor.ptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Table;
            table[RootParameterIndex].rootDescriptorTable = BaseDescriptor;
        }
    }

    s_SetComputeRootDescriptorTable.o_lateHook(commandList, RootParameterIndex, BaseDescriptor);

    lateInProgressSetComputeRootDescriptorTable = false;
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstantsLate, PFN_SetComputeRoot32BitConstants)
static void hkSetComputeRoot32BitConstantsLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               UINT Num32BitValuesToSet, const void* pSrcData,
                                               UINT DestOffsetIn32BitValues)
{
    lateInProgressSetComputeRoot32BitConstants = true;

    if (!isUpscalerActive && commandList != nullptr && pSrcData)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constants;
            table[RootParameterIndex].Num32BitValues = Num32BitValuesToSet;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            auto* src = static_cast<const uint32_t*>(pSrcData);
            table[RootParameterIndex].Data.assign(src, src + Num32BitValuesToSet);
        }
    }

    s_SetComputeRoot32BitConstants.o_lateHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                              DestOffsetIn32BitValues);

    lateInProgressSetComputeRoot32BitConstants = false;
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstantLate, PFN_SetComputeRoot32BitConstant)
static void hkSetComputeRoot32BitConstantLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                              UINT SrcData, UINT DestOffsetIn32BitValues)
{
    lateInProgressSetComputeRoot32BitConstant = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constant;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            table[RootParameterIndex].Data.assign(1, SrcData);
        }
    }

    s_SetComputeRoot32BitConstant.o_lateHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);

    lateInProgressSetComputeRoot32BitConstant = false;
}

VALIDATE_HOOK(hkSetComputeRootConstantBufferViewLate, PFN_SetComputeRootConstantBufferView)
static void hkSetComputeRootConstantBufferViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                   D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetComputeRootConstantBufferView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::CBV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootConstantBufferView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetComputeRootConstantBufferView = false;
}

VALIDATE_HOOK(hkSetComputeRootShaderResourceViewLate, PFN_SetComputeRootShaderResourceView)
static void hkSetComputeRootShaderResourceViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                   D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetComputeRootShaderResourceView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::SRV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootShaderResourceView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetComputeRootShaderResourceView = false;
}

VALIDATE_HOOK(hkSetComputeRootUnorderedAccessViewLate, PFN_SetComputeRootUnorderedAccessView)
static void hkSetComputeRootUnorderedAccessViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetComputeRootUnorderedAccessView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::UAV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetComputeRootUnorderedAccessView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetComputeRootUnorderedAccessView = false;
}

VALIDATE_HOOK(hkSetGraphicsRootSignatureLate, PFN_SetGraphicsRootSignature)
static void hkSetGraphicsRootSignatureLate(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    lateInProgressSetGraphicsRootSignature = true;

    if (Config::Instance()->RestoreGraphicSignature.value_or_default() && !isUpscalerActive && commandList != nullptr &&
        pRootSignature != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);
        signatures.insert_or_assign(commandList, SignatureEntry { SignatureEntryType::Graphics, pRootSignature });
    }

    s_SetGraphicsRootSignature.o_lateHook(commandList, pRootSignature);

    lateInProgressSetGraphicsRootSignature = false;
}

VALIDATE_HOOK(hkSetGraphicsRootDescriptorTableLate, PFN_SetGraphicsRootDescriptorTable)
static void hkSetGraphicsRootDescriptorTableLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    lateInProgressSetGraphicsRootDescriptorTable = true;

    if (!isUpscalerActive && commandList != nullptr && BaseDescriptor.ptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Table;
            table[RootParameterIndex].rootDescriptorTable = BaseDescriptor;
        }
    }

    s_SetGraphicsRootDescriptorTable.o_lateHook(commandList, RootParameterIndex, BaseDescriptor);

    lateInProgressSetGraphicsRootDescriptorTable = false;
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstantsLate, PFN_SetGraphicsRoot32BitConstants)
static void hkSetGraphicsRoot32BitConstantsLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                UINT Num32BitValuesToSet, const void* pSrcData,
                                                UINT DestOffsetIn32BitValues)
{
    lateInProgressSetGraphicsRoot32BitConstants = true;

    if (!isUpscalerActive && commandList != nullptr && pSrcData)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constants;
            table[RootParameterIndex].Num32BitValues = Num32BitValuesToSet;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            auto* src = static_cast<const uint32_t*>(pSrcData);
            table[RootParameterIndex].Data.assign(src, src + Num32BitValuesToSet);
        }
    }

    s_SetGraphicsRoot32BitConstants.o_lateHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                               DestOffsetIn32BitValues);

    lateInProgressSetGraphicsRoot32BitConstants = false;
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstantLate, PFN_SetGraphicsRoot32BitConstant)
static void hkSetGraphicsRoot32BitConstantLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               UINT SrcData, UINT DestOffsetIn32BitValues)
{
    lateInProgressSetGraphicsRoot32BitConstant = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::Constant;
            table[RootParameterIndex].DestOffset = DestOffsetIn32BitValues;
            table[RootParameterIndex].Data.assign(1, SrcData);
        }
    }

    s_SetGraphicsRoot32BitConstant.o_lateHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);

    lateInProgressSetGraphicsRoot32BitConstant = false;
}

VALIDATE_HOOK(hkSetGraphicsRootConstantBufferViewLate, PFN_SetGraphicsRootConstantBufferView)
static void hkSetGraphicsRootConstantBufferViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetGraphicsRootConstantBufferView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::CBV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootConstantBufferView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetGraphicsRootConstantBufferView = false;
}

VALIDATE_HOOK(hkSetGraphicsRootShaderResourceViewLate, PFN_SetGraphicsRootShaderResourceView)
static void hkSetGraphicsRootShaderResourceViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetGraphicsRootShaderResourceView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::SRV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootShaderResourceView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetGraphicsRootShaderResourceView = false;
}

VALIDATE_HOOK(hkSetGraphicsRootUnorderedAccessViewLate, PFN_SetGraphicsRootUnorderedAccessView)
static void hkSetGraphicsRootUnorderedAccessViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                     D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    lateInProgressSetGraphicsRootUnorderedAccessView = true;

    if (!isUpscalerActive && commandList != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
        auto& table = rootStates[commandList];
        if (RootParameterIndex < table.size())
        {
            table[RootParameterIndex].type = RootEntryType::UAV;
            table[RootParameterIndex].bufferLocation = BufferLocation;
        }
    }

    s_SetGraphicsRootUnorderedAccessView.o_lateHook(commandList, RootParameterIndex, BufferLocation);

    lateInProgressSetGraphicsRootUnorderedAccessView = false;
}

void D3D12Hooks::HookToCommandListLate(ID3D12GraphicsCommandList* commandList)
{
    if (s_SetComputeRootSignature.o_lateHook || s_SetGraphicsRootSignature.o_lateHook)
        return;

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) commandList;

    const bool restoreComputeSignature = Config::Instance()->RestoreComputeSignature.value_or_default();
    const bool restoreGraphicSignature = Config::Instance()->RestoreGraphicSignature.value_or_default();
    const bool extendedRestoreSignature = Config::Instance()->ExtendedStateRestore.value_or_default();

    s_SetPipelineState.o_lateHook = (PFN_SetPipelineState) pVTable[25];
    s_SetDescriptorHeaps.o_lateHook = (PFN_SetDescriptorHeaps) pVTable[28];
    s_SetComputeRootSignature.o_lateHook = (PFN_SetComputeRootSignature) pVTable[29];
    s_SetGraphicsRootSignature.o_lateHook = (PFN_SetGraphicsRootSignature) pVTable[30];
    s_SetComputeRootDescriptorTable.o_lateHook = (PFN_SetComputeRootDescriptorTable) pVTable[31];
    s_SetComputeRoot32BitConstant.o_lateHook = (PFN_SetComputeRoot32BitConstant) pVTable[33];
    s_SetComputeRoot32BitConstants.o_lateHook = (PFN_SetComputeRoot32BitConstants) pVTable[35];
    s_SetComputeRootConstantBufferView.o_lateHook = (PFN_SetComputeRootConstantBufferView) pVTable[37];
    s_SetComputeRootShaderResourceView.o_lateHook = (PFN_SetComputeRootShaderResourceView) pVTable[39];
    s_SetComputeRootUnorderedAccessView.o_lateHook = (PFN_SetComputeRootUnorderedAccessView) pVTable[41];

    if (s_SetPipelineState.o_lateHook || s_SetDescriptorHeaps.o_lateHook || s_SetComputeRootSignature.o_lateHook ||
        s_SetGraphicsRootSignature.o_lateHook || s_SetComputeRootDescriptorTable.o_lateHook ||
        s_SetComputeRoot32BitConstant.o_lateHook || s_SetComputeRoot32BitConstants.o_lateHook ||
        s_SetComputeRootConstantBufferView.o_lateHook || s_SetComputeRootShaderResourceView.o_lateHook ||
        s_SetComputeRootUnorderedAccessView.o_lateHook)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // Common
        if (extendedRestoreSignature)
        {
            if (s_SetPipelineState.o_lateHook != nullptr)
                DetourAttach(&(PVOID&) s_SetPipelineState.o_lateHook, hkSetPipelineStateLate);

            if (s_SetDescriptorHeaps.o_lateHook != nullptr)
                DetourAttach(&(PVOID&) s_SetDescriptorHeaps.o_lateHook, hkSetDescriptorHeapsLate);
        }

        if (restoreComputeSignature)
        {
            if (s_SetComputeRootSignature.o_lateHook != nullptr)
                DetourAttach(&(PVOID&) s_SetComputeRootSignature.o_lateHook, hkSetComputeRootSignatureLate);

            if (extendedRestoreSignature)
            {

                if (s_SetComputeRootDescriptorTable.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootDescriptorTable.o_lateHook,
                                 hkSetComputeRootDescriptorTableLate);
                }

                if (s_SetComputeRoot32BitConstant.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRoot32BitConstant.o_lateHook, hkSetComputeRoot32BitConstantLate);
                }

                if (s_SetComputeRoot32BitConstants.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRoot32BitConstants.o_lateHook,
                                 hkSetComputeRoot32BitConstantsLate);
                }

                if (s_SetComputeRootConstantBufferView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootConstantBufferView.o_lateHook,
                                 hkSetComputeRootConstantBufferViewLate);
                }

                if (s_SetComputeRootShaderResourceView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootShaderResourceView.o_lateHook,
                                 hkSetComputeRootShaderResourceViewLate);
                }

                if (s_SetComputeRootUnorderedAccessView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootUnorderedAccessView.o_lateHook,
                                 hkSetComputeRootUnorderedAccessViewLate);
                }
            }
        }

        if (restoreGraphicSignature)
        {
            if (s_SetGraphicsRootSignature.o_lateHook != nullptr)
                DetourAttach(&(PVOID&) s_SetGraphicsRootSignature.o_lateHook, hkSetGraphicsRootSignatureLate);

            if (extendedRestoreSignature)
            {

                if (s_SetGraphicsRootDescriptorTable.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRootDescriptorTable.o_lateHook,
                                 hkSetGraphicsRootDescriptorTableLate);
                }

                if (s_SetGraphicsRoot32BitConstant.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRoot32BitConstant.o_lateHook,
                                 hkSetGraphicsRoot32BitConstantLate);
                }

                if (s_SetGraphicsRoot32BitConstants.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRoot32BitConstants.o_lateHook,
                                 hkSetGraphicsRoot32BitConstantsLate);
                }

                if (s_SetGraphicsRootConstantBufferView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRootConstantBufferView.o_lateHook,
                                 hkSetGraphicsRootConstantBufferViewLate);
                }

                if (s_SetGraphicsRootShaderResourceView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRootShaderResourceView.o_lateHook,
                                 hkSetGraphicsRootShaderResourceViewLate);
                }

                if (s_SetGraphicsRootUnorderedAccessView.o_lateHook != nullptr)
                {
                    DetourAttach(&(PVOID&) s_SetGraphicsRootUnorderedAccessView.o_lateHook,
                                 hkSetGraphicsRootUnorderedAccessViewLate);
                }
            }
        }

        if (DetourTransactionCommit() == NO_ERROR)
        {
            LOG_DEBUG("Hooked RootSignature functions Late");
        }
        else
        {
            s_SetPipelineState.o_lateHook = nullptr;
            s_SetDescriptorHeaps.o_lateHook = nullptr;
            s_SetComputeRootSignature.o_lateHook = nullptr;
            s_SetGraphicsRootSignature.o_lateHook = nullptr;
            s_SetComputeRootDescriptorTable.o_lateHook = nullptr;
            s_SetComputeRoot32BitConstant.o_lateHook = nullptr;
            s_SetComputeRoot32BitConstants.o_lateHook = nullptr;
            s_SetComputeRootConstantBufferView.o_lateHook = nullptr;
            s_SetComputeRootShaderResourceView.o_lateHook = nullptr;
            s_SetComputeRootUnorderedAccessView.o_lateHook = nullptr;

            LOG_WARN("Hooking RootSignature Late failed");
        }
    }
    else
    {
        LOG_WARN("Late hooks into RootSignature are nullptr");
    }
}

static void HookToCommandList(ID3D12Device* InDevice)
{
    if (s_SetComputeRootSignature.o_earlyHook != nullptr || s_SetGraphicsRootSignature.o_earlyHook != nullptr)
        return;

    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;

    if (InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)) == S_OK)
    {
        if (InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
                                        IID_PPV_ARGS(&commandList)) == S_OK)
        {
            // Get the vtable pointer
            PVOID* pVTable = *(PVOID**) commandList;

            const bool extendedRestoreSignature = Config::Instance()->ExtendedStateRestore.value_or_default();

            s_SetPipelineState.o_earlyHook = (PFN_SetPipelineState) pVTable[25];
            s_SetDescriptorHeaps.o_earlyHook = (PFN_SetDescriptorHeaps) pVTable[28];
            s_SetComputeRootSignature.o_earlyHook = (PFN_SetComputeRootSignature) pVTable[29];
            s_SetGraphicsRootSignature.o_earlyHook = (PFN_SetGraphicsRootSignature) pVTable[30];
            s_SetComputeRootDescriptorTable.o_earlyHook = (PFN_SetComputeRootDescriptorTable) pVTable[31];
            s_SetComputeRoot32BitConstant.o_earlyHook = (PFN_SetComputeRoot32BitConstant) pVTable[33];
            s_SetComputeRoot32BitConstants.o_earlyHook = (PFN_SetComputeRoot32BitConstants) pVTable[35];
            s_SetComputeRootConstantBufferView.o_earlyHook = (PFN_SetComputeRootConstantBufferView) pVTable[37];
            s_SetComputeRootShaderResourceView.o_earlyHook = (PFN_SetComputeRootShaderResourceView) pVTable[39];
            s_SetComputeRootUnorderedAccessView.o_earlyHook = (PFN_SetComputeRootUnorderedAccessView) pVTable[41];

            if (s_SetPipelineState.o_earlyHook || s_SetDescriptorHeaps.o_earlyHook ||
                s_SetComputeRootSignature.o_earlyHook || s_SetGraphicsRootSignature.o_earlyHook ||
                s_SetComputeRootDescriptorTable.o_earlyHook || s_SetComputeRoot32BitConstant.o_earlyHook ||
                s_SetComputeRoot32BitConstants.o_earlyHook || s_SetComputeRootConstantBufferView.o_earlyHook ||
                s_SetComputeRootShaderResourceView.o_earlyHook || s_SetComputeRootUnorderedAccessView.o_earlyHook)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (s_SetPipelineState.o_earlyHook != nullptr && extendedRestoreSignature)
                    DetourAttach(&(PVOID&) s_SetPipelineState.o_earlyHook, hkSetPipelineState);

                if (s_SetDescriptorHeaps.o_earlyHook != nullptr && extendedRestoreSignature)
                    DetourAttach(&(PVOID&) s_SetDescriptorHeaps.o_earlyHook, hkSetDescriptorHeaps);

                if (s_SetComputeRootSignature.o_earlyHook != nullptr)
                    DetourAttach(&(PVOID&) s_SetComputeRootSignature.o_earlyHook, hkSetComputeRootSignature);

                if (s_SetGraphicsRootSignature.o_earlyHook != nullptr)
                    DetourAttach(&(PVOID&) s_SetGraphicsRootSignature.o_earlyHook, hkSetGraphicsRootSignature);

                if (s_SetComputeRootDescriptorTable.o_earlyHook != nullptr && extendedRestoreSignature)
                    DetourAttach(&(PVOID&) s_SetComputeRootDescriptorTable.o_earlyHook,
                                 hkSetComputeRootDescriptorTable);

                if (s_SetComputeRoot32BitConstant.o_earlyHook != nullptr && extendedRestoreSignature)
                    DetourAttach(&(PVOID&) s_SetComputeRoot32BitConstant.o_earlyHook, hkSetComputeRoot32BitConstant);

                if (s_SetComputeRoot32BitConstants.o_earlyHook != nullptr && extendedRestoreSignature)
                    DetourAttach(&(PVOID&) s_SetComputeRoot32BitConstants.o_earlyHook, hkSetComputeRoot32BitConstants);

                if (s_SetComputeRootConstantBufferView.o_earlyHook != nullptr && extendedRestoreSignature)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootConstantBufferView.o_earlyHook,
                                 hkSetComputeRootConstantBufferView);
                }

                if (s_SetComputeRootShaderResourceView.o_earlyHook != nullptr && extendedRestoreSignature)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootShaderResourceView.o_earlyHook,
                                 hkSetComputeRootShaderResourceView);
                }

                if (s_SetComputeRootUnorderedAccessView.o_earlyHook != nullptr && extendedRestoreSignature)
                {
                    DetourAttach(&(PVOID&) s_SetComputeRootUnorderedAccessView.o_earlyHook,
                                 hkSetComputeRootUnorderedAccessView);
                }

                if (DetourTransactionCommit() == NO_ERROR)
                {
                    LOG_DEBUG("Hooked RootSignature functions");
                }
                else
                {
                    s_SetPipelineState.o_earlyHook = nullptr;
                    s_SetDescriptorHeaps.o_earlyHook = nullptr;
                    s_SetComputeRootSignature.o_earlyHook = nullptr;
                    s_SetGraphicsRootSignature.o_earlyHook = nullptr;
                    s_SetComputeRootDescriptorTable.o_earlyHook = nullptr;
                    s_SetComputeRoot32BitConstant.o_earlyHook = nullptr;
                    s_SetComputeRoot32BitConstants.o_earlyHook = nullptr;
                    s_SetComputeRootConstantBufferView.o_earlyHook = nullptr;
                    s_SetComputeRootShaderResourceView.o_earlyHook = nullptr;
                    s_SetComputeRootUnorderedAccessView.o_earlyHook = nullptr;

                    LOG_WARN("Hooking RootSignature failed");
                }
            }
            else
            {
                LOG_WARN("Early hooks into RootSignature are nullptr");
            }

            commandList->Close();
            commandList->Release();
        }

        commandAllocator->Reset();
        commandAllocator->Release();
    }
}

static void UnhookAll()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (s_SetComputeRootSignature.o_earlyHook != nullptr)
    {
        DetourDetach(&(PVOID&) s_SetComputeRootSignature.o_earlyHook, hkSetComputeRootSignature);
        s_SetComputeRootSignature.o_earlyHook = nullptr;
    }

    if (s_SetGraphicsRootSignature.o_earlyHook != nullptr)
    {
        DetourDetach(&(PVOID&) s_SetGraphicsRootSignature.o_earlyHook, hkSetGraphicsRootSignature);
        s_SetGraphicsRootSignature.o_earlyHook = nullptr;
    }
}

VALIDATE_HOOK(hkD3D12CreateDevice, D3d12Proxy::PFN_D3D12CreateDevice)
static HRESULT hkD3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                   void** ppDevice)
{
    LOG_DEBUG("Adapter: {:X}, Level: {:X}, Caller: {}", (size_t) pAdapter, (UINT) MinimumFeatureLevel,
              Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX12
    LOG_WARN("Debug layers active!");
    if (debugController == nullptr && D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) == S_OK)
    {
        debugController->EnableDebugLayer();

#ifdef ENABLE_GPU_VALIDATION
        LOG_WARN("GPU Based Validation active!");
        debugController->SetEnableGPUBasedValidation(TRUE);
#endif

        debugController->Release();
    }
#endif
    IdentifyGpu::updateD3d12Capabilities(o_D3D12CreateDevice);

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    bool nonPrimaryGpu = false;
    if (pAdapter != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        if (((IDXGIAdapter*) pAdapter)->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            if (!IsEqualLUID(desc.AdapterLuid, primaryGpu.luid))
            {
                LOG_WARN("D3D12Device created with non-primary GPU");
                nonPrimaryGpu = true;
            }
        }
    }

    auto minLevel = MinimumFeatureLevel;
    if (Config::Instance()->SpoofFeatureLevel.value_or_default() && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_INFO("Forcing feature level 0xb000 for new device");
        minLevel = D3D_FEATURE_LEVEL_11_0;
    }

    if (ppDevice == nullptr)
    {
        LOG_TRACE("ppDevice is nullptr");

        _creatingD3D12Device = true;
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        auto result = o_D3D12CreateDevice(pAdapter, minLevel, riid, ppDevice);
        _creatingD3D12Device = false;

        return result;
    }

    HRESULT result;
    _creatingD3D12Device = true;
    if (desc.VendorId == VendorId::Intel)
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_D3D12CreateDevice(pAdapter, minLevel, riid, ppDevice);
    }
    else
    {
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        result = o_D3D12CreateDevice(pAdapter, minLevel, riid, ppDevice);
    }
    _creatingD3D12Device = false;

    LOG_DEBUG("o_D3D12CreateDevice result: {:X}", (UINT) result);

    if (result == S_OK && ppDevice != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE && !nonPrimaryGpu)
    {
        LOG_DEBUG("Device captured: {0:X}", (size_t) *ppDevice);
        State::Instance().currentD3D12Device = (ID3D12Device*) *ppDevice;

        if (desc.VendorId == VendorId::Intel && Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            IGDExtProxy::EnableAtomicSupport(State::Instance().currentD3D12Device);
            _intelD3D12Device = State::Instance().currentD3D12Device;
            _intelD3D12DeviceRefTarget = _intelD3D12Device->AddRef();

            if (o_D3D12DeviceRelease == nullptr)
                _intelD3D12Device->Release();
            else
                o_D3D12DeviceRelease(_intelD3D12Device);
        }

        // if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        //     UnhookDevice();

        if (State::Instance().gameQuirks & GameQuirk::CreateSLOnThe2ndDevice)
        {
            static void* lastDevice = nullptr;

            if (lastDevice && lastDevice != *ppDevice && StreamlineProxy::IsD3D12Inited() &&
                StreamlineProxy::SetD3DDevice()(*ppDevice) == sl::Result::eOk)
            {
                auto reflexConst = sl::ReflexOptions {};
                reflexConst.mode = sl::ReflexMode::eLowLatency;
                reflexConst.useMarkersToOptimize = false;

                auto result = StreamlineProxy::ReflexSetOptions()(reflexConst);
                LOG_TRACE("ReflexSetOptions");
            }

            lastDevice = *ppDevice;
        }

        HookToDevice(State::Instance().currentD3D12Device);
        _d3d12Captured = true;

        State::Instance().d3d12Devices.push_back((ID3D12Device*) *ppDevice);

#ifdef ENABLE_DEBUG_LAYER_DX12
        if (infoQueue != nullptr)
            infoQueue->Release();

        if (infoQueue1 != nullptr)
            infoQueue1->Release();

        if (State::Instance().currentD3D12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)) == S_OK)
        {
            LOG_DEBUG("infoQueue accuired");

            infoQueue->ClearRetrievalFilter();
            infoQueue->SetMuteDebugOutput(false);

            HRESULT res;
            res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            if (infoQueue->QueryInterface(IID_PPV_ARGS(&infoQueue1)) == S_OK && infoQueue1 != nullptr)
            {
                LOG_DEBUG("infoQueue1 accuired, registering MessageCallback");
                res = infoQueue1->RegisterMessageCallback(D3D12DebugCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, NULL,
                                                          NULL);
            }
        }
#endif
    }

    LOG_DEBUG("final result: {:X}", (UINT) result);
    return result;
}

VALIDATE_HOOK(hkCreateDevice, PFN_CreateDevice)
static HRESULT hkCreateDevice(ID3D12DeviceFactory* pFactory, IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel,
                              REFIID riid, void** ppDevice)
{
    LOG_DEBUG("Adapter: {:X}, Level: {:X}, Caller: {}", (size_t) pAdapter, (UINT) MinimumFeatureLevel,
              Util::WhoIsTheCaller(_ReturnAddress()));

    if (_creatingD3D12Device)
    {
        LOG_DEBUG("Calling from hkD3D12CreateDevice, calling original CreateDevice");
        return o_CreateDevice(pFactory, pAdapter, MinimumFeatureLevel, riid, ppDevice);
    }

#ifdef ENABLE_DEBUG_LAYER_DX12
    LOG_WARN("Debug layers active!");
    if (debugController == nullptr && D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) == S_OK)
    {
        debugController->EnableDebugLayer();

#ifdef ENABLE_GPU_VALIDATION
        LOG_WARN("GPU Based Validation active!");
        debugController->SetEnableGPUBasedValidation(TRUE);
#endif

        debugController->Release();
    }
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        if (((IDXGIAdapter*) pAdapter)->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            if (!IsEqualLUID(desc.AdapterLuid, primaryGpu.luid))
                LOG_WARN("D3D12Device created with non-primary GPU");
        }
    }

    auto minLevel = MinimumFeatureLevel;
    if (Config::Instance()->SpoofFeatureLevel.value_or_default() && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_INFO("Forcing feature level 0xb000 for new device");
        minLevel = D3D_FEATURE_LEVEL_11_0;
    }

    if (ppDevice == nullptr)
    {
        LOG_ERROR("ppDevice is nullptr");
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        return o_CreateDevice(pFactory, pAdapter, minLevel, riid, ppDevice);
    }

    HRESULT result;
    if (desc.VendorId == VendorId::Intel)
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        result = o_CreateDevice(pFactory, pAdapter, minLevel, riid, ppDevice);
    }
    else
    {
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        result = o_CreateDevice(pFactory, pAdapter, minLevel, riid, ppDevice);
    }

    LOG_DEBUG("o_D3D12CreateDevice result: {:X}", (UINT) result);

    if (result == S_OK && ppDevice != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_DEBUG("Device captured: {0:X}", (size_t) *ppDevice);
        State::Instance().currentD3D12Device = (ID3D12Device*) *ppDevice;

        if (desc.VendorId == VendorId::Intel && Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            IGDExtProxy::EnableAtomicSupport(State::Instance().currentD3D12Device);
            _intelD3D12Device = State::Instance().currentD3D12Device;
            _intelD3D12DeviceRefTarget = _intelD3D12Device->AddRef();

            if (o_D3D12DeviceRelease == nullptr)
                _intelD3D12Device->Release();
            else
                o_D3D12DeviceRelease(_intelD3D12Device);
        }

        HookToDevice(State::Instance().currentD3D12Device);
        _d3d12Captured = true;

        State::Instance().d3d12Devices.push_back((ID3D12Device*) *ppDevice);

#ifdef ENABLE_DEBUG_LAYER_DX12
        if (infoQueue != nullptr)
            infoQueue->Release();

        if (infoQueue1 != nullptr)
            infoQueue1->Release();

        if (State::Instance().currentD3D12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)) == S_OK)
        {
            LOG_DEBUG("infoQueue accuired");

            infoQueue->ClearRetrievalFilter();
            infoQueue->SetMuteDebugOutput(false);

            HRESULT res;
            res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            if (infoQueue->QueryInterface(IID_PPV_ARGS(&infoQueue1)) == S_OK && infoQueue1 != nullptr)
            {
                LOG_DEBUG("infoQueue1 accuired, registering MessageCallback");
                res = infoQueue1->RegisterMessageCallback(D3D12DebugCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                                                          NULL, NULL);
            }
        }
#endif
    }

    LOG_DEBUG("final result: {:X}", (UINT) result);
    return result;
}

VALIDATE_HOOK(hkD3D12SerializeRootSignature, D3d12Proxy::PFN_D3D12SerializeRootSignature)
static HRESULT hkD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
                                             D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob,
                                             ID3DBlob** ppErrorBlob)
{
    if (pRootSignature == nullptr)
        return o_D3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);

    auto localRootSignature = *pRootSignature;
    std::vector<D3D12_STATIC_SAMPLER_DESC> localSamplers;

    if (pRootSignature->NumStaticSamplers > 0)
    {
        localSamplers.assign(pRootSignature->pStaticSamplers,
                             pRootSignature->pStaticSamplers + pRootSignature->NumStaticSamplers);
    }

    for (auto& sampler : localSamplers)
    {
        ApplySamplerOverrides(sampler);
    }

    localRootSignature.pStaticSamplers = localSamplers.data();

    return o_D3D12SerializeRootSignature(&localRootSignature, Version, ppBlob, ppErrorBlob);
}

VALIDATE_HOOK(hkD3D12SerializeVersionedRootSignature, D3d12Proxy::PFN_D3D12SerializeVersionedRootSignature)
static HRESULT hkD3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
                                                      ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob)
{
    if (pRootSignature == nullptr)
        return o_D3D12SerializeVersionedRootSignature(pRootSignature, ppBlob, ppErrorBlob);

    auto localVersionedRootSignature = *pRootSignature;
    std::vector<D3D12_STATIC_SAMPLER_DESC> localSamplers;
    std::vector<D3D12_STATIC_SAMPLER_DESC1> localSamplers1;

    if (pRootSignature->Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        if (pRootSignature->Desc_1_0.NumStaticSamplers > 0)
        {
            localSamplers.assign(pRootSignature->Desc_1_0.pStaticSamplers,
                                 pRootSignature->Desc_1_0.pStaticSamplers + pRootSignature->Desc_1_0.NumStaticSamplers);
        }

        for (auto& sampler : localSamplers)
        {
            ApplySamplerOverrides(sampler);
        }

        localVersionedRootSignature.Desc_1_0.pStaticSamplers = localSamplers.data();
    }
    else if (pRootSignature->Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        if (pRootSignature->Desc_1_1.NumStaticSamplers > 0)
        {
            localSamplers.assign(pRootSignature->Desc_1_1.pStaticSamplers,
                                 pRootSignature->Desc_1_1.pStaticSamplers + pRootSignature->Desc_1_1.NumStaticSamplers);
        }

        for (auto& sampler : localSamplers)
        {
            ApplySamplerOverrides(sampler);
        }

        localVersionedRootSignature.Desc_1_1.pStaticSamplers = localSamplers.data();
    }
    else if (pRootSignature->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        if (pRootSignature->Desc_1_2.NumStaticSamplers > 0)
        {
            localSamplers1.assign(pRootSignature->Desc_1_2.pStaticSamplers,
                                  pRootSignature->Desc_1_2.pStaticSamplers +
                                      pRootSignature->Desc_1_2.NumStaticSamplers);
        }

        for (auto& sampler : localSamplers1)
        {
            ApplySamplerOverrides(sampler);
        }

        localVersionedRootSignature.Desc_1_2.pStaticSamplers = localSamplers1.data();
    }

    auto result = o_D3D12SerializeVersionedRootSignature(&localVersionedRootSignature, ppBlob, ppErrorBlob);

    return result;
}

VALIDATE_HOOK(hkD3D12DeviceRelease, PFN_Release)
static ULONG hkD3D12DeviceRelease(IUnknown* device)
{
    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && device == _intelD3D12Device)
    {
        auto refCount = device->AddRef();

        if (refCount == _intelD3D12DeviceRefTarget)
        {
            LOG_INFO("Destroying IGDExt context!");
            _intelD3D12Device = nullptr;
            IGDExtProxy::DestroyContext();
        }

        o_D3D12DeviceRelease(device);
    }
    else if (State::Instance().currentD3D12Device == device)
    {
        device->AddRef();
        auto refCount = o_D3D12DeviceRelease(device);

        if (refCount == 1)
        {
            LOG_DEBUG("Set State::Instance().currentD3D12Device = nullptr, was: {:X}", (size_t) device);
            State::Instance().currentD3D12Device = nullptr;
        }
    }

    auto result = o_D3D12DeviceRelease(device);
    return result;
}

VALIDATE_HOOK(hkCheckFeatureSupport, PFN_CheckFeatureSupport)
static HRESULT hkCheckFeatureSupport(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                     UINT FeatureSupportDataSize)
{
    auto result = o_CheckFeatureSupport(device, Feature, pFeatureSupportData, FeatureSupportDataSize);

    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && Feature == D3D12_FEATURE_D3D12_OPTIONS9 &&
        device == State::Instance().currentD3D12Device)
    {
        auto featureSupport = (D3D12_FEATURE_DATA_D3D12_OPTIONS9*) pFeatureSupportData;
        LOG_INFO("Spoofing AtomicInt64OnTypedResourceSupported {} -> 1",
                 featureSupport->AtomicInt64OnTypedResourceSupported);

        featureSupport->AtomicInt64OnTypedResourceSupported = 1;
    }

    return result;
}

VALIDATE_HOOK(hkCreateCommittedResource, PFN_CreateCommittedResource)
static HRESULT hkCreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                         D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc,
                                         D3D12_RESOURCE_STATES InitialResourceState,
                                         const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                         void** ppvResource)
{
    if (!_skipCommitedResource)
    {
        D3D12_RESOURCE_DESC localDesc = {};
        memcpy(&localDesc, pDesc, sizeof(D3D12_RESOURCE_DESC));
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(&pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            _skipCommitedResource = true;
            auto result =
                IGDExtProxy::CreateCommitedResource(pHeapProperties, HeapFlags, &localDesc, InitialResourceState,
                                                    pOptimizedClearValue, riidResource, ppvResource);

            LOG_DEBUG("IGDExtProxy::hkCreateCommittedResource result: {:X}", (UINT) result);
            _skipCommitedResource = false;

            return result;
        }
    }

    return o_CreateCommittedResource(device, pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                     pOptimizedClearValue, riidResource, ppvResource);
}

static bool skipPlacedResource = false;

VALIDATE_HOOK(hkCreatePlacedResource, PFN_CreatePlacedResource)
static HRESULT hkCreatePlacedResource(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                      const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource)
{
    if (!skipPlacedResource)
    {
        D3D12_RESOURCE_DESC localDesc = {};
        memcpy(&localDesc, pDesc, sizeof(D3D12_RESOURCE_DESC));
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(&pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipPlacedResource = true;
            auto result = IGDExtProxy::CreatePlacedResource(pHeap, HeapOffset, &localDesc, InitialState,
                                                            pOptimizedClearValue, riid, ppvResource);
            LOG_DEBUG("IGDExtProxy::hkCreatePlacedResource result: {:X}", (UINT) result);
            skipPlacedResource = false;

            return result;
        }
    }

    return o_CreatePlacedResource(device, pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid,
                                  ppvResource);
}

VALIDATE_HOOK(hkSetResidencyPriority, PFN_SetResidencyPriority)
static HRESULT hkSetResidencyPriority(ID3D12Device1* This, UINT NumObjects, ID3D12Pageable* const* ppObjects,
                                      const D3D12_RESIDENCY_PRIORITY* pPriorities)
{
    auto result = o_SetResidencyPriority(This, NumObjects, ppObjects, pPriorities);

    // HACK: AMD Windows 25.20 drivers fail in xess/xefg with E_INVALIDARG
    // This hack allows them to work without the priority being actually set
    if (FAILED(result))
    {
        auto callerModule = Util::GetCallerModule(_ReturnAddress());
        auto xefgModule = XeFGProxy::Module();
        auto xessModule = XeSSProxy::Module();

        if (callerModule == xefgModule || callerModule == xessModule)
        {
            LOG_WARN("SetResidencyPriority failed, faking success for xess/xefg");
            result = S_OK;
        }
    }

    return result;
}

/*
The Golden Rule of x64 Struct Returns
If a Windows x64 function returns a struct larger than 8 bytes (and isn't a vector intrinsic):

Input: The caller allocates stack memory and passes a pointer to it as a hidden argument.

Static Function: RCX = Hidden Ptr, RDX = Arg1

Member Function: RCX = this, RDX = Hidden Ptr, R8 = Arg1

Output: The function must return that same hidden pointer in RAX.

Why Agility SDK crashed but legacy didn't: The Agility SDK is compiled with newer MSVC optimizations that strictly
enforce the "Return in RAX" rule for chained calls. The legacy DLL likely had some wiggle room or didn't immediately
dereference RAX after the call.
*/
// Not using validation because of this hooks special case
static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE
hkGetResourceAllocationInfo(ID3D12Device* device, D3D12_RESOURCE_ALLOCATION_INFO* pResult, UINT visibleMask,
                            UINT numResourceDescs, D3D12_RESOURCE_DESC* pResourceDescs)
{
    if (!_skipGetResourceAllocationInfo)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pResourceDescs);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            _skipGetResourceAllocationInfo = true;
            auto result = IGDExtProxy::GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
            LOG_DEBUG("IGDExtProxy::GetResourceAllocationInfo result: SizeInBytes={}", result.SizeInBytes);
            _skipGetResourceAllocationInfo = false;
            *pResult = result;
            return pResult;
        }
    }

    pResult->Alignment = 0;
    pResult->SizeInBytes = 0;
    o_GetResourceAllocationInfo(device, pResult, visibleMask, numResourceDescs, pResourceDescs);
    return pResult;
}

VALIDATE_HOOK(hkCreateSampler, PFN_CreateSampler)
static void hkCreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pDesc == nullptr || device == nullptr)
        return;

    D3D12_SAMPLER_DESC newDesc = *pDesc;

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", pDesc->MaxAnisotropy,
                  Config::Instance()->AnisotropyOverride.value(), (UINT) newDesc.Filter);

        newDesc.Filter = UpgradeToAF(pDesc->Filter);
        newDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
    }
    else
    {
        newDesc.Filter = pDesc->Filter;
        newDesc.MaxAnisotropy = pDesc->MaxAnisotropy;
    }

    if ((newDesc.MipLODBias < 0.0f && newDesc.MinLOD != newDesc.MaxLOD) ||
        Config::Instance()->MipmapBiasOverrideAll.value_or_default())
    {
        if (Config::Instance()->MipmapBiasOverride.has_value())
        {
            LOG_DEBUG("Overriding mipmap bias {0} -> {1}", pDesc->MipLODBias,
                      Config::Instance()->MipmapBiasOverride.value());

            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                newDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                newDesc.MipLODBias = newDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
            else
                newDesc.MipLODBias = newDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
        }

        if (State::Instance().lastMipBiasMax < newDesc.MipLODBias)
            State::Instance().lastMipBiasMax = newDesc.MipLODBias;

        if (State::Instance().lastMipBias > newDesc.MipLODBias)
            State::Instance().lastMipBias = newDesc.MipLODBias;
    }

    return o_CreateSampler(device, &newDesc, DestDescriptor);
}

VALIDATE_HOOK(hkCreateRootSignature, PFN_CreateRootSignature)
static HRESULT hkCreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* pBlobWithRootSignature,
                                     SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature)
{
    if (!Config::Instance()->MipmapBiasOverride.has_value() && !Config::Instance()->AnisotropyOverride.has_value() &&
        !Config::Instance()->ExtendedStateRestore.value_or_default())
    {
        return o_CreateRootSignature(device, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid,
                                     ppvRootSignature);
    }

    ID3D12VersionedRootSignatureDeserializer* deserializer = nullptr;
    auto result = D3d12Proxy::D3D12CreateVersionedRootSignatureDeserializer_()(
        pBlobWithRootSignature, blobLengthInBytes, IID_PPV_ARGS(&deserializer));

    // Deserialize the blob
    if (FAILED(result))
    {
        LOG_ERROR("Failed to create deserializer, error: {:X}", (UINT) result);
        return o_CreateRootSignature(device, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid,
                                     ppvRootSignature);
    }

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = deserializer->GetUnconvertedRootSignatureDesc();

    // Only ExtendedStateRestore is set, return early
    if (!Config::Instance()->MipmapBiasOverride.has_value() && !Config::Instance()->AnisotropyOverride.has_value())
    {
        auto result =
            o_CreateRootSignature(device, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);

        if (SUCCEEDED(result))
        {
            std::unique_lock<std::shared_mutex> lock(rootSigParameterCountMutex);
            if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
            {
                rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                       desc->Desc_1_0.NumParameters);
            }
            else if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
            {
                rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                       desc->Desc_1_1.NumParameters);
            }
            else if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
            {
                rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                       desc->Desc_1_2.NumParameters);
            }
        }

        deserializer->Release();
        return result;
    }

    // Create a modifiable copy
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC descCopy {};
    std::memcpy(&descCopy, desc, sizeof(D3D12_VERSIONED_ROOT_SIGNATURE_DESC));

    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    std::vector<D3D12_STATIC_SAMPLER_DESC1> samplers1;

    // Modify Samplers based on Version
    if (descCopy.Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        {
            std::unique_lock<std::shared_mutex> lock(rootSigParameterCountMutex);
            rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                   desc->Desc_1_0.NumParameters);
        }

        if (descCopy.Desc_1_0.NumStaticSamplers > 0)
        {
            samplers.assign(descCopy.Desc_1_0.pStaticSamplers,
                            descCopy.Desc_1_0.pStaticSamplers + descCopy.Desc_1_0.NumStaticSamplers);

            for (auto& s : samplers)
                ApplySamplerOverrides(s);

            descCopy.Desc_1_0.pStaticSamplers = samplers.data();
        }
    }
    else if (descCopy.Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        {
            std::unique_lock<std::shared_mutex> lock(rootSigParameterCountMutex);
            rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                   desc->Desc_1_1.NumParameters);
        }

        if (descCopy.Desc_1_1.NumStaticSamplers > 0)
        {
            samplers.assign(descCopy.Desc_1_1.pStaticSamplers,
                            descCopy.Desc_1_1.pStaticSamplers + descCopy.Desc_1_1.NumStaticSamplers);

            for (auto& s : samplers)
                ApplySamplerOverrides(s);

            descCopy.Desc_1_1.pStaticSamplers = samplers.data();
        }
    }
    else if (descCopy.Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        {
            std::unique_lock<std::shared_mutex> lock(rootSigParameterCountMutex);
            rootSigParameterCount.insert_or_assign((ID3D12RootSignature*) *ppvRootSignature,
                                                   desc->Desc_1_2.NumParameters);
        }

        if (descCopy.Desc_1_2.NumStaticSamplers > 0)
        {
            samplers1.assign(descCopy.Desc_1_2.pStaticSamplers,
                             descCopy.Desc_1_2.pStaticSamplers + descCopy.Desc_1_2.NumStaticSamplers);

            for (auto& s : samplers1)
                ApplySamplerOverrides(s);

            descCopy.Desc_1_2.pStaticSamplers = samplers1.data();
        }
    }

    ID3DBlob* newBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    result = S_OK;

    // Reserialize
    result = o_D3D12SerializeVersionedRootSignature(&descCopy, &newBlob, &errorBlob);

    if (SUCCEEDED(result))
    {
        result = o_CreateRootSignature(device, nodeMask, newBlob->GetBufferPointer(), newBlob->GetBufferSize(), riid,
                                       ppvRootSignature);
        newBlob->Release();

        if (errorBlob)
            errorBlob->Release();
    }
    else
    {
        LOG_ERROR("Failed to reserialize modified RootSig, error: {:X}", (UINT) result);

        if (errorBlob)
        {
            LOG_ERROR("RootSig Serialization Failed: {}", (char*) errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        // Fallback to original blob
        result =
            o_CreateRootSignature(device, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
    }

    deserializer->Release();
    return result;
}

VALIDATE_HOOK(hkD3D12GetInterface, PFN_D3D12GetInterface)
static HRESULT hkD3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvDebug)
{
    LOG_DEBUG("D3D12GetInterface called: {:X}, {:X}, Caller: {}", (size_t) &rclsid, (size_t) &riid,
              Util::WhoIsTheCaller(_ReturnAddress()));

    auto result = o_D3D12GetInterface(rclsid, riid, ppvDebug);

    if (rclsid == CLSID_D3D12DeviceFactory && o_CreateDevice == nullptr)
    {
        auto deviceFactory = (ID3D12DeviceFactory*) *ppvDebug;

        PVOID* pVTable = *(PVOID**) deviceFactory;

        o_CreateDevice = (PFN_CreateDevice) pVTable[9];

        if (o_CreateDevice != nullptr)
        {
            LOG_DEBUG("Detouring ID3D12DeviceFactory::CreateDevice");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_CreateDevice, hkCreateDevice);
            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to detour ID3D12DeviceFactory::CreateDevice, error: {:X}", detourResult);
                o_CreateDevice = nullptr;
            }
        }
    }

    return result;
}

static void HookToDevice(ID3D12Device* InDevice)
{
    if (o_CreateSampler != nullptr || InDevice == nullptr)
        return;

    LOG_DEBUG("Dx12");

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) InDevice;

    ID3D12Device* realDevice = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, InDevice, (IUnknown**) &realDevice))
        pVTable = *(PVOID**) realDevice;

    // hudless
    o_D3D12DeviceRelease = (PFN_Release) pVTable[2];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CheckFeatureSupport = (PFN_CheckFeatureSupport) pVTable[13];
    o_CreateRootSignature = (PFN_CreateRootSignature) pVTable[16];
    o_GetResourceAllocationInfo = (PFN_GetResourceAllocationInfo) pVTable[25];
    o_CreateCommittedResource = (PFN_CreateCommittedResource) pVTable[27];
    o_CreatePlacedResource = (PFN_CreatePlacedResource) pVTable[29];

    ID3D12Device1* device12_1 = nullptr;
    if (realDevice)
        realDevice->QueryInterface(IID_PPV_ARGS(&device12_1));
    else
        InDevice->QueryInterface(IID_PPV_ARGS(&device12_1));

    if (device12_1 /*&& isAMD*/)
    {
        PVOID* pVTable = *(PVOID**) device12_1;
        o_SetResidencyPriority = (PFN_SetResidencyPriority) pVTable[46];
        device12_1->Release();
    }

    // Apply the detour
    if (o_CreateSampler != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_CreateSampler != nullptr)
            DetourAttach(&(PVOID&) o_CreateSampler, hkCreateSampler);

        if (o_CreateRootSignature != nullptr)
            DetourAttach(&(PVOID&) o_CreateRootSignature, hkCreateRootSignature);

        // Will be used for tracking current d3d12 device too
        if (o_D3D12DeviceRelease != nullptr)
            DetourAttach(&(PVOID&) o_D3D12DeviceRelease, hkD3D12DeviceRelease);

        // HACK: see reason in hkSetResidencyPriority
        if (o_SetResidencyPriority != nullptr)
            DetourAttach(&(PVOID&) o_SetResidencyPriority, hkSetResidencyPriority);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            LOG_DEBUG("UE spoofing for Intel Atomics64 enabled, applying detours");

            if (o_CheckFeatureSupport != nullptr)
                DetourAttach(&(PVOID&) o_CheckFeatureSupport, hkCheckFeatureSupport);

            if (o_CreateCommittedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreateCommittedResource, hkCreateCommittedResource);

            if (o_CreatePlacedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreatePlacedResource, hkCreatePlacedResource);

            if (o_GetResourceAllocationInfo != nullptr)
                DetourAttach(&(PVOID&) o_GetResourceAllocationInfo, hkGetResourceAllocationInfo);
        }

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to detour ID3D12Device methods, error: {:X}", detourResult);
            o_CreateSampler = nullptr;
            o_CheckFeatureSupport = nullptr;
            o_CreateRootSignature = nullptr;
            o_CreateCommittedResource = nullptr;
            o_CreatePlacedResource = nullptr;
            o_D3D12DeviceRelease = nullptr;
            o_GetResourceAllocationInfo = nullptr;
        }
    }

    HookToCommandList(InDevice);

    if (State::Instance().activeFgInput == FGInput::Upscaler &&
        !Config::Instance()->FGDisableHUDFix.value_or_default() &&
        State::Instance().swapchainInteropApi == SwapchainInteropApi::None)
    {
        ResTrack_Dx12::HookDevice(InDevice);
    }

    if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::LoadStreamline())
    {
        StreamlineProxy::InitWithD3D12(InDevice);
    }
}

static void UnhookDevice()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_CreateSampler != nullptr)
        DetourDetach(&(PVOID&) o_CreateSampler, hkCreateSampler);

    if (o_CreateRootSignature != nullptr)
        DetourDetach(&(PVOID&) o_CreateRootSignature, hkCreateRootSignature);

    if (o_CheckFeatureSupport != nullptr)
        DetourDetach(&(PVOID&) o_CheckFeatureSupport, hkCheckFeatureSupport);

    if (o_CreateCommittedResource != nullptr)
        DetourDetach(&(PVOID&) o_CreateCommittedResource, hkCreateCommittedResource);

    if (o_CreatePlacedResource != nullptr)
        DetourDetach(&(PVOID&) o_CreatePlacedResource, hkCreatePlacedResource);

    if (o_D3D12DeviceRelease != nullptr)
        DetourDetach(&(PVOID&) o_D3D12DeviceRelease, hkD3D12DeviceRelease);

    if (o_GetResourceAllocationInfo != nullptr)
        DetourDetach(&(PVOID&) o_GetResourceAllocationInfo, hkGetResourceAllocationInfo);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook ID3D12Device methods, error: {:X}", detourResult);
    }
    else
    {
        o_CreateSampler = nullptr;
        o_CheckFeatureSupport = nullptr;
        o_CreateCommittedResource = nullptr;
        o_CreatePlacedResource = nullptr;
        o_D3D12DeviceRelease = nullptr;
        o_GetResourceAllocationInfo = nullptr;
    }

    ResTrack_Dx12::ReleaseDeviceHooks();
}

void D3D12Hooks::Hook()
{
    std::lock_guard<std::mutex> lock(hookMutex);

    LOG_DEBUG("");

    if (o_D3D12CreateDevice == nullptr)
        o_D3D12CreateDevice = D3d12Proxy::Hook_D3D12CreateDevice(hkD3D12CreateDevice);

    if (o_D3D12SerializeRootSignature == nullptr)
        o_D3D12SerializeRootSignature = D3d12Proxy::Hook_D3D12SerializeRootSignature(hkD3D12SerializeRootSignature);

    if (o_D3D12SerializeVersionedRootSignature == nullptr)
        o_D3D12SerializeVersionedRootSignature =
            D3d12Proxy::Hook_D3D12SerializeVersionedRootSignature(hkD3D12SerializeVersionedRootSignature);
}

void D3D12Hooks::HookAgility(HMODULE module)
{
    std::lock_guard<std::mutex> lock(agilityMutex);

    if (module == nullptr || o_D3D12GetInterface != nullptr)
        return;

    LOG_DEBUG("Hooking D3D12GetInterface from D3D12 Agility SDK");

    o_D3D12GetInterface = (PFN_D3D12GetInterface) KernelBaseProxy::GetProcAddress_()(module, "D3D12GetInterface");

    if (o_D3D12GetInterface != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) o_D3D12GetInterface, hkD3D12GetInterface);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to detour D3D12GetInterface, error: {:X}", detourResult);
            o_D3D12GetInterface = nullptr;
        }
    }
}

void D3D12Hooks::Unhook()
{
    if (o_D3D12CreateDevice == nullptr)
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_CreateSampler != nullptr)
        DetourDetach(&(PVOID&) o_CreateSampler, hkCreateSampler);

    if (o_CheckFeatureSupport != nullptr)
        DetourDetach(&(PVOID&) o_CheckFeatureSupport, hkCheckFeatureSupport);

    if (o_CreateCommittedResource != nullptr)
        DetourDetach(&(PVOID&) o_CreateCommittedResource, hkCreateCommittedResource);

    if (o_CreatePlacedResource != nullptr)
        DetourDetach(&(PVOID&) o_CreatePlacedResource, hkCreatePlacedResource);

    if (o_GetResourceAllocationInfo != nullptr)
        DetourDetach(&(PVOID&) o_GetResourceAllocationInfo, hkGetResourceAllocationInfo);

    if (o_D3D12DeviceRelease != nullptr)
        DetourDetach(&(PVOID&) o_D3D12DeviceRelease, hkD3D12DeviceRelease);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook ID3D12Device methods, error: {:X}", detourResult);
    }
    else
    {
        o_CreateSampler = nullptr;
        o_CheckFeatureSupport = nullptr;
        o_CreateCommittedResource = nullptr;
        o_CreatePlacedResource = nullptr;
        o_D3D12DeviceRelease = nullptr;
        o_GetResourceAllocationInfo = nullptr;
    }
}

void D3D12Hooks::SetRootSignatureTracking(bool enable) { isUpscalerActive = !enable; }

bool D3D12Hooks::CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList)
{
    std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);
    return signatures.contains(cmdList);
}

bool D3D12Hooks::RestoreDescriptorHeaps(ID3D12GraphicsCommandList* cmdList)
{
    std::unique_lock<std::shared_mutex> lock(descriptorHeapsMutex);
    if (descriptorHeaps.contains(cmdList))
    {
        auto& heaps = descriptorHeaps[cmdList];

        if (heaps.NumDescriptorHeaps > 0 && heaps.Heaps[0] != nullptr)
        {
            if (auto hook = s_SetDescriptorHeaps.GetHook())
            {
                hook(cmdList, heaps.NumDescriptorHeaps, heaps.Heaps);
            }
            else
            {
                LOG_ERROR("Couldn't restore DescriptorHeaps, no original SetDescriptorHeaps");
                return false;
            }
        }

        return true;
    }

    return false;
}

bool D3D12Hooks::RestorePipelineState(ID3D12GraphicsCommandList* cmdList)
{
    std::unique_lock<std::shared_mutex> lock(pipelineStatesMutex);
    if (pipelineStates.contains(cmdList))
    {
        auto& pipelineState = pipelineStates[cmdList];

        if (auto hook = s_SetPipelineState.GetHook())
        {
            hook(cmdList, pipelineState);
        }
        else
        {
            LOG_ERROR("Couldn't restore PipelineState, no original SetPipelineState");
            return false;
        }

        return true;
    }

    return false;
}

bool D3D12Hooks::RestoreComputeRootState(ID3D12GraphicsCommandList* cmdList)
{
    std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
    if (rootStates.contains(cmdList))
    {
        auto& table = rootStates[cmdList];

        for (uint32_t i = 0; i < table.size(); i++)
        {
            if (table[i].type == RootEntryType::Table)
            {
                if (auto hook = s_SetComputeRootDescriptorTable.GetHook())
                    hook(cmdList, i, table[i].rootDescriptorTable);
                else
                    LOG_ERROR("Couldn't restore ComputeRootDescriptorTable, no original SetComputeRootDescriptorTable");
            }
            else if (table[i].type == RootEntryType::Constant)
            {
                if (auto hook = s_SetComputeRoot32BitConstant.GetHook())
                    hook(cmdList, i, table[i].Data[0], table[i].DestOffset);
                else
                    LOG_ERROR("Couldn't restore ComputeRoot32BitConstant, no original SetComputeRoot32BitConstant");
            }
            else if (table[i].type == RootEntryType::Constants)
            {
                if (auto hook = s_SetComputeRoot32BitConstants.GetHook())
                {
                    hook(cmdList, i, table[i].Num32BitValues, table[i].Data.data(), table[i].DestOffset);
                }
                else
                {
                    LOG_ERROR("Couldn't restore ComputeRoot32BitConstants, no original SetComputeRoot32BitConstants");
                }
            }
            else if (table[i].type == RootEntryType::CBV)
            {
                if (auto hook = s_SetComputeRootConstantBufferView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore ComputeRoot CBV, no original SetComputeRootConstantBufferView");
            }
            else if (table[i].type == RootEntryType::SRV)
            {
                if (auto hook = s_SetComputeRootShaderResourceView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore ComputeRoot SRV, no original SetComputeRootShaderResourceView");
            }
            else if (table[i].type == RootEntryType::UAV)
            {
                if (auto hook = s_SetComputeRootUnorderedAccessView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore ComputeRoot UAV, no original SetComputeRootUnorderedAccessView");
            }
            else if (table[i].type == RootEntryType::Invalid)
            {
                LOG_WARN("Can't restore index: {} for CmdList: {:X}", i, (UINT64) cmdList);
            }
        }

        return true;
    }

    return false;
}

bool D3D12Hooks::RestoreGraphicsRootState(ID3D12GraphicsCommandList* cmdList)
{
    std::unique_lock<std::shared_mutex> lock(rootStatesMutex);
    if (rootStates.contains(cmdList))
    {
        auto& table = rootStates[cmdList];

        for (uint32_t i = 0; i < table.size(); i++)
        {
            if (table[i].type == RootEntryType::Table)
            {
                if (auto hook = s_SetGraphicsRootDescriptorTable.GetHook())
                {
                    hook(cmdList, i, table[i].rootDescriptorTable);
                }
                else
                {
                    LOG_ERROR(
                        "Couldn't restore GraphicsRootDescriptorTable, no original SetGraphicsRootDescriptorTable");
                }
            }
            else if (table[i].type == RootEntryType::Constant)
            {
                if (auto hook = s_SetGraphicsRoot32BitConstant.GetHook())
                    hook(cmdList, i, table[i].Data[0], table[i].DestOffset);
                else
                    LOG_ERROR("Couldn't restore GraphicsRoot32BitConstant, no original SetGraphicsRoot32BitConstant");
            }
            else if (table[i].type == RootEntryType::Constants)
            {
                if (auto hook = s_SetGraphicsRoot32BitConstants.GetHook())
                    hook(cmdList, i, table[i].Num32BitValues, table[i].Data.data(), table[i].DestOffset);
                else
                    LOG_ERROR("Couldn't restore GraphicsRoot32BitConstants, no original SetGraphicsRoot32BitConstants");
            }
            else if (table[i].type == RootEntryType::CBV)
            {
                if (auto hook = s_SetGraphicsRootConstantBufferView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore GraphicsRoot CBV, no original SetGraphicsRootConstantBufferView");
            }
            else if (table[i].type == RootEntryType::SRV)
            {
                if (auto hook = s_SetGraphicsRootShaderResourceView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore GraphicsRoot SRV, no original SetGraphicsRootShaderResourceView");
            }
            else if (table[i].type == RootEntryType::UAV)
            {
                if (auto hook = s_SetGraphicsRootUnorderedAccessView.GetHook())
                    hook(cmdList, i, table[i].bufferLocation);
                else
                    LOG_ERROR("Couldn't restore GraphicsRoot UAV, no original SetGraphicsRootUnorderedAccessView");
            }
            else if (table[i].type == RootEntryType::Invalid)
            {
                LOG_WARN("Can't restore index: {} for CmdList: {:X}", i, (UINT64) cmdList);
            }
        }

        return true;
    }

    return false;
}

void D3D12Hooks::RestoreRoot(ID3D12GraphicsCommandList* cmdList)
{
    const bool restoreComputeSignature = Config::Instance()->RestoreComputeSignature.value_or_default();
    const bool restoreGraphicSignature = Config::Instance()->RestoreGraphicSignature.value_or_default();

    if (restoreComputeSignature || restoreGraphicSignature)
    {
        std::unique_lock<std::shared_mutex> lock(rootSignatureMutex);

        if (signatures.contains(cmdList))
        {
            auto& signature = signatures[cmdList];
            const bool extendedRestoreSignature = Config::Instance()->ExtendedStateRestore.value_or_default();

            if (extendedRestoreSignature)
            {
                if (RestoreDescriptorHeaps(cmdList))
                    LOG_TRACE("Restored DescriptorHeaps for CmdList: {:X}", (UINT64) cmdList);
                else
                    LOG_WARN("Can't restore DescriptorHeaps for CmdList: {:X}", (UINT64) cmdList);
            }

            if (signature.type == SignatureEntryType::Compute)
            {
                LOG_TRACE("Restore ComputeRootSig: {:X}, for CmdList: {:X}", (UINT64) signature.ptr, (UINT64) cmdList);

                if (auto hook = s_SetComputeRootSignature.GetHook())
                    hook(cmdList, signature.ptr);
                else
                    LOG_ERROR("Couldn't restore Compute RootSignature, no original SetComputeRootSignature");
            }
            else if (signature.type == SignatureEntryType::Graphics)
            {
                LOG_TRACE("Restore GraphicsRootSig: {:X}, for CmdList: {:X}", (UINT64) signature.ptr, (UINT64) cmdList);

                if (auto hook = s_SetGraphicsRootSignature.GetHook())
                    hook(cmdList, signature.ptr);
                else
                    LOG_ERROR("Couldn't restore Graphics RootSignature, no original SetGraphicsRootSignature");
            }

            if (extendedRestoreSignature)
            {
                if (signature.type == SignatureEntryType::Compute)
                {
                    if (RestoreComputeRootState(cmdList))
                        LOG_TRACE("Restored ComputeRootState for CmdList: {:X}", (UINT64) cmdList);
                    else
                        LOG_WARN("Can't restore ComputeRootState for CmdList: {:X}", (UINT64) cmdList);
                }
                else if (signature.type == SignatureEntryType::Graphics)
                {
                    if (RestoreGraphicsRootState(cmdList))
                        LOG_TRACE("Restored GraphicsRootState for CmdList: {:X}", (UINT64) cmdList);
                    else
                        LOG_WARN("Can't restore GraphicsRootState for CmdList: {:X}", (UINT64) cmdList);
                }

                if (RestorePipelineState(cmdList))
                    LOG_TRACE("Restored PipelineState for CmdList: {:X}", (UINT64) cmdList);
                else
                    LOG_TRACE("Can't restore PipelineState for CmdList: {:X}", (UINT64) cmdList);
            }
        }
        else
        {
            LOG_TRACE("Can't restore Root Signature for CmdList: {:X}", (UINT64) cmdList);
        }
    }
}

void D3D12Hooks::HookDevice(ID3D12Device* device) { HookToDevice(device); }
