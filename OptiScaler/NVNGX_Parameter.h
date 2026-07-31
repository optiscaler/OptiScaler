#pragma once

// Use real NVNGX params encapsulated in custom one
// Which is not working correctly
// #define ENABLE_ENCAPSULATED_PARAMS

// Log NVParam Set/Get operations
// #define LOG_PARAMS_VALUES

#ifdef LOG_PARAMS_VALUES
#define LOG_PARAM(msg, ...) spdlog::trace(__FUNCTION__ " " msg, ##__VA_ARGS__)
#else
#define LOG_PARAM(msg, ...)
#endif

/** @brief Indicates the lifetime management required by an NGX parameter table. */
namespace NGX_AllocTypes
{
// Key used to get/set enum from table
constexpr std::string_view AllocKey = "OptiScaler.ParamAllocType";

constexpr uint32_t Unknown = 0;
// Standard behavior in modern DLSS. Created with NGX Allocate(). Freed with Destroy().
constexpr uint32_t NVDynamic = 1;
// Legacy DLSS. Lifetime managed internally by the SDK.
constexpr uint32_t NVPersistent = 2;
// OptiScaler implementation used internally with new/delete.
constexpr uint32_t InternDynamic = 3;
// OptiScaler implementation for legacy applications. Must maintain a persistent instance
// for the lifetime of the application.
constexpr uint32_t InternPersistent = 4;
} // namespace NGX_AllocTypes

// inline static std::optional<float> GetQualityOverrideRatio(const NVSDK_NGX_PerfQuality_Value input);

/// @brief Callback invoked by the game/SDK to calculate optimal DLSS render settings (resolution, scaling) based on
/// inputs.
/// @param InParams The parameter object containing input width/height and output destinations.
/// @return Success or Failure result code.
static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSS_GetOptimalSettingsCallback(NVSDK_NGX_Parameter* InParams);

/// @brief Callback invoked by the game/SDK to calculate optimal DLSS-D (Ray Reconstruction) settings.
/// @param InParams The parameter object containing input width/height and output destinations.
/// @return Success or Failure result code.
static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSSD_GetOptimalSettingsCallback(NVSDK_NGX_Parameter* InParams);

/// @brief Callback used to retrieve statistics for DLSS.
static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSS_GetStatsCallback(NVSDK_NGX_Parameter* InParams);

/// @brief Initializes an NGX parameter object with supported feature flags (DLSS, FrameGen), version info, and default
/// values.
void InitNGXParameters(NVSDK_NGX_Parameter* InParams, API api);

/// @brief Internal variant structure holding the value of a single NGX parameter.
struct Parameter
{
    template <typename T> void operator=(T value)
    {
        if constexpr (std::is_same<T, void*>::value || std::is_same<T, ID3D11Resource*>::value ||
                      std::is_same<T, ID3D12Resource*>::value)
        {
            key = typeid(void*).hash_code();
            values.vp = (void*) value;
        }
        else
        {
            key = typeid(T).hash_code();
            if constexpr (std::is_same<T, float>::value)
                values.f = value;
            else if constexpr (std::is_same<T, int>::value)
                values.i = value;
            else if constexpr (std::is_same<T, unsigned int>::value)
                values.ui = value;
            else if constexpr (std::is_same<T, double>::value)
                values.d = value;
            else if constexpr (std::is_same<T, unsigned long long>::value)
                values.ull = value;
        }
    }

    template <typename T> operator T() const
    {
        T v = {};

        bool isPointerKey = (key == typeid(void*).hash_code() || key == typeid(ID3D11Resource*).hash_code() ||
                             key == typeid(ID3D12Resource*).hash_code());

        if constexpr (std::is_same<T, float>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
        }
        else if constexpr (std::is_same<T, int>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, unsigned int>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, double>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, unsigned long long>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
            else if (isPointerKey)
                v = (T) values.vp;
        }
        else if constexpr (std::is_same<T, void*>::value)
        {
            if (isPointerKey)
                v = values.vp;
        }
        else if constexpr (std::is_same<T, ID3D11Resource*>::value)
        {
            if (isPointerKey)
                v = (T) values.vp;
        }
        else if constexpr (std::is_same<T, ID3D12Resource*>::value)
        {
            if (isPointerKey)
                v = (T) values.vp;
        }

        return v;
    }

