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
#include <hooks/Reflex_Hooks.h>

#include <detours/detours.h>

#include <dxgi1_6.h>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"

#include <algorithm>
#include <array>
#include <memory>
#include <type_traits>

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

using PFN_Reset = rewrite_signature<decltype(&ID3D12GraphicsCommandList::Reset)>::type;

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

// ID3D12GraphicsCommandList vtable indices
namespace CmdListVTable
{
constexpr int Reset = 10;
constexpr int SetPipelineState = 25;
constexpr int SetDescriptorHeaps = 28;
constexpr int SetComputeRootSignature = 29;
constexpr int SetGraphicsRootSignature = 30;
constexpr int SetComputeRootDescriptorTable = 31;
constexpr int SetGraphicsRootDescriptorTable = 32;
constexpr int SetComputeRoot32BitConstant = 33;
constexpr int SetGraphicsRoot32BitConstant = 34;
constexpr int SetComputeRoot32BitConstants = 35;
constexpr int SetGraphicsRoot32BitConstants = 36;
constexpr int SetComputeRootConstantBufferView = 37;
constexpr int SetGraphicsRootConstantBufferView = 38;
constexpr int SetComputeRootShaderResourceView = 39;
constexpr int SetGraphicsRootShaderResourceView = 40;
constexpr int SetComputeRootUnorderedAccessView = 41;
constexpr int SetGraphicsRootUnorderedAccessView = 42;
} // namespace CmdListVTable

template <typename T> struct RootRestoreHook
{
    T o_earlyHook = nullptr;
    T o_lateHook = nullptr;

    T GetHook() const { return o_lateHook ? o_lateHook : o_earlyHook; };
};

// D3D12 limits: a root signature is at most 64 DWORDs, so there can't be more than 64 parameters
// or more than 64 root constants in one parameter. At most one CBV_SRV_UAV and one SAMPLER heap.
static constexpr UINT kMaxRootParameters = 64;
static constexpr UINT kMaxRootConstants = 64;
static constexpr UINT kMaxDescriptorHeaps = 2;

enum class Pipeline
{
    Compute,
    Graphics,
};

enum class RootEntryType
{
    Invalid,
    Table,
    Constants,
    CBV,
    SRV,
    UAV,
};

struct RootParamInfo
{
    D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    UINT num32BitValues = 0;
};

struct RootSigLayout
{
    std::vector<RootParamInfo> params;
};

struct RootArg
{
    RootEntryType type = RootEntryType::Invalid;

    // Table
    D3D12_GPU_DESCRIPTOR_HANDLE table {};

    // CBV / SRV / UAV
    D3D12_GPU_VIRTUAL_ADDRESS buffer {};

    // Constants, shadowed per DWORD. Bit N of constantsMask = DWORD N has been set.
    std::array<uint32_t, kMaxRootConstants> constants {};
    uint64_t constantsMask = 0;
};

struct RootBindings
{
    ID3D12RootSignature* signature = nullptr;
    std::shared_ptr<const RootSigLayout> layout; // nullptr if the signature wasn't created through our hook
    std::vector<RootArg> args;

    void Bind(ID3D12RootSignature* newSignature, std::shared_ptr<const RootSigLayout> newLayout)
    {
        signature = newSignature;
        layout = std::move(newLayout);
        args.assign(layout ? layout->params.size() : 0, RootArg {});
    }

    void Clear()
    {
        signature = nullptr;
        layout.reset();
        args.clear();
    }
};

struct CommandListState
{
    UINT numDescriptorHeaps = 0;
    ID3D12DescriptorHeap* descriptorHeaps[kMaxDescriptorHeaps] {};
    ID3D12PipelineState* pipelineState = nullptr;

    RootBindings compute;
    RootBindings graphics;

    void Clear()
    {
        numDescriptorHeaps = 0;
        std::fill(std::begin(descriptorHeaps), std::end(descriptorHeaps), nullptr);
        pipelineState = nullptr;
        compute.Clear();
        graphics.Clear();
    }
};

static std::shared_mutex s_cmdListStatesMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListState>> s_cmdListStates;

static std::shared_mutex s_rootSigLayoutsMutex;
static ankerl::unordered_dense::map<ID3D12RootSignature*, std::shared_ptr<const RootSigLayout>> s_rootSigLayouts;

static RootRestoreHook<PFN_Reset> s_Reset {};
static RootRestoreHook<PFN_SetDescriptorHeaps> s_SetDescriptorHeaps {};
static RootRestoreHook<PFN_SetPipelineState> s_SetPipelineState {};

static RootRestoreHook<PFN_SetComputeRootSignature> s_SetComputeRootSignature {};
static RootRestoreHook<PFN_SetComputeRootDescriptorTable> s_SetComputeRootDescriptorTable {};
static RootRestoreHook<PFN_SetComputeRoot32BitConstant> s_SetComputeRoot32BitConstant {};
static RootRestoreHook<PFN_SetComputeRoot32BitConstants> s_SetComputeRoot32BitConstants {};
static RootRestoreHook<PFN_SetComputeRootConstantBufferView> s_SetComputeRootConstantBufferView {};
static RootRestoreHook<PFN_SetComputeRootShaderResourceView> s_SetComputeRootShaderResourceView {};
static RootRestoreHook<PFN_SetComputeRootUnorderedAccessView> s_SetComputeRootUnorderedAccessView {};

static RootRestoreHook<PFN_SetGraphicsRootSignature> s_SetGraphicsRootSignature {};
static RootRestoreHook<PFN_SetGraphicsRootDescriptorTable> s_SetGraphicsRootDescriptorTable {};
static RootRestoreHook<PFN_SetGraphicsRoot32BitConstant> s_SetGraphicsRoot32BitConstant {};
static RootRestoreHook<PFN_SetGraphicsRoot32BitConstants> s_SetGraphicsRoot32BitConstants {};
static RootRestoreHook<PFN_SetGraphicsRootConstantBufferView> s_SetGraphicsRootConstantBufferView {};
static RootRestoreHook<PFN_SetGraphicsRootShaderResourceView> s_SetGraphicsRootShaderResourceView {};
static RootRestoreHook<PFN_SetGraphicsRootUnorderedAccessView> s_SetGraphicsRootUnorderedAccessView {};

