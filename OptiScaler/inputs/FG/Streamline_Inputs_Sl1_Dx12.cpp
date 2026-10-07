#include "pch.h"
#include "Streamline_Inputs_Sl1_Dx12.h"

#include <Config.h>
#include <magic_enum.hpp>
#include <resource_tracking/ResTrack_dx12.h>
#include <sl1_reflex.h>

uint64_t Sl1_Inputs_Dx12::TagKey(uint32_t id, sl1::BufferType type)
{
    return (static_cast<uint64_t>(id) << 32) | static_cast<uint32_t>(type);
}

void Sl1_Inputs_Dx12::CheckForFrame(IFGFeature_Dx12* fg, uint32_t frameIndex)
{
    std::scoped_lock lock(_frameBoundaryMutex);

    if (frameIndex != 0)
    {
        LOG_DEBUG("SL1 CheckForFrame: frameIndex:{}, lastPresentFrameId:{}, isFrameFinished:{}", frameIndex,
                  _lastPresentFrameId, _isFrameFinished);

        _isFrameFinished = false;

        fg->SetFrameCount(frameIndex - 1);
        fg->StartNewFrame();
        _currentIndex = fg->GetIndex();
        _frameIdIndex[_currentIndex] = frameIndex;
    }
}

int Sl1_Inputs_Dx12::IndexForFrameId(uint32_t frameIndex) const
{
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (_frameIdIndex[i] == frameIndex)
            return i;
    }

    return -1;
}

bool Sl1_Inputs_Dx12::setTag(const sl1::Resource* resource, sl1::BufferType type, uint32_t id,
                             const sl1::Extent* extent)
{
    std::scoped_lock lock(_mutex);

    const auto key = TagKey(id, type);

    if (resource == nullptr || resource->native == nullptr)
    {
        LOG_TRACE("SL1 removing/nulling tag type: {}, id: {}", magic_enum::enum_name(type), id);
        _tags.erase(key);
        return false;
    }

    CachedTag tag {};
    tag.resource = *resource;
    tag.type = type;
    tag.id = id;

    if (extent != nullptr && *extent)
    {
        tag.extent = *extent;
        tag.hasExtent = true;
    }

    _tags[key] = tag;

    LOG_TRACE("SL1 cached tag type: {}, id: {}, native: {:X}, state: {:X}, hasExtent: {}", magic_enum::enum_name(type),
              id, reinterpret_cast<size_t>(resource->native), resource->state, tag.hasExtent);

    return true;
}

bool Sl1_Inputs_Dx12::setConstants(const sl1::Constants& constants, uint32_t frameIndex, uint32_t id)
{
    {
        std::scoped_lock lock(_mutex);
        lastConstants = constants;
        hasLastConstants = true;
    }

    return applyConstants(constants, frameIndex, id);
}

