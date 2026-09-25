#pragma once

#include <shared_mutex>
#include <DirectXMath.h>

struct InputDelta
{
    int32_t x;
    int32_t y;
    uint64_t startTimestampNs;

    constexpr InputDelta& operator+=(const InputDelta& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    operator DirectX::XMINT2() const { return { x, y }; }
};

class InputCollection
{
    std::shared_mutex mutex;
    std::shared_mutex simDeltasMutex;

    InputDelta simToPresentDeltas[8] {};
    InputDelta inProgressSimToSimDelta {}; // assumes only one in-flight sim
    InputDelta simToSimDeltas[8] {};       // just for reading

  public:
    static InputCollection& getInstance()
    {
        static InputCollection instance;
        return instance;
    }

    void addNewDelta(InputDelta delta);

    void startCollectingForFrame(uint32_t frameId);

    // Returns the mouse delta between sim starts of frameId-1 and frameId
    InputDelta readSimsDelta(uint32_t frameId);

    // Returns the mouse delta since sim start of the provided frame id
    InputDelta readDeltaSinceSim(uint32_t frameId);
};