static std::mutex s_lateHookMutex;
static bool s_lateHooksInstalled = false;

// Raised while a late hook calls down the chain and while restoring
static thread_local int t_suppressRecording = 0;

// Set by D3D12Hooks::SetRootSignatureTracking, per thread so other threads keep recording
static thread_local bool t_upscalerActive = false;

struct SuppressRecordingScope
{
    SuppressRecordingScope() { ++t_suppressRecording; }
    ~SuppressRecordingScope() { --t_suppressRecording; }
    SuppressRecordingScope(const SuppressRecordingScope&) = delete;
    SuppressRecordingScope& operator=(const SuppressRecordingScope&) = delete;
};

static inline bool ShouldRecord(ID3D12GraphicsCommandList* commandList)
{
    return commandList != nullptr && t_suppressRecording == 0 && !t_upscalerActive;
}

static CommandListState* GetCmdListState(ID3D12GraphicsCommandList* commandList, bool create)
{
    {
        std::shared_lock<std::shared_mutex> lock(s_cmdListStatesMutex);
        auto it = s_cmdListStates.find(commandList);
        if (it != s_cmdListStates.end())
            return it->second.get();
    }

    if (!create)
        return nullptr;

    std::unique_lock<std::shared_mutex> lock(s_cmdListStatesMutex);
    auto& entry = s_cmdListStates[commandList];
    if (!entry)
        entry = std::make_unique<CommandListState>();

    return entry.get();
}

static std::shared_ptr<const RootSigLayout> GetRootSigLayout(ID3D12RootSignature* rootSignature)
{
    std::shared_lock<std::shared_mutex> lock(s_rootSigLayoutsMutex);
    auto it = s_rootSigLayouts.find(rootSignature);
    return (it != s_rootSigLayouts.end()) ? it->second : nullptr;
}

// Kept for external users
UINT GetRootParameterCount(ID3D12RootSignature* pRootSignature)
{
    auto layout = GetRootSigLayout(pRootSignature);
    return layout ? static_cast<UINT>(layout->params.size()) : 0;
}

static std::shared_ptr<const RootSigLayout> BuildRootSigLayout(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc)
{
    if (desc == nullptr)
        return nullptr;

    auto layout = std::make_shared<RootSigLayout>();

    auto fill = [&](const auto* params, UINT count)
    {
        layout->params.reserve(count);

        for (UINT i = 0; i < count; ++i)
        {
            RootParamInfo info {};
            info.type = params[i].ParameterType;

            if (info.type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
                info.num32BitValues = params[i].Constants.Num32BitValues;

            layout->params.push_back(info);
        }
    };

    switch (desc->Version)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        fill(desc->Desc_1_0.pParameters, desc->Desc_1_0.NumParameters);
        break;
    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        fill(desc->Desc_1_1.pParameters, desc->Desc_1_1.NumParameters);
        break;
    case D3D_ROOT_SIGNATURE_VERSION_1_2:
        fill(desc->Desc_1_2.pParameters, desc->Desc_1_2.NumParameters);
        break;
    default:
        return nullptr;
    }

    return layout;
}

static void TrackCreatedRootSignature(ID3D12RootSignature* rootSignature,
                                      const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, bool trackLayout,
                                      bool trackHudfix)
{
    if (rootSignature == nullptr || desc == nullptr)
        return;

    if (trackLayout)
    {
        // Always overwrite, the pointer may be reused from a released signature
        auto layout = BuildRootSigLayout(desc);
        std::unique_lock<std::shared_mutex> lock(s_rootSigLayoutsMutex);
        s_rootSigLayouts.insert_or_assign(rootSignature, std::move(layout));
    }

    if (trackHudfix)
        ResTrack_Dx12::RegisterRootSignature(rootSignature, desc);
}

// Per pipeline accessors so record / restore code is written once
template <Pipeline P> struct PipelineTraits;

template <> struct PipelineTraits<Pipeline::Compute>
{
    static constexpr const char* Name = "Compute";
    static RootBindings& Bindings(CommandListState& s) { return s.compute; }
    static bool Enabled() { return Config::Instance()->RestoreComputeSignature.value_or_default(); }
    static auto& Signature() { return s_SetComputeRootSignature; }
    static auto& Table() { return s_SetComputeRootDescriptorTable; }
    static auto& Constants() { return s_SetComputeRoot32BitConstants; }
    static auto& CBV() { return s_SetComputeRootConstantBufferView; }
    static auto& SRV() { return s_SetComputeRootShaderResourceView; }
    static auto& UAV() { return s_SetComputeRootUnorderedAccessView; }
};

template <> struct PipelineTraits<Pipeline::Graphics>
{
    static constexpr const char* Name = "Graphics";
    static RootBindings& Bindings(CommandListState& s) { return s.graphics; }
    static bool Enabled() { return Config::Instance()->RestoreGraphicSignature.value_or_default(); }
    static auto& Signature() { return s_SetGraphicsRootSignature; }
    static auto& Table() { return s_SetGraphicsRootDescriptorTable; }
    static auto& Constants() { return s_SetGraphicsRoot32BitConstants; }
    static auto& CBV() { return s_SetGraphicsRootConstantBufferView; }
    static auto& SRV() { return s_SetGraphicsRootShaderResourceView; }
    static auto& UAV() { return s_SetGraphicsRootUnorderedAccessView; }
};

// ---------------------------------------------------------------------------- Recording

static void RecordReset(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pInitialState)
{
    // Not gated by the upscaler flag, a reset always invalidates everything
    if (commandList == nullptr || t_suppressRecording > 0)
        return;

    auto* state = GetCmdListState(commandList, false);
    if (state == nullptr)
        return;

    state->Clear();
    state->pipelineState = pInitialState;
}

