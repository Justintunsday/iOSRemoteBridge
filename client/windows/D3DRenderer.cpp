#include "D3DRenderer.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#define A_CPU 1
#include "shaders/vendor/ffx_a.h"
#include "shaders/vendor/ffx_fsr1.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char *operation) {
    if(FAILED(hr)) { std::ostringstream message; message<<operation<<" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<')'; throw std::runtime_error(message.str()); }
}
struct Resource { const void *data; UINT size; };
Resource resource(int id) {
    HMODULE module=GetModuleHandleW(nullptr);
    HRSRC entry=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!entry) throw std::runtime_error("Embedded FSR shader resource missing");
    HGLOBAL handle=LoadResource(module,entry);
    const void *data=handle?LockResource(handle):nullptr;
    if(!data) throw std::runtime_error("Embedded FSR shader resource cannot be read");
    return {data,SizeofResource(module,entry)};
}
class Includes : public ID3DInclude {
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID *data,UINT *bytes) override {
        try {
            int id=!std::strcmp(name,"ffx_a.h")?5002:!std::strcmp(name,"ffx_fsr1.h")?5003:0;
            if(!id) return E_FAIL;
            auto source=resource(id); *data=source.data; *bytes=source.size; return S_OK;
        } catch(...) { return E_FAIL; }
    }
    HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};
