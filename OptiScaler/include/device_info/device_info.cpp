// From https://github.com/GPUOpen-Tools/device_info licensed under MIT
#include "pch.h"

//==============================================================================
/// Copyright Advanced Micro Devices, Inc. All rights reserved.
/// @author AMD Developer Tools Team
/// @file
/// @brief  device_info library implementation.
//==============================================================================

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <type_traits>

#include "device_info.hpp"

namespace
{
using device_info::AdapterId;
using device_info::AsicType;
using device_info::CardInfo;
using device_info::DeviceInfo;
using device_info::HwGeneration;
using enum HwGeneration;

static_assert(std::numeric_limits<std::underlying_type_t<HwGeneration>>::max() >=
                  static_cast<std::underlying_type_t<HwGeneration>>(HwGeneration::kTotalHwGenerations),
              "HwGeneration exceeds range of underlying type");

// If this happens we need to consider changing the underlying type of AsicType to a larger integer type
// Or removing older ASIC types from the enum if they are no longer relevant.
static_assert(std::numeric_limits<std::underlying_type_t<AsicType>>::max() >=
                  static_cast<std::underlying_type_t<AsicType>>(AsicType::kTotalAsics),
              "AsicType exceeds range of underlying type");

/// Check if the hardware generation is an AMD generation.
///
/// @param [in] gen Hardware generation
///
/// @return True if the hardware generation is an AMD generation
[[nodiscard]] constexpr bool IsAmdGeneration(const HwGeneration gen) noexcept
{
    return gen >= HwGeneration::kFirstAmd && gen < HwGeneration::kTotalHwGenerations;
}

/// Sorting function for CardInfo, sorts by device ID first, then by revision ID.
///
/// @param [in] a First CardInfo to compare.
/// @param [in] b Second CardInfo to compare.
///
/// @return True if a should come before b in sorted order, false otherwise.
[[nodiscard]] constexpr bool SortCardInfo(const CardInfo& a, const CardInfo& b) noexcept
{
    if (a.device_id != b.device_id)
    {
        return a.device_id < b.device_id;
    }
    return a.revision_id < b.revision_id;
}

// The card info table is sorted by device ID for faster lookup.
// Sorting is handled at compile time, so it doesn't affect runtime performance and makes adding new devices easier.
static constexpr auto kAmdCardInfo = []() consteval
{
    using enum AsicType;
    using enum HwGeneration;

    auto devices = std::to_array<CardInfo>({
        { kTahitiXt, 0x6798, 0x00, kSouthernIsland, false, "Tahiti", "AMD Radeon R9 200 / HD 7900 Series" },
        { kTahitiXt, 0x6799, 0x00, kSouthernIsland, false, "Tahiti", "AMD Radeon HD 7900 Series" },
        { kTahitiPro, 0x679A, 0x00, kSouthernIsland, false, "Tahiti", "AMD Radeon HD 7900 Series" },
        { kTahitiPro, 0x679B, 0x00, kSouthernIsland, false, "Tahiti", "AMD Radeon HD 7900 Series" },
        { kTahitiPro, 0x679E, 0x00, kSouthernIsland, false, "Tahiti", "AMD Radeon HD 7800 Series" },
        { kTahitiXt, 0x6780, 0x00, kSouthernIsland, false, "Tahiti", "AMD FirePro W9000" },
        { kTahitiPro, 0x6784, 0x00, kSouthernIsland, false, "Tahiti", "ATI FirePro V (FireGL V) Graphics Adapter" },
        { kTahitiXt, 0x6788, 0x00, kSouthernIsland, false, "Tahiti", "ATI FirePro V (FireGL V) Graphics Adapter" },
        { kTahitiPro, 0x678A, 0x00, kSouthernIsland, false, "Tahiti", "AMD FirePro W8000" },

        { kPitcairnXt, 0x6818, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon HD 7800 Series" },
        { kPitcairnPro, 0x6819, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon HD 7800 Series" },
        { kPitcairnXt, 0x6808, 0x00, kSouthernIsland, false, "Pitcairn", "AMD FirePro W7000" },
        { kPitcairnXt, 0x6809, 0x00, kSouthernIsland, false, "Pitcairn", "ATI FirePro W5000" },
        { kPitcairnXt, 0x684C, 0x00, kSouthernIsland, false, "Pitcairn", "ATI FirePro V(FireGL V) Graphics Adapter" },
        { kPitcairnXt, 0x6800, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon HD 7970M" },
        { kPitcairnPro, 0x6801, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon(TM) HD8970M" },
        { kPitcairnXt, 0x6806, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon (TM) R9 M290X" },
        { kPitcairnXt, 0x6810, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon R9 200 Series" },
        { kPitcairnXt, 0x6810, 0x81, kSouthernIsland, false, "Pitcairn", "AMD Radeon (TM) R9 370 Series" },
        { kPitcairnPro, 0x6811, 0x00, kSouthernIsland, false, "Pitcairn", "AMD Radeon R9 200 Series" },
        { kPitcairnPro, 0x6811, 0x81, kSouthernIsland, false, "Pitcairn", "AMD Radeon (TM) R7 370 Series" },

        { kCapeVerdeXt, 0x6820, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon R9 M275X" },
        { kCapeVerdeXt, 0x6820, 0x81, kSouthernIsland, false, "Capeverde", "AMD Radeon (TM) R9 M375" },
        { kCapeVerdeXt, 0x6820, 0x83, kSouthernIsland, false, "Capeverde", "AMD Radeon (TM) R9 M375X" },
        { kCapeVerdeXt, 0x6821, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon R9 M200X Series" },
        { kCapeVerdeXt, 0x6821, 0x83, kSouthernIsland, false, "Capeverde", "AMD Radeon R9 (TM) M370X" },
        { kCapeVerdeXt, 0x6821, 0x87, kSouthernIsland, false, "Capeverde", "AMD Radeon (TM) R7 M380" },
        { kCapeVerdePro, 0x6822, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon E8860" },
        { kCapeVerdePro, 0x6823, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon R9 M200X Series" },
        { kCapeVerdeXt, 0x6825, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7800M Series" },
        { kCapeVerdePro, 0x6826, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700M Series" },
        { kCapeVerdePro, 0x6827, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7800M Series" },
        { kCapeVerdeXt, 0x682B, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 8800M Series" },
        { kCapeVerdeXt, 0x682B, 0x87, kSouthernIsland, false, "Capeverde", "AMD Radeon (TM) R9 M360" },
        { kCapeVerdeXt, 0x682D, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700M Series" },
        { kCapeVerdePro, 0x682F, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700M Series" },

        { kCapeVerdeXt, 0x6828, 0x00, kSouthernIsland, false, "Capeverde", "AMD FirePro W600" },
        { kCapeVerdePro, 0x682C, 0x00, kSouthernIsland, false, "Capeverde", "AMD FirePro W4100" },
        { kCapeVerdeXt, 0x6830, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon 7800M Series" },
        { kCapeVerdeXt, 0x6831, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon 7700M Series" },
        { kCapeVerdePro, 0x6835, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon R7 Series / HD 9000 Series" },
        { kCapeVerdeXt, 0x6837, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700 Series" },
        { kCapeVerdeXt, 0x683D, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700 Series" },
        { kCapeVerdePro, 0x683F, 0x00, kSouthernIsland, false, "Capeverde", "AMD Radeon HD 7700 Series" },

        { kOland, 0x6608, 0x00, kSouthernIsland, false, "Oland", "AMD FirePro W2100" },
        { kOland, 0x6610, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6610, 0x81, kSouthernIsland, false, "Oland", "AMD Radeon (TM) R7 350" },
        { kOland, 0x6610, 0x83, kSouthernIsland, false, "Oland", "AMD Radeon (TM) R5 340" },
        { kOland, 0x6610, 0x87, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6611, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6611, 0x87, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6613, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6617, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 240 Series" },
        { kOland, 0x6617, 0x87, kSouthernIsland, false, "Oland", "AMD Radeon R7 200 Series" },
        { kOland, 0x6617, 0xC7, kSouthernIsland, false, "Oland", "AMD Radeon R7 240 Series" },
        { kOland, 0x6600, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon HD 8600/8700M" },
        { kOland, 0x6600, 0x81, kSouthernIsland, false, "Oland", "AMD Radeon (TM) R7 M370" },
        { kOland, 0x6601, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon (TM) HD 8500M/8700M" },
        { kOland, 0x6604, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 M265 Series" },
        { kOland, 0x6604, 0x81, kSouthernIsland, false, "Oland", "AMD Radeon (TM) R7 M350" },
        { kOland, 0x6605, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R7 M260 Series" },
        { kOland, 0x6605, 0x81, kSouthernIsland, false, "Oland", "AMD Radeon (TM) R7 M340" },
        { kOland, 0x6606, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon HD 8790M" },
        { kOland, 0x6607, 0x00, kSouthernIsland, false, "Oland", "AMD Radeon R5 M240" },

        { kHainan, 0x6660, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon HD 8600M Series" },
        { kHainan, 0x6660, 0x81, kSouthernIsland, false, "Hainan", "AMD Radeon (TM) R5 M335" },
        { kHainan, 0x6660, 0x83, kSouthernIsland, false, "Hainan", "AMD Radeon (TM) R5 M330" },
        { kHainan, 0x6663, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon HD 8500M Series" },
        { kHainan, 0x6663, 0x83, kSouthernIsland, false, "Hainan", "AMD Radeon (TM) R5 M320" },
        { kHainan, 0x6664, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon R5 M200 Series" },
        { kHainan, 0x6665, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon R5 M230 Series" },
        { kHainan, 0x6665, 0x83, kSouthernIsland, false, "Hainan", "AMD Radeon (TM) R5 M320" },
        { kHainan, 0x6665, 0xC3, kSouthernIsland, false, "Hainan", "AMD Radeon R5 M435" },
        { kHainan, 0x6666, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon R5 M200 Series" },
        { kHainan, 0x6667, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon R5 M200 Series" },
        { kHainan, 0x666F, 0x00, kSouthernIsland, false, "Hainan", "AMD Radeon HD 8500M" },

        { kBonaire, 0x6649, 0x00, kSeaIsland, false, "Bonaire", "AMD FirePro W5100" },
        { kBonaire, 0x6658, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon R7 200 Series" },
        { kBonaire, 0x665C, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon HD 7700 Series" },
        { kBonaire, 0x665D, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon R7 200 Series" },
        { kBonaire, 0x665F, 0x81, kSeaIsland, false, "Bonaire", "AMD Radeon (TM) R7 360 Series" },
        { kBonaire, 0x6640, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon HD 8950" },
        { kBonaire, 0x6640, 0x80, kSeaIsland, false, "Bonaire", "AMD Radeon (TM) R9 M380" },
        { kBonaire, 0x6646, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon R9 M280X" },
        { kBonaire, 0x6646, 0x80, kSeaIsland, false, "Bonaire", "AMD Radeon (TM) R9 M385" },
        { kBonaire, 0x6647, 0x00, kSeaIsland, false, "Bonaire", "AMD Radeon R9 M200X Series" },
        { kBonaire, 0x6647, 0x80, kSeaIsland, false, "Bonaire", "AMD Radeon (TM) R9 M380" },

        { kHawaii, 0x67A0, 0x00, kSeaIsland, false, "Hawaii", "AMD FirePro W9100" },
        { kHawaii, 0x67A1, 0x00, kSeaIsland, false, "Hawaii", "AMD FirePro W8100" },
        { kHawaii, 0x67B0, 0x00, kSeaIsland, false, "Hawaii", "AMD Radeon R9 200 Series" },
        { kHawaii, 0x67B0, 0x80, kSeaIsland, false, "Hawaii", "AMD Radeon (TM) R9 390 Series" },
        { kHawaii, 0x67B1, 0x00, kSeaIsland, false, "Hawaii", "AMD Radeon R9 200 Series" },
        { kHawaii, 0x67B1, 0x80, kSeaIsland, false, "Hawaii", "AMD Radeon (TM) R9 390 Series" },
        { kHawaii, 0x67B9, 0x00, kSeaIsland, false, "Hawaii", "AMD Radeon R9 200 Series" },

        { kSpectreLite, 0x1309, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x130A, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R6 Graphics" },
        { kSpectre, 0x130C, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x130D, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R6 Graphics" },
        { kSpectreSl, 0x130E, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectre, 0x130F, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectre, 0x130F, 0xD4, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectre, 0x130F, 0xD5, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectre, 0x130F, 0xD6, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectre, 0x130F, 0xD7, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x1313, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x1313, 0xD4, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x1313, 0xD5, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x1313, 0xD6, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreSl, 0x1315, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectreSl, 0x1315, 0xD4, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectreSl, 0x1315, 0xD5, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectreSl, 0x1315, 0xD6, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectreSl, 0x1315, 0xD7, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectreSl, 0x1318, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R5 Graphics" },
        { kSpectre, 0x131C, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R7 Graphics" },
        { kSpectreLite, 0x131D, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R6 Graphics" },
        { kSpooky, 0x130B, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R4 Graphics" },
        { kSpooky, 0x1316, 0x00, kSeaIsland, true, "Spooky", "AMD Radeon(TM) R5 Graphics" },
        { kSpooky, 0x131B, 0x00, kSeaIsland, true, "Spectre", "AMD Radeon(TM) R4 Graphics" },

        { kKalindi, 0x9830, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8400 / R3 Series" },
        { kKalindi, 0x9831, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon(TM) HD 8400E" },
        { kKalindi, 0x9832, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8330" },
        { kKalindi, 0x9833, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon(TM) HD 8330E" },
        { kKalindi, 0x9834, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8210" },
        { kKalindi, 0x9835, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon(TM) HD 8210E" },
        { kKalindi, 0x9836, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8200 / R3 Series" },
        { kKalindi, 0x9837, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon(TM) HD 8280E" },
        { kKalindi, 0x9838, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8200 / R3 series" },
        { kKalindi, 0x9839, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8180" },
        { kKalindi, 0x983D, 0x00, kSeaIsland, true, "Kalindi", "AMD Radeon HD 8250" },

        { kKalindi, 0x9850, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9850, 0x03, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9850, 0x40, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9850, 0x45, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9851, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R4 Graphics" },
        { kKalindi, 0x9851, 0x01, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R5E Graphics" },
        { kKalindi, 0x9851, 0x05, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R5 Graphics" },
        { kKalindi, 0x9851, 0x06, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R5E Graphics" },
        { kKalindi, 0x9851, 0x40, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R4 Graphics" },
        { kKalindi, 0x9851, 0x45, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R5 Graphics" },
        { kKalindi, 0x9852, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9852, 0x40, kSeaIsland, true, "Mullins", "AMD Radeon(TM) E1 Graphics" },
        { kKalindi, 0x9853, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9853, 0x01, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R4E Graphics" },
        { kKalindi, 0x9853, 0x03, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9853, 0x05, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R1E Graphics" },
        { kKalindi, 0x9853, 0x06, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R1E Graphics" },
        { kKalindi, 0x9853, 0x40, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },

        { kKalindi, 0x9853, 0x07, kSeaIsland, true, "Mullins", "AMD Radeon R1E Graphics" },
        { kKalindi, 0x9853, 0x08, kSeaIsland, true, "Mullins", "AMD Radeon R1E Graphics" },
        { kKalindi, 0x9854, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9854, 0x01, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3E Graphics" },
        { kKalindi, 0x9854, 0x02, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9854, 0x05, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9854, 0x06, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R4 Graphics" },
        { kKalindi, 0x9854, 0x07, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R3 Graphics" },
        { kKalindi, 0x9855, 0x02, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R6 Graphics" },
        { kKalindi, 0x9855, 0x05, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R4 Graphics" },
        { kKalindi, 0x9856, 0x00, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9856, 0x01, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2E Graphics" },
        { kKalindi, 0x9856, 0x02, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9856, 0x05, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R1E Graphics" },
        { kKalindi, 0x9856, 0x06, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R2 Graphics" },
        { kKalindi, 0x9856, 0x07, kSeaIsland, true, "Mullins", "AMD Radeon(TM) R1E Graphics" },
        { kKalindi, 0x9856, 0x08, kSeaIsland, true, "Mullins", "AMD Radeon R1E Graphics" },
        { kKalindi, 0x9856, 0x13, kSeaIsland, true, "Mullins", "AMD Radeon R1E Graphics" },

        { kIceland, 0x6900, 0x00, kVolcanicIsland, false, "Iceland", "AMD Radeon R7 M260" },
        { kIceland, 0x6900, 0x81, kVolcanicIsland, false, "Iceland", "AMD Radeon (TM) R7 M360" },
        { kIceland, 0x6900, 0x83, kVolcanicIsland, false, "Iceland", "AMD Radeon (TM) R7 M340" },
        { kIceland, 0x6900, 0xC1, kVolcanicIsland, false, "Iceland", "AMD Radeon R5 M465 Series" },
        { kIceland, 0x6900, 0xC3, kVolcanicIsland, false, "Iceland", "AMD Radeon R5 M445 Series" },
        { kIceland, 0x6900, 0xD1, kVolcanicIsland, false, "Iceland", "Radeon 530 Series" },
        { kIceland, 0x6900, 0xD3, kVolcanicIsland, false, "Iceland", "Radeon 530 Series" },
        { kIceland, 0x6901, 0x00, kVolcanicIsland, false, "Iceland", "AMD Radeon R5 M255" },
        { kIceland, 0x6902, 0x00, kVolcanicIsland, false, "Iceland", "AMD Radeon Series" },
        { kIceland, 0x6907, 0x00, kVolcanicIsland, false, "Iceland", "AMD Radeon R5 M255" },
        { kIceland, 0x6907, 0x87, kVolcanicIsland, false, "Iceland", "AMD Radeon (TM) R5 M315" },

        { kTonga, 0x6920, 0x00, kVolcanicIsland, false, "Tonga", "AMD RADEON R9 M395X" },
        { kTonga, 0x6920, 0x01, kVolcanicIsland, false, "Tonga", "AMD RADEON R9 M390X" },
        { kTonga, 0x6921, 0x00, kVolcanicIsland, false, "Tonga", "AMD Radeon (TM) R9 M390X" },
        { kTonga, 0x6929, 0x00, kVolcanicIsland, false, "Tonga", "AMD FirePro S7150" },
        { kTonga, 0x6929, 0x01, kVolcanicIsland, false, "Tonga", "AMD FirePro S7100X" },
        { kTonga, 0x692B, 0x00, kVolcanicIsland, false, "Tonga", "AMD FirePro W7100" },
        { kTonga, 0x692F, 0x00, kVolcanicIsland, false, "Tonga", "AMD MxGPU" },
        { kTonga, 0x692F, 0x01, kVolcanicIsland, false, "Tonga", "AMD MxGPU" },
        { kTonga, 0x6930, 0xF0, kVolcanicIsland, false, "Tonga", "AMD MxGPU" },
        { kTonga, 0x6938, 0x00, kVolcanicIsland, false, "Tonga", "AMD Radeon R9 200 Series" },
        { kTonga, 0x6938, 0xF1, kVolcanicIsland, false, "Tonga", "AMD Radeon (TM) R9 380 Series" },
        { kTonga, 0x6938, 0xF0, kVolcanicIsland, false, "Tonga", "AMD Radeon R9 200 Series" },
        { kTonga, 0x6939, 0x00, kVolcanicIsland, false, "Tonga", "AMD Radeon R9 200 Series" },
        { kTonga, 0x6939, 0xF0, kVolcanicIsland, false, "Tonga", "AMD Radeon R9 200 Series" },
        { kTonga, 0x6939, 0xF1, kVolcanicIsland, false, "Tonga", "AMD Radeon (TM) R9 380 Series" },

        { kCarrizo, 0x9874, 0xC4, kVolcanicIsland, true, "Carrizo", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xC5, kVolcanicIsland, true, "Carrizo", "AMD Radeon R6 Graphics" },
        { kCarrizo, 0x9874, 0xC6, kVolcanicIsland, true, "Carrizo", "AMD Radeon R6 Graphics" },
        { kCarrizo, 0x9874, 0xC7, kVolcanicIsland, true, "Carrizo", "AMD Radeon R5 Graphics" },
        { kCarrizoEmb, 0x9874, 0x81, kVolcanicIsland, true, "Carrizo", "AMD Radeon R6 Graphics" },
        { kCarrizoEmb, 0x9874, 0x84, kVolcanicIsland, true, "Carrizo", "AMD Radeon R7 Graphics" },
        { kCarrizoEmb, 0x9874, 0x85, kVolcanicIsland, true, "Carrizo", "AMD Radeon R6 Graphics" },
        { kCarrizoEmb, 0x9874, 0x87, kVolcanicIsland, true, "Carrizo", "AMD Radeon R5 Graphics" },
        { kCarrizoEmb, 0x9874, 0x88, kVolcanicIsland, true, "Carrizo", "AMD Radeon R7E Graphics" },
        { kCarrizoEmb, 0x9874, 0x89, kVolcanicIsland, true, "Carrizo", "AMD Radeon R6E Graphics" },
        { kCarrizo, 0x9874, 0xC8, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xC9, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xCA, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R5 Graphics" },
        { kCarrizo, 0x9874, 0xCB, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R5 Graphics" },
        { kCarrizo, 0x9874, 0xCC, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xCD, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xCE, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R5 Graphics" },
        { kCarrizo, 0x9874, 0xE1, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xE2, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xE3, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xE4, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R7 Graphics" },
        { kCarrizo, 0x9874, 0xE5, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R5 Graphics" },
        { kCarrizo, 0x9874, 0xE6, kVolcanicIsland, true, "Bristol Ridge", "AMD Radeon R5 Graphics" },

        { kFiji, 0x7300, 0x00, kVolcanicIsland, false, "Fiji", "AMD Radeon (TM) Graphics Processor" },
        { kFiji, 0x7300, 0xC0, kVolcanicIsland, false, "Fiji", "AMD Radeon Graphics Processor" },
        { kFiji, 0x7300, 0xC1, kVolcanicIsland, false, "Fiji", "AMD FirePro (TM) S9300 x2" },
        { kFiji, 0x7300, 0xC8, kVolcanicIsland, false, "Fiji", "AMD Radeon (TM) R9 Fury Series" },
        { kFiji, 0x7300, 0xC9, kVolcanicIsland, false, "Fiji", "Radeon (TM) Pro Duo" },
        { kFiji, 0x7300, 0xCA, kVolcanicIsland, false, "Fiji", "AMD Radeon (TM) R9 Fury Series" },
        { kFiji, 0x7300, 0xCB, kVolcanicIsland, false, "Fiji", "AMD Radeon (TM) R9 Fury Series" },
        { kFiji, 0x730F, 0xC9, kVolcanicIsland, false, "Fiji", "AMD MxGPU" },

        { kStoney, 0x98E4, 0x80, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5E Graphics" },
        { kStoney, 0x98E4, 0x81, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4E Graphics" },
        { kStoney, 0x98E4, 0x83, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2E Graphics" },
        { kStoney, 0x98E4, 0x84, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2E Graphics" },
        { kStoney, 0x98E4, 0x86, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R1E Graphics" },
        { kStoney, 0x98E4, 0xC0, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xC1, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xC2, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xC4, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xC6, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xC8, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xC9, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xCA, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xD0, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2 Graphics" },
        { kStoney, 0x98E4, 0xD1, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2 Graphics" },
        { kStoney, 0x98E4, 0xD2, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2 Graphics" },
        { kStoney, 0x98E4, 0xD4, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R2 Graphics" },
        { kStoney, 0x98E4, 0xD9, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xDA, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R5 Graphics" },
        { kStoney, 0x98E4, 0xDB, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R3 Graphics" },
        { kStoney, 0x98E4, 0xE1, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R3 Graphics" },
        { kStoney, 0x98E4, 0xE2, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R3 Graphics" },
        { kStoney, 0x98E4, 0xE9, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xEA, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xEB, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R4 Graphics" },
        { kStoney, 0x98E4, 0xEC, kVolcanicIsland, true, "Stoney", "AMD Radeon(TM) R3 Graphics" },

        { kEllesmere, 0x67C0, 0x00, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) Pro WX 7100 Graphics" },
        { kEllesmere, 0x67C0, 0x80, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) E9550" },
        { kEllesmere, 0x67C1, 0x00, kVolcanicIsland, false, "Ellesmere", "67C1:00" },
        { kEllesmere, 0x67C2, 0x00, kVolcanicIsland, false, "Ellesmere", "67C2:00" },
        { kEllesmere, 0x67C2, 0x01, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) Pro V7350x2" },
        { kEllesmere, 0x67C2, 0x02, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) Pro V7300X" },
        { kEllesmere, 0x67C2, 0x03, kVolcanicIsland, false, "Ellesmere", "67C2:03" },
        { kEllesmere, 0x67C4, 0x00, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) Pro WX 7100 Graphics" },
        { kEllesmere, 0x67C4, 0x80, kVolcanicIsland, false, "Ellesmere", "AMD Radeon Embedded E9560" },
        { kEllesmere, 0x67C7, 0x00, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) Pro WX 5100 Graphics" },
        { kEllesmere, 0x67C7, 0x80, kVolcanicIsland, false, "Ellesmere", "AMD Radeon Embedded E9390" },
        { kEllesmere, 0x67D0, 0x01, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) Pro V7350x2" },
        { kEllesmere, 0x67FF, 0xE3, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) E9550" },
        { kEllesmere, 0x67FF, 0xF3, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) PRO E9565 Graphics" },
        { kEllesmere, 0x67FF, 0xF7, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) Pro WX 5100 Graphics" },
        { kEllesmere, 0x67D0, 0x02, kVolcanicIsland, false, "Ellesmere", "AMD Radeon (TM) Pro V7300X" },
        { kEllesmere, 0x67DF, 0x04, kVolcanicIsland, false, "Ellesmere", "67DF:04" },
        { kEllesmere, 0x67DF, 0x05, kVolcanicIsland, false, "Ellesmere", "67DF:05" },
        { kEllesmere, 0x67DF, 0xC4, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) RX 480 Graphics" },
        { kEllesmere, 0x67DF, 0xC5, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) RX 470 Graphics" },
        { kEllesmere, 0x67DF, 0xC7, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) RX 480 Graphics" },
        { kEllesmere, 0x67DF, 0xCF, kVolcanicIsland, false, "Ellesmere", "Radeon (TM) RX 470 Graphics" },
        { kEllesmere, 0x67DF, 0xFF, kVolcanicIsland, false, "Ellesmere", "Radeon RX 470 Series" },
        { kEllesmere, 0x67FF, 0xE7, kVolcanicIsland, false, "Ellesmere", "AMD Radeon Embedded E9390" },
        { kEllesmere, 0x67DF, 0xC0, kVolcanicIsland, false, "Ellesmere", "Radeon Pro 580X" },
        { kEllesmere, 0x67DF, 0xC1, kVolcanicIsland, false, "Ellesmere", "Radeon RX 580 Series" },
        { kEllesmere, 0x67DF, 0xC2, kVolcanicIsland, false, "Ellesmere", "Radeon RX 570 Series" },
        { kEllesmere, 0x67DF, 0xC3, kVolcanicIsland, false, "Ellesmere", "Radeon RX 580 Series" },
        { kEllesmere, 0x67DF, 0xC6, kVolcanicIsland, false, "Ellesmere", "Radeon RX 570 Series" },
        { kEllesmere, 0x67DF, 0xCC, kVolcanicIsland, false, "Ellesmere", "67DF:CC" },
        { kEllesmere, 0x67DF, 0xCD, kVolcanicIsland, false, "Ellesmere", "67DF:CD" },
        { kEllesmere, 0x67DF, 0xD7, kVolcanicIsland, false, "Ellesmere", "Radeon(TM) RX 470 Graphics" },
        { kEllesmere, 0x67DF, 0xE0, kVolcanicIsland, false, "Ellesmere", "Radeon RX 470 Series" },
        { kEllesmere, 0x67DF, 0xE1, kVolcanicIsland, false, "Ellesmere", "Radeon RX 590 Series" },
        { kEllesmere, 0x67DF, 0xE3, kVolcanicIsland, false, "Ellesmere", "Radeon RX Series" },
        { kEllesmere, 0x67DF, 0xE7, kVolcanicIsland, false, "Ellesmere", "Radeon RX 580 Series" },
        { kEllesmere, 0x67DF, 0xEB, kVolcanicIsland, false, "Ellesmere", "Radeon Pro 580X" },
        { kEllesmere, 0x67DF, 0xEF, kVolcanicIsland, false, "Ellesmere", "Radeon RX 570 Series" },
        { kEllesmere, 0x67DF, 0xF7, kVolcanicIsland, false, "Ellesmere", "P30PH" },
        { kEllesmere, 0x6FDF, 0xEF, kVolcanicIsland, false, "Ellesmere", "AMD Radeon RX 580 2048SP" },
        { kEllesmere, 0x6FDF, 0xFF, kVolcanicIsland, false, "Ellesmere", "6FDF:FF" },

        { kBaffin, 0x67E0, 0x00, kVolcanicIsland, false, "Baffin", "Radeon (TM) Pro WX Series" },
        { kBaffin, 0x67E3, 0x00, kVolcanicIsland, false, "Baffin", "Radeon (TM) Pro WX 4100" },
        { kBaffin, 0x67E8, 0x00, kVolcanicIsland, false, "Baffin", "Radeon (TM) Pro WX Series" },
        { kBaffin, 0x67E8, 0x01, kVolcanicIsland, false, "Baffin", "Radeon (TM) Pro WX Series" },
        { kBaffin, 0x67E8, 0x80, kVolcanicIsland, false, "Baffin", "AMD Radeon (TM) E9260" },
        { kBaffin, 0x67EB, 0x00, kVolcanicIsland, false, "Baffin", "Radeon (TM) Pro V5300X" },
        { kBaffin, 0x67EF, 0xC0, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xC1, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xC5, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xC7, kVolcanicIsland, false, "Baffin", "Radeon 550 Series" },
        { kBaffin, 0x67EF, 0xCF, kVolcanicIsland, false, "Baffin", "Radeon(TM) RX 460 Graphics" },
        { kBaffin, 0x67EF, 0xEF, kVolcanicIsland, false, "Baffin", "Radeon 550 Series" },
        { kBaffin, 0x67FF, 0xC0, kVolcanicIsland, false, "Baffin", "AMD Radeon Pro 465" },
        { kBaffin, 0x67FF, 0xC1, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xC2, kVolcanicIsland, false, "Baffin", "Radeon Pro Series" },
        { kBaffin, 0x67EF, 0xC3, kVolcanicIsland, false, "Baffin", "67EF:C3" },
        { kBaffin, 0x67EF, 0xE2, kVolcanicIsland, false, "Baffin", "RX 560X" },
        { kBaffin, 0x67EF, 0xE3, kVolcanicIsland, false, "Baffin", "Radeon Pro Series" },
        { kBaffin, 0x67EF, 0xE5, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xE7, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xE0, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67EF, 0xFF, kVolcanicIsland, false, "Baffin", "Radeon(TM) RX 460 Graphics" },
        { kBaffin, 0x67FF, 0x08, kVolcanicIsland, false, "Baffin", "67FF:08" },
        { kBaffin, 0x67FF, 0xCF, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67FF, 0xEF, kVolcanicIsland, false, "Baffin", "Radeon RX 560 Series" },
        { kBaffin, 0x67FF, 0xFF, kVolcanicIsland, false, "Baffin", "Radeon RX550/550 Series" },

        { kGfx8_0_4, 0x6980, 0x00, kVolcanicIsland, false, "gfx804", "Radeon Pro WX 3100" },
        { kGfx8_0_4, 0x6981, 0x00, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 3200 Series" },
        { kGfx8_0_4, 0x6981, 0x01, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 3200 Series" },
        { kGfx8_0_4, 0x6981, 0x10, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 3200 Series" },
        { kGfx8_0_4, 0x6981, 0xC0, kVolcanicIsland, false, "gfx804", "6981:C0" },
        { kGfx8_0_4, 0x6984, 0x80, kVolcanicIsland, false, "gfx804", "6984:80" },
        { kGfx8_0_4, 0x6985, 0x00, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 3100" },
        { kGfx8_0_4, 0x6986, 0x00, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 2100" },
        { kGfx8_0_4, 0x6987, 0x80, kVolcanicIsland, false, "gfx804", "AMD Embedded Radeon E9171" },
        { kGfx8_0_4, 0x6987, 0xC0, kVolcanicIsland, false, "gfx804", "Radeon 550X Series" },
        { kGfx8_0_4, 0x6987, 0xC1, kVolcanicIsland, false, "gfx804", "AMD Radeon RX 640" },
        { kGfx8_0_4, 0x6987, 0xC3, kVolcanicIsland, false, "gfx804", "Radeon 540X Series" },
        { kGfx8_0_4, 0x6987, 0xC7, kVolcanicIsland, false, "gfx804", "Radeon 540" },
        { kGfx8_0_4, 0x6995, 0x00, kVolcanicIsland, false, "gfx804", "AMD Radeon Pro WX 2100" },
        { kGfx8_0_4, 0x6997, 0x00, kVolcanicIsland, false, "gfx804", "Radeon Pro WX 2100" },
        { kGfx8_0_4, 0x699F, 0x81, kVolcanicIsland, false, "gfx804", "AMD Embedded Radeon E9170 Series" },
        { kGfx8_0_4, 0x699F, 0xC0, kVolcanicIsland, false, "gfx804", "Radeon 500 Series" },
        { kGfx8_0_4, 0x699F, 0xC1, kVolcanicIsland, false, "gfx804", "Radeon 540 Series" },
        { kGfx8_0_4, 0x699F, 0xC3, kVolcanicIsland, false, "gfx804", "Radeon 500 Series" },
        { kGfx8_0_4, 0x699F, 0xC5, kVolcanicIsland, false, "gfx804", "699F:C5" },
        { kGfx8_0_4, 0x699F, 0xC7, kVolcanicIsland, false, "gfx804", "Radeon RX550/550 Series" },
        { kGfx8_0_4, 0x699F, 0xC9, kVolcanicIsland, false, "gfx804", "Radeon 540" },
        { kGfx8_0_4, 0x699F, 0xCF, kVolcanicIsland, false, "gfx804", "699F:CF" },

        { kVegaM1, 0x694C, 0xC0, kVolcanicIsland, true, "gfx804", "Radeon RX Vega M GH Graphics" },
        { kVegaM2, 0x694E, 0xC0, kVolcanicIsland, true, "gfx804", "Radeon RX Vega M GL Graphics" },
        { kVegaM2, 0x694F, 0xC0, kVolcanicIsland, true, "gfx804", "Radeon Pro WX Vega M GL Graphics" },

        { kGfx9_0_0, 0x6860, 0x00, kGfx9, false, "gfx900", "Radeon Instinct MI25" },
        { kGfx9_0_0, 0x6860, 0x01, kGfx9, false, "gfx900", "Radeon Instinct MI25" },
        { kGfx9_0_0, 0x6860, 0x02, kGfx9, false, "gfx900", "Radeon Instinct MI25" },
        { kGfx9_0_0, 0x6860, 0x03, kGfx9, false, "gfx900", "Radeon Pro V340" },
        { kGfx9_0_0, 0x6860, 0x04, kGfx9, false, "gfx900", "Radeon Instinct MI25x2" },
        { kGfx9_0_0, 0x6860, 0x06, kGfx9, false, "gfx900", "Radeon Instinct MI25" },
        { kGfx9_0_0, 0x6860, 0x07, kGfx9, false, "gfx900", "Radeon(TM) Pro V320" },
        { kGfx9_0_0, 0x6860, 0xC0, kGfx9, false, "gfx900", "6860:C0" },
        { kGfx9_0_0, 0x6861, 0x00, kGfx9, false, "gfx900", "Radeon (TM) Pro WX 9100" },
        { kGfx9_0_0, 0x6862, 0x00, kGfx9, false, "gfx900", "Radeon Pro SSG" },
        { kGfx9_0_0, 0x6863, 0x00, kGfx9, false, "gfx900", "Radeon Vega Frontier Edition" },
        { kGfx9_0_0, 0x6864, 0x00, kGfx9, false, "gfx900", "6864:00" },
        { kGfx9_0_0, 0x6864, 0x03, kGfx9, false, "gfx900", "Radeon Pro V340" },
        { kGfx9_0_0, 0x6864, 0x04, kGfx9, false, "gfx900", "Instinct MI25x2" },
        { kGfx9_0_0, 0x6864, 0x05, kGfx9, false, "gfx900", "Radeon Pro V340" },
        { kGfx9_0_0, 0x6867, 0x00, kGfx9, false, "gfx900", "Radeon Pro Vega 56" },
        { kGfx9_0_0, 0x6868, 0x00, kGfx9, false, "gfx900", "Radeon (TM) PRO WX 8200" },
        { kGfx9_0_0, 0x6869, 0x00, kGfx9, false, "gfx900", "6869:00" },
        { kGfx9_0_0, 0x686A, 0x00, kGfx9, false, "gfx900", "686A:00" },
        { kGfx9_0_0, 0x686B, 0x00, kGfx9, false, "gfx900", "686B:00" },
        { kGfx9_0_0, 0x686C, 0x00, kGfx9, false, "gfx900", "Radeon Instinct MI25 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x01, kGfx9, false, "gfx900", "Radeon Instinct MI25 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x02, kGfx9, false, "gfx900", "Radeon Instinct MI25 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x03, kGfx9, false, "gfx900", "Radeon Pro V340 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x04, kGfx9, false, "gfx900", "AMD Radeon Instinct MI25x2 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x05, kGfx9, false, "gfx900", "Radeon Pro V340 MxGPU" },
        { kGfx9_0_0, 0x686C, 0x06, kGfx9, false, "gfx900", "Radeon Instinct MI25 MxGPU" },
        { kGfx9_0_0, 0x686C, 0xC1, kGfx9, false, "gfx900", "686C:C1" },
        { kGfx9_0_0, 0x686D, 0x00, kGfx9, false, "gfx900", "686D:00" },
        { kGfx9_0_0, 0x686E, 0x00, kGfx9, false, "gfx900", "686E:00" },
        { kGfx9_0_0, 0x687F, 0x01, kGfx9, false, "gfx900", "Radeon RX Vega" },
        { kGfx9_0_0, 0x687F, 0xC0, kGfx9, false, "gfx900", "Radeon RX Vega" },
        { kGfx9_0_0, 0x687F, 0xC1, kGfx9, false, "gfx900", "Radeon RX Vega" },
        { kGfx9_0_0, 0x687F, 0xC3, kGfx9, false, "gfx900", "Radeon RX Vega" },
        { kGfx9_0_0, 0x687F, 0xC4, kGfx9, false, "gfx900", "687F:C4" },
        { kGfx9_0_0, 0x687F, 0xC7, kGfx9, false, "gfx900", "Radeon RX Vega" },

        { kGfx9_0_2, 0x15DD, 0x00, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0x81, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x82, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x83, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x84, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x85, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x86, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0x87, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0x88, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC1, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC3, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 10 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC5, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC6, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC7, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC8, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xC9, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCA, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCB, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCC, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCD, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCE, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xCF, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD0, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 10 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD1, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD2, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD4, kGfx9, true, "gfx902", "AMD 15DD Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD5, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD6, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD7, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD8, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xD9, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xE1, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15DD, 0xE2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0x00, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 8 Graphics WS" },
        { kGfx9_0_2, 0x15D8, 0x71, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0x72, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0x73, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0x74, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0x75, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0x91, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0x92, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0x93, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 1 Graphics" },
        { kGfx9_0_2, 0x15D8, 0x94, kGfx9, true, "gfx902", "15D8:94" },
        { kGfx9_0_2, 0x15D8, 0x95, kGfx9, true, "gfx902", "15D8:95" },
        { kGfx9_0_2, 0x15D8, 0xA1, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 10 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xA2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xA3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xA4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xB1, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 10 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xB2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xB3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xB4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC1, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 10 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC5, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC6, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC8, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xC9, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCA, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCB, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCC, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCD, kGfx9, true, "gfx902", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCE, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xCF, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD1, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 10 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 6 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD8, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xD9, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 8 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDA, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 11 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDB, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDC, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDD, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDE, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xDF, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xE1, kGfx9, true, "gfx902", "AMD Radeon(TM) RX Vega 11 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xE2, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 9 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xE3, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xE4, kGfx9, true, "gfx902", "AMD Radeon(TM) Vega 3 Graphics" },
        { kGfx9_0_2, 0x15D8, 0xE9, kGfx9, true, "gfx902", "15D8:E9" },
        { kGfx9_0_2, 0x15D8, 0xEA, kGfx9, true, "gfx902", "15D8:EA" },
        { kGfx9_0_2, 0x15D8, 0xEB, kGfx9, true, "gfx902", "15D8:EB" },
        { kGfx9_0_2, 0x15D9, 0x91, kGfx9, true, "gfx902", "15D9:91" },
        { kGfx9_0_2, 0x15D9, 0x92, kGfx9, true, "gfx902", "15D9:92" },
        { kGfx9_0_2, 0x15D9, 0xC1, kGfx9, true, "gfx902", "15D9:C1" },
        { kGfx9_0_2, 0x15D9, 0xC2, kGfx9, true, "gfx902", "15D9:C2" },
        { kGfx9_0_2, 0x15D9, 0xC3, kGfx9, true, "gfx902", "15D9:C3" },

        { kGfx9_0_4, 0x69A0, 0x00, kGfx9, false, "gfx904", "69A0:00" },
        { kGfx9_0_4, 0x69A1, 0x00, kGfx9, false, "gfx904", "69A1:00" },
        { kGfx9_0_4, 0x69A2, 0x00, kGfx9, false, "gfx904", "69A2:00" },
        { kGfx9_0_4, 0x69A3, 0x00, kGfx9, false, "gfx904", "69A3:00" },
        { kGfx9_0_4, 0x69AF, 0xC0, kGfx9, false, "gfx904", "Radeon Pro Vega 20" },
        { kGfx9_0_4, 0x69AF, 0xC1, kGfx9, false, "gfx904", "69AF:C1" },
        { kGfx9_0_4, 0x69AF, 0xC3, kGfx9, false, "gfx904", "69AF:C3" },
        { kGfx9_0_4, 0x69AF, 0xC7, kGfx9, false, "gfx904", "Radeon Pro Vega 16" },
        { kGfx9_0_4, 0x69AF, 0xCF, kGfx9, false, "gfx904", "69AF:CF" },
        { kGfx9_0_4, 0x69AF, 0xD7, kGfx9, false, "gfx904", "Radeon(TM) RX Vega 16" },
        { kGfx9_0_4, 0x69AF, 0xFF, kGfx9, false, "gfx904", "69AF:FF" },

        { kGfx9_0_6, 0x66A3, 0x00, kGfx9, false, "gfx906", "66A3:00" },
        { kGfx9_0_6, 0x66A7, 0x00, kGfx9, false, "gfx906", "66A7:00" },
        { kGfx9_0_6, 0x66AF, 0xC0, kGfx9, false, "gfx906", "66AF:C0" },
        { kGfx9_0_6, 0x66AF, 0xC1, kGfx9, false, "gfx906", "AMD Radeon VII" },
        { kGfx9_0_6, 0x66AF, 0xCF, kGfx9, false, "gfx906", "66AF:CF" },
        { kGfx9_0_6, 0x66A0, 0x00, kGfx9, false, "gfx906", "66A0:00" },
        { kGfx9_0_6, 0x66A1, 0x00, kGfx9, false, "gfx906", "66A1:00" },
        { kGfx9_0_6, 0x66A1, 0x02, kGfx9, false, "gfx906", "66A1:02" },
        { kGfx9_0_6, 0x66A1, 0x03, kGfx9, false, "gfx906", "66A1:03" },
        { kGfx9_0_6, 0x66A1, 0x06, kGfx9, false, "gfx906", "AMD Radeon Pro VII" },
        { kGfx9_0_6, 0x66A2, 0x00, kGfx9, false, "gfx906", "66A2:00" },
        { kGfx9_0_6, 0x66A2, 0x02, kGfx9, false, "gfx906", "66A2:02" },
        { kGfx9_0_6, 0x66A4, 0x00, kGfx9, false, "gfx906", "66A4:00" },
        { kGfx9_0_6, 0x66AF, 0xC3, kGfx9, false, "gfx906", "66AF:C3" },
        { kGfx9_0_6, 0x66AF, 0xC7, kGfx9, false, "gfx906", "66AF:C7" },

        { kGfx9_0_A, 0x740C, 0x01, kCdna2, false, "gfx90a", "AMD Instinct(TM) MI250X" },
        { kGfx9_0_A, 0x740F, 0x02, kCdna2, false, "gfx90a", "AMD Instinct(TM) MI210" },

        { kGfx9_0_C, 0x1636, 0x00, kGfx9, true, "gfx90c", "1636:00" },
        { kGfx9_0_C, 0x1636, 0x80, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0x81, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0x82, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0x83, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0x84, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC7, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC8, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xC9, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCA, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCB, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCC, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCD, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCE, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xCF, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD7, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD8, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xD9, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDA, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDB, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDC, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDD, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDE, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xDF, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xE1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xE2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xE3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1636, 0xF0, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0x00, kGfx9, true, "gfx90c", "1638:00" },
        { kGfx9_0_C, 0x1638, 0xC0, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC7, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC8, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xC9, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xCA, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xCB, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xCC, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xCD, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD8, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xD9, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xDA, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xDB, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xDC, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xDD, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xE1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x1638, 0xE2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xC6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD4, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD5, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xD6, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xE8, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xE9, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x15E7, 0xEA, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x164C, 0xC1, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x164C, 0xC2, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },
        { kGfx9_0_C, 0x164C, 0xC3, kGfx9, true, "gfx90c", "AMD Radeon(TM) Graphics" },

        { kGfx9_4_2, 0x74A1, 0x00, kCdna3, false, "gfx942", "AMD Instinct(TM) MI300X" },
        { kGfx9_4_2, 0x74A1, 0x01, kCdna3, true, "gfx942", "AMD Instinct(TM) MI300A" },
        { kGfx9_4_2, 0x74A9, 0x00, kCdna3, false, "gfx942", "AMD Instinct(TM) MI300XHF" },

        { kGfx9_5_0, 0x75A0, 0x00, kCdna4, false, "gfx950", "AMD Instinct(TM) MI355X" },
        { kGfx9_5_0, 0x75A1, 0x00, kCdna4, false, "gfx950", "AMD Instinct(TM) MI355X" },

        { kGfx10_1_0, 0x7310, 0x00, kGfx10, false, "gfx1010", "AMD Radeon Pro W5700X" },
        { kGfx10_1_0, 0x7312, 0x00, kGfx10, false, "gfx1010", "AMD Radeon Pro W5700" },
        { kGfx10_1_0, 0x7318, 0x40, kGfx10, false, "gfx1010", "7318:40" },
        { kGfx10_1_0, 0x7319, 0x40, kGfx10, false, "gfx1010", "AMD Radeon Pro 5700 XT" },
        { kGfx10_1_0, 0x731A, 0x40, kGfx10, false, "gfx1010", "731A:40" },
        { kGfx10_1_0, 0x731B, 0x40, kGfx10, false, "gfx1010", "731B:40" },
        { kGfx10_1_0, 0x731E, 0xC6, kGfx10, false, "gfx1010", "731E:C6" },
        { kGfx10_1_0, 0x731E, 0xC7, kGfx10, false, "gfx1010", "AMD Radeon RX 5700B" },
        { kGfx10_1_0, 0x731F, 0xC0, kGfx10, false, "gfx1010", "AMD Radeon RX 5700 XT 50th Anniversary" },
        { kGfx10_1_0, 0x731F, 0xC1, kGfx10, false, "gfx1010", "AMD Radeon RX 5700 XT" },
        { kGfx10_1_0, 0x731F, 0xC2, kGfx10, false, "gfx1010", "AMD Radeon RX 5600M" },
        { kGfx10_1_0, 0x731F, 0xC3, kGfx10, false, "gfx1010", "AMD Radeon RX 5700M" },
        { kGfx10_1_0Xl, 0x731F, 0xC4, kGfx10, false, "gfx1010", "AMD Radeon RX 5700" },
        { kGfx10_1_0, 0x731F, 0xC5, kGfx10, false, "gfx1010", "AMD Radeon RX 5700 XT" },
        { kGfx10_1_0, 0x731F, 0xC7, kGfx10, false, "gfx1010", "731F:C7" },
        { kGfx10_1_0, 0x731F, 0xCA, kGfx10, false, "gfx1010", "AMD Radeon RX 5600 XT" },
        { kGfx10_1_0, 0x731F, 0xCB, kGfx10, false, "gfx1010", "AMD Radeon RX 5600 OEM" },
        { kGfx10_1_0, 0x731F, 0xCE, kGfx10, false, "gfx1010", "731F:CE" },
        { kGfx10_1_0, 0x731F, 0xE1, kGfx10, false, "gfx1010", "731F:E1" },
        { kGfx10_1_0, 0x731F, 0xE3, kGfx10, false, "gfx1010", "731F:E3" },
        { kGfx10_1_0, 0x731F, 0xE7, kGfx10, false, "gfx1010", "731F:E7" },
        { kGfx10_1_0, 0x731F, 0xEB, kGfx10, false, "gfx1010", "731F:EB" },

        { kGfx10_1_1, 0x7360, 0x40, kGfx10, false, "gfx1011", "7360:40" },
        { kGfx10_1_1, 0x7360, 0xC1, kGfx10, false, "gfx1011", "7360:C1" },
        { kGfx10_1_1, 0x7360, 0xC7, kGfx10, false, "gfx1011", "7360:C7" },
        { kGfx10_1_1, 0x7360, 0x41, kGfx10, false, "gfx1011", "AMD Radeon Pro 5600M" },
        { kGfx10_1_1, 0x7360, 0xC3, kGfx10, false, "gfx1011", "AMD Radeon Pro V520" },
        { kGfx10_1_1, 0x7362, 0x71, kGfx10, false, "gfx1011", "7362:71" },
        { kGfx10_1_1, 0x7362, 0xC1, kGfx10, false, "gfx1011", "7362:C1" },
        { kGfx10_1_1, 0x7362, 0xC3, kGfx10, false, "gfx1011", "AMD Radeon Pro V520 MxGPU" },

        { kGfx10_1_2X, 0x7340, 0x00, kGfx10, false, "gfx1012", "AMD Radeon Pro W5500X" },
        { kGfx10_1_2Xt, 0x7340, 0x41, kGfx10, false, "gfx1012", "AMD Radeon Pro 5500 XT" },
        { kGfx10_1_2Xt, 0x7340, 0x43, kGfx10, false, "gfx1012", "7340:43" },
        { kGfx10_1_2, 0x7340, 0x47, kGfx10, false, "gfx1012", "AMD Radeon Pro 5300" },
        { kGfx10_1_2X, 0x7340, 0xC1, kGfx10, false, "gfx1012", "Radeon RX 5500M" },
        { kGfx10_1_2, 0x7340, 0xC3, kGfx10, false, "gfx1012", "Radeon RX 5300M" },
        { kGfx10_1_2X, 0x7340, 0xC5, kGfx10, false, "gfx1012", "Radeon RX 5500 XT" },
        { kGfx10_1_2X, 0x7340, 0xC7, kGfx10, false, "gfx1012", "Radeon RX 5500" },
        { kGfx10_1_2Xt, 0x7340, 0xC9, kGfx10, false, "gfx1012", "7340:43" },
        { kGfx10_1_2, 0x7340, 0xCF, kGfx10, false, "gfx1012", "Radeon RX 5300" },
        { kGfx10_1_2Xt, 0x7340, 0x70, kGfx10, false, "gfx1012", "7340:70" },
        { kGfx10_1_2Xt, 0x7340, 0xF2, kGfx10, false, "gfx1012", "7340:F2" },
        { kGfx10_1_2Xt, 0x7340, 0xF3, kGfx10, false, "gfx1012", "7340:F3" },
        { kGfx10_1_2X, 0x7341, 0x00, kGfx10, false, "gfx1012", "AMD Radeon Pro W5500" },
        { kGfx10_1_2Xt, 0x7343, 0x00, kGfx10, false, "gfx1012", "7343:00" },
        { kGfx10_1_2X, 0x7347, 0x00, kGfx10, false, "gfx1012", "AMD Radeon Pro W5500M" },
        { kGfx10_1_2, 0x734F, 0x00, kGfx10, false, "gfx1012", "AMD Radeon Pro W5300M" },

        { kGfx10_3_0Xtx, 0x73A5, 0xC0, kGfx10_3, false, "gfx1030", "AMD Radeon RX 6950 XT" },
        { kGfx10_3_0Xtx, 0x73AF, 0xC0, kGfx10_3, false, "gfx1030", "AMD Radeon RX 6900 XT" },
        { kGfx10_3_0Xtx, 0x73BF, 0x40, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73BF, 0x41, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73BF, 0xC0, kGfx10_3, false, "gfx1030", "AMD Radeon RX 6900 XT" },
        { kGfx10_3_0Xt, 0x73BF, 0xC1, kGfx10_3, false, "gfx1030", "AMD Radeon RX 6800 XT" },
        { kGfx10_3_0, 0x73BF, 0xC3, kGfx10_3, false, "gfx1030", "AMD Radeon RX 6800" },
        { kGfx10_3_0Xtx, 0x73BF, 0xC7, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73BF, 0xCF, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73BF, 0xD0, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73A0, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73A1, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon Pro V620" },
        { kGfx10_3_0Xtx, 0x73A2, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73A3, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon PRO W6800" },
        { kGfx10_3_0Xtx, 0x73AB, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_0Xtx, 0x73AE, 0x00, kGfx10_3, false, "gfx1030", "AMD Radeon Pro V620 MxGPU" },

        { kGfx10_3_1, 0x73C0, 0x00, kGfx10_3, false, "gfx1031", "73C0:00" },
        { kGfx10_3_1, 0x73C1, 0x00, kGfx10_3, false, "gfx1031", "73C1:00" },
        { kGfx10_3_1, 0x73C3, 0x00, kGfx10_3, false, "gfx1031", "73C3:00" },
        { kGfx10_3_1, 0x73CE, 0xFF, kGfx10_3, false, "gfx1031", "AMD Radeon V520 MxGPU" },
        { kGfx10_3_1, 0x73DF, 0x40, kGfx10_3, false, "gfx1031", "73DF:40" },
        { kGfx10_3_1, 0x73DF, 0x41, kGfx10_3, false, "gfx1031", "73DF:41" },
        { kGfx10_3_1, 0x73DF, 0xC0, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6750 XT" },
        { kGfx10_3_1, 0x73DF, 0xC1, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6700 XT" },
        { kGfx10_3_1, 0x73DF, 0xC5, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6700 XT" },
        { kGfx10_3_1, 0x73DF, 0xC7, kGfx10_3, false, "gfx1031", "73DF:C7" },
        { kGfx10_3_1, 0x73DF, 0xD5, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6750 GRE 12GB" },
        { kGfx10_3_1, 0x73DF, 0xDF, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6700" },
        { kGfx10_3_1, 0x73DF, 0xC2, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6800M" },
        { kGfx10_3_1, 0x73DF, 0xC3, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6800M" },
        { kGfx10_3_1, 0x73DF, 0xCF, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6700M" },
        { kGfx10_3_1, 0x73DF, 0xE5, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6750 GRE 12GB" },
        { kGfx10_3_1, 0x73DF, 0xFF, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6700" },
        { kGfx10_3_1, 0x73FF, 0xDF, kGfx10_3, false, "gfx1031", "AMD Radeon RX 6750 GRE 10GB" },

        { kGfx10_3_2Xt, 0x73E0, 0x00, kGfx10_3, false, "gfx1032", "73E0:00" },
        { kGfx10_3_2Xt, 0x73EF, 0xC0, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6800S" },
        { kGfx10_3_2Xt, 0x73EF, 0xC1, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6650 XT" },
        { kGfx10_3_2Xt, 0x73EF, 0xC2, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6700S" },
        { kGfx10_3_2Xt, 0x73EF, 0xC3, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6650M" },
        { kGfx10_3_2Xt, 0x73EF, 0xC4, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6650M XT" },
        { kGfx10_3_2Xt, 0x73FF, 0xC1, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6600 XT" },
        { kGfx10_3_2Xt, 0x73FF, 0xC7, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6600" },
        { kGfx10_3_2Xt, 0x73FF, 0x40, kGfx10_3, false, "gfx1032", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_2Xt, 0x73FF, 0x41, kGfx10_3, false, "gfx1032", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_2Xt, 0x73FF, 0x42, kGfx10_3, false, "gfx1032", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_2Xt, 0x73FF, 0x43, kGfx10_3, false, "gfx1032", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_2Xt, 0x73FF, 0xC3, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6600M" },
        { kGfx10_3_2Xt, 0x73FF, 0xCB, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6600S" },
        { kGfx10_3_2Xt, 0x73FF, 0xEF, kGfx10_3, false, "gfx1032", "AMD Radeon RX 6600M" },
        { kGfx10_3_2Xt, 0x73FF, 0xD7, kGfx10_3, false, "gfx1032", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_2Xt, 0x73E1, 0x00, kGfx10_3, false, "gfx1032", "AMD Radeon PRO W6600M" },
        { kGfx10_3_2, 0x73E3, 0x00, kGfx10_3, false, "gfx1032", "AMD Radeon PRO W6600" },

        { kGfx10_3_3, 0x163F, 0x00, kGfx10_3, true, "gfx1033",
          "AMD Radeon(TM) Navi Graphics Custom Made for Magic Leap" },
        { kGfx10_3_3, 0x163F, 0xE1, kGfx10_3, true, "gfx1033",
          "AMD Radeon(TM) Navi Graphics Custom Made for Magic Leap" },
        { kGfx10_3_3, 0x163F, 0xAF, kGfx10_3, true, "gfx1033", "AMD Custom GPU 0405" },
        { kGfx10_3_3, 0x1435, 0xAE, kGfx10_3, true, "gfx1033", "AMD Custom GPU 0932" },

        { kGfx10_3_4, 0x7420, 0x00, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_4, 0x7422, 0x00, kGfx10_3, false, "gfx1034", "AMD Radeon PRO W6400" },
        { kGfx10_3_4, 0x743F, 0xC1, kGfx10_3, false, "gfx1034", "AMD Radeon RX 6500 XT" },
        { kGfx10_3_4, 0x743F, 0xC7, kGfx10_3, false, "gfx1034", "AMD Radeon RX 6400" },
        { kGfx10_3_4, 0x743F, 0xD7, kGfx10_3, false, "gfx1034", "AMD Radeon RX 6400" },
        { kGfx10_3_4, 0x7421, 0x00, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) PRO W6500M" },
        { kGfx10_3_4, 0x7423, 0x00, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) PRO W6300M" },
        { kGfx10_3_4, 0x7423, 0x01, kGfx10_3, false, "gfx1034", "AMD Radeon PRO W6300" },
        { kGfx10_3_4, 0x743F, 0xC3, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) RX 6500M" },
        { kGfx10_3_4, 0x743F, 0xCF, kGfx10_3, false, "gfx1034", "AMD Radeon RX 6300M" },
        { kGfx10_3_4, 0x743F, 0xC8, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) RX 6550M" },
        { kGfx10_3_4, 0x743F, 0xCC, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) 6550S" },
        { kGfx10_3_4, 0x743F, 0xCE, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) RX 6450M" },
        { kGfx10_3_4, 0x743F, 0xD3, kGfx10_3, false, "gfx1034", "AMD Radeon(TM) RX 6550M" },

        { kGfx10_3_5, 0x164D, 0x00, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x01, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x02, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x03, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x11, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x12, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x13, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x84, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x85, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x86, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0x88, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC3, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC4, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC5, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC7, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC8, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xC9, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD3, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD4, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD5, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD6, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD7, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD8, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xD9, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xE1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xE2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x01, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x02, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x03, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x04, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x05, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x11, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x12, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x13, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x14, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x84, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x85, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x86, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0x88, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC3, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC4, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC5, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC7, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC8, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xC9, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD3, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD4, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD5, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD6, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD7, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD8, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xD9, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xE1, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xE2, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCA, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCB, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCC, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCD, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCE, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xCF, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDA, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDB, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDC, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDD, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDE, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x164D, 0xDF, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCA, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCB, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCC, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCD, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCE, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xCF, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDA, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDB, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDC, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDD, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDE, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_5, 0x1681, 0xDF, kGfx10_3, true, "gfx1035", "AMD Radeon(TM) Graphics" },

        { kGfx10_3_6, 0x1506, 0x00, kGfx10_3, true, "gfx1036", "1506:00" },
        { kGfx10_3_6, 0x1506, 0xC1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xC2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xC3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xC4, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xC5, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xC6, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xD8, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xD9, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xDA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xDB, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xDC, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xDD, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x1506, 0xDE, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },

        { kGfx10_3_6, 0x13C0, 0xC1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC4, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC5, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC6, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC7, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xC9, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xCA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xCB, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xCC, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xCD, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE4, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE5, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE6, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE7, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE8, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xE9, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xEA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },

        { kGfx10_3_6, 0x13C0, 0xD4, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD5, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD6, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x13C0, 0xD8, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xD9, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDB, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDC, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDD, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDE, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x13C0, 0xDF, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xC1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC4, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC5, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC6, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xC7, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xCA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xCB, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xD1, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xD2, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xD3, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) Graphics" },
        { kGfx10_3_6, 0x164E, 0xD8, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xD9, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xDA, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xDB, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xDC, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xDD, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },
        { kGfx10_3_6, 0x164E, 0xDE, kGfx10_3, true, "gfx1036", "AMD Radeon(TM) 610M" },

        { kGfx11_0_0, 0x73A8, 0x00, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xC0, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xC8, kGfx11, false, "gfx1100", "AMD Radeon RX 7900 XTX" },
        { kGfx11_0_0Xt, 0x744C, 0xCC, kGfx11, false, "gfx1100", "AMD Radeon RX 7900 XT" },
        { kGfx11_0_0Gre, 0x744C, 0xCE, kGfx11, false, "gfx1100", "AMD Radeon RX 7900 GRE" },
        { kGfx11_0_0, 0x744C, 0xE0, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xE8, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xEC, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xEE, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0, 0x744C, 0xEF, kGfx11, false, "gfx1100", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_0M, 0x744C, 0xCF, kGfx11, false, "gfx1100", "AMD Radeon RX 7900M" },
        { kGfx11_0_0, 0x7448, 0x00, kGfx11, false, "gfx1100", "AMD Radeon PRO W7900" },
        { kGfx11_0_0, 0x7449, 0x00, kGfx11, false, "gfx1100", "AMD Radeon PRO W7800 48GB" },
        { kGfx11_0_0, 0x744A, 0x00, kGfx11, false, "gfx1100", "AMD Radeon PRO W7900 Dual Slot" },
        { kGfx11_0_0, 0x744B, 0x00, kGfx11, false, "gfx1100", "AMD Radeon PRO W7900D" },
        { kGfx11_0_0, 0x745E, 0xCC, kGfx11, false, "gfx1100", "AMD Radeon PRO W7800" },

        { kGfx11_0_1, 0x73C8, 0x00, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x73C8, 0x02, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x73C8, 0x03, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1Xt, 0x747E, 0xC8, kGfx11, false, "gfx1101", "AMD Radeon RX 7800 XT" },
        { kGfx11_0_1, 0x747E, 0xC9, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x747E, 0xDB, kGfx11, false, "gfx1101", "AMD Radeon RX 7700" },
        { kGfx11_0_1, 0x747E, 0xFF, kGfx11, false, "gfx1101", "AMD Radeon RX 7700 XT" },
        { kGfx11_0_1Xt, 0x747E, 0xD8, kGfx11, false, "gfx1101", "AMD Radeon RX 7800M" },
        { kGfx11_0_1, 0x747E, 0xD9, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x747E, 0x9B, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x73C4, 0x00, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x73C5, 0x00, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_1, 0x7460, 0x00, kGfx11, false, "gfx1101", "AMD Radeon PRO V710" },
        { kGfx11_0_1, 0x7461, 0x00, kGfx11, false, "gfx1101", "AMD Radeon PRO V710 MxGPU" },
        { kGfx11_0_1, 0x7470, 0x00, kGfx11, false, "gfx1101", "AMD Radeon PRO W7700" },
        { kGfx11_0_1, 0x7478, 0x00, kGfx11, false, "gfx1101", "AMD Radeon(TM) Graphics" },

        { kGfx11_0_2Xt, 0x73F0, 0x3C, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0x3D, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF5, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF6, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF7, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x7480, 0xC0, kGfx11, false, "gfx1102", "AMD Radeon RX 7600 XT" },
        { kGfx11_0_2Xt, 0x7480, 0xC2, kGfx11, false, "gfx1102", "AMD Radeon RX 7650 GRE" },
        { kGfx11_0_2Xt, 0x7480, 0xCF, kGfx11, false, "gfx1102", "AMD Radeon RX 7600" },
        { kGfx11_0_2Xt, 0x7499, 0xC0, kGfx11, false, "gfx1102", "AMD Radeon RX 7400" },
        { kGfx11_0_2Xt, 0x73F0, 0x3E, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0x3F, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF1, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF2, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF3, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x73F0, 0xF4, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x7480, 0xC1, kGfx11, false, "gfx1102", "AMD Radeon RX 7700S" },
        { kGfx11_0_2Xt, 0x7480, 0xC3, kGfx11, false, "gfx1102", "AMD Radeon RX 7600S" },
        { kGfx11_0_2Xt, 0x7480, 0xC7, kGfx11, false, "gfx1102", "AMD Radeon RX 7600M XT" },
        { kGfx11_0_2Xt, 0x7481, 0xC3, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x7481, 0xC7, kGfx11, false, "gfx1102", "AMD Radeon RX 7600 series" },
        { kGfx11_0_2Xt, 0x7483, 0xCF, kGfx11, false, "gfx1102", "AMD Radeon RX 7600M" },
        { kGfx11_0_2Xt, 0x7487, 0xCF, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2Xt, 0x7480, 0x00, kGfx11, false, "gfx1102", "AMD Radeon PRO W7600" },
        { kGfx11_0_2Xt, 0x7481, 0x00, kGfx11, false, "gfx1102", "AMD Radeon(TM) Graphics" },
        { kGfx11_0_2, 0x7489, 0x00, kGfx11, false, "gfx1102", "AMD Radeon PRO W7500" },
        { kGfx11_0_2Xt, 0x748B, 0x00, kGfx11, false, "gfx1102", "AMD Radeon(TM) Pro W7500M" },
        { kGfx11_0_2Xt, 0x7499, 0x00, kGfx11, false, "gfx1102", "AMD Radeon PRO W7400" },

        { kGfx11_0_3, 0x15BF, 0x00, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0x01, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0x02, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0x03, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0x04, kGfx11, true, "gfx1103", "AMD Radeon Graphics" },
        { kGfx11_0_3A, 0x15BF, 0x05, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0x06, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0x07, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0x08, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0x0A, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC0, kGfx11, true, "gfx1103", "Ryzen9" },
        { kGfx11_0_3, 0x15BF, 0xC1, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xC3, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC4, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xC5, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC6, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC7, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xC8, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xC9, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xCA, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xCB, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xCC, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xCD, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xCE, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xCF, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD0, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xD1, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD3, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD4, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xD5, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xD6, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD7, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xD8, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xD9, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xDA, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xDB, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xDC, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xDD, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x15BF, 0xDE, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3A, 0x15BF, 0xDF, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x15BF, 0xE0, kGfx11, true, "gfx1103", "15BF:E0" },
        { kGfx11_0_3, 0x15BF, 0xE1, kGfx11, true, "gfx1103", "15BF:E1" },
        { kGfx11_0_3, 0x15BF, 0xE7, kGfx11, true, "gfx1103", "15BF:E7" },
        { kGfx11_0_3, 0x15BF, 0xE8, kGfx11, true, "gfx1103", "15BF:E8" },
        { kGfx11_0_3, 0x15BF, 0xE9, kGfx11, true, "gfx1103", "15BF:E9" },
        { kGfx11_0_3, 0x15BF, 0xEA, kGfx11, true, "gfx1103", "15BF:EA" },
        { kGfx11_0_3A, 0x15BF, 0xF0, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0x01, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0x02, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0x03, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0x04, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0x05, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0x06, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0x07, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB0, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB1, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB3, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB4, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB5, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB6, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xB7, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xB8, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xB9, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xBA, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xBB, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3B, 0x1900, 0xBC, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1900, 0xBD, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1900, 0xBE, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC0, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xC1, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xC3, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC4, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC5, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xC6, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC7, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xC8, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xC9, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xCA, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xCB, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xCC, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xCD, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xCE, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xCF, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD0, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xD1, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xD3, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD4, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD5, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xD6, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD7, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xD8, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xD9, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xDA, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xDB, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xDC, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xDD, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xDE, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3A, 0x1900, 0xDF, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },
        { kGfx11_0_3, 0x1900, 0xF0, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xF1, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0xF2, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0x09, kGfx11, true, "gfx1103", "AMD Radeon 780M Graphics" },
        { kGfx11_0_3, 0x1900, 0x0A, kGfx11, true, "gfx1103", "AMD Radeon 760M Graphics" },

        { kGfx11_0_3B, 0x15C8, 0xC1, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC2, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC3, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC4, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC5, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC6, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC7, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC8, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xC9, kGfx11, true, "gfx1103", "AMD Radeon Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD1, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD2, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD3, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD4, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD5, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD6, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD7, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x15C8, 0xD8, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC1, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC2, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC3, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC6, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC7, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC8, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xC9, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xCA, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD1, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD2, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD3, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD4, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD5, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD6, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD7, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xD8, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xCB, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xCC, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xCD, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },
        { kGfx11_0_3B, 0x1901, 0xCE, kGfx11, true, "gfx1103", "AMD Radeon 740M Graphics" },

        { kGfx11_5_0, 0x150E, 0xC1, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xC2, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_0, 0x150E, 0xC3, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_0, 0x150E, 0xC4, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xC5, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xC6, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xC7, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xD1, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xD2, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xD3, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xE1, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xE2, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_0, 0x150E, 0xE3, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xE4, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xF1, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },
        { kGfx11_5_0, 0x150E, 0xF3, kGfx11_5, true, "gfx1150", "AMD Radeon(TM) 890M Graphics" },

        { kGfx11_5_1, 0x1586, 0x00, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_1, 0x1586, 0xC1, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics" },
        { kGfx11_5_1, 0x1586, 0xC2, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },
        { kGfx11_5_1, 0x1586, 0xC3, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics" },
        { kGfx11_5_1, 0x1586, 0xC4, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },
        { kGfx11_5_1, 0x1586, 0xC5, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_1, 0x1586, 0xC6, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics" },
        { kGfx11_5_1, 0x1586, 0xD1, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics" },
        { kGfx11_5_1, 0x1586, 0xD2, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },
        { kGfx11_5_1, 0x1586, 0xD4, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },
        { kGfx11_5_1, 0x1586, 0xD5, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8040S Graphics" },
        { kGfx11_5_1, 0x1586, 0xE1, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics" },
        { kGfx11_5_1, 0x1586, 0xE2, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },
        { kGfx11_5_1, 0x1586, 0xE3, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics" },

        {kGfx11_5_1, 0x1586, 0xCA, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8065S Graphics"},
        {kGfx11_5_1, 0x1586, 0xCB, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics"},
        {kGfx11_5_1, 0x1586, 0xCC, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics"},
        {kGfx11_5_1, 0x1586, 0xCD, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8060S Graphics"},
        {kGfx11_5_1, 0x1586, 0xCE, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics"},
        {kGfx11_5_1, 0x1586, 0xCF, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8040S Graphics"},
        {kGfx11_5_1, 0x1586, 0xDA, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8065S Graphics"},
        {kGfx11_5_1, 0x1586, 0xDB, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics"},
        {kGfx11_5_1, 0x1586, 0xDC, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8050S Graphics"},
        {kGfx11_5_1, 0x1586, 0xDD, kGfx11_5, true, "gfx1151", "AMD Radeon(TM) 8040S Graphics"},

        { kGfx11_5_2, 0x1114, 0x00, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xC1, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xC2, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xC3, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xC4, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xC5, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xD1, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xD2, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xD3, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xD4, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xD5, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xE1, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xE2, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xE3, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xE4, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xE5, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xE6, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xE7, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xE9, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xEA, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xED, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xEE, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xF1, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_2, 0x1114, 0xF2, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xF3, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xF9, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xFA, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_2, 0x1114, 0xFC, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 860M Graphics" },
        { kGfx11_5_2, 0x1114, 0xFD, kGfx11_5, true, "gfx1152", "AMD Radeon(TM) 840M Graphics" },

        { kGfx11_5_3, 0x1902, 0xC0, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xC1, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3A, 0x1902, 0xC2, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 820M Graphics" },
        { kGfx11_5_3, 0x1902, 0xC3, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xC4, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3A, 0x1902, 0xC5, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3A, 0x1902, 0xC6, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 820M Graphics" },
        { kGfx11_5_3, 0x1902, 0xC7, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xC8, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3A, 0x1902, 0xC9, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 820M Graphics" },
        { kGfx11_5_3, 0x1902, 0xCA, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xD1, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xD2, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xD3, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xD4, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xD7, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xD8, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) 840M Graphics" },
        { kGfx11_5_3, 0x1902, 0xD9, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3A, 0x1902, 0xDA, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xE1, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xE2, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xE3, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xE4, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3, 0x1902, 0xE6, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },
        { kGfx11_5_3A, 0x1902, 0xE8, kGfx11_5, true, "gfx1153", "AMD Radeon(TM) Graphics" },

        { kGfx12_0_0Xt, 0x7590, 0xC0, kGfx12, false, "gfx1200", "AMD Radeon RX 9060 XT" },
        { kGfx12_0_0Xt, 0x7590, 0xC1, kGfx12, false, "gfx1200", "AMD Radeon RX 9060 XT LP" },
        { kGfx12_0_0, 0x7590, 0xC7, kGfx12, false, "gfx1200", "AMD Radeon RX 9060" },
        { kGfx12_0_0, 0x7590, 0xCF, kGfx12, false, "gfx1200", "AMD Radeon RX 9050" },

        { kGfx12_0_1Xt, 0x7550, 0xC0, kGfx12, false, "gfx1201", "AMD Radeon RX 9070 XT" },
        { kGfx12_0_1Gre, 0x7550, 0xC2, kGfx12, false, "gfx1201", "AMD Radeon RX 9070 GRE" },
        { kGfx12_0_1, 0x7550, 0xC3, kGfx12, false, "gfx1201", "AMD Radeon RX 9070" },
        { kGfx12_0_1Xt, 0x7551, 0xC0, kGfx12, false, "gfx1201", "AMD Radeon AI PRO R9700" },
        { kGfx12_0_1Xt, 0x7551, 0xC8, kGfx12, false, "gfx1201", "AMD Radeon AI PRO R9600 Series" },

    });

    // Sort devices based on device ID, then based on revision ID.
    std::ranges::sort(devices, SortCardInfo);

    return devices;
}();

[[nodiscard]] constexpr std::optional<CardInfo> GetCardInfoForDevice(const AdapterId& desc) noexcept
{
    using device_info::kAmdVendorId;
    using device_info::kRevisionIdAny;

    // Currently everything in device_info is AMD, so if the vendor ID doesn't match AMD's vendor ID, we won't find the
    // device in our table and can just return std::nullopt right away. If we ever add non-AMD devices to device_info,
    // we can revisit whether this early check is still appropriate.
    if (desc.vendor_id != kAmdVendorId)
    {
        return std::nullopt;
    }

    static_assert(
        []() consteval
        {
            return std::ranges::all_of(kAmdCardInfo,
                                       [](const CardInfo& card) { return IsAmdGeneration(card.generation); });
        }(),
        "Invalid HW generation in kAmdCardInfo!");

    static_assert([]() consteval { return std::ranges::is_sorted(kAmdCardInfo, SortCardInfo); }(),
                  "kAmdCardInfo is not sorted by device / revision ID!");

    // This static assert depends on the fact that kAmdCardInfo is sorted by device ID then revision ID,
    // so that duplicate device/revision ID pairs would be adjacent to each other.
    constexpr auto kFirstDuplicateIndex = []() consteval -> size_t
    {
        for (size_t i = 1; i < kAmdCardInfo.size(); ++i)
        {
            if (kAmdCardInfo[i].device_id == kAmdCardInfo[i - 1].device_id &&
                kAmdCardInfo[i].revision_id == kAmdCardInfo[i - 1].revision_id)
            {
                return i;
            }
        }
        return 0; // 0 means no duplicate found.
    }();

    // If this fails, check kDuplicateDeviceId and kDuplicateRevisionId in compiler output.
    static_assert(kFirstDuplicateIndex == 0, "kAmdCardInfo contains duplicate device ID and revision ID pairs! "
                                             "Inspect kDuplicateDeviceId and kDuplicateRevisionId constexpr variables "
                                             "in the compiler error output to identify the duplicate entry.");

    // Hover over or inspect these values to find the offending entry.
    constexpr std::optional<uint32_t> kDuplicateDeviceId =
        kFirstDuplicateIndex != 0 ? std::optional<uint32_t> { kAmdCardInfo[kFirstDuplicateIndex].device_id }
                                  : std::nullopt;
    constexpr std::optional<uint32_t> kDuplicateRevisionId =
        kFirstDuplicateIndex != 0 ? std::optional<uint32_t> { kAmdCardInfo[kFirstDuplicateIndex].revision_id }
                                  : std::nullopt;

    static_assert(!kDuplicateDeviceId.has_value(), "Duplicate device ID found");
    static_assert(!kDuplicateRevisionId.has_value(), "Duplicate revision ID found");

    static_assert(
        []() consteval
        {
            return std::ranges::none_of(kAmdCardInfo,
                                        [](const CardInfo& card) { return card.revision_id == kRevisionIdAny; });
        }(),
        "kRevisionIdAny is not a valid revision ID and cannot be used in kAmdCardInfo!");

    static_assert(
        []() consteval
        { return std::ranges::none_of(kAmdCardInfo, [](const CardInfo& card) { return card.device_id == 0; }); }(),
        "Device ID 0 is reserved for unknown devices and cannot be used in kAmdCardInfo!");

    // Binary search by device_id (kAmdCardInfo is sorted by device_id).
    const auto device_id_range =
        std::ranges::equal_range(kAmdCardInfo, desc.device_id, std::less {}, &CardInfo::device_id);
    if (device_id_range.begin() == device_id_range.end())
    {
        return std::nullopt;
    }

    // If revision is "any" then return the first entry for that device_id.
    if (desc.revision_id == kRevisionIdAny)
    {
        return *device_id_range.begin();
    }

    // Binary search by revision_id within the device_id range (kAmdCardInfo is sorted by revision_id within each
    // device_id).
    const auto revision_id_range =
        std::ranges::equal_range(device_id_range, desc.revision_id, std::less {}, &CardInfo::revision_id);
    if (revision_id_range.begin() == revision_id_range.end())
    {
        return std::nullopt;
    }

    return *revision_id_range.begin();
}

// C++23 allows static constexpr locals inside constexpr functions.
// When the project moves to C++23, kUnknownVgprsPerSIMD and kDeviceInfo can
// move back inside GetDeviceInfoForAsicType to reduce their scope, since they are only used by that function.
#if __cplusplus >= 202302L
#warning "C++23 detected: move kDeviceInfo and kUnknownVgprsPerSIMD inside GetDeviceInfoForAsicType"
#endif

// GPUs older than GFX10 have an unknown number of VGPRs per SIMD.
constexpr uint16_t kUnknownVgprsPerSIMD = 0;

/// Device info for AMD GPUs, indexed by AsicType enum values.
static constexpr auto kDeviceInfo = std::to_array<DeviceInfo>({
    { 2, 10, 1, 8, 2, 64, 2, 28, 4, kUnknownVgprsPerSIMD },  // kTahitiPro
    { 2, 10, 1, 8, 2, 64, 2, 32, 4, kUnknownVgprsPerSIMD },  // kTahitiXt
    { 2, 10, 1, 8, 2, 64, 2, 16, 4, kUnknownVgprsPerSIMD },  // kPitcairnPro
    { 2, 10, 1, 8, 2, 64, 2, 20, 4, kUnknownVgprsPerSIMD },  // kPitcairnXt
    { 1, 10, 1, 8, 1, 64, 2, 8, 4, kUnknownVgprsPerSIMD },   // kCapeVerdePro
    { 1, 10, 1, 8, 1, 64, 2, 10, 4, kUnknownVgprsPerSIMD },  // kCapeVerdeXt
    { 1, 10, 1, 8, 1, 64, 1, 6, 4, kUnknownVgprsPerSIMD },   // kOland
    { 1, 10, 1, 8, 1, 64, 1, 5, 4, kUnknownVgprsPerSIMD },   // kHainan
    { 2, 10, 1, 8, 2, 64, 1, 14, 4, kUnknownVgprsPerSIMD },  // kBonaire
    { 4, 10, 1, 8, 4, 64, 1, 44, 4, kUnknownVgprsPerSIMD },  // kHawaii
    { 1, 10, 1, 8, 1, 64, 1, 2, 4, kUnknownVgprsPerSIMD },   // kKalindi
    { 1, 10, 1, 8, 1, 64, 1, 8, 4, kUnknownVgprsPerSIMD },   // kSpectre
    { 1, 10, 1, 8, 1, 64, 1, 4, 4, kUnknownVgprsPerSIMD },   // kSpectreSl
    { 1, 10, 1, 8, 1, 64, 1, 6, 4, kUnknownVgprsPerSIMD },   // kSpectreLite
    { 1, 10, 1, 8, 1, 64, 1, 3, 4, kUnknownVgprsPerSIMD },   // kSpooky
    { 1, 10, 1, 8, 1, 64, 1, 6, 4, kUnknownVgprsPerSIMD },   // kIceland
    { 4, 10, 1, 8, 4, 64, 1, 32, 4, kUnknownVgprsPerSIMD },  // kTonga
    { 1, 10, 1, 8, 1, 64, 1, 8, 4, kUnknownVgprsPerSIMD },   // kCarrizo
    { 1, 10, 1, 8, 1, 64, 1, 3, 4, kUnknownVgprsPerSIMD },   // kCarrizo_EM
    { 4, 10, 1, 8, 4, 64, 1, 64, 4, kUnknownVgprsPerSIMD },  // kFiji
    { 1, 10, 1, 8, 1, 64, 1, 3, 4, kUnknownVgprsPerSIMD },   // kStoney
    { 4, 8, 1, 8, 4, 64, 1, 36, 4, kUnknownVgprsPerSIMD },   // kEllesmere
    { 2, 8, 1, 8, 2, 64, 1, 16, 4, kUnknownVgprsPerSIMD },   // kBaffin
    { 2, 8, 1, 8, 2, 64, 1, 10, 4, kUnknownVgprsPerSIMD },   // kGfx8_0_4
    { 4, 8, 1, 8, 4, 64, 1, 24, 4, kUnknownVgprsPerSIMD },   // kVegaM1
    { 4, 8, 1, 8, 4, 64, 1, 20, 4, kUnknownVgprsPerSIMD },   // kVegaM2
    { 4, 10, 1, 8, 4, 64, 1, 64, 4, kUnknownVgprsPerSIMD },  // kGfx9_0_0
    { 1, 10, 1, 8, 1, 64, 1, 11, 4, kUnknownVgprsPerSIMD },  // kGfx9_0_2
    { 4, 10, 1, 8, 4, 64, 1, 20, 4, kUnknownVgprsPerSIMD },  // kGfx9_0_4
    { 4, 10, 1, 8, 4, 64, 1, 64, 4, kUnknownVgprsPerSIMD },  // kGfx9_0_6
    { 1, 10, 1, 8, 1, 64, 1, 8, 4, kUnknownVgprsPerSIMD },   // kGfx9_0_9
    { 8, 10, 1, 8, 0, 64, 1, 112, 4, kUnknownVgprsPerSIMD }, // kGfx9_0_A
    { 1, 10, 1, 8, 1, 64, 1, 8, 4, kUnknownVgprsPerSIMD },   // kGfx9_0_C
    { 4, 10, 1, 8, 0, 64, 1, 40, 4, kUnknownVgprsPerSIMD },  // kGfx9_4_2
    { 4, 10, 1, 8, 0, 64, 1, 36, 4, kUnknownVgprsPerSIMD },  // kGfx9_5_0
    { 2, 20, 1, 16, 4, 64, 2, 40, 2, 1024 },                 // kGfx10_1_0
    { 2, 20, 1, 16, 4, 64, 2, 36, 2, 1024 },                 // kGfx10_1_0Xl
    { 1, 20, 1, 16, 4, 64, 2, 20, 2, 1024 },                 // kGfx10_1_2
    { 1, 20, 1, 16, 4, 64, 2, 22, 2, 1024 },                 // kGfx10_1_2X
    { 1, 20, 1, 16, 4, 64, 2, 24, 2, 1024 },                 // kGfx10_1_2Xt
    { 2, 20, 1, 16, 4, 64, 2, 40, 2, 1024 },                 // kGfx10_1_1
    { 3, 16, 1, 16, 4, 64, 2, 60, 2, 1024 },                 // kGfx10_3_0
    { 4, 16, 1, 16, 4, 64, 2, 72, 2, 1024 },                 // kGfx10_3_0Xt
    { 4, 16, 1, 16, 4, 64, 2, 80, 2, 1024 },                 // kGfx10_3_0Xtx
    { 2, 16, 1, 16, 2, 64, 2, 40, 2, 1024 },                 // kGfx10_3_1
    { 2, 16, 1, 16, 2, 64, 2, 28, 2, 1024 },                 // kGfx10_3_2
    { 2, 16, 1, 16, 2, 64, 2, 32, 2, 1024 },                 // kGfx10_3_2Xt
    { 1, 16, 1, 16, 4, 32, 1, 8, 2, 1024 },                  // kGfx10_3_3
    { 1, 16, 1, 16, 2, 64, 2, 16, 2, 1024 },                 // kGfx10_3_4
    { 1, 16, 1, 16, 1, 64, 2, 12, 2, 1024 },                 // kGfx10_3_5
    { 1, 16, 1, 16, 1, 64, 1, 2, 2, 1024 },                  // kGfx10_3_6
    { 6, 16, 1, 8, 12, 64, 2, 96, 2, 1536 },                 // kGfx11_0_0
    { 6, 16, 1, 8, 12, 64, 2, 84, 2, 1536 },                 // kGfx11_0_0Xt
    { 6, 16, 1, 8, 12, 64, 2, 80, 2, 1536 },                 // kGfx11_0_0Gre
    { 6, 16, 1, 8, 12, 64, 2, 72, 2, 1536 },                 // kGfx11_0_0M
    { 3, 16, 1, 8, 6, 64, 2, 54, 2, 1536 },                  // kGfx11_0_1
    { 3, 16, 1, 8, 6, 64, 2, 60, 2, 1536 },                  // kGfx11_0_1Xt
    { 2, 16, 1, 8, 4, 64, 2, 28, 2, 1024 },                  // kGfx11_0_2
    { 2, 16, 1, 8, 4, 64, 2, 32, 2, 1024 },                  // kGfx11_0_2Xt
    { 1, 16, 1, 8, 2, 64, 2, 12, 2, 1024 },                  // kGfx11_0_3
    { 1, 16, 1, 8, 2, 64, 2, 8, 2, 1024 },                   // kGfx11_0_3A
    { 1, 16, 1, 8, 1, 64, 1, 4, 2, 1024 },                   // kGfx11_0_3B
    { 1, 16, 1, 8, 1, 64, 2, 16, 2, 1024 },                  // kGfx11_5_0
    { 2, 16, 1, 8, 1, 64, 2, 40, 2, 1536 },                  // kGfx11_5_1
    { 1, 16, 1, 8, 1, 64, 2, 8, 2, 1024 },                   // kGfx11_5_2
    { 1, 16, 1, 8, 1, 64, 1, 4, 2, 1024 },                   // kGfx11_5_3
    { 1, 16, 1, 8, 1, 64, 1, 2, 2, 1024 },                   // kGfx11_5_3A
    { 2, 16, 1, 8, 1, 64, 2, 28, 2, 1536 },                  // kGfx12_0_0
    { 2, 16, 1, 8, 1, 64, 2, 32, 2, 1536 },                  // kGfx12_0_0Xt
    { 3, 16, 1, 8, 1, 64, 2, 48, 2, 1536 },                  // kGfx12_0_1Gre
    { 4, 16, 1, 8, 1, 64, 2, 56, 2, 1536 },                  // kGfx12_0_1
    { 4, 16, 1, 8, 1, 64, 2, 64, 2, 1536 },                  // kGfx12_0_1Xt
});

[[nodiscard]] constexpr std::optional<DeviceInfo> GetDeviceInfoForAsicType(const AsicType asic_type) noexcept
{
    // Ensure array indexing is correct.
    using enum AsicType;
    static_assert(static_cast<std::underlying_type_t<AsicType>>(kTahitiPro) == 0);
    static_assert(kDeviceInfo.size() == static_cast<size_t>(kTotalAsics),
                  "kDeviceInfo needs to have the same number of entries as the AsicType enum.");
    static_assert(static_cast<size_t>(kUndefinedAsic) == std::numeric_limits<size_t>::max(),
                  "Ensure static cast below is handled correctly for kUndefinedAsic.");
    if (const size_t i = static_cast<size_t>(asic_type); i < kDeviceInfo.size()) [[likely]]
    {
        return kDeviceInfo[i];
    }
    return std::nullopt;
}

static_assert(
        []() constexpr noexcept {
            constexpr uint32_t kRevisions[] = {0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xDA, 0xDB, 0xDC, 0xDD};
            for (const uint32_t revision_id : kRevisions)
            {
                const auto card_info = GetCardInfoForDevice({device_info::kAmdVendorId, 0x1586, revision_id});
                if (!card_info.has_value() || card_info->asic_type != device_info::AsicType::kGfx11_5_1 || card_info->generation != kGfx11_5 ||
                    !card_info->is_apu)
                {
                    return false;
                }

                const auto device_info = GetDeviceInfoForAsicType(card_info->asic_type);
                if (!device_info.has_value() || device_info->num_cus != 40)
                {
                    return false;
                }
            }
            return true;
        }(),
        "Device ID 0x1586 (revisions 0xCA-0xCF and 0xDA-0xDD) is not properly supported by GetCardInfoForDevice.");

    [[nodiscard]] constexpr std::optional<uint32_t> GetTotalLdsSizeInBytesImpl(const HwGeneration gen, const uint8_t num_cus) noexcept
    {
        constexpr uint32_t kKiB             = 1024;
        uint32_t           lds_bytes_per_cu = 0;
        switch (gen)
        {
        case kGfx9:
        case kGfx10:
        case kGfx10_3:
        case kGfx11:
        case kGfx11_5:
        case kGfx12:
        case kCdna:
        case kCdna2:
        case kCdna3:
            lds_bytes_per_cu = 64 * kKiB;
            break;
        case kCdna4:
            lds_bytes_per_cu = 160 * kKiB;
            break;
        case kUndefinedGeneration:
        case kNvidia:
        case kIntel:
        case kSouthernIsland:
        case kSeaIsland:
        case kVolcanicIsland:
            [[fallthrough]];
        [[unlikely]] default:
            return std::nullopt;
        }
        return num_cus * lds_bytes_per_cu;
    }

// Compile-time tests — one per HW generation, with an arbitrary CU count of 4.
static_assert(!GetTotalLdsSizeInBytesImpl(kUndefinedGeneration, 4).has_value());
static_assert(!GetTotalLdsSizeInBytesImpl(kNvidia, 4).has_value());
static_assert(!GetTotalLdsSizeInBytesImpl(kIntel, 4).has_value());
static_assert(!GetTotalLdsSizeInBytesImpl(kSouthernIsland, 4).has_value());
static_assert(!GetTotalLdsSizeInBytesImpl(kSeaIsland, 4).has_value());
static_assert(!GetTotalLdsSizeInBytesImpl(kVolcanicIsland, 4).has_value());

// GFX9 and CDNA generations: 64 KiB/CU.
static_assert(GetTotalLdsSizeInBytesImpl(kGfx9, 4) == 4U * 64 * 1024);
static_assert(GetTotalLdsSizeInBytesImpl(kCdna, 4) == 4U * 64 * 1024);
static_assert(GetTotalLdsSizeInBytesImpl(kCdna2, 4) == 4U * 64 * 1024);
static_assert(GetTotalLdsSizeInBytesImpl(kCdna3, 4) == 4U * 64 * 1024);

// GFX10 generations: 64 KiB/CU.
static_assert(GetTotalLdsSizeInBytesImpl(kGfx10, 4) == 4U * 64 * 1024);
static_assert(GetTotalLdsSizeInBytesImpl(kGfx10_3, 4) == 4U * 64 * 1024);

// GFX11 and GFX11.5 generations: 64 KiB/CU.
static_assert(GetTotalLdsSizeInBytesImpl(kGfx11, 4) == 4U * 64 * 1024);
static_assert(GetTotalLdsSizeInBytesImpl(kGfx11_5, 4) == 4U * 64 * 1024);

// GFX12: 64 KiB/CU (kGfx12 < kGfx11_5 numerically, so it falls into the <= kGfx11_5 branch).
static_assert(GetTotalLdsSizeInBytesImpl(kGfx12, 4) == 4U * 64 * 1024);

// CDNA4: 160 KiB/CU.
static_assert(GetTotalLdsSizeInBytesImpl(kCdna4, 4) == 4U * 160 * 1024);

static_assert(static_cast<uint8_t>(kTotalHwGenerations) == 16, "Add test for new HW gen!");

} // namespace

// In C++26 we could return std::optional references here to avoid unnecessary copying of CardInfo and DeviceInfo
// structs.
#if __cplusplus > 202302L
#warning "C++26 detected: consider returning std::optional<const CardInfo&> / std::optional<const DeviceInfo&>"
#endif

namespace device_info
{
std::optional<CardInfo> GetCardInfo(const AdapterId& desc) noexcept { return GetCardInfoForDevice(desc); }

std::optional<DeviceInfo> GetDeviceInfo(const CardInfo& info) noexcept
{
    return GetDeviceInfoForAsicType(info.asic_type);
}

std::optional<uint32_t> GetTotalLdsSizeInBytes(const HwGeneration gen, const DeviceInfo& info) noexcept
{
    return GetTotalLdsSizeInBytesImpl(gen, info.num_cus);
}
} // namespace device_info