static void RecordPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pPipelineState)
{
    if (!ShouldRecord(commandList) || pPipelineState == nullptr ||
        !Config::Instance()->ExtendedStateRestore.value_or_default())
    {
        return;
    }

    GetCmdListState(commandList, true)->pipelineState = pPipelineState;
}

static void RecordDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT NumDescriptorHeaps,
                                  ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    if (!ShouldRecord(commandList))
        return;

    auto config = Config::Instance();

    if (config->FGHudfixPersistentBindings.value_or_default())
        ResTrack_Dx12::OnSetDescriptorHeaps(commandList, NumDescriptorHeaps, ppDescriptorHeaps);

    if (!config->ExtendedStateRestore.value_or_default() || ppDescriptorHeaps == nullptr)
        return;

    auto* state = GetCmdListState(commandList, true);
    state->numDescriptorHeaps = std::min(NumDescriptorHeaps, kMaxDescriptorHeaps);

    for (UINT i = 0; i < kMaxDescriptorHeaps; ++i)
        state->descriptorHeaps[i] = (i < state->numDescriptorHeaps) ? ppDescriptorHeaps[i] : nullptr;
}

template <Pipeline P>
static void RecordRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    if (!ShouldRecord(commandList))
        return;

    if (Config::Instance()->FGHudfixPersistentBindings.value_or_default())
    {
        if constexpr (P == Pipeline::Compute)
            ResTrack_Dx12::OnSetComputeRootSignature(commandList, pRootSignature);
        else
            ResTrack_Dx12::OnSetGraphicsRootSignature(commandList, pRootSignature);
    }

    if (pRootSignature == nullptr || !PipelineTraits<P>::Enabled())
        return;

    auto& bindings = PipelineTraits<P>::Bindings(*GetCmdListState(commandList, true));

    // Setting the same signature again keeps the root arguments
    if (bindings.signature == pRootSignature)
        return;

    bindings.Bind(pRootSignature, GetRootSigLayout(pRootSignature));
}

template <Pipeline P> static RootArg* GetRootArgForWrite(ID3D12GraphicsCommandList* commandList, UINT index)
{
    if (!ShouldRecord(commandList) || !Config::Instance()->ExtendedStateRestore.value_or_default())
        return nullptr;

    auto* state = GetCmdListState(commandList, false);
    if (state == nullptr)
        return nullptr;

    auto& bindings = PipelineTraits<P>::Bindings(*state);
    if (bindings.signature == nullptr)
        return nullptr;

    if (index >= bindings.args.size())
    {
        // With a known layout an out of range index is a game bug, ignore it.
        // Without one (signature created before our hooks) grow on demand.
        if (bindings.layout != nullptr || index >= kMaxRootParameters)
            return nullptr;

        bindings.args.resize(index + 1);
    }

    return &bindings.args[index];
}

static void WriteRootConstants(RootArg& arg, UINT destOffset, UINT count, const uint32_t* src)
{
    if (count == 0 || destOffset >= kMaxRootConstants || count > kMaxRootConstants - destOffset)
        return;

    if (arg.type != RootEntryType::Constants)
    {
        arg.type = RootEntryType::Constants;
        arg.constantsMask = 0;
    }

    std::copy(src, src + count, arg.constants.begin() + destOffset);

    const uint64_t bits = (count >= 64) ? ~0ull : ((1ull << count) - 1);
    arg.constantsMask |= bits << destOffset;
}

template <Pipeline P>
static void RecordRootTable(ID3D12GraphicsCommandList* commandList, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    if (handle.ptr == 0)
        return;

    if (auto* arg = GetRootArgForWrite<P>(commandList, index))
    {
        arg->type = RootEntryType::Table;
        arg->table = handle;
    }
}

template <Pipeline P>
static void RecordRootConstants(ID3D12GraphicsCommandList* commandList, UINT index, UINT count, const void* pSrcData,
                                UINT destOffset)
{
    if (pSrcData == nullptr)
        return;

    if (auto* arg = GetRootArgForWrite<P>(commandList, index))
        WriteRootConstants(*arg, destOffset, count, static_cast<const uint32_t*>(pSrcData));
}

template <Pipeline P>
static void RecordRootConstant(ID3D12GraphicsCommandList* commandList, UINT index, UINT value, UINT destOffset)
{
    if (auto* arg = GetRootArgForWrite<P>(commandList, index))
    {
        const uint32_t v = value;
        WriteRootConstants(*arg, destOffset, 1, &v);
    }
}

template <Pipeline P>
static void RecordRootView(RootEntryType type, ID3D12GraphicsCommandList* commandList, UINT index,
                           D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
{
    if (auto* arg = GetRootArgForWrite<P>(commandList, index))
    {
        arg->type = type;
        arg->buffer = bufferLocation;
    }
}

// ---------------------------------------------------------------------------- Early hooks (Opti's own cmdlist vtable)

VALIDATE_HOOK(hkReset, PFN_Reset)
static HRESULT hkReset(ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* pAllocator,
                       ID3D12PipelineState* pInitialState)
{
    auto result = s_Reset.o_earlyHook(commandList, pAllocator, pInitialState);

    if (SUCCEEDED(result))
        RecordReset(commandList, pInitialState);

    return result;
}

VALIDATE_HOOK(hkSetPipelineState, PFN_SetPipelineState)
static void hkSetPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pPipelineState)
{
    RecordPipelineState(commandList, pPipelineState);
    s_SetPipelineState.o_earlyHook(commandList, pPipelineState);
}

VALIDATE_HOOK(hkSetDescriptorHeaps, PFN_SetDescriptorHeaps)
static void hkSetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT NumDescriptorHeaps,
                                 ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    RecordDescriptorHeaps(commandList, NumDescriptorHeaps, ppDescriptorHeaps);
    s_SetDescriptorHeaps.o_earlyHook(commandList, NumDescriptorHeaps, ppDescriptorHeaps);
}

