#include "Shared.hlsli"

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

// ============================================================================
// Constants
// ============================================================================

static const float PI = 3.14159265f;

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

    float4 pBaseColor;
    float4 pParams; // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)

    float4 pEnvDiff; // rgb=color, w=intensity
    float4 pEnvSpec; // rgb=color, w=intensity
    float4 pEnvInfo; // x=prefilterMaxMip
}

// ============================================================================
// BRDF Helpers (GGX / Schlick / Smith)
// ============================================================================

float D_GGX(float NdotH, float a)
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

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0
        + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0)
        * pow(1.0f - cosTheta, 5.0f);
}

// ============================================================================
// PS
// ============================================================================

float4 main(PS_INPUT input) : SV_Target
{
    // ------------------------------------------------------------------------
    // Opacity / Cutout
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // World Basis (N/T/B) + Normal Map
    // ------------------------------------------------------------------------
    float3 Nw_base = normalize(input.NormalW);

    float3 T = normalize(input.TangentW.xyz);
    T = normalize(T - Nw_base * dot(T, Nw_base));
    float3 B = normalize(cross(Nw_base, T) * input.TangentW.w);

    float normalStrength = clamp(pParams.z, 0.0f, 2.0f);
    float3 Nw = Nw_base;

    if (pUseNormalTex != 0 && useNormal != 0)
    {
        float3 nts = txNormal.Sample(samLinear, input.Tex).xyz * 2.0f - 1.0f;

        if (pParams.w > 0.5f)
            nts.y = -nts.y;

        nts.xy *= normalStrength;
        nts = normalize(nts);

        Nw = normalize(nts.x * T + nts.y * B + nts.z * Nw_base);
    }

    // ------------------------------------------------------------------------
    // Lighting Vectors
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
    // Metallic / Roughness (채널 정책 유지)
    // ------------------------------------------------------------------------
    float metallic = pParams.x;
    float roughness = pParams.y;

    if (pUseMetallicTex != 0 && useSpecular == 1)
        metallic = txSpecular.Sample(samLinear, input.Tex).r;

    if (pUseRoughnessTex != 0 && useEmissive != 0)
        roughness = txEmissive.Sample(samLinear, input.Tex).r;

    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.04f, 1.0f);

    // ------------------------------------------------------------------------
    // Direct Lighting (Cook-Torrance)
    // ------------------------------------------------------------------------
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);

    float aRough = roughness * roughness;
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
    // IBL (Diffuse Irradiance + Specular Prefilter + BRDF LUT)
    // ------------------------------------------------------------------------
    float ao = 1.0f;

    float3 R = reflect(-V, Nw);

    float3 envDiff = pEnvDiff.rgb * pEnvDiff.w;
    float3 envSpec = pEnvSpec.rgb * pEnvSpec.w;

    float3 irradiance = txIrr.Sample(samClampLinear, Nw).rgb * envDiff;

    float maxMip = max(pEnvInfo.x, 0.0f);
    float3 prefiltered = txPref.SampleLevel(samClampLinear, R, roughness * maxMip).rgb * envSpec;

    float2 brdf = txBRDF.Sample(samClampLinear, float2(NdotV, roughness)).rg;

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
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
