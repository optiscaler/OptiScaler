#include "../OptiScaler/inputs/NgxFeatureRegistry.h"
#include "../OptiScaler/upscalers/NgxOptionalDx12Inputs.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

struct Parameters
{
    std::unordered_map<std::string, void*> resources;
    void Set(const char* key, void* resource) { resources[key] = resource; }
};

int main()
{
    NgxFeatureRegistry registry;
    assert(!registry.Read(100).feature);
    registry.Record(100, NVSDK_NGX_Feature_RayReconstruction);
    registry.Record(200, NVSDK_NGX_Feature_FrameGeneration);
    assert(registry.Read(100).feature == NVSDK_NGX_Feature_RayReconstruction);
    assert(registry.Read(100).frameGenerationCreated);
    registry.Forget(200);
    assert(!registry.Read(100).frameGenerationCreated);
    registry.Forget(100);
    assert(!registry.Read(100).feature);
    registry.Record(100, NVSDK_NGX_Feature_SuperSampling);
    assert(registry.Read(100).feature == NVSDK_NGX_Feature_SuperSampling);
    std::thread writer(
        [&]
        {
            for (unsigned int i = 0; i < 1000; ++i)
            {
                registry.Record(200, NVSDK_NGX_Feature_FrameGeneration);
                registry.Forget(200);
            }
        });
    for (unsigned int i = 0; i < 1000; ++i)
        assert(registry.Read(100).feature == NVSDK_NGX_Feature_SuperSampling);
    writer.join();
    assert(!registry.Read(100).frameGenerationCreated);

    // Foreign-API sentinels must never survive an optional-input handoff.
    int foreignExposure, foreignReactive, dx12Exposure, dx12Reactive;
    for (bool automatic : { false, true })
        for (bool disabled : { false, true })
            for (bool available : { false, true })
            {
                Parameters parameters;
                parameters.Set(NVSDK_NGX_Parameter_ExposureTexture, &foreignExposure);
                parameters.Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &foreignReactive);
                SetOptionalDx12Inputs(
                    &parameters, available ? reinterpret_cast<ID3D12Resource*>(&dx12Exposure) : nullptr,
                    available ? reinterpret_cast<ID3D12Resource*>(&dx12Reactive) : nullptr, automatic, disabled);
                assert(parameters.resources[NVSDK_NGX_Parameter_ExposureTexture] ==
                       (automatic || !available ? nullptr : &dx12Exposure));
                assert(parameters.resources[NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask] ==
                       (disabled || !available ? nullptr : &dx12Reactive));
            }
    std::cout << "NGX handle lifecycle and 8 optional bridge input cases passed\n";
}
