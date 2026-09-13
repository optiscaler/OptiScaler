// Opt-in production private-adapter hardware test; see nr_private_upscaler_smoke.md.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <nvsdk_ngx.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <algorithm>
#include <memory>
#include <string>
#include <cstdlib>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include <xess_d3d12.h>
struct ScopedSkipSpoofingGlobal{};
struct ScopedSkipHeapCapture{};
namespace OptiKeys {constexpr auto FSR_CameraFovVertical="FSR.cameraFovAngleVertical";}
static HMODULE ffxModule=nullptr, xessModule=nullptr;
struct FfxApiProxy {
    static void InitFfxDx12(){}
    static bool IsSRReady(bool){return ffxModule!=nullptr;}
    static auto D3D12_CreateContext(ffxContext* c,ffxCreateContextDescHeader* d,const ffxAllocationCallbacks* a){
        return ((PfnFfxCreateContext)GetProcAddress(ffxModule,"ffxCreateContext"))(c,d,a);}
    static auto D3D12_DestroyContext(ffxContext* c,const ffxAllocationCallbacks* a){
        return ((PfnFfxDestroyContext)GetProcAddress(ffxModule,"ffxDestroyContext"))(c,a);}
    static auto D3D12_Dispatch(ffxContext* c,const ffxDispatchDescHeader* d){
        return ((PfnFfxDispatch)GetProcAddress(ffxModule,"ffxDispatch"))(c,d);}
};
struct XeSSProxy {
    static void InitXeSS(){}
#define FN(alias,name) static auto alias(){return (decltype(&name))GetProcAddress(xessModule,#name);}
    FN(D3D12CreateContext,xessD3D12CreateContext)
    FN(D3D12Init,xessD3D12Init)
    FN(D3D12Execute,xessD3D12Execute)
    FN(DestroyContext,xessDestroyContext)
    FN(SetVelocityScale,xessSetVelocityScale)
    FN(GetOptimalInputResolution,xessGetOptimalInputResolution)
#undef FN
};
struct NVNGXProxy {
    struct ScopedFeatureCreationTrace {};
    static inline HMODULE module = nullptr;
    static inline const wchar_t* srDirectory = nullptr;
    static inline bool initialized = false;
    static bool InitDx12(ID3D12Device* device) {
        if (initialized) return true;
        using Init = NVSDK_NGX_Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
        auto init = (Init)GetProcAddress(module,"NVSDK_NGX_D3D12_Init_Ext");
        const wchar_t* paths[] = { srDirectory };
        NVSDK_NGX_FeatureCommonInfo info {}; info.PathListInfo.Path=paths; info.PathListInfo.Length=1;
        initialized = init && init(useRr ? 0x5F83393 : 0x24480451,L".",device,NVSDK_NGX_Version_API,&info)==NVSDK_NGX_Result_Success;
        return initialized;
    }
#define FN(alias,name) static auto alias(){return (decltype(&name))GetProcAddress(module,#name);}
    FN(D3D12_AllocateParameters,NVSDK_NGX_D3D12_AllocateParameters)
    FN(D3D12_DestroyParameters,NVSDK_NGX_D3D12_DestroyParameters)
    static inline unsigned srCreates = 0;
    static inline bool useRr = false;
    static NVSDK_NGX_Result CreateSr(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Feature id,
                                    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle) {
        if (id != (useRr ? NVSDK_NGX_Feature_RayReconstruction : NVSDK_NGX_Feature_SuperSampling))
            throw std::runtime_error("Private edit upscaler requested the wrong NGX feature");
        // Match coexistence with Cyberpunk's HDR scene RR before creating the LDR residual RR.
        if (useRr && srCreates == 0)
        {
            unsigned flags=0; parameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags);
            parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                             flags | NVSDK_NGX_DLSS_Feature_Flags_IsHDR);
        }
        ++srCreates;
        const auto result = ((decltype(&NVSDK_NGX_D3D12_CreateFeature))GetProcAddress(module,
            "NVSDK_NGX_D3D12_CreateFeature"))(cmd,id,parameters,handle);
        std::printf("Private create result: %08X\n",(unsigned)result);
        return result;
    }
    static auto D3D12_CreateFeature() { return &CreateSr; }
    FN(D3D12_EvaluateFeature,NVSDK_NGX_D3D12_EvaluateFeature)
    FN(D3D12_ReleaseFeature,NVSDK_NGX_D3D12_ReleaseFeature)
