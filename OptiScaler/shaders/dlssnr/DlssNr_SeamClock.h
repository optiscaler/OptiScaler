#pragma once

// Call under g_nrMutex. This is a render-seam identity, never evidence of GPU submission.
class DlssNrSeamClock
{
    unsigned long long lastRaw = ~0ull;
    unsigned long long epoch = 0;

  public:
    unsigned long long AtSeam(bool begin, bool bridge, unsigned long long submitted)
    {
        // Bridges already supply a submitted-frame counter; preserve duplicate detection there.
        if (bridge)
            return submitted;
        // Present may advance between Before and After. Both halves retain the same identity.
        if (!begin)
            return epoch;
        if (submitted != lastRaw)
        {
            epoch = submitted > epoch ? submitted : epoch + 1;
            lastRaw = submitted;
        }
        else
            ++epoch;
        return epoch;
    }
};
