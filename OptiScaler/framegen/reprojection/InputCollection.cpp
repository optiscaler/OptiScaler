#include "pch.h"
#include "InputCollection.h"

#include <thread>

void InputCollection::addNewDelta(InputDelta delta)
{
    std::unique_lock<std::shared_mutex> lock(mutex);

    for (auto& inputDelta : inputDeltas)
    {
        inputDelta += delta;
    }
}

void InputCollection::markFrameStart(uint32_t frameId)
{
    std::unique_lock<std::shared_mutex> lock(mutex);

    auto index = frameId % 8;
    inputDeltas[index] = { 0, 0 };
}

InputDelta InputCollection::readDelta(uint32_t frameId)
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto index = frameId % 8;
    return inputDeltas[index];
}

InputDelta InputCollection::readPresentDelta()
{
    std::unique_lock<std::shared_mutex> lock(mutex);

    auto value = inputDeltas[8];
    inputDeltas[8] = { 0, 0 };

    return value;
}
