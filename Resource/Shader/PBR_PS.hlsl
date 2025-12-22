#include "Shared.hlsli"

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

// ============================================================================
// PBR Params (PS b8)
// - 토글/강제값/IBL 정보 전달용
// ============================================================================
cbuffer PBRParams : register(b8)
{
    uint pUseBaseColorTex;
    uint pUseNormalTex;
    uint pUseMetallicTex;
    uint pUseRoughnessTex;

    float4 pBaseColor; // rgb: baseColor override
    float4 pParams; // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)

    float4 pEnvDiff; //  rgb=color, w=intensity
    float4 pEnvSpec; //  rgb=color, w=intensity

    float4 pEnvInfo; // x=prefilterMaxMip
}

// ============================================================================
// PBR BRDF Helpers (GGX / Schlick / Smith)
// ============================================================================
static const float PI = 3.14159265f;

// GGX / Trowbridge-Reitz NDF
// a = alpha (일반적으로 alpha = roughness^2)
// D = a^2 / (pi * ((N.H)^2*(a^2-1)+1)^2)
float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d + 1e-7f);
}

// Schlick-GGX geometry term (single direction)
float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0f - k) + k + 1e-7f);
}

// Smith geometry term using UE4-style k mapping
float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

// Fresnel (Schlick)
float3 F_Schlick(float3 F0, float VdotH)
{
    float f = pow(1.0f - VdotH, 5.0f);
    return F0 + (1.0f - F0) * f;
}

// Fresnel Schlick with roughness compensation (IBL에서 자주 쓰는 형태)
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0
        + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0)
        * pow(1.0f - cosTheta, 5.0f);
}

// ============================================================================
// Main PS
// ============================================================================
float4 main(PS_INPUT input) : SV_Target
{
    // ------------------------------------------------------------------------
    // Alpha / Cutout / Transparent 정책 (기존 규칙 유지)
    // ------------------------------------------------------------------------
    float a = 1.0f;

    if (useOpacity != 0)
    {
        a = txOpacity.Sample(samLinear, input.Tex).a;

        // alphaCut >= 0 : 컷아웃(통과 픽셀은 불투명 취급)
        if (alphaCut >= 0.0f)
        {
            clip(a - alphaCut);
            a = 1.0f;
        }
        else
        {
            // 투명 패스: a 그대로 사용, 거의 0이면 컷
            if (a <= 1e-3f)
                clip(-1);
        }
    }

    // ------------------------------------------------------------------------
    // World-space basis (Normal/Tangent) + Normal Map 적용
    // ------------------------------------------------------------------------
    float3 Nw_base = normalize(input.NormalW);
    float3 Tw = OrthonormalizeTangent(Nw_base, input.TangentW.xyz);

    float normalStrength = clamp(pParams.z, 0.0f, 2.0f);

    float3 Nw = Nw_base;
    if (pUseNormalTex != 0 && useNormal != 0)
    {
        int flipGreen = (pParams.w > 0.5f) ? 1 : 0;
        float3 nTex = ApplyNormalMapTS(Nw_base, Tw, input.TangentW.w, input.Tex, flipGreen);
        Nw = normalize(lerp(Nw_base, nTex, normalStrength));
    }

    // ------------------------------------------------------------------------
    // Lighting vectors (World-space)
    // ------------------------------------------------------------------------
    float3 L = normalize(-vLightDir.xyz);
    float3 V = normalize(EyePosW.xyz - input.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(Nw, L));
    float NdotV = saturate(dot(Nw, V));
    float NdotH = saturate(dot(Nw, H));
    float VdotH = saturate(dot(V, H));

    // ------------------------------------------------------------------------
    // BaseColor (텍스처/머티리얼 정책 유지)
    // ------------------------------------------------------------------------
    float3 baseColFromMat = matBaseColor.rgb;

    float3 baseColor = pBaseColor.rgb;
    if (pUseBaseColorTex != 0)
    {
        if (useDiffuse != 0)
        {
            float3 texCol = txDiffuse.Sample(samLinear, input.Tex).rgb;
            float3 mulCol = (matUseBaseColor != 0) ? baseColFromMat : float3(1, 1, 1);
            baseColor = texCol * mulCol;
        }
        else
        {
            baseColor = baseColFromMat;
        }
    }

    // ------------------------------------------------------------------------
    // Metallic / Roughness (텍스처 채널 정책 유지)
    // ------------------------------------------------------------------------
    float metallic = pParams.x;
    float roughness = pParams.y;

    if (pUseMetallicTex != 0 && useSpecular == 1)
        metallic = txSpecular.Sample(samLinear, input.Tex).r;

    if (pUseRoughnessTex != 0 && useEmissive != 0)
        roughness = txEmissive.Sample(samLinear, input.Tex).r;

    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.04f, 1.0f); // 0에 너무 붙으면 스파이크 심해짐

    // ------------------------------------------------------------------------
    // Direct Lighting (Cook-Torrance)
    // ------------------------------------------------------------------------
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);

    float aRough = roughness * roughness; // alpha
    float D = D_GGX(NdotH, aRough);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(F0, VdotH);

    float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-4f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diff = kD * baseColor / PI;

    float shadow = SampleShadow_PCF(input.WorldPos, Nw);
    float3 direct = (diff + spec) * vLightColor.rgb * NdotL * shadow;

    // ------------------------------------------------------------------------
    // IBL (Image Based Lighting) : diffuse irradiance + spec prefilter + BRDF LUT
    // ------------------------------------------------------------------------
    float ao = 1.0f; // AO 텍스처 붙이면 여기만 교체
    
    float3 R = reflect(-V, Nw);

    float3 envDiff = pEnvDiff.rgb * pEnvDiff.w;
    float3 envSpec = pEnvSpec.rgb * pEnvSpec.w;
    
    // Diffuse IBL (irradiance cube)
    float3 irradiance = txIrr.Sample(samClampLinear, Nw).rgb * envDiff;
        
    // Specular IBL (prefiltered cube, mip = roughness 기반)
    float maxMip = max(pEnvInfo.x, 0.0f);
    float3 prefiltered = txPref.SampleLevel(samClampLinear, R, roughness * maxMip).rgb * envSpec;

    // BRDF LUT (2D)
    float2 brdf = txBRDF.Sample(samClampLinear, float2(NdotV, roughness)).rg;

    // Split-sum approximation
    float3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_amb = (1.0f - F_ibl) * (1.0f - metallic);

    float3 diffIBL = irradiance * baseColor / PI;
    float3 specIBL = prefiltered * (F_ibl * brdf.x + brdf.y);

    float3 ambient = (kD_amb * diffIBL + specIBL) * ao;

    // ------------------------------------------------------------------------
    // Final
    // ------------------------------------------------------------------------
    float3 color = ambient + direct;

#if SWAPCHAIN_SRGB
    // (주의) 여기선 sRGB 변환을 하지 않는 정책(엔진/백버퍼 설정에 의존)
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
