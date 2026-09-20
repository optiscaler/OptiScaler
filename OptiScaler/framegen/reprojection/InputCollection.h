#pragma once

#include <shared_mutex>
#include <DirectXMath.h>

struct InputDelta
{
    int32_t x;
    int32_t y;

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

    // Used to calculate mouse movement for a given sim frameid
    // To be called just before game pulled user input
    void markFrameStart(uint32_t frameId);

    InputDelta readSimDelta(uint32_t frameId);

    // To be called just after the game polled user input
    void startCollectingForFrame(uint32_t frameId);

    // To be used in Present, calling resets the provided frame id
    InputDelta readDelta(uint32_t frameId);
};