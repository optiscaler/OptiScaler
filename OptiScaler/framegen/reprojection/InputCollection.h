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
    InputDelta inputDeltas[9] {}; // 8 sim -> present + 1 present -> next present

  public:
    static InputCollection& getInstance()
    {
        static InputCollection instance;
        return instance;
    }

    void addNewDelta(InputDelta delta);

    // To be called just after the game polled user input
    void markFrameStart(uint32_t frameId);

    InputDelta readDelta(uint32_t frameId);

    // For autocalibration
    InputDelta readPresentDelta();
};