VALIDATE_HOOK(hkSetComputeRootSignature, PFN_SetComputeRootSignature)
static void hkSetComputeRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    RecordRootSignature<Pipeline::Compute>(commandList, pRootSignature);
    s_SetComputeRootSignature.o_earlyHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetComputeRootDescriptorTable, PFN_SetComputeRootDescriptorTable)
static void hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                            D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    RecordRootTable<Pipeline::Compute>(commandList, RootParameterIndex, BaseDescriptor);
    s_SetComputeRootDescriptorTable.o_earlyHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstants, PFN_SetComputeRoot32BitConstants)
static void hkSetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                           UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues)
{
    RecordRootConstants<Pipeline::Compute>(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                           DestOffsetIn32BitValues);
    s_SetComputeRoot32BitConstants.o_earlyHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                               DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstant, PFN_SetComputeRoot32BitConstant)
static void hkSetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, UINT SrcData,
                                          UINT DestOffsetIn32BitValues)
{
    RecordRootConstant<Pipeline::Compute>(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    s_SetComputeRoot32BitConstant.o_earlyHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRootConstantBufferView, PFN_SetComputeRootConstantBufferView)
static void hkSetComputeRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::CBV, commandList, RootParameterIndex, BufferLocation);
    s_SetComputeRootConstantBufferView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetComputeRootShaderResourceView, PFN_SetComputeRootShaderResourceView)
static void hkSetComputeRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::SRV, commandList, RootParameterIndex, BufferLocation);
    s_SetComputeRootShaderResourceView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetComputeRootUnorderedAccessView, PFN_SetComputeRootUnorderedAccessView)
static void hkSetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::UAV, commandList, RootParameterIndex, BufferLocation);
    s_SetComputeRootUnorderedAccessView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootSignature, PFN_SetGraphicsRootSignature)
static void hkSetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    RecordRootSignature<Pipeline::Graphics>(commandList, pRootSignature);
    s_SetGraphicsRootSignature.o_earlyHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetGraphicsRootDescriptorTable, PFN_SetGraphicsRootDescriptorTable)
static void hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                             D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    RecordRootTable<Pipeline::Graphics>(commandList, RootParameterIndex, BaseDescriptor);
    s_SetGraphicsRootDescriptorTable.o_earlyHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstants, PFN_SetGraphicsRoot32BitConstants)
static void hkSetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                            UINT Num32BitValuesToSet, const void* pSrcData,
                                            UINT DestOffsetIn32BitValues)
{
    RecordRootConstants<Pipeline::Graphics>(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                            DestOffsetIn32BitValues);
    s_SetGraphicsRoot32BitConstants.o_earlyHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                                DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstant, PFN_SetGraphicsRoot32BitConstant)
static void hkSetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                           UINT SrcData, UINT DestOffsetIn32BitValues)
{
    RecordRootConstant<Pipeline::Graphics>(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    s_SetGraphicsRoot32BitConstant.o_earlyHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRootConstantBufferView, PFN_SetGraphicsRootConstantBufferView)
static void hkSetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::CBV, commandList, RootParameterIndex, BufferLocation);
    s_SetGraphicsRootConstantBufferView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootShaderResourceView, PFN_SetGraphicsRootShaderResourceView)
static void hkSetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::SRV, commandList, RootParameterIndex, BufferLocation);
    s_SetGraphicsRootShaderResourceView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootUnorderedAccessView, PFN_SetGraphicsRootUnorderedAccessView)
static void hkSetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                 D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::UAV, commandList, RootParameterIndex, BufferLocation);
    s_SetGraphicsRootUnorderedAccessView.o_earlyHook(commandList, RootParameterIndex, BufferLocation);
}

// ---------------------------------------------------------------------------- Late hooks (from upscaler eval)
// Record first, then call down with recording suppressed so a chained early hook doesn't record again.

VALIDATE_HOOK(hkResetLate, PFN_Reset)
static HRESULT hkResetLate(ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* pAllocator,
                           ID3D12PipelineState* pInitialState)
{
    HRESULT result;
    {
        SuppressRecordingScope suppress;
        result = s_Reset.o_lateHook(commandList, pAllocator, pInitialState);
    }

    if (SUCCEEDED(result))
        RecordReset(commandList, pInitialState);

    return result;
}

VALIDATE_HOOK(hkSetPipelineStateLate, PFN_SetPipelineState)
static void hkSetPipelineStateLate(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pPipelineState)
{
    RecordPipelineState(commandList, pPipelineState);
    SuppressRecordingScope suppress;
    s_SetPipelineState.o_lateHook(commandList, pPipelineState);
}

VALIDATE_HOOK(hkSetDescriptorHeapsLate, PFN_SetDescriptorHeaps)
static void hkSetDescriptorHeapsLate(ID3D12GraphicsCommandList* commandList, UINT NumDescriptorHeaps,
                                     ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    RecordDescriptorHeaps(commandList, NumDescriptorHeaps, ppDescriptorHeaps);
    SuppressRecordingScope suppress;
    s_SetDescriptorHeaps.o_lateHook(commandList, NumDescriptorHeaps, ppDescriptorHeaps);
}

VALIDATE_HOOK(hkSetComputeRootSignatureLate, PFN_SetComputeRootSignature)
static void hkSetComputeRootSignatureLate(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    RecordRootSignature<Pipeline::Compute>(commandList, pRootSignature);
    SuppressRecordingScope suppress;
    s_SetComputeRootSignature.o_lateHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetComputeRootDescriptorTableLate, PFN_SetComputeRootDescriptorTable)
static void hkSetComputeRootDescriptorTableLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    RecordRootTable<Pipeline::Compute>(commandList, RootParameterIndex, BaseDescriptor);
    SuppressRecordingScope suppress;
    s_SetComputeRootDescriptorTable.o_lateHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstantsLate, PFN_SetComputeRoot32BitConstants)
