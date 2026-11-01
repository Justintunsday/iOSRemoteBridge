// iOSRemoteBridge: D3D11 presentation and AMD FSR 1 integration (MIT).
Texture2D<float4> image : register(t0);
SamplerState clampSampler : register(s0);
cbuffer Settings : register(b0) {
    uint4 easu0,easu1,easu2,easu3;
    uint4 rcas;
    uint4 sizes; // Scaled texture dimensions, viewport origin.
};
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex VSMain(uint id:SV_VertexID) {
    Vertex result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
float4 PSCopy(Vertex input):SV_Target { return float4(image.SampleLevel(clampSampler,input.uv,0).rgb,1); }

#if defined(IRB_EASU) || defined(IRB_RCAS)
#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
#ifdef IRB_EASU
#define FSR_EASU_F 1
#endif
#ifdef IRB_RCAS
#define FSR_RCAS_F 1
#define FSR_RCAS_DENOISE 1
#endif
#include "ffx_fsr1.h"
#ifdef IRB_EASU
AF4 FsrEasuRF(AF2 p) { return image.GatherRed(clampSampler,p); }
AF4 FsrEasuGF(AF2 p) { return image.GatherGreen(clampSampler,p); }
AF4 FsrEasuBF(AF2 p) { return image.GatherBlue(clampSampler,p); }
float4 PSEasu(Vertex input):SV_Target {
    float3 color;
    FsrEasuF(color,uint2(input.position.xy),easu0,easu1,easu2,easu3);
    return float4(saturate(color),1);
}
#endif
#ifdef IRB_RCAS
AF4 FsrRcasLoadF(ASU2 p) { return image.Load(int3(clamp(p,int2(0,0),int2(sizes.xy)-1),0)); }
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b) {}
float4 PSRcas(Vertex input):SV_Target {
    float3 color;
    FsrRcasF(color.r,color.g,color.b,uint2(input.position.xy)-sizes.zw,rcas);
    return float4(saturate(color),1);
}
#endif
#endif
