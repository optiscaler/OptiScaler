// Standalone CPU tests; this target does not use the DLL's precompiled header or graphics dependencies.
#include "../OptiScaler/gpu_time/FgTiming.h"

#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    FgTiming::Channel channel("test", "generation");
    const auto now = FgTiming::Clock::time_point {};
    auto epoch = channel.Epoch();
    Check(!channel.Read(now).milliseconds, "An empty channel must not report zero cost");
    Check(!channel.Publish(epoch, 0, 1.0, 0, now), "Missing identities must be rejected");
    Check(!channel.Publish(epoch, 1, -1.0, 0, now), "Negative durations must be rejected");
    Check(!channel.Publish(epoch, 1, std::numeric_limits<double>::quiet_NaN(), 0, now), "NaN must be rejected");
    Check(!channel.Publish(epoch, 1, std::numeric_limits<double>::infinity(), 0, now), "Infinity must be rejected");
    Check(channel.Publish(epoch, 10, 2.0, 3, now), "A completed measurement must be accepted");
    Check(!channel.Publish(epoch, 10, 99.0, 0, now), "UI polling must not count the same GPU sample twice");
    Check(!channel.Publish(epoch, 9, 99.0, 0, now), "Late older samples must not replace a newer result");
    Check(channel.Publish(epoch, 11, 4.0, 3, now), "A newer sample must be accepted");
    auto snapshot = channel.Read(now);
    Check(snapshot.milliseconds == 4.0 && snapshot.averageMilliseconds == 3.0 && snapshot.sampleCount == 2,
          "Only unique valid measurements may contribute to the average");
    Check(snapshot.generatedFrames == 3 && snapshot.frameId == 11, "Batch metadata must be preserved");
    Check(!channel.Read(now + std::chrono::seconds(2)).milliseconds, "Stale timing must become unavailable");
    channel.Reset();
    Check(!channel.Publish(epoch, 12, 8.0, 0, now), "Old in-flight results must not survive a context reset");
    epoch = channel.Epoch();
    Check(channel.Publish(epoch, 1, 0.0, 0, now),
          "A valid zero-duration timestamp interval is distinct from missing data");
    Check(channel.Read(now).milliseconds == 0.0, "Reset must allow restarted frame IDs");
    for (uint64_t id = 2; id <= 100; ++id)
        Check(channel.Publish(epoch, id, 1.0, 0, now), "Sequential completed results must be accepted");
    snapshot = channel.Read(now);
    Check(snapshot.sampleCount == FgTiming::Channel::HistorySize && snapshot.averageMilliseconds == 1.0,
          "History must remain bounded and evict old samples");

    Check(!FgTiming::VulkanMilliseconds(0, 1, 0, 1.0), "Queues without timestamp support must be unavailable");
    Check(!FgTiming::VulkanMilliseconds(0, 1, 64, 0.0), "Invalid timestamp periods must be rejected");
    Check(FgTiming::VulkanMilliseconds(250, 5, 8, 1000000.0) == 11.0, "Valid-bit counter rollover must be handled");
    Check(FgTiming::VulkanMilliseconds(100, 1100, 64, 1000.0) == 1.0, "Timestamp periods must convert to milliseconds");
    std::cout << "FG timing validity, epoch, freshness, history and timestamp conversion tests passed\n";
    return 0;
}
