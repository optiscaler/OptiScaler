#pragma once

namespace DlssNr
{
// Persisted in [DlssNr] PrivateUpscaler. The game-facing backend is independent.
enum class PrivateUpscaler
{
    DLSS,
    FSR22,
    FFX,
    XeSS
};
inline PrivateUpscaler GetPrivateUpscaler(int value)
{
    return value >= 0 && value <= 3 ? static_cast<PrivateUpscaler>(value) : PrivateUpscaler::DLSS;
}
inline const char* PrivateUpscalerName(PrivateUpscaler backend)
{
    switch (backend)
    {
    case PrivateUpscaler::FSR22:
        return "FSR 2.2";
    case PrivateUpscaler::FFX:
        return "FSR (FidelityFX)";
    case PrivateUpscaler::XeSS:
        return "XeSS";
    default:
        return "DLSS";
    }
}
} // namespace DlssNr
