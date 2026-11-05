#pragma once
#include "SysUtils.h"

#include <Config.h>
#include <State.h>

#include <d3d12.h>

// Anisotropy and mipmap bias overrides, D3D11 and D3D12 samplers use the same filter encoding
namespace SamplerOverrides
{
inline D3D12_FILTER UpgradeToAF(D3D12_FILTER f)
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

// Only mipmapped samplers the game already biased negative, unless MipmapBiasOverrideAll.
// Static samplers can't be biased by the game later, anisotropic ones are texture samplers too
inline void ApplyMipBias(float& bias, float minLOD, float maxLOD, bool anisotropicStatic)
{
    if (!((minLOD != maxLOD && (bias < 0.0f || anisotropicStatic)) ||
          Config::Instance()->MipmapBiasOverrideAll.value_or_default()))
    {
        return;
    }

    if (Config::Instance()->MipmapBiasOverride.has_value())
    {
        const auto value = Config::Instance()->MipmapBiasOverride.value();
        LOG_DEBUG("Overriding mipmap bias {0} -> {1}", bias, value);

        if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
            bias = value;
        else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
            bias = bias * value;
        else
            bias = bias + value;

        bias = std::clamp(bias, -16.0f, 15.99f);
    }

    if (State::Instance().lastMipBiasMax < bias)
        State::Instance().lastMipBiasMax = bias;

    if (State::Instance().lastMipBias > bias)
        State::Instance().lastMipBias = bias;
}

template <typename TFilter> inline void ApplyAnisotropy(TFilter& filter, UINT& maxAnisotropy)
{
    if (!Config::Instance()->AnisotropyOverride.has_value())
        return;

    LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", maxAnisotropy,
              Config::Instance()->AnisotropyOverride.value(), (UINT) filter);

    filter = static_cast<TFilter>(UpgradeToAF(static_cast<D3D12_FILTER>(filter)));
    maxAnisotropy = Config::Instance()->AnisotropyOverride.value();
}

template <typename TDesc> inline void Apply(TDesc& desc, bool staticSampler = false)
{
    const bool anisotropic =
        D3D12_DECODE_IS_ANISOTROPIC_FILTER(static_cast<D3D12_FILTER>(desc.Filter)) || desc.MaxAnisotropy > 1;

    ApplyMipBias(desc.MipLODBias, desc.MinLOD, desc.MaxLOD, staticSampler && anisotropic);
    ApplyAnisotropy(desc.Filter, desc.MaxAnisotropy);
}
} // namespace SamplerOverrides
