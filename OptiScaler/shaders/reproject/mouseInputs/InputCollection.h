#pragma once

#include <shared_mutex>
#include <DirectXMath.h>

struct InputDelta
{
    int32_t x;
    int32_t y;
    uint64_t startTimestampNs;

    // Compound assignment operators (modify the current object)
    constexpr InputDelta& operator+=(const InputDelta& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr InputDelta& operator-=(const InputDelta& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr InputDelta& operator*=(double mul)
    {
        x = static_cast<int32_t>(x * mul);
        y = static_cast<int32_t>(y * mul);
        return *this;
    }

    constexpr InputDelta& operator/=(double div)
    {
        x = static_cast<int32_t>(x / div);
        y = static_cast<int32_t>(y / div);
        return *this;
    }

    constexpr InputDelta operator+(const InputDelta& other) const { return InputDelta { x + other.x, y + other.y }; }

    constexpr InputDelta operator-(const InputDelta& other) const { return InputDelta { x - other.x, y - other.y }; }

    constexpr InputDelta operator*(double mul) const
    {
        return InputDelta { static_cast<int32_t>(x * mul), static_cast<int32_t>(y * mul) };
    }

    constexpr InputDelta operator/(double div) const
    {
        return InputDelta { static_cast<int32_t>(x / div), static_cast<int32_t>(y / div) };
    }

    // Cast operator
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

    void startCollectingForFrame(uint64_t frameId);

    // Returns the mouse delta between sim starts of frameId-1 and frameId
    InputDelta readSimsDelta(uint64_t frameId);

    // Returns the mouse delta since sim start of the provided frame id
    InputDelta readDeltaSinceSim(uint64_t frameId);
};