static void hkSetComputeRoot32BitConstantsLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               UINT Num32BitValuesToSet, const void* pSrcData,
                                               UINT DestOffsetIn32BitValues)
{
    RecordRootConstants<Pipeline::Compute>(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                           DestOffsetIn32BitValues);
    SuppressRecordingScope suppress;
    s_SetComputeRoot32BitConstants.o_lateHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                              DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRoot32BitConstantLate, PFN_SetComputeRoot32BitConstant)
static void hkSetComputeRoot32BitConstantLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                              UINT SrcData, UINT DestOffsetIn32BitValues)
{
    RecordRootConstant<Pipeline::Compute>(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    SuppressRecordingScope suppress;
    s_SetComputeRoot32BitConstant.o_lateHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetComputeRootConstantBufferViewLate, PFN_SetComputeRootConstantBufferView)
static void hkSetComputeRootConstantBufferViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                   D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::CBV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetComputeRootConstantBufferView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetComputeRootShaderResourceViewLate, PFN_SetComputeRootShaderResourceView)
static void hkSetComputeRootShaderResourceViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                   D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::SRV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetComputeRootShaderResourceView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetComputeRootUnorderedAccessViewLate, PFN_SetComputeRootUnorderedAccessView)
static void hkSetComputeRootUnorderedAccessViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Compute>(RootEntryType::UAV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetComputeRootUnorderedAccessView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootSignatureLate, PFN_SetGraphicsRootSignature)
static void hkSetGraphicsRootSignatureLate(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* pRootSignature)
{
    RecordRootSignature<Pipeline::Graphics>(commandList, pRootSignature);
    SuppressRecordingScope suppress;
    s_SetGraphicsRootSignature.o_lateHook(commandList, pRootSignature);
}

VALIDATE_HOOK(hkSetGraphicsRootDescriptorTableLate, PFN_SetGraphicsRootDescriptorTable)
static void hkSetGraphicsRootDescriptorTableLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    RecordRootTable<Pipeline::Graphics>(commandList, RootParameterIndex, BaseDescriptor);
    SuppressRecordingScope suppress;
    s_SetGraphicsRootDescriptorTable.o_lateHook(commandList, RootParameterIndex, BaseDescriptor);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstantsLate, PFN_SetGraphicsRoot32BitConstants)
