#pragma once

#include <cassert>
#include <map>
#include <string>
#include <variant>
#include <nvsdk_ngx_params.h>

struct ID3D12Device {};
struct ID3D12CommandList {};
struct ID3D12GraphicsCommandList : ID3D12CommandList {};
using UINT = unsigned int;
struct ID3D12CommandQueue
{
};
struct ID3D12Resource {};

#define LOG_INFO(...) ((void) 0)
#define LOG_ERROR(...) ((void) 0)

template <typename T> struct Setting
{
    T value {};
    T value_or_default() const { return value; }
};

class Config
{
  public:
    Setting<int> DlssNrPreset, DlssNrStyle;
    Setting<float> DlssNrIntensity { 0.5f }, DlssNrLocalStructure { 0.25f },
                   DlssNrLocalTone { 0.75f }, DlssNrSkinStructure { 0.375f };
    Setting<bool> DlssNrAutoMask { true };
    static Config* Instance() { static Config config; return &config; }
};

namespace Mock
{
struct Params : NVSDK_NGX_Parameter
{
    using Value = std::variant<unsigned long long, float, double, unsigned int, int,
                               ID3D11Resource*, ID3D12Resource*, void*>;
    std::map<std::string, Value> values;
#define PARAMETER_OVERLOAD(Type) \
    void Set(const char* key, Type value) override { values[key] = value; } \
    NVSDK_NGX_Result Get(const char* key, Type* value) const override \
    { \
        auto it = values.find(key); \
        if (it == values.end() || !std::holds_alternative<Type>(it->second)) \
            return NVSDK_NGX_Result_Fail; \
        *value = std::get<Type>(it->second); \
        return NVSDK_NGX_Result_Success; \
    }
    PARAMETER_OVERLOAD(unsigned long long)
    PARAMETER_OVERLOAD(float)
    PARAMETER_OVERLOAD(double)
    PARAMETER_OVERLOAD(unsigned int)
    PARAMETER_OVERLOAD(int)
    PARAMETER_OVERLOAD(ID3D11Resource*)
    PARAMETER_OVERLOAD(ID3D12Resource*)
    PARAMETER_OVERLOAD(void*)
#undef PARAMETER_OVERLOAD
    void Reset() override { values.clear(); }
};

inline bool initialized = false;
inline unsigned int allocations = 0, destructions = 0, creations = 0, releases = 0, evaluations = 0;
inline NVSDK_NGX_Result createResult = NVSDK_NGX_Result_Success;
inline NVSDK_NGX_Result evaluateResult = NVSDK_NGX_Result_Success;
inline Params* latest = nullptr;
inline std::map<NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*> handles;

inline NVSDK_NGX_Result Allocate(NVSDK_NGX_Parameter** params)
{
    ++allocations;
    *params = latest = new Params;
    return NVSDK_NGX_Result_Success;
}
inline NVSDK_NGX_Result Destroy(NVSDK_NGX_Parameter* params)
{
    for (const auto& pair : handles)
        assert(pair.second != params); // Release the feature before its parameter map.
    ++destructions;
    delete static_cast<Params*>(params);
    return NVSDK_NGX_Result_Success;
}
inline NVSDK_NGX_Result Create(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature feature,
                                NVSDK_NGX_Parameter* params, NVSDK_NGX_Handle** handle)
{
    assert((int) feature == 18);
    ++creations;
    if (createResult != NVSDK_NGX_Result_Success)
        return createResult;
    *handle = new NVSDK_NGX_Handle {};
    handles[*handle] = params;
    return NVSDK_NGX_Result_Success;
}
inline NVSDK_NGX_Result Release(NVSDK_NGX_Handle* handle)
{
    assert(handles.erase(handle) == 1);
    ++releases;
    delete handle;
    return NVSDK_NGX_Result_Success;
}
inline NVSDK_NGX_Result Evaluate(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
                                  const NVSDK_NGX_Parameter*, void*)
{
    ++evaluations;
    return evaluateResult;
}
} // namespace Mock

struct NVNGXProxy
{
    static bool IsDx12Inited() { return Mock::initialized; }
    static bool InitDx12(ID3D12Device*) { Mock::initialized = true; return true; }
    static auto D3D12_GetCapabilityParameters() { return &Mock::Allocate; }
    static auto D3D12_DestroyParameters() { return &Mock::Destroy; }
    static auto D3D12_CreateFeature() { return &Mock::Create; }
    static auto D3D12_ReleaseFeature() { return &Mock::Release; }
    static auto D3D12_EvaluateFeature() { return &Mock::Evaluate; }
};
