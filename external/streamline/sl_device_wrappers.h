#pragma once

#include <cstdint>

#include "sl_struct.h"

namespace sl
{

//! Rendering API
//! 
enum class RenderAPI : uint32_t
{
    eD3D11,
    eD3D12,
    eVulkan,
    eCount
};

}
