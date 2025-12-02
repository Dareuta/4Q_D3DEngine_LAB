#include "Shared.hlsli"

#ifndef NORMALMAP_FLIP_GREEN
#define NORMALMAP_FLIP_GREEN 0
#endif

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

#ifndef OPACITY_MAP_IS_TRANSPARENCY // 1이면 흰=투명(=뒤집기 필요)
#define OPACITY_MAP_IS_TRANSPARENCY 0
#endif

float4 main(PS_INPUT input) : SV_Target
{
    // ---- 0) 알파 결정 ----
    float a = 1.0f;
    if (useOpacity != 0)
    {
        a = txOpacity.Sample(samLinear, input.Tex).a;

#if OPACITY_MAP_IS_TRANSPARENCY
        a = 1.0f - a; // 흰=투명 맵 뒤집기
#endif

        // 완전 투명 취급(희끗한 샘플 제거)
        const float MIN_ALPHA = 1e-3; // 필요시 1e-2 ~ 5e-3 사이로 조정
        if (a <= MIN_ALPHA)
            clip(-1);

        // 컷아웃 패스(αcut >= 0)일 때만 자르고, 통과 픽셀은 불투명으로
        if (alphaCut >= 0.0f)
        {
            clip(a - alphaCut);
            a = 1.0f;
        }
    }

    // ---- 1) 셰이딩 ----
    //float3 albedo   = (useDiffuse  != 0) ? txDiffuse .Sample(samLinear, input.Tex).rgb : float3(1,1,1);
    float3 texCol = (useDiffuse != 0) ? txDiffuse.Sample(samLinear, input.Tex).rgb : float3(1, 1, 1);
    float3 baseCol = (matUseBaseColor != 0) ? matBaseColor.rgb : float3(1, 1, 1);
    float3 albedo = texCol * baseCol; // 텍스처 없으면 baseCol만 남음

   // 스펙 상태 (0=off, 1=use map, 2=no map)
    uint uSpec = useSpecular;
    bool specOn = (uSpec != 0);

    // 스펙 마스크: 맵 있으면 샘플, 없으면 1.0, 끄면 0.0
    float specMask = (uSpec == 1) ? txSpecular.Sample(samLinear, input.Tex).r :
                   (uSpec == 2) ? 1.0f : 0.0f;
    
    float3 emissive = (useEmissive != 0) ? txEmissive.Sample(samLinear, input.Tex).rgb : float3(0, 0, 0);

    float3 Nw_base = normalize(input.NormalW);
    float3 Tw = OrthonormalizeTangent(Nw_base, input.TangentW.xyz);
    float3 Nw = (useNormal != 0)
        ? ApplyNormalMapTS(Nw_base, Tw, input.TangentW.w, input.Tex, NORMALMAP_FLIP_GREEN)
        : Nw_base;
     
    float3 L = normalize(-vLightDir.xyz);
    float3 V = normalize(EyePosW.xyz - input.WorldPos);
    float3 H = normalize(L + V);

    float ks = kSAlpha.x;
    float shin = max(1.0f, kSAlpha.y);

    float NdotL = dot(Nw, L);
    float3 diff;
    float3 spec;
 
    if (gUseToon != 0)
    {
    // 1) 디퓨즈: N·L → 램프 샘플
                
        float ndl = (gToonHalfLambert != 0) ? (NdotL * 0.5f + 0.5f) // half-lambert
                                    : saturate(NdotL); // 일반 램버트 
        
        ndl = saturate(ndl);

        //float3 ramp = txRamp.Sample(samRampPointClamp, float2(ndl, 0.5f)).rgb;
        //ramp = max(ramp, gToonShadowMin.xxx); // 바닥 띄우기(옵션)
        
        uint w, h;
        txRamp.GetDimensions(w, h); // OK

        float u = (ndl * (float) (w - 1) + 0.5f) / (float) w;
        float3 ramp = txRamp.SampleLevel(samRampPointClamp, float2(u, 0.5f), 0).rgb;
        
        ramp = max(ramp, gToonShadowMin.xxx);

        diff = albedo * ramp; // 램프 기반 디퓨즈

    // 2) 스펙큘러: 단계화 (임계값 넘으면 켜짐)
        if (specOn)
        {
            float s = pow(saturate(dot(Nw, H)), shin);
            s = step(gToonSpecStep, s) * gToonSpecBoost;
            spec = specMask * ks * s;
        }
        else
        {
            spec = 0.0.xxx; // 안씀
        }
    }
    else
    {
        // 기존 블린-폰
        NdotL = saturate(NdotL);
        diff = albedo * NdotL;
        spec = specOn ? (specMask * ks * pow(saturate(dot(Nw, H)), shin)) : 0.0.xxx;
    }
    
    
//     float  NdotL = saturate(dot(Nw, L));
//     float3 diff  = albedo * NdotL;
//     float3 spec = specOn * specMask * ks * pow(saturate(dot(Nw, H)), shin);

    float3 amb = I_ambient.rgb * kA.rgb * albedo;
    
    float shadow = SampleShadow_PCF(input.WorldPos, Nw);
    float3 direct = (diff + spec) * shadow;
    
    float3 color = amb + emissive + vLightColor.rgb * direct;

#if SWAPCHAIN_SRGB // 0 ~ 255, 0 ~ 1(선형)
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
