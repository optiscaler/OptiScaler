#pragma once

#include <cstdint>

#include "sl_struct.h"

namespace sl
{

//! Engine types
//! 
enum class EngineType : uint32_t
{
    eCustom,
    eUnreal,
    eUnity,
    eCount
};

}