static void hkSetGraphicsRoot32BitConstantsLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                UINT Num32BitValuesToSet, const void* pSrcData,
                                                UINT DestOffsetIn32BitValues)
{
    RecordRootConstants<Pipeline::Graphics>(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                            DestOffsetIn32BitValues);
    SuppressRecordingScope suppress;
    s_SetGraphicsRoot32BitConstants.o_lateHook(commandList, RootParameterIndex, Num32BitValuesToSet, pSrcData,
                                               DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRoot32BitConstantLate, PFN_SetGraphicsRoot32BitConstant)
static void hkSetGraphicsRoot32BitConstantLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                               UINT SrcData, UINT DestOffsetIn32BitValues)
{
    RecordRootConstant<Pipeline::Graphics>(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    SuppressRecordingScope suppress;
    s_SetGraphicsRoot32BitConstant.o_lateHook(commandList, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

VALIDATE_HOOK(hkSetGraphicsRootConstantBufferViewLate, PFN_SetGraphicsRootConstantBufferView)
static void hkSetGraphicsRootConstantBufferViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::CBV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetGraphicsRootConstantBufferView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootShaderResourceViewLate, PFN_SetGraphicsRootShaderResourceView)
static void hkSetGraphicsRootShaderResourceViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::SRV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetGraphicsRootShaderResourceView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

VALIDATE_HOOK(hkSetGraphicsRootUnorderedAccessViewLate, PFN_SetGraphicsRootUnorderedAccessView)
static void hkSetGraphicsRootUnorderedAccessViewLate(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex,
                                                     D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    RecordRootView<Pipeline::Graphics>(RootEntryType::UAV, commandList, RootParameterIndex, BufferLocation);
    SuppressRecordingScope suppress;
    s_SetGraphicsRootUnorderedAccessView.o_lateHook(commandList, RootParameterIndex, BufferLocation);
}

// ---------------------------------------------------------------------------- Hook installation

template <typename F> static void ForEachCmdListHook(F&& f)
{
    f(s_Reset);
    f(s_SetPipelineState);
    f(s_SetDescriptorHeaps);
    f(s_SetComputeRootSignature);
    f(s_SetGraphicsRootSignature);
    f(s_SetComputeRootDescriptorTable);
    f(s_SetGraphicsRootDescriptorTable);
    f(s_SetComputeRoot32BitConstant);
    f(s_SetGraphicsRoot32BitConstant);
    f(s_SetComputeRoot32BitConstants);
    f(s_SetGraphicsRoot32BitConstants);
    f(s_SetComputeRootConstantBufferView);
    f(s_SetGraphicsRootConstantBufferView);
    f(s_SetComputeRootShaderResourceView);
    f(s_SetGraphicsRootShaderResourceView);
    f(s_SetComputeRootUnorderedAccessView);
    f(s_SetGraphicsRootUnorderedAccessView);
}

static void LoadCmdListHooks(PVOID* vtable, bool late)
{
    auto load = [&](auto& hook, int index)
    {
        using T = std::remove_reference_t<decltype(hook.o_earlyHook)>;
        (late ? hook.o_lateHook : hook.o_earlyHook) = reinterpret_cast<T>(vtable[index]);
    };

    using namespace CmdListVTable;
    load(s_Reset, Reset);
    load(s_SetPipelineState, SetPipelineState);
    load(s_SetDescriptorHeaps, SetDescriptorHeaps);
    load(s_SetComputeRootSignature, SetComputeRootSignature);
    load(s_SetGraphicsRootSignature, SetGraphicsRootSignature);
    load(s_SetComputeRootDescriptorTable, SetComputeRootDescriptorTable);
    load(s_SetGraphicsRootDescriptorTable, SetGraphicsRootDescriptorTable);
    load(s_SetComputeRoot32BitConstant, SetComputeRoot32BitConstant);
    load(s_SetGraphicsRoot32BitConstant, SetGraphicsRoot32BitConstant);
    load(s_SetComputeRoot32BitConstants, SetComputeRoot32BitConstants);
    load(s_SetGraphicsRoot32BitConstants, SetGraphicsRoot32BitConstants);
    load(s_SetComputeRootConstantBufferView, SetComputeRootConstantBufferView);
    load(s_SetGraphicsRootConstantBufferView, SetGraphicsRootConstantBufferView);
    load(s_SetComputeRootShaderResourceView, SetComputeRootShaderResourceView);
    load(s_SetGraphicsRootShaderResourceView, SetGraphicsRootShaderResourceView);
    load(s_SetComputeRootUnorderedAccessView, SetComputeRootUnorderedAccessView);
    load(s_SetGraphicsRootUnorderedAccessView, SetGraphicsRootUnorderedAccessView);
}

template <typename T, typename D> static void AttachIf(bool condition, T& target, D detour)
{
    if (condition && target != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&target), reinterpret_cast<PVOID>(detour));
}

struct CmdListHookConfig
{
    bool restoreCompute;
    bool restoreGraphics;
    bool extended;
    bool hookDescriptorHeaps;

    static CmdListHookConfig Get()
    {
        auto config = Config::Instance();
        CmdListHookConfig c {};
        c.restoreCompute = config->RestoreComputeSignature.value_or_default();
        c.restoreGraphics = config->RestoreGraphicSignature.value_or_default();
        c.extended = config->ExtendedStateRestore.value_or_default();
        c.hookDescriptorHeaps = c.extended || (config->FGHudfixPersistentBindings.value_or_default() &&
                                               State::Instance().activeFgInput == FGInput::Upscaler);
        return c;
    }
};

void D3D12Hooks::HookToCommandListLate(ID3D12GraphicsCommandList* commandList)
{
    if (commandList == nullptr)
        return;

    std::lock_guard<std::mutex> lock(s_lateHookMutex);

    // Only try once, a failing transaction shouldn't be retried every frame
    if (s_lateHooksInstalled)
        return;

    s_lateHooksInstalled = true;

    LoadCmdListHooks(*(PVOID**) commandList, true);

    const auto c = CmdListHookConfig::Get();
    const bool computeArgs = c.extended && c.restoreCompute;
    const bool graphicsArgs = c.extended && c.restoreGraphics;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Common
    AttachIf(c.restoreCompute || c.restoreGraphics, s_Reset.o_lateHook, hkResetLate);
    AttachIf(c.extended, s_SetPipelineState.o_lateHook, hkSetPipelineStateLate);
    AttachIf(c.hookDescriptorHeaps, s_SetDescriptorHeaps.o_lateHook, hkSetDescriptorHeapsLate);

    // Compute
    AttachIf(c.restoreCompute, s_SetComputeRootSignature.o_lateHook, hkSetComputeRootSignatureLate);
    AttachIf(computeArgs, s_SetComputeRootDescriptorTable.o_lateHook, hkSetComputeRootDescriptorTableLate);
    AttachIf(computeArgs, s_SetComputeRoot32BitConstant.o_lateHook, hkSetComputeRoot32BitConstantLate);
    AttachIf(computeArgs, s_SetComputeRoot32BitConstants.o_lateHook, hkSetComputeRoot32BitConstantsLate);
    AttachIf(computeArgs, s_SetComputeRootConstantBufferView.o_lateHook, hkSetComputeRootConstantBufferViewLate);
    AttachIf(computeArgs, s_SetComputeRootShaderResourceView.o_lateHook, hkSetComputeRootShaderResourceViewLate);
    AttachIf(computeArgs, s_SetComputeRootUnorderedAccessView.o_lateHook, hkSetComputeRootUnorderedAccessViewLate);

    // Graphics
    AttachIf(c.restoreGraphics, s_SetGraphicsRootSignature.o_lateHook, hkSetGraphicsRootSignatureLate);
    AttachIf(graphicsArgs, s_SetGraphicsRootDescriptorTable.o_lateHook, hkSetGraphicsRootDescriptorTableLate);
    AttachIf(graphicsArgs, s_SetGraphicsRoot32BitConstant.o_lateHook, hkSetGraphicsRoot32BitConstantLate);
    AttachIf(graphicsArgs, s_SetGraphicsRoot32BitConstants.o_lateHook, hkSetGraphicsRoot32BitConstantsLate);
    AttachIf(graphicsArgs, s_SetGraphicsRootConstantBufferView.o_lateHook, hkSetGraphicsRootConstantBufferViewLate);
    AttachIf(graphicsArgs, s_SetGraphicsRootShaderResourceView.o_lateHook, hkSetGraphicsRootShaderResourceViewLate);
    AttachIf(graphicsArgs, s_SetGraphicsRootUnorderedAccessView.o_lateHook, hkSetGraphicsRootUnorderedAccessViewLate);

    if (DetourTransactionCommit() == NO_ERROR)
    {
        LOG_DEBUG("Hooked command list functions Late");
    }
    else
    {
        ForEachCmdListHook([](auto& hook) { hook.o_lateHook = nullptr; });
        LOG_WARN("Hooking command list functions Late failed");
    }
}

static void HookToCommandList(ID3D12Device* InDevice)
{
    if (s_SetComputeRootSignature.o_earlyHook != nullptr || s_SetGraphicsRootSignature.o_earlyHook != nullptr)
        return;

    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;

    if (InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)) != S_OK)
        return;

    if (InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
                                    IID_PPV_ARGS(&commandList)) == S_OK)
    {
        LoadCmdListHooks(*(PVOID**) commandList, false);

        const auto c = CmdListHookConfig::Get();
        const bool computeArgs = c.extended && c.restoreCompute;
        const bool graphicsArgs = c.extended && c.restoreGraphics;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // Common
        AttachIf(c.restoreCompute || c.restoreGraphics, s_Reset.o_earlyHook, hkReset);
        AttachIf(c.extended, s_SetPipelineState.o_earlyHook, hkSetPipelineState);
        AttachIf(c.hookDescriptorHeaps, s_SetDescriptorHeaps.o_earlyHook, hkSetDescriptorHeaps);

        // Signatures are always hooked, ResTrack uses them too
        AttachIf(true, s_SetComputeRootSignature.o_earlyHook, hkSetComputeRootSignature);
        AttachIf(true, s_SetGraphicsRootSignature.o_earlyHook, hkSetGraphicsRootSignature);

        // Compute
        AttachIf(computeArgs, s_SetComputeRootDescriptorTable.o_earlyHook, hkSetComputeRootDescriptorTable);
        AttachIf(computeArgs, s_SetComputeRoot32BitConstant.o_earlyHook, hkSetComputeRoot32BitConstant);
        AttachIf(computeArgs, s_SetComputeRoot32BitConstants.o_earlyHook, hkSetComputeRoot32BitConstants);
        AttachIf(computeArgs, s_SetComputeRootConstantBufferView.o_earlyHook, hkSetComputeRootConstantBufferView);
        AttachIf(computeArgs, s_SetComputeRootShaderResourceView.o_earlyHook, hkSetComputeRootShaderResourceView);
        AttachIf(computeArgs, s_SetComputeRootUnorderedAccessView.o_earlyHook, hkSetComputeRootUnorderedAccessView);

        // Graphics
        AttachIf(graphicsArgs, s_SetGraphicsRootDescriptorTable.o_earlyHook, hkSetGraphicsRootDescriptorTable);
        AttachIf(graphicsArgs, s_SetGraphicsRoot32BitConstant.o_earlyHook, hkSetGraphicsRoot32BitConstant);
        AttachIf(graphicsArgs, s_SetGraphicsRoot32BitConstants.o_earlyHook, hkSetGraphicsRoot32BitConstants);
        AttachIf(graphicsArgs, s_SetGraphicsRootConstantBufferView.o_earlyHook, hkSetGraphicsRootConstantBufferView);
        AttachIf(graphicsArgs, s_SetGraphicsRootShaderResourceView.o_earlyHook, hkSetGraphicsRootShaderResourceView);
        AttachIf(graphicsArgs, s_SetGraphicsRootUnorderedAccessView.o_earlyHook, hkSetGraphicsRootUnorderedAccessView);

        if (DetourTransactionCommit() == NO_ERROR)
        {
            LOG_DEBUG("Hooked command list functions");
        }
        else
        {
            ForEachCmdListHook([](auto& hook) { hook.o_earlyHook = nullptr; });
            LOG_WARN("Hooking command list functions failed");
        }

        commandList->Close();
        commandList->Release();
    }

    commandAllocator->Reset();
    commandAllocator->Release();
}