#undef FN
};
// Compile the exact production adapter; only runtime loader and application-hook seams above differ.
#include "../OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.cpp"

using Microsoft::WRL::ComPtr;
void check(HRESULT h) { if (FAILED(h)) throw std::runtime_error("D3D12 failure"); }
void ngx(NVSDK_NGX_Result r) { if (r != NVSDK_NGX_Result_Success) {
    std::fprintf(stderr,"NGX result: %08X\n",(unsigned)r); throw std::runtime_error("NGX failure"); } }
void expect(bool b,const char* why) { if (!b) throw std::runtime_error(why); }
template<class T> T proc(HMODULE m,const char* name) { auto p=GetProcAddress(m,name); expect(p!=nullptr,name); return (T)p; }
void barrier(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x {}; x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource=r;
    x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; x.Transition.StateBefore=a; x.Transition.StateAfter=b;
    c->ResourceBarrier(1,&x);
}
int wmain(int argc,wchar_t** argv) try {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    expect(argc>=2,"Usage: private_smoke <0 DLSS|1 FSR2|2 FFX|3 XeSS> [runtime DLL] [DLSS SR directory]");
    const auto selected = DlssNr::GetPrivateUpscaler(_wtoi(argv[1]));
    if(selected==DlssNr::PrivateUpscaler::DLSS) {
        expect(argc==4 || argc==5,"DLSS requires installed nvngx.dll and official SR/RR DLL directory");
        NVNGXProxy::useRr = argc == 5 && std::wstring(argv[4]) == L"--rr";
        NVNGXProxy::module=LoadLibraryW(argv[2]); expect(NVNGXProxy::module!=nullptr,"NGX load");
        NVNGXProxy::srDirectory=argv[3];
    }
    if(selected==DlssNr::PrivateUpscaler::FFX){expect(argc==3,"FFX DLL required");ffxModule=LoadLibraryW(argv[2]);expect(ffxModule!=nullptr,"FFX load");}
    if(selected==DlssNr::PrivateUpscaler::XeSS){expect(argc==3,"XeSS DLL required");xessModule=LoadLibraryW(argv[2]);expect(xessModule!=nullptr,"XeSS load");}
    ComPtr<ID3D12Debug> debug; if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer(); else std::puts("D3D12 debug layer unavailable; checking GPU results and fence completion.");
    ComPtr<IDXGIFactory1> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; ComPtr<ID3D12Device> device;
    for (UINT i=0; factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc {}; adapter->GetDesc1(&desc);
        if ((selected!=DlssNr::PrivateUpscaler::DLSS || desc.VendorId==0x10de) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))) break;
        adapter.Reset();
    }
    expect(device!=nullptr,"No supported D3D12 adapter");
    ComPtr<ID3D12InfoQueue> infoQueue; device.As(&infoQueue);
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC q {};
    check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator; check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands; check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
    ComPtr<ID3D12Fence> fence; check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); expect(event!=nullptr,"Fence event"); UINT64 serial=0;
    auto submit=[&]() {
        check(commands->Close()); ID3D12CommandList* lists[]={commands.Get()}; queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.Get(),++serial)); check(fence->SetEventOnCompletion(serial,event));
        expect(WaitForSingleObject(event,15000)==WAIT_OBJECT_0,"GPU fence timeout");
        check(allocator->Reset()); check(commands->Reset(allocator.Get(),nullptr));
    };