ComPtr<ID3DBlob> compile(const char *entry,const char *profile,const char *define=nullptr) {
    auto source=resource(5001); Includes includes;
    D3D_SHADER_MACRO macros[]={{define,"1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,errors;
    HRESULT hr=D3DCompile(source.data,source.size,"Video.hlsl",define?macros:nullptr,&includes,entry,profile,
                         D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr)) throw std::runtime_error(errors?std::string(static_cast<const char *>(errors->GetBufferPointer()),errors->GetBufferSize()):"Shader compilation failed");
    return code;
}
struct alignas(16) Settings { uint32_t easu[4][4]{},rcas[4]{},sizes[4]{}; };
}
struct D3DRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11Texture2D> back,source,scaled,staging;
    ComPtr<ID3D11RenderTargetView> backRTV,scaledRTV;
    ComPtr<ID3D11ShaderResourceView> sourceSRV,scaledSRV;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> copy,easu,rcas;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    int width=0,height=0,sourceW=0,sourceH=0,scaledW=0,scaledH=0;
    bool tearing=false,activeFSR=false,usingWarp=false;
    void init(HWND window,bool warp) {
        D3D_FEATURE_LEVEL level{},levels[]={D3D_FEATURE_LEVEL_11_0};
        HRESULT hr=D3D11CreateDevice(nullptr,warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context);
        usingWarp=warp;
        if(FAILED(hr)&&!warp) {
            usingWarp=true;
            hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                levels,1,D3D11_SDK_VERSION,&device,&level,&context);
        }
        check(hr,"Create D3D11 device");
        ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi),"Query DXGI device");
        ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"Query adapter");
        ComPtr<IDXGIFactory2> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)),"Query DXGI factory");
        ComPtr<IDXGIFactory5> factory5;
        BOOL supported=FALSE;
        if(SUCCEEDED(factory.As(&factory5))&&SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&supported,sizeof(supported)))) tearing=supported!=FALSE;
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling=DXGI_SCALING_STRETCH; desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        desc.Flags=tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
        check(factory->CreateSwapChainForHwnd(device.Get(),window,&desc,nullptr,nullptr,&swap),"Create presentation swap chain");
        factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
        ComPtr<IDXGIDevice1> latency; if(SUCCEEDED(dxgi.As(&latency))) latency->SetMaximumFrameLatency(1);
        auto vs=compile("VSMain","vs_5_0"),ps=compile("PSCopy","ps_5_0");
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create video vertex shader");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&copy),"Create copy shader");
        ps=compile("PSEasu","ps_5_0","IRB_EASU");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&easu),"Create FSR EASU shader");
        ps=compile("PSRcas","ps_5_0","IRB_RCAS");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&rcas),"Create FSR RCAS shader");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Settings); cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&cb,nullptr,&constants),"Create shader constants");
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd,&sampler),"Create clamp sampler");
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&raster),"Create video rasterizer");
    }
    void resize(int w,int h) {
        if(width==w&&height==h&&backRTV) return;
        context->OMSetRenderTargets(0,nullptr,nullptr); backRTV.Reset(); back.Reset(); staging.Reset();
        check(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0),"Resize video buffers");
        check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Get video buffer");
        check(device->CreateRenderTargetView(back.Get(),nullptr,&backRTV),"Create video target"); width=w; height=h;
    }
    void texture(int w,int h,bool target,ComPtr<ID3D11Texture2D> &tex,ComPtr<ID3D11ShaderResourceView> &srv) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(target?D3D11_BIND_RENDER_TARGET:0);
        srv.Reset(); tex.Reset(); check(device->CreateTexture2D(&d,nullptr,&tex),"Create video texture");
        check(device->CreateShaderResourceView(tex.Get(),nullptr,&srv),"Create video sampling view");
    }
    void draw(ID3D11RenderTargetView *target,ID3D11ShaderResourceView *input,ID3D11PixelShader *shader,int x,int y,int w,int h) {
        D3D11_VIEWPORT vp{static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h),0,1};
        context->RSSetViewports(1,&vp); context->OMSetRenderTargets(1,&target,nullptr);
        context->PSSetShaderResources(0,1,&input); context->PSSetShader(shader,nullptr,0); context->Draw(3,0);
        ID3D11ShaderResourceView *none=nullptr; context->PSSetShaderResources(0,1,&none); context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void readback(VideoFrame &output) {
        if(!staging) {
            D3D11_TEXTURE2D_DESC d{}; back->GetDesc(&d); d.Usage=D3D11_USAGE_STAGING;
            d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
            check(device->CreateTexture2D(&d,nullptr,&staging),"Create diagnostic staging texture");
        }
        context->CopyResource(staging.Get(),back.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read diagnostic pixels");
        output.width=width; output.height=height; output.bgra.resize(static_cast<size_t>(width)*height*4);
        for(int y=0;y<height;++y) std::memcpy(output.bgra.data()+static_cast<size_t>(y)*width*4,
            static_cast<const uint8_t *>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,static_cast<size_t>(width)*4);
        context->Unmap(staging.Get(),0);
    }
};
D3DRenderer::D3DRenderer(bool forceWarp):warp_(forceWarp) {}
D3DRenderer::~D3DRenderer()=default;
void D3DRenderer::reset() { impl_.reset(); failed_=false; error_.clear(); }
void D3DRenderer::setSharpening(int strength) { sharpness_=std::clamp(strength,0,100); }
bool D3DRenderer::fsrActive() const { return impl_&&impl_->activeFSR; }
const char *D3DRenderer::backend() const { return impl_?(impl_->usingWarp?"D3D11 WARP":"D3D11 hardware"):"unavailable"; }
bool D3DRenderer::paint(HWND window,int width,int height,const VideoFrame &frame,RECT &videoRect,VideoFrame *diagnosticOutput) {
    videoRect={}; if(failed_||width<=0||height<=0) return false;
    try {
        if(!impl_) { impl_=std::make_unique<Impl>(); impl_->init(window,warp_); }
        auto &g=*impl_; g.resize(width,height); g.activeFSR=false;
        float black[4]={0,0,0,1}; g.context->ClearRenderTargetView(g.backRTV.Get(),black);
        if(frame.width>0&&frame.height>0&&frame.bgra.size()>=static_cast<size_t>(frame.width)*frame.height*4) {
            if(g.sourceW!=frame.width||g.sourceH!=frame.height) {
                g.texture(frame.width,frame.height,false,g.source,g.sourceSRV); g.sourceW=frame.width; g.sourceH=frame.height;
            }
            g.context->UpdateSubresource(g.source.Get(),0,nullptr,frame.bgra.data(),frame.width*4,0);
            double scale=std::min(static_cast<double>(width)/frame.width,static_cast<double>(height)/frame.height);
            int w=std::max(1,static_cast<int>(std::lround(frame.width*scale))),h=std::max(1,static_cast<int>(std::lround(frame.height*scale)));
            int x=(width-w)/2,y=(height-h)/2; videoRect={x,y,x+w,y+h};
            if(g.scaledW!=w||g.scaledH!=h) {
                g.scaledRTV.Reset(); g.texture(w,h,true,g.scaled,g.scaledSRV);
                check(g.device->CreateRenderTargetView(g.scaled.Get(),nullptr,&g.scaledRTV),"Create upscaling target"); g.scaledW=w; g.scaledH=h;
            }
            Settings data{}; FsrEasuCon(data.easu[0],data.easu[1],data.easu[2],data.easu[3],
                static_cast<float>(frame.width),static_cast<float>(frame.height),static_cast<float>(frame.width),static_cast<float>(frame.height),static_cast<float>(w),static_cast<float>(h));
            FsrRcasCon(data.rcas,2.0f-2.0f*sharpness_/100.0f);
            data.sizes[0]=w; data.sizes[1]=h;
            g.context->IASetInputLayout(nullptr); g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g.context->RSSetState(g.raster.Get()); g.context->VSSetShader(g.vertex.Get(),nullptr,0);
            ID3D11Buffer *cb=g.constants.Get(); g.context->PSSetConstantBuffers(0,1,&cb);
            ID3D11SamplerState *sampler=g.sampler.Get(); g.context->PSSetSamplers(0,1,&sampler);
            g.context->UpdateSubresource(g.constants.Get(),0,nullptr,&data,0,0);
            g.activeFSR=fsr_&&w>frame.width&&h>frame.height;
            g.draw(g.scaledRTV.Get(),g.sourceSRV.Get(),g.activeFSR?g.easu.Get():g.copy.Get(),0,0,w,h);
            data.sizes[2]=x; data.sizes[3]=y; g.context->UpdateSubresource(g.constants.Get(),0,nullptr,&data,0,0);
            g.draw(g.backRTV.Get(),g.scaledSRV.Get(),sharpness_?g.rcas.Get():g.copy.Get(),x,y,w,h);
        }
        if(diagnosticOutput) g.readback(*diagnosticOutput);
        check(g.swap->Present(vsync_?1:0,(!vsync_&&g.tearing)?DXGI_PRESENT_ALLOW_TEARING:0),"Present video");
        return true;
    } catch(const std::exception &error) { error_=error.what(); failed_=true; impl_.reset(); return false; }
}
