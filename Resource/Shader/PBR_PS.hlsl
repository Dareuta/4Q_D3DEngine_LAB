#include "Shared.hlsli"

#ifndef NORMALMAP_FLIP_GREEN
#define NORMALMAP_FLIP_GREEN 0
#endif

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

// ------------------------------------------------------------
// b8: PBR 토글/강제값 (과제 요구사항용)
// ------------------------------------------------------------
cbuffer PBRParams : register(b8)
{
    uint pUseBaseColorTex; // 1: 텍스처, 0: 강제값
    uint pUseNormalTex; // 1: 텍스처, 0: 버텍스 노말
    uint pUseMetallicTex; // 1: 텍스처, 0: 강제값
    uint pUseRoughnessTex; // 1: 텍스처, 0: 강제값

    float4 pBaseColor; // 강제 baseColor (rgb)
    float4 pParams; // x=metallic, y=roughness, z=normalStrength(0~2), w=unused
}

// ------------------------------------------------------------
// PBR BRDF helpers (GGX / Schlick)
// ------------------------------------------------------------
static const float PI = 3.14159265f;

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
    // UE4 스타일 k
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
// main
// ------------------------------------------------------------
float4 main(PS_INPUT input) : SV_Target
{
    // ----- 알파(기존 규칙 유지) -----
    float a = 1.0f;
    if (useOpacity != 0)
    {
        a = txOpacity.Sample(samLinear, input.Tex).a;

        // alphaCut >= 0 이면 컷아웃으로 처리 (통과 픽셀은 불투명 취급)
        if (alphaCut >= 0.0f)
        {
            clip(a - alphaCut);
            a = 1.0f;
        }
        else
        {
            // 투명 패스라면 a 그대로 사용
            if (a <= 1e-3f)
                clip(-1);
        }
    }

    // ----- 월드 벡터 -----
    float3 Nw_base = normalize(input.NormalW);
    float3 Tw = OrthonormalizeTangent(Nw_base, input.TangentW.xyz);

    float normalStrength = saturate(pParams.z); // 0~1로 쓰고 싶으면 saturate, 2까지 허용하려면 clamp
    // (원하면 0~2까지 허용)
    normalStrength = clamp(pParams.z, 0.0f, 2.0f);

    float3 Nw = Nw_base;
    if (pUseNormalTex != 0 && useNormal != 0)
    {
        float3 nTex = ApplyNormalMapTS(Nw_base, Tw, input.TangentW.w, input.Tex, NORMALMAP_FLIP_GREEN);
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
            // 기존 정책 유지: 텍스처 * 머티리얼 컬러(또는 1)
            float3 mulCol = (matUseBaseColor != 0) ? baseColFromMat : float3(1, 1, 1);
            baseColor = texCol * mulCol;
        }
        else
        {
            // 텍스처가 없으면 머티리얼 컬러 사용
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
    roughness = clamp(roughness, 0.04f, 1.0f); // 0에 너무 붙으면 스파이크 심해짐
    float aRough = roughness * roughness;

    // ----- PBR (Cook-Torrance) -----
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

    // 기존 엔진의 ambient 규칙 유지
    float3 ambient = I_ambient.rgb * kA.rgb * baseColor;

    float3 color = ambient + direct;

#if SWAPCHAIN_SRGB
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
