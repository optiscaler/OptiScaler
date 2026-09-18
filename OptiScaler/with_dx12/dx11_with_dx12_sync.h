#pragma once

#include <mutex>
#include <shared_mutex>

namespace Dx11wDx12Sync
{
// This mutex will be used for preventing resize while present is running
inline std::shared_mutex& PresentResizeMutex()
{
    static std::shared_mutex mutex;
    return mutex;
}
} // namespace Dx11wDx12Sync
