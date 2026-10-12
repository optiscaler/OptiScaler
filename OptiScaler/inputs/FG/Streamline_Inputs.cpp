#include "pch.h"
#include "Streamline_Inputs.h"
#include <Config.h>
#include <resource_tracking/ResTrack_dx12.h>
#include <magic_enum.hpp>

void Sl_Inputs::CheckForFrame(IFGFeature* fg, uint32_t frameId)
{
    std::scoped_lock lock(_frameBoundaryMutex);

    const bool presentMarked = _isFrameFinished && _lastPresentFrameId == _currentFrameId;
    const bool fgDispatched =
        fg->IsActive() && !fg->IsPaused() && fg->IsDispatched() && fg->FrameCount() == _currentFrameId;

    if ((presentMarked || fgDispatched) && frameId == 0 && _currentFrameId != 0)
    {
        LOG_DEBUG("1> CheckForFrame: frameId={}, currentFrameId={}, lastPresentFrameId={}, isFrameFinished={}", frameId,
                  _currentFrameId, _lastPresentFrameId, _isFrameFinished);

        _isFrameFinished = false;

        fg->StartNewFrame();
        _currentIndex = fg->GetIndex();
        _currentFrameId++;
        _frameIdIndex[_currentIndex] = _currentFrameId;
    }
    else if (frameId != 0 && frameId > _currentFrameId)
    {
        LOG_DEBUG("2> CheckForFrame: frameId={}, currentFrameId={}, lastPresentFrameId={}, isFrameFinished={}", frameId,
                  _currentFrameId, _lastPresentFrameId, _isFrameFinished);

        _isFrameFinished = false;
        //_lastPresentFrameId = frameId - 1;

        fg->StartNewFrame();
        _currentIndex = fg->GetIndex();
        _currentFrameId = frameId;
        _frameIdIndex[_currentIndex] = _currentFrameId;
    }
}

int Sl_Inputs::IndexForFrameId(uint32_t frameId) const
{
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (_frameIdIndex[i] == frameId)
            return i;
    }

    return -1;
}

