#include "pch.h"
#include "FSR4DriverProviderVk.h"

#include "FSR4PipelineLayouts.h"

#include <misc/IdentifyGpu.h>
#include <proxies/D3D12_Proxy.h>
#include <proxies/FfxApi_Proxy.h>

#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#define SPV_ENABLE_UTILITY_CODE
#include "../../external/FidelityFX-SDK/sdk/tools/ffx_shader_compiler/libs/SPIRV-Reflect/include/spirv/unified1/spirv.h"
#undef SPV_ENABLE_UTILITY_CODE

// What the providers receive in IsSupported, same as ffxCreateBackendVKDesc without the header
struct ProviderBackendVk
{
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    PFN_vkGetDeviceProcAddr gdpa;
};

// Shader modules created by the driver provider get rewritten before reaching the driver:
// AMD's FSR 4 shaders read/write images declared with a 16 bit float sampled type, which only AMD's own
// driver accepts, so they get rewritten to 32 bit float images.
namespace
{
struct DriverDeviceFunctions
{
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
    PFN_vkCreateShaderModule createShaderModule;
};

std::mutex driverDevicesMutex;
std::unordered_map<VkDevice, DriverDeviceFunctions> driverDevices;
PfnFfxCreateContext driverCreateContext = nullptr;
PfnFfxCreateContext driverCreateFgContext = nullptr;

DriverDeviceFunctions GetDriverDeviceFunctions(VkDevice device)
{
    std::lock_guard lock(driverDevicesMutex);
    const auto entry = driverDevices.find(device);
    return entry != driverDevices.end() ? entry->second : DriverDeviceFunctions {};
}

template <typename Word> std::vector<std::span<Word>> ParseSpirvInstructions(std::span<Word> code)
{
    if (code.size() < 5 || code[0] != SpvMagicNumber)
        return {};

    std::vector<std::span<Word>> instructions;
    for (size_t offset = 5; offset < code.size();)
    {
        const size_t count = code[offset] >> 16;
        if (count == 0 || count > code.size() - offset)
            return {};
        instructions.push_back(code.subspan(offset, count));
        offset += count;
    }
    return instructions;
}

void Emit(std::vector<uint32_t>& output, SpvOp opcode, std::initializer_list<uint32_t> operands)
{
    output.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
    output.insert(output.end(), operands.begin(), operands.end());
}

static std::vector<uint32_t> NormalizeHalfImageIO(std::span<const uint32_t> code)
{
    const auto originalInstructions = ParseSpirvInstructions(code);
    if (originalInstructions.empty())
        return {};

    std::unordered_set<uint32_t> halfTypes;
    uint32_t floatType = 0;
    for (auto instruction : originalInstructions)
    {
        if ((instruction[0] & 0xffff) != SpvOpTypeFloat || instruction.size() != 3)
            continue;
        if (instruction[2] == 16)
            halfTypes.insert(instruction[1]);
        else if (instruction[2] == 32)
            floatType = instruction[1];
    }

    const bool hasHalfImages = std::any_of(originalInstructions.begin(), originalInstructions.end(),
                                           [&](auto instruction)
                                           {
                                               return (instruction[0] & 0xffff) == SpvOpTypeImage &&
                                                      instruction.size() >= 9 && halfTypes.contains(instruction[2]);
                                           });
    if (!hasHalfImages || floatType == 0)
        return {};

    std::vector<uint32_t> normalized(code.begin(), code.end());
    const auto instructions = ParseSpirvInstructions(std::span<uint32_t>(normalized));
    auto nextId = code[3];
    std::vector<uint32_t> declarations;

    std::unordered_map<uint32_t, uint32_t> promotedTypes;
    for (auto type : halfTypes)
        promotedTypes[type] = floatType;

    std::unordered_map<uint32_t, uint32_t> floatVectorTypes;
    for (auto instruction : instructions)
    {
        if ((instruction[0] & 0xffff) == SpvOpTypeVector && instruction.size() == 4 && instruction[2] == floatType)
            floatVectorTypes.try_emplace(instruction[3], instruction[1]);
    }
    for (auto instruction : instructions)
    {
        if ((instruction[0] & 0xffff) != SpvOpTypeVector || instruction.size() != 4 ||
            !halfTypes.contains(instruction[2]))
            continue;

        const auto existing = floatVectorTypes.find(instruction[3]);
        const auto promoted = existing != floatVectorTypes.end() ? existing->second : nextId++;
        promotedTypes[instruction[1]] = promoted;
        if (existing == floatVectorTypes.end())
            Emit(declarations, SpvOpTypeVector, { promoted, floatType, instruction[3] });
    }

    std::unordered_map<uint32_t, uint32_t> halfValueTypes;
    for (auto instruction : instructions)
    {
        bool hasResult = false, hasType = false;
        SpvHasResultAndType(static_cast<SpvOp>(instruction[0] & 0xffff), &hasResult, &hasType);
        if (hasResult && hasType && instruction.size() >= 3 && promotedTypes.contains(instruction[1]))
            halfValueTypes[instruction[2]] = instruction[1];
    }

    std::unordered_map<uint32_t, uint32_t> aliases;
    std::vector<std::span<uint32_t>> canonicalTypes;
    auto resolveType = [&](uint32_t& type)
    {
        if (const auto alias = aliases.find(type); alias != aliases.end())
            type = alias->second;
    };
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        if (opcode == SpvOpTypeImage && instruction.size() >= 9)
        {
            if (halfTypes.contains(instruction[2]))
                instruction[2] = floatType;
        }
        else if (opcode == SpvOpTypeSampledImage && instruction.size() == 3)
            resolveType(instruction[2]);
        else if (opcode == SpvOpTypePointer && instruction.size() == 4)
            resolveType(instruction[3]);
        else
            continue;

        const auto existing =
            std::find_if(canonicalTypes.begin(), canonicalTypes.end(),
                         [&](auto candidate)
                         {
                             return candidate[0] == instruction[0] &&
                                    std::equal(candidate.begin() + 2, candidate.end(), instruction.begin() + 2);
                         });
        if (existing != canonicalTypes.end())
            aliases[instruction[1]] = (*existing)[1];
        else
            canonicalTypes.push_back(instruction);
    }
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        bool hasResult = false, hasType = false;
        SpvHasResultAndType(opcode, &hasResult, &hasType);
        if (hasResult && hasType && instruction.size() >= 3)
            resolveType(instruction[1]);
        if ((opcode == SpvOpName || opcode == SpvOpDecorate) && instruction.size() >= 3)
            resolveType(instruction[1]);
        if (opcode == SpvOpTypeFunction)
            for (size_t i = 2; i < instruction.size(); ++i)
                resolveType(instruction[i]);
    }

    std::vector<uint32_t> result(code.begin(), code.begin() + 5);
    result.reserve(code.size() + declarations.size());
    bool declarationsAdded = false;
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        if ((opcode == SpvOpTypeImage || opcode == SpvOpTypeSampledImage || opcode == SpvOpTypePointer) &&
            instruction.size() >= 2 && aliases.contains(instruction[1]))
            continue;
        if (opcode == SpvOpFunction && !declarationsAdded)
        {
            result.insert(result.end(), declarations.begin(), declarations.end());
            declarationsAdded = true;
        }
        const auto offset = result.size();
        if (opcode >= SpvOpImageSampleImplicitLod && opcode <= SpvOpImageRead && instruction.size() >= 4)
        {
            const auto promoted = promotedTypes.find(instruction[1]);
            if (promoted != promotedTypes.end())
            {
                const auto temporary = nextId++;
                result.insert(result.end(), instruction.begin(), instruction.end());
                result[offset + 1] = promoted->second;
                result[offset + 2] = temporary;
                Emit(result, SpvOpFConvert, { instruction[1], instruction[2], temporary });
                continue;
            }
        }
        if (opcode == SpvOpImageWrite && instruction.size() >= 4)
        {
            const auto type = halfValueTypes.find(instruction[3]);
            if (type != halfValueTypes.end())
            {
                const auto temporary = nextId++;
                Emit(result, SpvOpFConvert, { promotedTypes.at(type->second), temporary, instruction[3] });
                const auto writeOffset = result.size();
                result.insert(result.end(), instruction.begin(), instruction.end());
                result[writeOffset + 3] = temporary;
                continue;
            }
        }
        result.insert(result.end(), instruction.begin(), instruction.end());
    }
    result[3] = nextId;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDriverShaderModule(VkDevice device, const VkShaderModuleCreateInfo* info,
                                                        const VkAllocationCallbacks* allocator, VkShaderModule* module)
{
    const auto functions = GetDriverDeviceFunctions(device);
    if (functions.createShaderModule == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (info == nullptr || info->pCode == nullptr || info->codeSize % sizeof(uint32_t) != 0)
        return functions.createShaderModule(device, info, allocator, module);

    const std::span<const uint32_t> original { info->pCode, info->codeSize / sizeof(uint32_t) };
    const auto code = NormalizeHalfImageIO(original);
    if (code.empty())
        return functions.createShaderModule(device, info, allocator, module);

    auto compatibleInfo = *info;
    compatibleInfo.pCode = code.data();
    compatibleInfo.codeSize = code.size() * sizeof(uint32_t);
    LOG_DEBUG("rewrote driver shader ({} -> {} bytes)", info->codeSize, compatibleInfo.codeSize);
    return functions.createShaderModule(device, &compatibleInfo, allocator, module);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDriverDeviceProcAddr(VkDevice device, const char* name)
{
    const auto functions = GetDriverDeviceFunctions(device);
    if (name != nullptr && strcmp(name, "vkCreateShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateDriverShaderModule);
    return functions.getDeviceProcAddr != nullptr ? functions.getDeviceProcAddr(device, name) : nullptr;
}

ffxReturnCode_t CreateWithDriverShaders(PfnFfxCreateContext original, ffxContext* context,
                                        ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* allocator)
{
    for (auto header = desc; header != nullptr; header = header->pNext)
    {
        if (header->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2)
            continue;

        auto backend = reinterpret_cast<ffxCreateBackendVKDesc*>(header);
        const auto gdpa = backend->vkDeviceProcAddr;
        if (gdpa == nullptr)
            break;
        const auto createShader =
            reinterpret_cast<PFN_vkCreateShaderModule>(gdpa(backend->vkDevice, "vkCreateShaderModule"));
        if (createShader == nullptr)
            break;
        {
            std::lock_guard lock(driverDevicesMutex);
            driverDevices[backend->vkDevice] = { gdpa, createShader };
        }
        backend->vkDeviceProcAddr = &GetDriverDeviceProcAddr;
        const auto result = original(context, desc, allocator);
        backend->vkDeviceProcAddr = gdpa;
        return result;
    }
    return original(context, desc, allocator);
}

ffxReturnCode_t CreateDriverContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                    const ffxAllocationCallbacks* allocator)
{
    return CreateWithDriverShaders(driverCreateContext, context, desc, allocator);
}

ffxReturnCode_t CreateDriverFgContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                      const ffxAllocationCallbacks* allocator)
{
    return CreateWithDriverShaders(driverCreateFgContext, context, desc, allocator);
}
} // namespace

// amdvkffx64 only gives out providers when it sees a signed FFX SDK dll loaded in the process.
static FSR4DriverDll amdvkffx64 { L"amdvkffx64.dll",
                                  { L"amd_fidelityfx_loader_vk.dll", L"amd_fidelityfx_upscaler_vk.dll",
                                    L"amd_fidelityfx_framegeneration_vk.dll" } };

static std::optional<DriverProvider> GetProvider(uint64_t descType)
{
    if (amdvkffx64.Load() == nullptr)
        return std::nullopt;

    FSR4PipelineLayouts::Hook(amdvkffx64.Module());

    // Frame generation and its swapchain are only given out when enabled through the options
    ProviderOption swapchain { .effect = ProviderEffect::FrameGenerationSwapchain };
    ProviderOption frameGeneration { .effect = ProviderEffect::FrameGeneration, .next = &swapchain };
    ProviderOption upscale { .effect = ProviderEffect::Upscale, .next = &frameGeneration };

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    auto provider = amdvkffx64.QueryProvider(descType, { 3u }, &upscale);

    if (!provider.has_value())
        LOG_ERROR("amdvkffx64 has no provider for {:X}", descType);

    return provider;
}

static void FillModule(FfxModule& module, const DriverProvider& provider)
{
    module.dll = amdvkffx64.Module();
    module.CreateContext = provider.functions.createContext;
    module.DestroyContext = provider.functions.destroyContext;
    module.Configure = provider.functions.configure;
    module.Query = provider.functions.query;
    module.Dispatch = provider.functions.dispatch;
    module.version.parse_version(provider.versionName.c_str());
    module.vkBackendV2 = true;
}

static bool CallIsSupported(const DriverProvider& provider, VkDevice device, VkPhysicalDevice physicalDevice,
                            PFN_vkGetDeviceProcAddr gdpa)
{
    if (provider.isSupported == nullptr)
        return true;

    ProviderBackendVk backend { device, physicalDevice, gdpa };

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    return provider.isSupported(&backend);
}

bool FSR4DriverProviderVk::Init()
{
    static bool initTried = false;

    if (initTried)
        return IsAvailable();

    initTried = true;

    auto upscaler = GetProvider(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE);
    if (!upscaler.has_value())
        return false;

    _upscaler = std::move(*upscaler);

    FillModule(_module, _upscaler);
    driverCreateContext = _upscaler.functions.createContext;
    _module.CreateContext = &CreateDriverContext;

    return true;
}

bool FSR4DriverProviderVk::IsSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa)
{
    return IsAvailable() && CallIsSupported(_upscaler, device, physicalDevice, gdpa);
}