bool Sl1_Inputs_Dx12::applyConstants(const sl1::Constants& values, uint32_t frameIndex, uint32_t id)
{
    auto fgOutput = State::Instance().currentFG;

    if (fgOutput == nullptr)
        return false;

    auto data = values;
    auto config = Config::Instance();

    FG_Constants fgConstants {};
    fgConstants.displayWidth = 0;
    fgConstants.displayHeight = 0;
    fgConstants.flags.reset();

    if (IsTrue(data.depthInverted))
        fgConstants.flags |= FG_Flags::InvertedDepth;

    if (IsTrue(data.motionVectorsJittered))
        fgConstants.flags |= FG_Flags::JitteredMVs;

    if (IsTrue(data.motionVectorsDilated))
        fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

    if (config->FGAsync.value_or_default())
        fgConstants.flags |= FG_Flags::Async;

    if (infiniteDepth)
        fgConstants.flags |= FG_Flags::InfiniteDepth;

    if (config->FGXeFGDepthInverted.value_or_default() != IsTrue(data.depthInverted) ||
        config->FGXeFGJitteredMV.value_or_default() != IsTrue(data.motionVectorsJittered) ||
        config->FGXeFGHighResMV.value_or_default() != IsTrue(data.motionVectorsDilated))
    {
        config->FGXeFGDepthInverted = IsTrue(data.depthInverted);
        config->FGXeFGJitteredMV = IsTrue(data.motionVectorsJittered);
        config->FGXeFGHighResMV = IsTrue(data.motionVectorsDilated);
        LOG_DEBUG("SL1 XeFG DepthInverted: {}", config->FGXeFGDepthInverted.value_or_default());
        LOG_DEBUG("SL1 XeFG JitteredMV: {}", config->FGXeFGJitteredMV.value_or_default());
        LOG_DEBUG("SL1 XeFG HighResMV: {}", config->FGXeFGHighResMV.value_or_default());
        config->SaveXeFG();
    }

    fgOutput->EvaluateState(State::Instance().currentD3D12Device, fgConstants);

    if (!config->IsFGEnabled())
    {
        LOG_TRACE("SL1 FG not enabled");
        return true;
    }

    if (fgOutput->IsActive() && IsTrue(lastConstants.notRenderingGameFrames))
    {
        fgOutput->Deactivate();
        fgOutput->UpdateTarget();
        return true;
    }
    else if (!fgOutput->IsActive() && !fgOutput->IsPaused() && !IsTrue(lastConstants.notRenderingGameFrames))
    {
        fgOutput->Activate();
    }
    else if (!fgOutput->IsActive() || fgOutput->IsPaused())
    {
        LOG_TRACE("SL1 FG not active or paused (A:{}, P:{})", fgOutput->IsActive(), fgOutput->IsPaused());
        return true;
    }

    auto loadCameraMatrix = [&]()
    {
        if (IsTrue(data.orthographicProjection))
            return false;

        float projMatrix[4][4];
        memcpy(projMatrix, &data.cameraViewToClip, sizeof(projMatrix));

        const bool isEmptyOrIdentityMatrix = [&]()
        {
            float m[4][4] = {};
            if (memcmp(projMatrix, m, sizeof(m)) == 0)
                return true;

            m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
            return memcmp(projMatrix, m, sizeof(m)) == 0;
        }();

        if (isEmptyOrIdentityMatrix)
            return false;

        const double b = projMatrix[1][1];
        const double c = projMatrix[2][2];
        const double d = projMatrix[3][2];
        const double e = projMatrix[2][3];

        if (e < 0.0)
        {
            data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
            data.cameraFar = static_cast<float>(d / (c + 1.0));
        }
        else
        {
            data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
            data.cameraFar = static_cast<float>(-d / (c - 1.0));
        }

        if (IsTrue(data.depthInverted))
            std::swap(data.cameraNear, data.cameraFar);

        data.cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
        return true;
    };

    static bool dontRecalc = false;

    LOG_TRACE("SL1 Camera from SL pre recalc near: {}, far: {}, frameIndex: {}, id: {}", data.cameraNear,
              data.cameraFar, frameIndex, id);

    if (!dontRecalc)
        loadCameraMatrix();

    if (!dontRecalc && (data.cameraNear < 0.0f || data.cameraFar < 0.0f))
        dontRecalc = true;

    infiniteDepth = false;
    if (data.cameraNear != 0.0f && data.cameraFar == 0.0f)
    {
        infiniteDepth = true;
        data.cameraFar = data.cameraNear + 1.0f;
    }

    fgOutput->SetCameraValues(data.cameraNear, data.cameraFar, data.cameraFOV, data.cameraAspectRatio, 0.0f);
    fgOutput->SetJitter(data.jitterOffset.x, data.jitterOffset.y);

    const bool multiplyByResolution = true;
    if (multiplyByResolution)
        fgOutput->SetMVScale(data.mvecScale.x * mvsWidth, data.mvecScale.y * mvsHeight);
    else
        fgOutput->SetMVScale(data.mvecScale.x, data.mvecScale.y);

    fgOutput->SetCameraData(reinterpret_cast<float*>(&data.cameraPos), reinterpret_cast<float*>(&data.cameraUp),
                            reinterpret_cast<float*>(&data.cameraRight), reinterpret_cast<float*>(&data.cameraFwd));

    fgOutput->SetReset(IsTrue(data.reset));
    fgOutput->SetFrameTimeDelta(static_cast<float>(State::Instance().lastFGFrameTime));

    return true;
}

