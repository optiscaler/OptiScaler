// Opt-in headless NVIDIA hardware test. Supply an installed nvngx.dll and a directory containing
// your own official nvngx_dlss.dll. No DLL download, game injection, or NR model evaluation.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <nvsdk_ngx.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>
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
    expect(argc==3,"Usage: nr_residual_dlss_smoke <installed nvngx.dll> <official SR DLL directory>");
    HMODULE module=LoadLibraryW(argv[1]); expect(module!=nullptr,"LoadLibrary nvngx.dll");
    using Init=NVSDK_NGX_Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
    auto init=proc<Init>(module,"NVSDK_NGX_D3D12_Init_Ext");
    auto allocate=proc<decltype(&NVSDK_NGX_D3D12_AllocateParameters)>(module,"NVSDK_NGX_D3D12_AllocateParameters");
    auto destroy=proc<decltype(&NVSDK_NGX_D3D12_DestroyParameters)>(module,"NVSDK_NGX_D3D12_DestroyParameters");
    auto create=proc<decltype(&NVSDK_NGX_D3D12_CreateFeature)>(module,"NVSDK_NGX_D3D12_CreateFeature");
    auto evaluate=proc<decltype(&NVSDK_NGX_D3D12_EvaluateFeature)>(module,"NVSDK_NGX_D3D12_EvaluateFeature");
    auto release=proc<decltype(&NVSDK_NGX_D3D12_ReleaseFeature)>(module,"NVSDK_NGX_D3D12_ReleaseFeature");
    ComPtr<IDXGIFactory1> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; ComPtr<ID3D12Device> device;
    for (UINT i=0; factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc {}; adapter->GetDesc1(&desc);
        if (desc.VendorId==0x10de && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))) break;
        adapter.Reset();
    }
    expect(device!=nullptr,"No NVIDIA D3D12 adapter");
    const wchar_t* paths[]={argv[2]}; NVSDK_NGX_FeatureCommonInfo info {};
    info.PathListInfo.Path=paths; info.PathListInfo.Length=1;
    ngx(init(0x24480451,L".",device.Get(),NVSDK_NGX_Version_API,&info));
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
    constexpr UINT w=1920,h=1080,ow=3840,oh=2160;
    auto texture=[&](DXGI_FORMAT format,UINT width,UINT height) {
        D3D12_RESOURCE_DESC d {}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height;
        d.DepthOrArraySize=d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; ComPtr<ID3D12Resource> r;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r))); return r;
    };
    auto carrier=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h), depth=texture(DXGI_FORMAT_R32_FLOAT,w,h),
         motion=texture(DXGI_FORMAT_R16G16_FLOAT,w,h), exposure=texture(DXGI_FORMAT_R32_FLOAT,1,1),
         output=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,ow,oh);
    ComPtr<ID3D12DescriptorHeap> heap; D3D12_DESCRIPTOR_HEAP_DESC hd {};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=4; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    UINT stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    ID3D12Resource* inputs[]={carrier.Get(),depth.Get(),motion.Get(),exposure.Get()};
    for (UINT i=0;i<4;++i) {
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
    for (auto* r:inputs) barrier(commands.Get(),r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    NVSDK_NGX_Parameter *p=nullptr,*p2=nullptr; ngx(allocate(&p)); ngx(allocate(&p2));
    auto setup=[&](NVSDK_NGX_Parameter* param) {
        param->Set(NVSDK_NGX_Parameter_Width,w); param->Set(NVSDK_NGX_Parameter_Height,h);
        param->Set(NVSDK_NGX_Parameter_OutWidth,ow); param->Set(NVSDK_NGX_Parameter_OutHeight,oh);
        param->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u); param->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);
        param->Set(NVSDK_NGX_Parameter_PerfQualityValue,(int)NVSDK_NGX_PerfQuality_Value_MaxPerf);
        param->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,(unsigned)NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
    };
    setup(p); setup(p2); NVSDK_NGX_Handle *feature=nullptr,*other=nullptr;
    ngx(create(commands.Get(),NVSDK_NGX_Feature_SuperSampling,p,&feature)); submit();
    ngx(create(commands.Get(),NVSDK_NGX_Feature_SuperSampling,p2,&other)); submit();
    expect(feature!=other,"DLSS reused a feature handle instead of independent histories");
    p->Set(NVSDK_NGX_Parameter_Color,carrier.Get()); p->Set(NVSDK_NGX_Parameter_Output,output.Get());
    p->Set(NVSDK_NGX_Parameter_Depth,depth.Get()); p->Set(NVSDK_NGX_Parameter_MotionVectors,motion.Get());
    p->Set(NVSDK_NGX_Parameter_ExposureTexture,exposure.Get());
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,w); p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,h);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.0f); p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.0f); p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.0f); p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.0f);
    p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,16.67f); p->Set(NVSDK_NGX_Parameter_Sharpness,0.0f);
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
            p->Set(NVSDK_NGX_Parameter_Reset,frame==0?1u:0u);
            ngx(evaluate(commands.Get(),feature,p,nullptr));
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
        expect(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c),"Non-finite DLSS result");
        if (!test) expect(std::abs(a-0.5f)<0.01f && std::abs(b-0.5f)<0.01f && std::abs(c-0.5f)<0.01f,"Neutral carrier drift > 0.01");
        else expect(a<0.4f && b>0.45f && b<0.55f && c>0.6f,"DLSS lost the signed regions");
        readback->Unmap(0,nullptr);
    }
    ngx(release(feature)); ngx(release(other)); ngx(destroy(p)); ngx(destroy(p2)); CloseHandle(event);
    std::puts("PASS: two independent DLSS features, neutral/signed carrier 1080p -> 4K on NVIDIA hardware");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
