#include "Shared.hlsli"

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

// ------------------------------------------------------------
// b8: PBR 토글/강제값
// ------------------------------------------------------------
cbuffer PBRParams : register(b8)
{
    uint pUseBaseColorTex;
    uint pUseNormalTex;
    uint pUseMetallicTex;
    uint pUseRoughnessTex;

    float4 pBaseColor; // rgb
    float4 pParams; // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)

    float4 pEnvDiff; // rgb=color, w=intensity
    float4 pEnvSpec; // rgb=color, w=intensity
}

// ------------------------------------------------------------
// PBR BRDF helpers (GGX / Schlick)
// ------------------------------------------------------------
static const float PI = 3.14159265f;

float D_GGX(float NdotH, float a) // a = alpha(=roughness^2)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d + 1e-7f);
}

float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0f - k) + k + 1e-7f);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

float3 F_Schlick(float3 F0, float VdotH)
{
    float f = pow(1.0f - VdotH, 5.0f);
    return F0 + (1.0f - F0) * f;
}

// ------------------------------------------------------------
// IBL Sampling (MDR decode 적용)
// ------------------------------------------------------------

// Pref mips=10이면 0~9 사용
static const float IBL_MAX_MIP = 9.0f;

// MDR 인코딩 스케일(조명용): 여기로 “보기 좋게” 조절하지 말고
// pEnvDiff.w / pEnvSpec.w로 조절하는 게 깔끔함.
static const float IBL_MDR_SCALE = 1.0f;

float3 SampleIrradiance_IBL(float3 N)
{
    float4 s = txIrr.Sample(samClampLinear, N);
    return DecodeEnvMDR(s, IBL_MDR_SCALE); // ★ Shared.hlsli에 있는 디코더 사용
}

float3 SamplePrefilter_IBL(float3 R, float roughness)
{
    float lod = roughness * IBL_MAX_MIP;
    float4 s = txPref.SampleLevel(samClampLinear, R, lod);
    return DecodeEnvMDR(s, IBL_MDR_SCALE);
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
float4 main(PS_INPUT input) : SV_Target
{
    // ----- Alpha 규칙 -----
    float a = 1.0f;
    if (useOpacity != 0)
    {
        a = txOpacity.Sample(samLinear, input.Tex).a;

        if (alphaCut >= 0.0f)
        {
            clip(a - alphaCut);
            a = 1.0f;
        }
        else
        {
            if (a <= 1e-3f)
                clip(-1);
        }
    }

    // ----- World vectors -----
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

    float3 L = normalize(-vLightDir.xyz);
    float3 V = normalize(EyePosW.xyz - input.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(Nw, L));
    float NdotV = saturate(dot(Nw, V));
    float NdotH = saturate(dot(Nw, H));
    float VdotH = saturate(dot(V, H));

    // ----- BaseColor -----
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

    // ----- Metallic / Roughness -----
    float metallic = pParams.x;
    float roughness = pParams.y;

    if (pUseMetallicTex != 0 && useSpecular == 1)
        metallic = txSpecular.Sample(samLinear, input.Tex).r;

    if (pUseRoughnessTex != 0 && useEmissive != 0)
        roughness = txEmissive.Sample(samLinear, input.Tex).r;

    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.04f, 1.0f);

    // alpha(거칠기) = roughness^2 (정석)
    float aRough = roughness * roughness;

    // ----- Cook-Torrance -----
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);

    float D = D_GGX(NdotH, aRough);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(F0, VdotH);

    float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-4f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diff = kD * baseColor / PI;

    float shadow = SampleShadow_PCF(input.WorldPos, Nw);
    float3 direct = (diff + spec) * vLightColor.rgb * NdotL * shadow;

    float ao = 1.0f;

    // ----- IBL (Split-Sum) -----
    float3 R = reflect(-V, Nw);

    // ★ MDR 디코드된 irradiance/prefilter
    float3 irradiance = SampleIrradiance_IBL(Nw);
    float3 prefiltered = SamplePrefilter_IBL(R, roughness);

    // ★ BRDF LUT는 그대로 (디코드 금지)
    float2 brdf = txBRDF.Sample(samClampLinear, float2(NdotV, roughness)).rg;

    // Ambient Fresnel (NdotV 기반)
    float3 F_amb = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
    float3 kD_amb = (1.0f - F_amb) * (1.0f - metallic);

    float3 diffIBL = irradiance * baseColor / PI;
    float3 specIBL = prefiltered * (F0 * brdf.x + brdf.y);

    // 강도/색 조절은 여기서
    diffIBL *= (pEnvDiff.rgb * pEnvDiff.w);
    specIBL *= (pEnvSpec.rgb * pEnvSpec.w);

    float3 ambient = (kD_amb * diffIBL + specIBL) * ao;

    float3 color = ambient + direct;

#if SWAPCHAIN_SRGB
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