bool Sl1_Inputs_Dx12::reportCachedResource(const CachedTag& tag, ID3D12GraphicsCommandList* cmdBuffer,
                                           uint32_t frameIndex)
{
    auto& state = State::Instance();
    state.dlssgLastFrame = state.fgLastFrame;

    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

    if (fgOutput == nullptr || !Config::Instance()->IsFGEnabled())
        return false;

    if (tag.resource.native == nullptr)
        return false;

    if (tag.resource.type != sl1::eResourceTypeTex2d)
    {
        LOG_TRACE("SL1 ignoring non-texture tag type: {}, id: {}", magic_enum::enum_name(tag.type), tag.id);
        return false;
    }

    auto d3dRes = static_cast<ID3D12Resource*>(tag.resource.native);
    auto desc = d3dRes->GetDesc();

    Dx12Resource res {};
    res.resource = d3dRes;
    res.cmdList = cmdBuffer;
    res.width = tag.hasExtent ? tag.extent.width : desc.Width;
    res.height = tag.hasExtent ? tag.extent.height : desc.Height;
    res.state = static_cast<D3D12_RESOURCE_STATES>(tag.resource.state);
    res.validity = FG_ResourceValidity::UntilPresent;

    if (frameIndex > 0)
    {
        int index = IndexForFrameId(frameIndex);

        if (index >= 0)
        {
            res.frameIndex = index;
        }
        else
        {
            LOG_WARN("SL1 frame ID {} not found in tracking, using current index {}", frameIndex, _currentIndex);
            res.frameIndex = _currentIndex;
        }
    }
    else
    {
        res.frameIndex = -1;
    }

    bool handled = true;

    if (tag.type == sl1::eBufferTypeDepth)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Depth, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGDepthValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Depth;
        fgOutput->SetResource(&res);
    }
    else if (tag.type == sl1::eBufferTypeMVec)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Velocity, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGVelocityValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Velocity;
        mvsWidth = res.width;
        mvsHeight = res.height;
        fgOutput->SetResource(&res);
    }
    else if (tag.type == sl1::eBufferTypeHUDLessColor)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::HudlessColor, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGHudlessValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::HudlessColor;
        fgOutput->SetInterpolationRect(res.width, res.height);
        fgOutput->SetResource(&res);
    }
    else
    {
        handled = false;
    }

    if (handled)
    {
        LOG_DEBUG("SL1 reported resource type: {}, frameIndex: {}, id: {}, fgIndex: {}, validity: {}",
                  magic_enum::enum_name(tag.type), frameIndex, tag.id, res.frameIndex,
                  magic_enum::enum_name(res.validity));
    }

    return handled;
}

bool Sl1_Inputs_Dx12::evaluateFeature(sl1::CommandBuffer* cmdBuffer, sl1::Feature feature, uint32_t frameIndex,
                                      uint32_t id)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    CheckForFrame(fgOutput, frameIndex);

    auto dxCmd = reinterpret_cast<ID3D12GraphicsCommandList*>(cmdBuffer);

    sl1::Constants constants {};

    bool handled = false;
    bool applyLastConstants = false;

    if (feature == sl1::Feature::eFeatureReflex)
    {
        // Do not use this as an id for resources
        auto reflexMarker = (sl1::ReflexMarker) id;

        std::scoped_lock lock(_mutex);

        for (const auto& tag : _tags)
            handled |= reportCachedResource(tag.second, dxCmd, frameIndex);

        if (hasLastConstants)
        {
            constants = lastConstants;
            applyLastConstants = true;
        }

        LOG_TRACE("SL1 evaluateFeature feature: {}, frameIndex: {}, id: {}, cmd: {:X}, cachedTags {}",
                  magic_enum::enum_name(feature), frameIndex, id, reinterpret_cast<size_t>(cmdBuffer), _tags.size());
    }
    // else
    //{
    //     std::vector<CachedTag> localTags;

    //    {
    //        std::scoped_lock lock(_mutex);

    //        for (const auto& [key, tag] : _tags)
    //        {
    //            if (tag.id == id)
    //                localTags.push_back(tag);
    //        }

    //        if (hasLastConstants)
    //        {
    //            constants = lastConstants;
    //            applyLastConstants = true;
    //        }
    //    }

    //    LOG_TRACE("SL1 evaluateFeature feature: {}, frameIndex: {}, id: {}, cmd: {:X}, cachedTags: {}",
    //              magic_enum::enum_name(feature), frameIndex, id, reinterpret_cast<size_t>(cmdBuffer),
    //              localTags.size());

    //    for (const auto& tag : localTags)
    //        handled |= reportCachedResource(tag, dxCmd, frameIndex);
    //}

    // Re-apply constants after resources, because MV scale depends on the tagged MV resource resolution.
    if (applyLastConstants)
        applyConstants(constants, frameIndex, id);

    return handled;
}

bool Sl1_Inputs_Dx12::evaluateState()
{
    auto fgOutput = State::Instance().currentFG;

    if (fgOutput == nullptr)
        return false;

    LOG_FUNC();

    static UINT64 lastFrameCount = 0;
    static UINT64 repeatsInRow = 0;
    if (lastFrameCount == fgOutput->FrameCount())
    {
        repeatsInRow++;
    }
    else
    {
        lastFrameCount = fgOutput->FrameCount();
        repeatsInRow = 0;
    }

    if (repeatsInRow > 10 && fgOutput->IsActive())
    {
        LOG_WARN("SL1 many frame count repeats in a row, stopping FG");
        State::Instance().fgChanged = true;
        repeatsInRow = 0;
        return false;
    }

    return true;
}

void Sl1_Inputs_Dx12::markPresent(uint64_t frameIndex)
{
    std::scoped_lock lock(_frameBoundaryMutex);
    LOG_TRACE("SL1 frameIndex: {}", frameIndex);
    _isFrameFinished = true;
    _lastPresentFrameId = static_cast<uint32_t>(frameIndex);

    if (State::Instance().currentFG != nullptr)
        State::Instance().currentFG->SetFrameCount(frameIndex);
}
