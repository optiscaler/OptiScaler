#pragma once

// SDK 2.4 declares these with fewer parameters than dx12/ffx_api_dx12.h, files that include both headers
// fail to build unless the Vulkan ones get other names
#define PfnFfxResourceAllocatorFunc PfnFfxResourceAllocatorFuncVk
#define PfnFfxResourceDeallocatorFunc PfnFfxResourceDeallocatorFuncVk

#include <vk/ffx_api_vk.h>

#undef PfnFfxResourceAllocatorFunc
#undef PfnFfxResourceDeallocatorFunc
