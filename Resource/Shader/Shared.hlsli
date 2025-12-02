#ifndef SHARED_HLSLI_INCLUDED
#define SHARED_HLSLI_INCLUDED


cbuffer MAT : register(b5)
{
    float4 matBaseColor;
    uint matUseBaseColor;
    uint3 _matPad5;
}


// ===== CB0 (b0)
cbuffer CB0 : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldInvTranspose;
    float4 vLightDir;
    float4 vLightColor;
}

// ===== BP (b1)
cbuffer BP : register(b1)
{
    float4 EyePosW;
    float4 kA;
    float4 kSAlpha; // x: ks, y: shininess/alphaCut
    float4 I_ambient;
}

// ===== USE (b2)
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

// ===== Textures / Sampler
Texture2D txDiffuse : register(t0);
Texture2D txNormal : register(t1);
Texture2D txSpecular : register(t2);
Texture2D txEmissive : register(t3);
Texture2D txOpacity : register(t4);
SamplerState samLinear : register(s0);

//--------툰 쉐이딩--------
// === Toon Shading additions ===
Texture2D txRamp : register(t6); // 1D처럼 쓰는 램프 텍스처(가로 0..1)

cbuffer ToonCB : register(b7)
{
    uint gUseToon; // 0: 끔, 1: 켬
    uint gToonHalfLambert; // 0/1 (선택) Half-Lambert
    float gToonSpecStep; // 스펙 단계 임계값(0~1), 예: 0.55
    float gToonSpecBoost; // 스펙 부스트, 예: 1.0~2.0
    float gToonShadowMin; // 램프 최저 밝기 바닥(밴딩/먹먹함 완화), 예: 0.02
    float3 _padTo16; // 16바이트 정렬
};

// ===== VS input (단일 구조체)
struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Tang : TANGENT; // (= TANGENT0)

#if defined(SKINNED)
    uint4  BlendIndices : BLENDINDICES;
    float4 BlendWeights : BLENDWEIGHT;
#endif
};

// ===== PS input
struct PS_INPUT
{
    float4 PosH : SV_Position;
    float3 WorldPos : TEXCOORD0;
    float2 Tex : TEXCOORD1;
    float4 TangentW : TEXCOORD2;
    float3 NormalW : TEXCOORD3;
};

cbuffer ShadowCB : register(b6)
{
    float4x4 gLightViewProj;
    float4 gShadowParams; // x=CmpBias, y=1/ShadowW, z=1/ShadowH, w=unused
}

Texture2D<float> txShadow : register(t5);
SamplerComparisonState samShadow : register(s1);

//툰쉐이더용
SamplerState samRampPointClamp : register(s2)
{
    Filter = MIN_MAG_MIP_POINT; // 또렷하게: POINT
    AddressU = Clamp;
    AddressV = Clamp;
};

// ===== Helpers
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
        return 1.0f; // 라이트 뒤쪽 → 그림자 X

    float3 ndc = lp.xyz / lp.w; // ★ 원근 나눗셈
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f; // ★ Y 뒤집기 포함 매핑   
    float z = ndc.z;
    
    if (any(uv < 0.0f) || any(uv > 1.0f) || z < 0.0f || z > 1.0f)
        return 1.0f;

    // 기울기 기반 바이어스 (너무 크면 분리, 작으면 애크네)
    float ndotl = saturate(dot(Nw, normalize(-vLightDir.xyz)));
    float bias = max(0.0005f, gShadowParams.x * (1.0f - ndotl));

    // 3x3 PCF
    float2 texel = gShadowParams.yz;
    float acc = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            acc += txShadow.SampleCmpLevelZero(samShadow, uv + float2(dx, dy) * texel, z - bias);
        }
    return acc / 9.0f;
}
// ===== Bones (b4) — Shared에만 둔다!
static const uint kMaxBones = 256;
#if defined(SKINNED)
cbuffer Bones : register(b4)
{
    float4x4 BonePalette[kMaxBones];
}
#endif

#endif // SHARED_HLSLI_INCLUDED
