#pragma once
#include <algorithm>

namespace DlssNr
{
struct GuideExtent { unsigned int width, height; };
struct GuideRegion
{
    unsigned int x, y, width, height;
    bool valid() const { return width != 0 && height != 0; }
};
struct GuideRegions { GuideRegion depth, motion; };

inline GuideRegion GuideSubrect(GuideExtent allocation, GuideExtent wanted, unsigned int x, unsigned int y)
{
    x = std::min(x, allocation.width);
    y = std::min(y, allocation.height);
    const auto availableW = allocation.width - x;
    const auto availableH = allocation.height - y;
    return { x, y, std::min(wanted.width ? wanted.width : availableW, availableW),
                   std::min(wanted.height ? wanted.height : availableH, availableH) };
}

// Adapted from cmh1448's motion-vector metadata fix (6446cc8).
// Output is the DLSS output extent even when NR runs on its smaller pre-SR colour input.
// Depth allocation/offsets must never determine the motion texture's valid region.
inline GuideRegions ResolveGuideRegions(GuideExtent depthAllocation, GuideExtent motionAllocation,
                                       GuideExtent render, GuideExtent output, bool lowResolutionMotion,
                                       unsigned int depthX, unsigned int depthY,
                                       unsigned int motionX, unsigned int motionY)
{
    return { GuideSubrect(depthAllocation, render, depthX, depthY),
             GuideSubrect(motionAllocation, lowResolutionMotion ? render : output, motionX, motionY) };
}
}
