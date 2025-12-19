// Sky_PS.hlsl
#include "Shared.hlsli"

struct PS_IN
{
    float4 SvPos : SV_POSITION;
    float3 Dir : TEXCOORD0;
};

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

// 스카이(배경)에서만 쓰는 파라미터
static const float SKY_MDR_SCALE = 8.0f; // 네가 "괜찮다" 했던 값대. 0.5~1.0에서 튜닝
static const float SKY_EXPOSURE = 1.0f; // 필요하면 0.5~2.0

float4 main(PS_IN i) : SV_Target
{
    float3 dir = normalize(i.Dir);

    
    float4 s = SkyTex.SampleLevel(SkySamp, dir, 0);
    
    float3 hdr = DecodeEnvMDR(s, SKY_MDR_SCALE);
    
    hdr *= SKY_EXPOSURE;
    float3 ldr = Tonemap_Reinhard(hdr);

#if SWAPCHAIN_SRGB
    
    return float4(ldr, 1);
#else
    // RTV가 linear면 sRGB로 인코드
    return float4(LinearToSRGB_Approx(ldr), 1);
#endif
}