bool Sl_Inputs::setConstants(const sl::Constants& values, uint32_t frameId)
{
    auto& state = State::Instance();
    auto fgOutput = state.CurrentFG();

    if (fgOutput == nullptr)
        return false;

    CheckForFrame(fgOutput, frameId);

    auto data = sl::Constants {};
    bool dataFound = false;

    if (values.structVersion == data.structVersion)
    {
        data = values;

        dataFound = true;
    }
    else if ((data.structVersion == sl::kStructVersion2 && values.structVersion == sl::kStructVersion1) ||
             values.structVersion == 0)
    // Spider-Man Remastered does this funny thing of sending an invalid struct version
    {
        auto* pNext = data.next;
        memcpy(&data, &values, sizeof(values) - sizeof(sl::Constants::minRelativeLinearDepthObjectSeparation));
        data.structVersion = sl::kStructVersion2;
        data.next = pNext;

        dataFound = true;
    }

    if (dataFound)
    {
        auto config = Config::Instance();

        // FG Evaluate part
        FG_Constants fgConstants {};

        // TODO
        fgConstants.displayWidth = 0;
        fgConstants.displayHeight = 0;

        fgConstants.flags.reset();

        // if ()
        //     fgConstants.flags |= FG_Flags::Hdr;

        if (data.depthInverted)
            fgConstants.flags |= FG_Flags::InvertedDepth;

        if (data.motionVectorsJittered)
            fgConstants.flags |= FG_Flags::JitteredMVs;

        if (data.motionVectorsDilated)
            fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

        if (config->FGAsync.value_or_default())
            fgConstants.flags |= FG_Flags::Async;

        if (infiniteDepth)
            fgConstants.flags |= FG_Flags::InfiniteDepth;

        if (config->FGXeFGDepthInverted.value_or_default() != (data.depthInverted == sl::Boolean::eTrue) ||
            config->FGXeFGJitteredMV.value_or_default() != (data.motionVectorsJittered == sl::Boolean::eTrue) ||
            config->FGXeFGHighResMV.value_or_default() != (data.motionVectorsDilated == sl::Boolean::eTrue))
        {
            config->FGXeFGDepthInverted = (data.depthInverted == sl::Boolean::eTrue);
            config->FGXeFGJitteredMV = (data.motionVectorsJittered == sl::Boolean::eTrue);
            config->FGXeFGHighResMV = (data.motionVectorsDilated == sl::Boolean::eTrue);
            LOG_DEBUG("XeFG DepthInverted: {}", config->FGXeFGDepthInverted.value_or_default());
            LOG_DEBUG("XeFG JitteredMV: {}", config->FGXeFGJitteredMV.value_or_default());
            LOG_DEBUG("XeFG HighResMV: {}", config->FGXeFGHighResMV.value_or_default());
            config->SaveXeFG();
        }

        if (state.currentFG != nullptr)
            state.currentFG->EvaluateState(state.currentD3D12Device, fgConstants);
        else
            state.currentVkFG->EvaluateState(fgConstants);

        if (!config->IsFGEnabled())
        {
            LOG_TRACE("FG not enabled");
            return true;
        }
        else
        {
            if (!fgOutput->IsActive() && !fgOutput->IsPaused())
            {
                fgOutput->Activate();
            }
            else if (!fgOutput->IsActive() || fgOutput->IsPaused())
            {
                LOG_TRACE("FG not active or paused (A:{}, P:{})", fgOutput->IsActive(), fgOutput->IsPaused());
                return true;
            }
        }

        // Frame data part
        // Nukem's function, licensed under GPLv3
        auto loadCameraMatrix = [&]()
        {
            if (data.orthographicProjection)
                return false;

            float projMatrix[4][4];
            memcpy(projMatrix, (void*) &data.cameraViewToClip, sizeof(projMatrix));

            // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
            // idea why.
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

            // a 0 0 0
            // 0 b 0 0
            // 0 0 c e
            // 0 0 d 0
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

            if (data.depthInverted)
                std::swap(data.cameraNear, data.cameraFar);

            data.cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
            return true;
        };

        static bool dontRecalc = false;

        LOG_TRACE("Camera from SL pre recalc near: {}, far: {}", data.cameraNear, data.cameraFar);

        // UE seems to not be passing the correct cameraViewToClip
        // and we can't use it to calculate cameraNear and cameraFar.
        if (engineType != sl::EngineType::eUnreal && !dontRecalc)
            loadCameraMatrix();

        // Workaround for more games with broken cameraViewToClip
        if (!dontRecalc && (data.cameraNear < 0.0f || data.cameraFar < 0.0f))
            dontRecalc = true;

        infiniteDepth = false;
        if (data.cameraNear != 0.0f && data.cameraFar == 0.0f)
        {
            // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
            // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
            // CameraNear when in use.

            infiniteDepth = true;
            data.cameraFar = data.cameraNear + 1.0f;
        }

        fgOutput->SetCameraValues(data.cameraNear, data.cameraFar, data.cameraFOV, data.cameraAspectRatio, 0.0f);

        fgOutput->SetJitter(data.jitterOffset.x, data.jitterOffset.y);

        // Streamline is not 100% clear on if we should multiply by resolution or not.
        // But UE games and Dead Rising expect that multiplication to be done, even if the scale is 1.0.
        // bool multiplyByResolution = dataCopy.mvecScale.x != 1.f || dataCopy.mvecScale.y != 1.f;
        bool multiplyByResolution = true;
        if (multiplyByResolution)
            fgOutput->SetMVScale(data.mvecScale.x * mvsWidth, data.mvecScale.y * mvsHeight);
        else
            fgOutput->SetMVScale(data.mvecScale.x, data.mvecScale.y);

        fgOutput->SetCameraData(reinterpret_cast<float*>(&data.cameraPos), reinterpret_cast<float*>(&data.cameraUp),
                                reinterpret_cast<float*>(&data.cameraRight), reinterpret_cast<float*>(&data.cameraFwd));

        fgOutput->SetReset(data.reset == sl::Boolean::eTrue);

        fgOutput->SetFrameTimeDelta(static_cast<float>(State::Instance().lastFGFrameTime));
    }
    else
    {
        LOG_ERROR("Wrong constant struct version");
    }

    return dataFound;
}

bool Sl_Inputs::evaluateState()
{
    auto fgOutput = State::Instance().CurrentFG();

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
        LOG_WARN("Many frame count repeats in a row, stopping FG");
        State::Instance().fgChanged = true;
        repeatsInRow = 0;
        return false;
    }

    return true;
}

static std::optional<FG_ResourceType> GetResourceType(sl::BufferType type)
{
    if (type == sl::kBufferTypeDepth || type == sl::kBufferTypeHiResDepth || type == sl::kBufferTypeLinearDepth)
        return FG_ResourceType::Depth;

    if (type == sl::kBufferTypeMotionVectors)
        return FG_ResourceType::Velocity;

    if (type == sl::kBufferTypeHUDLessColor)
        return FG_ResourceType::HudlessColor;

    if (type == sl::kBufferTypeUIColorAndAlpha)
        return FG_ResourceType::UIColor;

    return std::nullopt;
}