// ---------------------------------------------------------------------------- Restore

static bool RestoreDescriptorHeaps(ID3D12GraphicsCommandList* cmdList, const CommandListState& state)
{
    if (state.numDescriptorHeaps == 0 || state.descriptorHeaps[0] == nullptr)
        return false;

    auto hook = s_SetDescriptorHeaps.GetHook();
    if (hook == nullptr)
    {
        LOG_ERROR("Couldn't restore DescriptorHeaps, no original SetDescriptorHeaps");
        return false;
    }

    ID3D12DescriptorHeap* heaps[kMaxDescriptorHeaps] = { state.descriptorHeaps[0], state.descriptorHeaps[1] };
    hook(cmdList, state.numDescriptorHeaps, heaps);
    return true;
}

static bool RestorePipelineState(ID3D12GraphicsCommandList* cmdList, const CommandListState& state)
{
    if (state.pipelineState == nullptr)
        return false;

    auto hook = s_SetPipelineState.GetHook();
    if (hook == nullptr)
    {
        LOG_ERROR("Couldn't restore PipelineState, no original SetPipelineState");
        return false;
    }

    hook(cmdList, state.pipelineState);
    return true;
}

template <Pipeline P>
static void RestoreBindings(ID3D12GraphicsCommandList* cmdList, const RootBindings& bindings, bool extended)
{
    using T = PipelineTraits<P>;

    auto setSignature = T::Signature().GetHook();
    if (setSignature == nullptr)
    {
        LOG_ERROR("Couldn't restore {} RootSignature, no original Set{}RootSignature", T::Name, T::Name);
        return;
    }

    LOG_TRACE("Restore {}RootSig: {:X}, for CmdList: {:X}", T::Name, (UINT64) bindings.signature, (UINT64) cmdList);
    setSignature(cmdList, bindings.signature);

    if (!extended)
        return;

    if (bindings.layout == nullptr)
    {
        LOG_DEBUG("{}RootSig {:X} layout unknown (created before hooks?), restoring recorded args only", T::Name,
                  (UINT64) bindings.signature);
    }

    for (UINT i = 0; i < static_cast<UINT>(bindings.args.size()); ++i)
    {
        const auto& arg = bindings.args[i];

        switch (arg.type)
        {
        case RootEntryType::Table:
            if (auto hook = T::Table().GetHook())
                hook(cmdList, i, arg.table);
            else
                LOG_ERROR("Couldn't restore {} root table {}, no original function", T::Name, i);
            break;

        case RootEntryType::Constants:
            if (auto hook = T::Constants().GetHook())
            {
                // Restore each contiguous run of DWORDs the game actually set
                UINT dw = 0;
                while (dw < kMaxRootConstants)
                {
                    if (((arg.constantsMask >> dw) & 1) == 0)
                    {
                        ++dw;
                        continue;
                    }

                    const UINT start = dw;
                    while (dw < kMaxRootConstants && ((arg.constantsMask >> dw) & 1))
                        ++dw;

                    hook(cmdList, i, dw - start, &arg.constants[start], start);
                }
            }
            else
            {
                LOG_ERROR("Couldn't restore {} root constants {}, no original function", T::Name, i);
            }
            break;

        case RootEntryType::CBV:
            if (auto hook = T::CBV().GetHook())
                hook(cmdList, i, arg.buffer);
            else
                LOG_ERROR("Couldn't restore {} root CBV {}, no original function", T::Name, i);
            break;

        case RootEntryType::SRV:
            if (auto hook = T::SRV().GetHook())
                hook(cmdList, i, arg.buffer);
            else
                LOG_ERROR("Couldn't restore {} root SRV {}, no original function", T::Name, i);
            break;

        case RootEntryType::UAV:
            if (auto hook = T::UAV().GetHook())
                hook(cmdList, i, arg.buffer);
            else
                LOG_ERROR("Couldn't restore {} root UAV {}, no original function", T::Name, i);
            break;

        case RootEntryType::Invalid:
            // Nothing was bound to this slot since the signature was set. That's legal in D3D12 (engines often
            // use one big shared root signature and only bind what the current shader reads), and leaving the
            // slot untouched reproduces the game's state exactly, so this isn't an error.
            if (bindings.layout != nullptr && i < bindings.layout->params.size())
            {
                LOG_TRACE("{} root index: {} ({}) unbound by game for CmdList: {:X}, skipping", T::Name, i,
                          magic_enum::enum_name(bindings.layout->params[i].type), (UINT64) cmdList);
            }
            else
            {
                LOG_TRACE("{} root index: {} unbound by game for CmdList: {:X}, skipping", T::Name, i,
                          (UINT64) cmdList);
            }
            break;
        }
    }
}