void FSR4DriverProviderVk::AddToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names,
                                            DriverListing listing)
{
    if (Init())
        AddDriverToVersionList(ids, names, VersionId(), VersionName(), listing);
}

bool FSR4DriverProviderVk::InitFg()
{
    static bool initTried = false;

    if (initTried)
        return IsFgAvailable();

    initTried = true;

    // Real FP8 support comes from the D3D12 checks, the game's Vulkan device can be created before anything ran them
    // or even loaded d3d12.dll, which the checks need
    if (IdentifyGpu::getPrimaryGpu().vendorId == VendorId::AMD)
    {
        D3d12Proxy::Init();
        IdentifyGpu::updateD3d12Capabilities();
    }

    if (IdentifyGpu::getPrimaryGpu().realFsr4Support != FSR4Support::FP8)
    {
        LOG_INFO("GPU doesn't support FP8, not using the driver FG");
        return false;
    }

    auto fg = GetProvider(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION);
    if (!fg.has_value())
        return false;

    auto swapchain = GetProvider(FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK);
    if (!swapchain.has_value())
        return false;

    _fg = std::move(*fg);

    // FG shaders are FP8 too, they get the same rewrites as the upscaler ones
    FillModule(_fgModule, _fg);
    driverCreateFgContext = _fg.functions.createContext;
    _fgModule.CreateContext = &CreateDriverFgContext;

    FillModule(_fgSwapchainModule, *swapchain);

    return true;
}

bool FSR4DriverProviderVk::IsFgSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa)
{
    return IsFgAvailable() && CallIsSupported(_fg, device, physicalDevice, gdpa);
}

void FSR4DriverProviderVk::AddFgToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names)
{
    // FFX SDK modules can get the driver FG on their own
    if (!InitFg() || std::ranges::find(ids, FgVersionId()) != ids.end())
        return;

    ids.push_back(FgVersionId());
    names.push_back(FgVersionName());
}