bool Sl_Inputs::reportResource(const sl::ResourceTag& tag, void* cmdBuffer, uint32_t frameId)
{
    auto& state = State::Instance();
    state.dlssgLastFrame = state.fgLastFrame;

    auto fgOutput = state.CurrentFG();
    auto config = Config::Instance();

    // It's possible for only some resources to be marked ready if FGEnabled is enabled during resource tagging
    if (fgOutput == nullptr || !config->IsFGEnabled())
        return false;

    static const bool ignoreValidUntilEvaluateForFG =
        State::Instance().gameQuirks[GameQuirk::IgnoreValidUntilEvaluateForFG];

    // eValidUntilEvaluate is usually used for upscaling, not FG
    // we could track multiple versions of the same resource for the same frame id
    // to be able to compare validity but that seems pointless
    if (tag.lifecycle == sl::eValidUntilEvaluate && ignoreValidUntilEvaluateForFG)
        return false;

    LOG_DEBUG("Reporting SL resource type: {} lifecycle: {} frameId: {}", tag.type,
              magic_enum::enum_name(tag.lifecycle), frameId);

    CheckForFrame(fgOutput, frameId);

    if (tag.resource->native == nullptr)
    {
        LOG_TRACE("tag.resource->native is null");
        return false;
    }

    if (!cmdBuffer && tag.lifecycle == sl::eOnlyValidNow)
        LOG_TRACE("cmdBuffer is null");

    auto type = GetResourceType(tag.type);

    if (!type.has_value())
        return false;

    auto validity =
        (tag.lifecycle == sl::eValidUntilPresent) ? FG_ResourceValidity::UntilPresent : FG_ResourceValidity::ValidNow;

    // If eValidUntilEvaluate is provided without cmdList when there's not much we can do
    if (!cmdBuffer && validity != FG_ResourceValidity::UntilPresent)
    {
        LOG_WARN("YOLOing resource validity due to missing cmdList");
        validity = FG_ResourceValidity::UntilPresent;
    }

    if ((type == FG_ResourceType::Depth && config->FGDepthValidNow.value_or_default()) ||
        (type == FG_ResourceType::Velocity && config->FGVelocityValidNow.value_or_default()) ||
        (type == FG_ResourceType::HudlessColor && config->FGHudlessValidNow.value_or_default()))
    {
        validity = FG_ResourceValidity::ValidNow;
    }

    int frameIndex = -1;

    if (frameId > 0)
    {
        frameIndex = IndexForFrameId(frameId);

        if (frameIndex < 0)
        {
            LOG_WARN("Frame ID {} not found in tracking, using current index {}", frameId, _currentIndex);
            frameIndex = _currentIndex;
        }
    }
    else
    {
        frameIndex = fgOutput->GetIndexWillBeDispatched();

        if (fgOutput->HasResource(type.value(), frameIndex))
            frameIndex = fgOutput->GetIndex();
    }

    // Vulkan resources come with their size
    const bool isVulkan = state.currentFG == nullptr;
    auto d3dRes = (ID3D12Resource*) tag.resource->native;
    D3D12_RESOURCE_DESC desc {};

    if (!isVulkan)
        desc = d3dRes->GetDesc();

    uint64_t width = tag.extent ? tag.extent.width : (isVulkan ? tag.resource->width : desc.Width);
    uint32_t height = tag.extent ? tag.extent.height : (isVulkan ? tag.resource->height : desc.Height);

    if (type == FG_ResourceType::Velocity)
    {
        // Track locally for dispatch logic
        mvsWidth = width;
        mvsHeight = height;
    }
    else if (type == FG_ResourceType::HudlessColor)
    {
        fgOutput->SetInterpolationRect(width, height);
    }
    else if (type == FG_ResourceType::UIColor)
    {
        // Fallback size logic
        UINT64 rectWidth = 0;
        UINT rectHeight = 0;
        fgOutput->GetInterpolationRect(rectWidth, rectHeight, _currentIndex);

        if (rectWidth == 0)
            fgOutput->SetInterpolationRect(width, height);
    }

    if (isVulkan)
    {
        VkFGResource res {};
        res.type = type.value();
        res.image = (VkImage) tag.resource->native;
        res.format = (VkFormat) tag.resource->nativeFormat;
        res.layout = (VkImageLayout) tag.resource->state;
        res.imageWidth = tag.resource->width;
        res.imageHeight = tag.resource->height;
        res.width = (uint32_t) width;
        res.height = height;
        res.cmdBuffer = (VkCommandBuffer) cmdBuffer;
        res.validity = validity;
        res.frameIndex = frameIndex;

        state.currentVkFG->SetResource(&res);
    }
    else
    {
        Dx12Resource res {};
        res.type = type.value();
        res.resource = d3dRes;
        res.cmdList = (ID3D12GraphicsCommandList*) cmdBuffer; // Critical for eOnlyValidNow
        res.width = width;
        res.height = height;
        res.state = (D3D12_RESOURCE_STATES) tag.resource->state;
        res.validity = validity;
        res.frameIndex = frameIndex;

        state.currentFG->SetResource(&res);
    }

    return true;
}

bool Sl_Inputs::dispatchFG()
{
    LOG_FUNC();
    return true;
}

void Sl_Inputs::markPresent(uint64_t frameId)
{
    std::scoped_lock lock(_frameBoundaryMutex);
    LOG_TRACE("frameId: {}", frameId);
    _isFrameFinished = true;
    _lastPresentFrameId = static_cast<uint32_t>(frameId);

    if (auto fg = State::Instance().CurrentFG(); fg != nullptr)
        fg->SetFrameCount(frameId);
}
