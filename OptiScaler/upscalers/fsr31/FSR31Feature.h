#pragma once

#include "ffx_upscale.h"

#include <upscalers/IFeature.h>

inline static void FfxLogCallback(uint32_t type, const wchar_t* message)
{
    std::wstring string(message);
    LOG_DEBUG("FSR Runtime: {0}", wstring_to_string(string));
}

class FSR31Feature : public virtual IFeature
{
  private:
    double _lastFrameTime;
    unsigned int _lastWidth = 0;
    unsigned int _lastHeight = 0;
    static inline feature_version _version { 3, 1, 2 };

  protected:
    std::string _name = "FSR";

    ffxContext _context = nullptr;
    ffxCreateContextDescUpscale _contextDesc = {};

    virtual bool InitFSR3(const NVSDK_NGX_Parameter* InParameters) = 0;

    double GetDeltaTime();

    static inline void parse_version(const char* version_str)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                if (sscanf(p, "%u.%u.%u", &_version.major, &_version.minor, &_version.patch) == 3)
                    return;
            }
            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

    // What FFX offers on this device, the upscaler list of the menu
    static inline void LogUpscalerVersions()
    {
        auto& state = State::Instance();

        for (size_t i = 0; i < state.ffxUpscalerVersionNames.size(); i++)
            LOG_INFO("FFX upscaler [{}]: {}, id: {:X}", i, state.ffxUpscalerVersionNames[i],
                     state.ffxUpscalerVersionIds[i]);
    }

    static inline uint32_t ffxResolveTypelessFormat(uint32_t format)
    {
        switch (format)
        {
        case FFX_API_SURFACE_FORMAT_R10G10B10A2_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;

        case FFX_API_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;

        case FFX_API_SURFACE_FORMAT_R16G16B16A16_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;

        case FFX_API_SURFACE_FORMAT_R32G32_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32G32_FLOAT;

        case FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

        case FFX_API_SURFACE_FORMAT_B8G8R8A8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;

        case FFX_API_SURFACE_FORMAT_R16G16_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;

        case FFX_API_SURFACE_FORMAT_R32_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R32_FLOAT;

        case FFX_API_SURFACE_FORMAT_R8G8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R8G8_UNORM;

        case FFX_API_SURFACE_FORMAT_R16_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R16_FLOAT;

        case FFX_API_SURFACE_FORMAT_R8_TYPELESS:
            return FFX_API_SURFACE_FORMAT_R8_UNORM;

        default:
            return format; // Already typed or unknown
        }
    }

    float _velocity = 1.0f;
    float _reactiveScale = 1.0f;
    float _shadingScale = 1.0f;
    float _accAddPerFrame = 0.333f;
    float _minDisOccAcc = -0.333f;

  public:
    feature_version Version() override { return _version; }
    std::string Name() const override { return _name.c_str(); }

    FSR31Feature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~FSR31Feature();
};
