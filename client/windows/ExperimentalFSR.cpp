#include "ExperimentalFSR.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#define PLATFORM_WINDOWS 1
#include "fsr-sdk/ffx_api_loader.h"
#include "fsr-sdk/dx12/ffx_api_dx12.h"
#include "fsr-sdk/ffx_upscale.h"
using Microsoft::WRL::ComPtr;
namespace {
void dxcheck(HRESULT hr,const char *name) {
    if(FAILED(hr)) { std::ostringstream s; s<<name<<" (0x"<<std::hex<<static_cast<uint32_t>(hr)<<')'; throw std::runtime_error(s.str()); }
}
void fxcheck(ffxReturnCode_t rc,const char *name) {
    if(rc!=FFX_API_RETURN_OK) throw std::runtime_error(std::string(name)+" (FSR code "+std::to_string(rc)+')');
}
const std::array<float,256> &linearValues() {
    static const auto table=[] { std::array<float,256> t{};
        for(int i=0;i<256;++i) { float s=i/255.0f; t[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f); } return t; }();
    return table;
}
uint8_t encode(float value) {
    if(!std::isfinite(value)) throw std::runtime_error("FSR returned a non-finite pixel");
    static const auto table=[] { std::array<uint8_t,65536> t{};
        for(int i=0;i<65536;++i) { float l=i/65535.0f,s=l<=.0031308f?12.92f*l:1.055f*std::pow(l,1.0f/2.4f)-.055f;
            t[i]=static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(s*255)),0,255)); } return t; }();
    return table[static_cast<size_t>(std::lround(std::clamp(value,0.0f,1.0f)*65535))];
}
struct Candidate { uint64_t id; std::string name; };
int providerRank(const std::string &name) {
    size_t p=name.find_first_of("0123456789"); if(p==std::string::npos) return 0;
    return name[p]=='4'?4:name[p]=='3'?3:name.compare(p,3,"2.3")==0?2:0;
}
struct Texture {
    ComPtr<ID3D12Resource> resource,upload;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t bytes=0;
};
}
struct ExperimentalFSR::Impl {
    HMODULE module=nullptr;
    ffxFunctions api{}; ffxContext effect=nullptr;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event=nullptr; uint64_t serial=0;
    std::vector<Candidate> candidates;
    size_t candidate=0;
    Texture color,depth,motion,exposure,output;
    ComPtr<ID3D12Resource> readback;
    int inputW=0,inputH=0,outputW=0,outputH=0;
    bool uploaded=false;
    ~Impl() {
        if(effect&&api.DestroyContext) api.DestroyContext(&effect,nullptr);
        if(event) CloseHandle(event);
        if(module) FreeLibrary(module);
    }
    void init(int maximumVersion) {
        wchar_t path[32768]{}; DWORD length=GetModuleFileNameW(nullptr,path,32768);
        if(!length||length>=32768) throw std::runtime_error("Cannot resolve FSR runtime directory");
        auto dll=std::filesystem::path(path).parent_path()/L"amd_fidelityfx_loader_dx12.dll";
        module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module) throw std::runtime_error("AMD FSR runtime DLLs are unavailable");
        ffxLoadFunctions(&api,module);
        if(!api.CreateContext||!api.DestroyContext||!api.Query||!api.Dispatch) throw std::runtime_error("Incomplete FSR loader exports");
        ComPtr<IDXGIFactory4> factory; dxcheck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create DX12 factory");
        for(UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter; if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc); if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) break;
        }
        if(!device) {
            ComPtr<IDXGIAdapter> warp; dxcheck(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"Get DX12 WARP adapter");
            dxcheck(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create DX12 device");
        }
        uint64_t count=0; ffxQueryDescGetVersions q{};
        q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS; q.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        q.device=device.Get(); q.outputCount=&count; fxcheck(api.Query(nullptr,&q.header),"Enumerate FSR providers");
        if(!count||count>64) throw std::runtime_error("No supported FSR 4/3/2 provider");
        std::vector<uint64_t> ids(static_cast<size_t>(count)); std::vector<const char *> names(static_cast<size_t>(count));
        q.versionIds=ids.data(); q.versionNames=names.data(); fxcheck(api.Query(nullptr,&q.header),"Read FSR providers");
        for(size_t i=0;i<count;++i) if(names[i]) {
            std::string name=names[i];
            if(providerRank(name)&&providerRank(name)<=maximumVersion) candidates.push_back({ids[i],name});
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate &a,const Candidate &b){return providerRank(a.name)>providerRank(b.name);});
        if(candidates.empty()) throw std::runtime_error("No supported FSR 4/3/2.3 provider");
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        dxcheck(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Create FSR queue");
        dxcheck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create FSR allocator");
        dxcheck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Create FSR command list");
        dxcheck(list->Close(),"Close initial FSR command list");
        dxcheck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create FSR fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!event) throw std::runtime_error("Create FSR fence event");
    }
    ComPtr<ID3D12Resource> buffer(uint64_t bytes,D3D12_HEAP_TYPE heap) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type=heap;
        D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes;
        d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;
        dxcheck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,heap==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&result)),"Allocate FSR transfer buffer"); return result;
    }
    Texture texture(int w,int h,DXGI_FORMAT format,bool target=false) {
        Texture t; D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h;
        d.DepthOrArraySize=1; d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1;
        d.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        dxcheck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,target?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&t.resource)),"Allocate FSR texture");
        device->GetCopyableFootprints(&d,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);
        if(!target) t.upload=buffer(t.bytes,D3D12_HEAP_TYPE_UPLOAD);
        return t;
    }
    void barrier(ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=r; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore=before; b.Transition.StateAfter=after; list->ResourceBarrier(1,&b);
    }
    void copy(Texture &t) {
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource=t.resource.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource=t.upload.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint=t.footprint;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(t.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    void neutral(Texture &t,float value) {
        void *data=nullptr; D3D12_RANGE range{0,0}; dxcheck(t.upload->Map(0,&range,&data),"Map auxiliary FSR input");
        std::memset(data,0,static_cast<size_t>(t.bytes));
        if(value) for(UINT y=0;y<t.footprint.Footprint.Height;++y) {
            float *row=reinterpret_cast<float *>(static_cast<uint8_t *>(data)+t.footprint.Offset+static_cast<size_t>(y)*t.footprint.Footprint.RowPitch);
            for(UINT x=0;x<t.footprint.Footprint.Width;++x) row[x]=value;
        }
        t.upload->Unmap(0,nullptr);
    }
    void clearContext() {
        if(effect) { api.DestroyContext(&effect,nullptr); effect=nullptr; }
        color={}; depth={}; motion={}; exposure={}; output={}; readback.Reset();
        inputW=inputH=outputW=outputH=0; uploaded=false;
    }
    void configure(int iw,int ih,int ow,int oh,std::string &provider) {
        if(effect&&inputW==iw&&inputH==ih&&outputW==ow&&outputH==oh) return;
        clearContext();
        ffxCreateBackendDX12Desc backend{}; backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device=device.Get();
        ffxCreateContextDescUpscaleVersion version{}; version.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION,&backend.header}; version.version=FFX_UPSCALER_VERSION;
        ffxOverrideVersion force{}; force.header={FFX_API_DESC_TYPE_OVERRIDE_VERSION,&version.header}; force.versionId=candidates[candidate].id;
        ffxCreateContextDescUpscale create{}; create.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,&force.header};
        create.maxRenderSize={static_cast<uint32_t>(iw),static_cast<uint32_t>(ih)};
        create.maxUpscaleSize={static_cast<uint32_t>(ow),static_cast<uint32_t>(oh)};
        fxcheck(api.CreateContext(&effect,&create.header,nullptr),"Create FSR context");
        ffxQueryGetProviderVersion actual{}; actual.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        fxcheck(api.Query(&effect,&actual.header),"Read actual FSR provider");
        provider=actual.versionName?actual.versionName:candidates[candidate].name;
        color=texture(iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT);
        depth=texture(iw,ih,DXGI_FORMAT_R32_FLOAT); motion=texture(iw,ih,DXGI_FORMAT_R32G32_FLOAT); exposure=texture(1,1,DXGI_FORMAT_R32_FLOAT);
        output=texture(ow,oh,DXGI_FORMAT_R32G32B32A32_FLOAT,true); readback=buffer(output.bytes,D3D12_HEAP_TYPE_READBACK);
        neutral(depth,.5f); neutral(motion,0); neutral(exposure,1);
        inputW=iw; inputH=ih; outputW=ow; outputH=oh;
    }
    void dispatch(const VideoFrame &input,VideoFrame &result) {
        D3D12_RANGE noRead{0,0}; void *mapped=nullptr;
        dxcheck(color.upload->Map(0,&noRead,&mapped),"Map FSR color input"); const auto &lut=linearValues();
        for(int y=0;y<inputH;++y) {
            float *row=reinterpret_cast<float *>(static_cast<uint8_t *>(mapped)+color.footprint.Offset+static_cast<size_t>(y)*color.footprint.Footprint.RowPitch);
            const uint8_t *source=input.bgra.data()+static_cast<size_t>(y)*inputW*4;
            for(int x=0;x<inputW;++x) { row[x*4]=lut[source[x*4+2]]; row[x*4+1]=lut[source[x*4+1]]; row[x*4+2]=lut[source[x*4]]; row[x*4+3]=1; }
        }
        color.upload->Unmap(0,nullptr);
        dxcheck(allocator->Reset(),"Reset FSR allocator"); dxcheck(list->Reset(allocator.Get(),nullptr),"Reset FSR commands");
        if(uploaded) {
            barrier(color.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            barrier(output.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else { copy(depth); copy(motion); copy(exposure); }
        copy(color);
        ffxDispatchDescUpscale desc{}; desc.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE; desc.commandList=list.Get();
        desc.color=ffxApiGetResourceDX12(color.resource.Get()); desc.depth=ffxApiGetResourceDX12(depth.resource.Get());
        desc.motionVectors=ffxApiGetResourceDX12(motion.resource.Get()); desc.exposure=ffxApiGetResourceDX12(exposure.resource.Get());
        desc.output=ffxApiGetResourceDX12(output.resource.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        desc.motionVectorScale={1,1}; desc.renderSize={static_cast<uint32_t>(inputW),static_cast<uint32_t>(inputH)};
        desc.upscaleSize={static_cast<uint32_t>(outputW),static_cast<uint32_t>(outputH)};
        desc.frameTimeDelta=1000.0f/60; desc.preExposure=1; desc.reset=true;
        desc.cameraNear=.1f; desc.cameraFar=1000; desc.cameraFovAngleVertical=1.04719755f; desc.viewSpaceToMetersFactor=1;
        fxcheck(api.Dispatch(&effect,&desc.header),"Dispatch single-frame FSR");
        barrier(output.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource=readback.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=output.footprint;
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource=output.resource.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr); dxcheck(list->Close(),"Close FSR commands");
        ID3D12CommandList *commands[]={list.Get()}; queue->ExecuteCommandLists(1,commands);
        dxcheck(queue->Signal(fence.Get(),++serial),"Signal FSR fence");
        if(fence->GetCompletedValue()<serial) {
            dxcheck(fence->SetEventOnCompletion(serial,event),"Wait for FSR completion");
            if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0) throw std::runtime_error("FSR GPU completion timed out");
        }
        dxcheck(device->GetDeviceRemovedReason(),"FSR device status"); uploaded=true;
        D3D12_RANGE readRange{0,static_cast<SIZE_T>(output.bytes)};
        dxcheck(readback->Map(0,&readRange,&mapped),"Read FSR output");
        result.width=outputW; result.height=outputH; result.bgra.resize(static_cast<size_t>(outputW)*outputH*4);
        for(int y=0;y<outputH;++y) {
            const float *row=reinterpret_cast<const float *>(static_cast<const uint8_t *>(mapped)+output.footprint.Offset+static_cast<size_t>(y)*output.footprint.Footprint.RowPitch);
            uint8_t *target=result.bgra.data()+static_cast<size_t>(y)*outputW*4;
            for(int x=0;x<outputW;++x) { target[x*4]=encode(row[x*4+2]); target[x*4+1]=encode(row[x*4+1]); target[x*4+2]=encode(row[x*4]); target[x*4+3]=255; }
        }
        readback->Unmap(0,&noRead);
    }
};
ExperimentalFSR::ExperimentalFSR()=default;
ExperimentalFSR::~ExperimentalFSR()=default;
void ExperimentalFSR::reset() { impl_.reset(); failed_=false; error_.clear(); provider_.clear(); }
void ExperimentalFSR::setMaximumVersion(int major) {
    major=std::clamp(major,2,4); if(major!=maximumVersion_) { reset(); maximumVersion_=major; }
}
bool ExperimentalFSR::probe(std::vector<std::string> &versions) {
    if(failed_) return false;
    try {
        if(!impl_) { impl_=std::make_unique<Impl>(); impl_->init(maximumVersion_); }
        versions.clear(); for(auto &c:impl_->candidates) versions.push_back(c.name); return true;
    } catch(const std::exception &e) { error_=e.what(); failed_=true; return false; }
}
bool ExperimentalFSR::process(const VideoFrame &input,int width,int height,VideoFrame &output) {
    if(failed_) return false;
    if(input.width<32||input.height<32||width<input.width||height<input.height||width>8192||height>8192||
       static_cast<uint64_t>(width)*height>8388608||input.bgra.size()<static_cast<size_t>(input.width)*input.height*4) {
        error_="Experimental FSR dimensions unsupported"; return false;
    }
    std::vector<std::string> versions;
    if(!impl_&&!probe(versions)) return false;
    while(impl_->candidate<impl_->candidates.size()) {
        try {
            impl_->configure(input.width,input.height,width,height,provider_); impl_->dispatch(input,output);
            error_.clear(); return true;
        } catch(const std::exception &e) {
            error_=impl_->candidates[impl_->candidate].name+": "+e.what();
            if(FAILED(impl_->device->GetDeviceRemovedReason())) { failed_=true; return false; }
            impl_->clearContext(); ++impl_->candidate;
        }
    }
    failed_=true; provider_.clear(); return false;
}
