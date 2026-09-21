#include "pch.h"
#include "InputCollection.h"

#include <thread>

void InputCollection::addNewDelta(InputDelta delta)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex);

        for (auto& inputDelta : simToPresentDeltas)
        {
            inputDelta += delta;
        }
    }

    std::unique_lock<std::shared_mutex> lock(simDeltasMutex);
    inProgressSimToSimDelta += delta;
}

InputDelta InputCollection::readSimDelta(uint32_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(simDeltasMutex);

    auto index = frameId % 8;
    return simToSimDeltas[index];
}

void InputCollection::startCollectingForFrame(uint32_t frameId)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto index = frameId % 8;
        simToPresentDeltas[index] = { 0, 0 };
    }

    std::unique_lock<std::shared_mutex> lock(simDeltasMutex);

    auto index = frameId % 8;
    simToSimDeltas[index] = inProgressSimToSimDelta;
    inProgressSimToSimDelta = { 0, 0 };
}

InputDelta InputCollection::readDelta(uint32_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto index = frameId % 8;
    return simToPresentDeltas[index];
}