#ifndef NR_SMOKE_SCALE
#define NR_SMOKE_SCALE 50
#endif
    const UINT w=NVNGXProxy::useRr ? 2560 : 3840*NR_SMOKE_SCALE/100,
               h=NVNGXProxy::useRr ? 1440 : 2160*NR_SMOKE_SCALE/100,ow=3840,oh=2160;
    auto texture=[&](DXGI_FORMAT format,UINT width,UINT height) {
        D3D12_RESOURCE_DESC d {}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height;
        d.DepthOrArraySize=d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; ComPtr<ID3D12Resource> r;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r))); return r;
    };
    auto carrier=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h), depth=texture(DXGI_FORMAT_R32_FLOAT,w,h),
         motion=texture(DXGI_FORMAT_R16G16_FLOAT,w,h), exposure=texture(DXGI_FORMAT_R32_FLOAT,1,1),
         output=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,ow,oh),
         albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h),
         specular=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h),
         normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h);
    ComPtr<ID3D12DescriptorHeap> heap; D3D12_DESCRIPTOR_HEAP_DESC hd {};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=7; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    UINT stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    ID3D12Resource* inputs[]={carrier.Get(),depth.Get(),motion.Get(),exposure.Get(),albedo.Get(),specular.Get(),normals.Get()};
    for (UINT i=0;i<7;++i) {
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr+=i*stride;
        device->CreateUnorderedAccessView(inputs[i],nullptr,nullptr,cpu);
    }
    auto clear=[&](UINT i,float value,const D3D12_RECT* rect=nullptr) {
        ID3D12DescriptorHeap* heaps[]={heap.Get()}; commands->SetDescriptorHeaps(1,heaps);
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr+=i*stride;
        auto gpu=heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr+=i*stride;
        float v[]={value,value,value,1}; commands->ClearUnorderedAccessViewFloat(gpu,cpu,inputs[i],v,rect?1:0,rect);
    };
    clear(0,0.5f); clear(1,0.5f); clear(2,0.0f); clear(3,1.0f);
    clear(4,0.5f); clear(5,0.04f);
    {
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr+=6*stride;
        auto gpu=heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr+=6*stride;
        float normal[]={0,0,1,1}; commands->ClearUnorderedAccessViewFloat(gpu,cpu,normals.Get(),normal,0,nullptr);
    }
    // Leave game guides in UAV state; exercise both transition and restoration on every evaluate.
    for (auto* r: {carrier.Get(),exposure.Get()}) barrier(commands.Get(),r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DlssNr::PrivateUpscalerCreateDx12 info { w, h, ow, oh, (int)NVSDK_NGX_PerfQuality_Value_MaxPerf };
    info.rayReconstruction=NVNGXProxy::useRr; info.roughnessMode=1;
    if (selected==DlssNr::PrivateUpscaler::DLSS) info.quality=NVSDK_NGX_PerfQuality_Value_MaxQuality;
    if (NVNGXProxy::useRr) { info.quality=NVSDK_NGX_PerfQuality_Value_MaxQuality; info.depthInverted=true; }
    auto feature=std::make_unique<DlssNr::PrivateUpscalerDx12>(selected);
    auto other=std::make_unique<DlssNr::PrivateUpscalerDx12>(selected);
    expect(feature->Init(device.Get(),commands.Get(),info),"First private backend init failed"); submit();
    expect(other->Init(device.Get(),commands.Get(),info),"Second private backend init failed"); submit();
    DlssNr::PrivateUpscalerFrameDx12 f {};
    f.color.resource=carrier.Get(); f.output.resource=output.Get(); f.exposure.resource=exposure.Get();
    f.depth={depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    f.motion={motion.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    f.width=w; f.height=h; f.outputWidth=ow; f.outputHeight=oh;
    if (NVNGXProxy::useRr)
    {
        NVSDK_NGX_Parameter* source=nullptr; ngx(NVNGXProxy::D3D12_AllocateParameters()(&source));
        source->Set("DLSS.Roughness.Mode",1u); source->Set("DLSS.Use.HW.Depth",1u);
        source->Set("DLSS.Input.DiffuseAlbedo",albedo.Get());
        source->Set("DLSS.Input.SpecularAlbedo",specular.Get());
        source->Set("GBuffer.Normals",normals.Get());
        source->Set("MotionVectorsReflection",motion.Get());
        float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        source->Set("WorldToViewMatrix",(void*)matrix); source->Set("ViewToClipMatrix",(void*)matrix);
        f.rr=DlssNr::PrivateUpscalerDx12::ReadRrInputs(source,w,h);
        expect(f.rr.valid,"Valid packed RR inputs rejected");
        matrix[0]=7; expect(f.rr.worldToView[0]==1,"RR matrix was borrowed rather than snapshotted");
        source->Set("DLSS.Input.Normals.Subrect.Base.X",1u);
        expect(!DlssNr::PrivateUpscalerDx12::ReadRrInputs(source,w,h).valid,"Out-of-bounds RR guide accepted");
        source->Set("DLSS.Input.Normals.Subrect.Base.X",0u);
        source->Set("DLSS.Input.SpecularAlbedo",(ID3D12Resource*)nullptr);
        expect(!DlssNr::PrivateUpscalerDx12::ReadRrInputs(source,w,h).valid,"Missing RR guide accepted");
        ngx(NVNGXProxy::D3D12_DestroyParameters()(source));
        for(auto& guide:f.rr.guides) guide.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; UINT64 bytes=0; auto od=output->GetDesc();
    device->GetCopyableFootprints(&od,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC bd {}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=bytes; bd.Height=1;
    bd.DepthOrArraySize=bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES rh {}; rh.Type=D3D12_HEAP_TYPE_READBACK; ComPtr<ID3D12Resource> readback;
    check(device->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    for (int test=0;test<2;++test) {
        if (test) {
            barrier(commands.Get(),carrier.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_RECT left{0,0,(LONG)w/3,(LONG)h},right{2*(LONG)w/3,0,(LONG)w,(LONG)h};
            clear(0,0.25f,&left); clear(0,0.75f,&right);
            barrier(commands.Get(),carrier.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        for (unsigned frame=0;frame<8;++frame) {
            f.reset=frame==0;
            expect(feature->Evaluate(commands.Get(),f),"Private backend evaluate failed");
            if(frame==0) { // A second live context must leave the first one's history/resources usable.
                D3D12_RESOURCE_BARRIER between {}; between.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
                between.UAV.pResource=output.Get(); commands->ResourceBarrier(1,&between);
                expect(other->Evaluate(commands.Get(),f),"Second private backend evaluate failed");
            }
            D3D12_RESOURCE_BARRIER uav {}; uav.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource=output.Get(); commands->ResourceBarrier(1,&uav);
            submit();
        }
        barrier(commands.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src {},dst {}; src.pResource=output.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=readback.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=footprint;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        barrier(commands.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS); submit();
        unsigned char* mapped=nullptr; check(readback->Map(0,nullptr,(void**)&mapped));
        auto sample=[&](UINT x) { auto* row=(uint16_t*)(mapped+(oh/2)*footprint.Footprint.RowPitch); return DirectX::PackedVector::XMConvertHalfToFloat(row[x*4]); };
        float a=sample(ow/6),b=sample(ow/2),c=sample(5*ow/6);
        std::printf("%s carrier output: %.6f %.6f %.6f\n",test?"signed":"neutral",a,b,c);
        expect(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c),"Non-finite private result");
        if (!test) expect(std::abs(a-0.5f)<0.01f && std::abs(b-0.5f)<0.01f && std::abs(c-0.5f)<0.01f,"Neutral carrier drift > 0.01");
        else expect(a<0.4f && b>0.45f && b<0.55f && c>0.6f,"Private upscaler lost the signed regions");
        readback->Unmap(0,nullptr);
    }
    if (selected == DlssNr::PrivateUpscaler::DLSS)
        expect(NVNGXProxy::srCreates == 2, "Both private contexts must use the selected NGX feature");
    f.depth.resource=nullptr;
    expect(!feature->Evaluate(commands.Get(),f),"Missing guide was accepted");
    feature.reset(); other.reset(); CloseHandle(event);
    unsigned errors=0;
    for(UINT64 i=0;infoQueue && i<infoQueue->GetNumStoredMessages();++i){
        SIZE_T size=0;infoQueue->GetMessage(i,nullptr,&size);std::vector<char> storage(size);
        auto* message=(D3D12_MESSAGE*)storage.data();infoQueue->GetMessage(i,message,&size);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D12: %s\n",message->pDescription);++errors;}
    }
    expect(errors==0,"D3D12 validation errors");
    std::printf("PASS: production %s adapter, two live contexts, neutral/signed %ux%u -> 4K carrier\n",NVNGXProxy::useRr ? "DLSS RR" : DlssNr::PrivateUpscalerName(selected),w,h);
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