void D3D12Hooks::SetRootSignatureTracking(bool enable) { t_upscalerActive = !enable; }

bool D3D12Hooks::CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList)
{
    auto* state = GetCmdListState(cmdList, false);
    return state != nullptr && (state->compute.signature != nullptr || state->graphics.signature != nullptr);
}

void D3D12Hooks::RestoreRoot(ID3D12GraphicsCommandList* cmdList)
{
    auto config = Config::Instance();
    const bool restoreCompute = config->RestoreComputeSignature.value_or_default();
    const bool restoreGraphics = config->RestoreGraphicSignature.value_or_default();

    if (!restoreCompute && !restoreGraphics)
        return;

    auto* state = GetCmdListState(cmdList, false);
    const bool hasCompute = restoreCompute && state != nullptr && state->compute.signature != nullptr;
    const bool hasGraphics = restoreGraphics && state != nullptr && state->graphics.signature != nullptr;

    if (!hasCompute && !hasGraphics)
    {
        LOG_TRACE("Can't restore Root Signature for CmdList: {:X}", (UINT64) cmdList);
        return;
    }

    // Restore calls must not be recorded (and must not take locks if they hit a chained hook)
    SuppressRecordingScope suppress;

    const bool extended = config->ExtendedStateRestore.value_or_default();

    // Heaps must be set before descriptor tables
    if (extended)
    {
        if (RestoreDescriptorHeaps(cmdList, *state))
            LOG_TRACE("Restored DescriptorHeaps for CmdList: {:X}", (UINT64) cmdList);
        else
            LOG_WARN("Can't restore DescriptorHeaps for CmdList: {:X}", (UINT64) cmdList);
    }

    if (hasCompute)
        RestoreBindings<Pipeline::Compute>(cmdList, state->compute, extended);

    if (hasGraphics)
        RestoreBindings<Pipeline::Graphics>(cmdList, state->graphics, extended);

    if (extended)
    {
        if (RestorePipelineState(cmdList, *state))
            LOG_TRACE("Restored PipelineState for CmdList: {:X}", (UINT64) cmdList);
        else
            LOG_TRACE("Can't restore PipelineState for CmdList: {:X}", (UINT64) cmdList);
    }
}

// ============================================================================================
// Device hooks
// ============================================================================================

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

                ScopedOptiScalerReflex optiScalerCall {};
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
    if (!_skipCommitedResource && pDesc != nullptr)
    {
        D3D12_RESOURCE_DESC localDesc = *pDesc;

        // UE passes its own extended desc, the extra fields follow the D3D12 ones
        auto ueDesc = reinterpret_cast<const UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc->bRequires64BitAtomicSupport)
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
    if (!skipPlacedResource && pDesc != nullptr)
    {
        D3D12_RESOURCE_DESC localDesc = *pDesc;

        // UE passes its own extended desc, the extra fields follow the D3D12 ones
        auto ueDesc = reinterpret_cast<const UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc->bRequires64BitAtomicSupport)
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
    auto* config = Config::Instance();
    const bool extendedStateRestore = config->ExtendedStateRestore.value_or_default();
    const bool samplerOverride = config->MipmapBiasOverride.has_value() || config->AnisotropyOverride.has_value();
    const bool trackRootLayout = extendedStateRestore || samplerOverride;
    const bool trackHudfixRootSignature = config->FGHudfixPersistentBindings.value_or_default() &&
                                          State::Instance().activeFgInput == FGInput::Upscaler &&
                                          !config->FGDisableHUDFix.value_or_default();

    if (!samplerOverride && !extendedStateRestore && !trackHudfixRootSignature)
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

    // No sampler override is needed; create the original signature and only record requested metadata.
    if (!samplerOverride)
    {
        auto result =
            o_CreateRootSignature(device, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);

        if (SUCCEEDED(result) && ppvRootSignature != nullptr && *ppvRootSignature != nullptr)
        {
            TrackCreatedRootSignature(static_cast<ID3D12RootSignature*>(*ppvRootSignature), desc, trackRootLayout,
                                      trackHudfixRootSignature);
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

    if (SUCCEEDED(result) && ppvRootSignature != nullptr && *ppvRootSignature != nullptr)
    {
        TrackCreatedRootSignature(static_cast<ID3D12RootSignature*>(*ppvRootSignature), desc, trackRootLayout,
                                  trackHudfixRootSignature);
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

void D3D12Hooks::HookDevice(ID3D12Device* device) { HookToDevice(device); }