    union
    {
        float f;
        double d;
        int i;
        unsigned int ui;
        unsigned long long ull;
        void* vp;
        ID3D11Resource* d11r;
        ID3D12Resource* d12r;
    } values;

    size_t key = 0;
};

/// @brief Implementation of the NVSDK_NGX_Parameter interface, providing thread-safe storage and retrieval of NGX
/// parameters.
struct NVNGX_Parameters : public NVSDK_NGX_Parameter
{
    API Api;

#ifdef ENABLE_ENCAPSULATED_PARAMS
    NVSDK_NGX_Parameter* OriginalParam = nullptr;
#endif // ENABLE_ENCAPSULATED_PARAMS

    NVNGX_Parameters(API api, bool isPersistent);

    void Set(const char* key, unsigned long long value) override;
    void Set(const char* key, float value) override;
    void Set(const char* key, double value) override;
    void Set(const char* key, unsigned int value) override;
    void Set(const char* key, int value) override;
    void Set(const char* key, void* value) override;
    void Set(const char* key, ID3D11Resource* value) override;
    void Set(const char* key, ID3D12Resource* value) override;

    NVSDK_NGX_Result Get(const char* key, unsigned long long* value) const override;

    NVSDK_NGX_Result Get(const char* key, float* value) const override;

    NVSDK_NGX_Result Get(const char* key, double* value) const override;

    NVSDK_NGX_Result Get(const char* key, unsigned int* value) const override;

    NVSDK_NGX_Result Get(const char* key, int* value) const override;

    NVSDK_NGX_Result Get(const char* key, void** value) const override;

    NVSDK_NGX_Result Get(const char* key, ID3D11Resource** value) const override;

    NVSDK_NGX_Result Get(const char* key, ID3D12Resource** value) const override;

    void Reset() override;

    std::vector<std::string> enumerate() const;

  private:
    ankerl::unordered_dense::map<std::string, Parameter> m_values;
    mutable std::mutex m_mutex;

    template <typename T> void setT(const char* key, T& value);

    template <typename T> NVSDK_NGX_Result getT(const char* key, T* value) const;
};

/**
 * @brief Allocates and populates a new custom NGX param map. The persistence flag indicates
 * whether the table should be destroyed when NGX DestroyParameters() is used.
 */
NVNGX_Parameters* GetNGXParameters(API api, bool isPersistent);

/**
 * @brief Sets a custom tracking tag to indicate the memory management strategy required by
 * the table, indicated by NGX_AllocTypes.
 */
void SetNGXParamAllocType(NVSDK_NGX_Parameter& params, uint32_t allocType);
/**
 * @brief Attempts to safely delete an NGX parameter table. Dynamically allocated NGX tables use the NGX API.
 * OptiScaler tables use delete. Persistent tables are not freed.
 */
template <typename PFN_DestroyNGXParameters>
bool TryDestroyNGXParameters(NVSDK_NGX_Parameter* InParameters, PFN_DestroyNGXParameters NVFree)
{
    if (InParameters == nullptr)
        return false;

    uint32_t allocType = NGX_AllocTypes::Unknown;
    NVSDK_NGX_Result result = InParameters->Get(NGX_AllocTypes::AllocKey.data(), &allocType);

    // Key not set. Either a bug, or the client application called Reset() on the table before destroying.
    // Derived type unknown if this happens. Not safe to delete. Leaking is the best option.
    if (result == NVSDK_NGX_Result_Fail)
    {
        LOG_WARN("Destroy called on NGX table with unset alloc type. Leaking.");
        return false;
    }

    if (allocType == NGX_AllocTypes::NVDynamic)
    {
        if (NVFree != nullptr)
        {
            LOG_INFO("Calling NVFree");
            result = NVFree(InParameters);
            LOG_INFO("Calling NVFree result: {0:X}", (UINT) result);
            return true;
        }
        else
            return false;
    }
    else if (allocType == NGX_AllocTypes::InternDynamic)
    {
        delete static_cast<NVNGX_Parameters*>(InParameters);
        return true;
    }

    return false;
}
