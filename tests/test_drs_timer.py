from pathlib import Path
source = Path('OptiScaler/upscalers/ffx/FFXFeature_Dx12.cpp').read_text()
start = source.index('    // Tick on EVERY evaluation.')
end = source.index('    LOG_DEBUG("FrameTimeDeltaInMsec:', start)
production = source[start:end]
harness = r'''
#include <cmath>
#include <cassert>
#include <cstring>
#include <limits>
#define LOG_DEBUG(...) ((void)0)
constexpr int NVSDK_NGX_Result_Success = 1;
constexpr const char* NVSDK_NGX_Parameter_FrameTimeDeltaInMsec = "NGX.time";
namespace Util { double now = 1000; double MillisecondsNow() { return now; } }
struct Option { bool value_or_default() { return true; } };
struct Config { Option FsrUseFsrInputValues; static Config* Instance() { static Config cfg; return &cfg; } };
struct Input {
    float ngx = 16; bool ngxPresent = true; float fsr = 0; bool fsrPresent = false;
    int Get(const char* key, float* value) {
        bool isFsr = std::strcmp(key, "FSR.frameTimeDelta") == 0;
        if (isFsr ? fsrPresent : ngxPresent) { *value = isFsr ? fsr : ngx; return 1; }
        return 0;
    }
};
struct Result { float frameTimeDelta = 0; bool reset = false; };
struct Timer {
    double _lastEvaluationTimeMs = 0;
    Result Run(Input* InParameters) {
        Result params;
        PRODUCTION_CODE
        return params;
    }
};
int main() {
    Timer timer; Input in;
    timer.Run(&in);
    // Thirty seconds of valid engine timings must not accumulate in fallback.
    for (int i=1;i<=1800;++i) { Util::now = 1000.0 + i * (1000.0/60.0); timer.Run(&in); }
    Util::now += 1000.0/60.0; in.ngxPresent = false;
    auto a = timer.Run(&in); assert(std::abs(a.frameTimeDelta - 1000.0/60.0)<0.01); assert(!a.reset);
    // Invalid supplied timing must also select the clock ticked on every call.
    in.ngxPresent = true; in.ngx = std::numeric_limits<float>::quiet_NaN(); Util::now += 12.5;
    auto b=timer.Run(&in); assert(std::abs(b.frameTimeDelta-12.5)<0.01);
    in.ngx = 0; Util::now += 17.0;
    auto c=timer.Run(&in); assert(std::abs(c.frameTimeDelta-17.0)<0.01);
    // Preserve valid FSR input priority and engine input without overriding them.
    in.fsrPresent=true; in.fsr=8.25f; Util::now += 20.0;
    assert(timer.Run(&in).frameTimeDelta==8.25f);
    in.fsrPresent=false; in.ngx=9.5f; Util::now += 20.0;
    assert(timer.Run(&in).frameTimeDelta==9.5f);
    // A real pause resets temporal integration when fallback is required.
    in.ngxPresent=false; Util::now+=2000;
    auto d=timer.Run(&in); assert(d.reset); assert(std::abs(d.frameTimeDelta-1000.0/60.0)<0.01);
    Timer first; auto e=first.Run(&in); assert(std::isfinite(e.frameTimeDelta)); assert(e.frameTimeDelta>0);
    return 0;
}
'''
Path('tests/drs_timer.cpp').write_text(harness.replace('PRODUCTION_CODE', production))
print('Generated regression harness from the actual production timing block')
