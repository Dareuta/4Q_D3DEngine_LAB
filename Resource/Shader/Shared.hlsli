#ifndef SHARED_HLSLI_INCLUDED
#define SHARED_HLSLI_INCLUDED

// ============================================================================
// Material (b5)
// ============================================================================

cbuffer MAT : register(b5)
{
    float4 matBaseColor;
    uint matUseBaseColor;
    uint3 _matPad5;
}

// ============================================================================
// Per-frame / Per-draw Common (b0)
// ============================================================================

cbuffer CB0 : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldInvTranspose;

    float4 vLightDir;
    float4 vLightColor;
}

// ============================================================================
// Blinn-Phong Legacy Params (b1)
// ============================================================================

cbuffer BP : register(b1)
{
    float4 EyePosW;
    float4 kA;
    float4 kSAlpha; // x=ks, y=shininess (일부 셰이더에서는 alphaCut로 쓰기도 함)
    float4 I_ambient;
}

// ============================================================================
// Texture Usage Flags (b2)
// ============================================================================

cbuffer USE : register(b2)
{
    uint useDiffuse;
    uint useNormal;
    uint useSpecular;
    uint useEmissive;
    uint useOpacity;

    float alphaCut;
    float2 _pad;
}

// ============================================================================
// Texture / Sampler Bindings
// ============================================================================

Texture2D txDiffuse : register(t0);
Texture2D txNormal : register(t1);
Texture2D txSpecular : register(t2);
Texture2D txEmissive : register(t3);
Texture2D txOpacity : register(t4);

SamplerState samLinear : register(s0);

// ============================================================================
// Shadow Map (t5 / s1 / b6)
// ============================================================================

cbuffer ShadowCB : register(b6)
{
    float4x4 gLightViewProj;
    float4 gShadowParams; // x=CmpBias, y=1/ShadowW, z=1/ShadowH, w=unused
}

Texture2D<float> txShadow : register(t5);
SamplerComparisonState samShadow : register(s1);

// ============================================================================
// Toon Shading (t6 / b7 / s2)
// ============================================================================

Texture2D txRamp : register(t6);

cbuffer ToonCB : register(b7)
{
    uint gUseToon;
    uint gToonHalfLambert;
    float gToonSpecStep;
    float gToonSpecBoost;
    float gToonShadowMin;
    float3 _padTo16;
}

SamplerState samRampPointClamp : register(s2)
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

// ============================================================================
// IBL (t7, t8, t9 / s3)
// ============================================================================

TextureCube txIrr : register(t7);
TextureCube txPref : register(t8);
Texture2D txBRDF : register(t9);

SamplerState samClampLinear : register(s3);

// ============================================================================
// Shared VS/PS Interfaces
// ============================================================================

struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Tang : TANGENT;

#if defined(SKINNED)
    uint4  BlendIndices : BLENDINDICES;
    float4 BlendWeights : BLENDWEIGHT;
#endif
};

struct PS_INPUT
{
    float4 PosH : SV_Position;
    float3 WorldPos : TEXCOORD0;
    float2 Tex : TEXCOORD1;
    float4 TangentW : TEXCOORD2; // xyz=tangent, w=handedness(sign)
    float3 NormalW : TEXCOORD3;
};

// ============================================================================
// Helpers
// ============================================================================

inline float3 OrthonormalizeTangent(float3 N, float3 T)
{
    T = normalize(T - N * dot(T, N));
    float3 B = normalize(cross(N, T));
    T = normalize(cross(B, N));
    return T;
}

inline float3 ApplyNormalMapTS(float3 Nw, float3 Tw, float sign, float2 uv, int flipGreen)
{
    float3 Bw = normalize(cross(Nw, Tw)) * sign;
    Tw = normalize(cross(Bw, Nw));

    float3x3 TBN = float3x3(Tw, Bw, Nw);

    float3 nTS = txNormal.Sample(samLinear, uv).xyz * 2.0f - 1.0f;
    if (flipGreen)
        nTS.g = -nTS.g;

    return normalize(mul(nTS, TBN));
}

inline void AlphaClip(float2 uv)
{
    if (useOpacity != 0)
    {
        float a = txOpacity.Sample(samLinear, uv).r;
        clip(a - alphaCut);
    }
}

float SampleShadow_PCF(float3 worldPos, float3 Nw)
{
    float4 lp = mul(float4(worldPos, 1.0f), gLightViewProj);

    if (lp.w <= 0.0f)
        return 1.0f;

    float3 ndc = lp.xyz / lp.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float z = ndc.z;

    if (any(uv < 0.0f) || any(uv > 1.0f) || z < 0.0f || z > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(Nw, normalize(-vLightDir.xyz)));
    float bias = max(0.0005f, gShadowParams.x * (1.0f - ndotl));

    float2 texel = gShadowParams.yz;
    float acc = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            acc += txShadow.SampleCmpLevelZero(
                samShadow,
                uv + float2(dx, dy) * texel,
                z - bias
            );
        }
    }

    return acc / 9.0f;
}

// ============================================================================
// Skinning Bone Palette (b4)
// ============================================================================

static const uint kMaxBones = 256;

#if defined(SKINNED)
cbuffer Bones : register(b4)
{
    float4x4 BonePalette[kMaxBones];
}
#endif

#endif // SHARED_HLSLI_INCLUDED
