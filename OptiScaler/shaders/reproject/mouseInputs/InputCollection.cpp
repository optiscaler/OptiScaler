#include "pch.h"
#include "InputCollection.h"
#include <Util.h>

#include <thread>
#include <algorithm>
#include <intrin.h>

void InputCollection::writeLiveSinks()
{
    const uint64_t packed = (uint64_t) cumulativeX | ((uint64_t) cumulativeY << 32);

    // Single aligned 64-bit store, so the GPU never sees x and y from different events
    for (auto sink : liveSinks)
        *sink = packed;

    // Flush the write-combining buffers so the value reaches memory right away
    _mm_sfence();
}

void InputCollection::addNewDelta(InputDelta delta)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex);

        for (auto& inputDelta : simToPresentDeltas)
        {
            inputDelta += delta;
        }

        cumulativeX += (uint32_t) delta.x;
        cumulativeY += (uint32_t) delta.y;
        writeLiveSinks();
    }

    std::unique_lock<std::shared_mutex> lock(simDeltasMutex);
    inProgressSimToSimDelta += delta;
}

InputDelta InputCollection::readSimsDelta(uint64_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(simDeltasMutex);

    auto index = frameId % 8;
    return simToSimDeltas[index];
}

void InputCollection::startCollectingForFrame(uint64_t frameId)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto index = frameId % 8;
        simToPresentDeltas[index] = { 0, 0, Util::GetTimestamp() };
        simStartCumulative[index] = { (int32_t) cumulativeX, (int32_t) cumulativeY };
    }

    std::unique_lock<std::shared_mutex> lock(simDeltasMutex);

    auto index = frameId % 8;
    simToSimDeltas[index] = inProgressSimToSimDelta;
    inProgressSimToSimDelta = { 0, 0 };
}

InputDelta InputCollection::readDeltaSinceSim(uint64_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto index = frameId % 8;
    return simToPresentDeltas[index];
}

DirectX::XMINT2 InputCollection::readCumulativeAtSim(uint64_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto index = frameId % 8;
    return simStartCumulative[index];
}

void InputCollection::registerLiveSink(void* sink)
{
    if (sink == nullptr)
        return;

    std::unique_lock<std::shared_mutex> lock(mutex);

    liveSinks.push_back((volatile uint64_t*) sink);
    writeLiveSinks();
}

void InputCollection::unregisterLiveSink(void* sink)
{
    std::unique_lock<std::shared_mutex> lock(mutex);
    std::erase(liveSinks, (volatile uint64_t*) sink);
}
