#ifndef SHARED_HLSLI_INCLUDED
#define SHARED_HLSLI_INCLUDED

// ============================================================================
// Material (PS b5)
// - 머티리얼 베이스 컬러(텍스처 곱 정책에 사용)
// ============================================================================
cbuffer MAT : register(b5)
{
    float4 matBaseColor;
    uint matUseBaseColor;
    uint3 _matPad5;
}

// ============================================================================
// Per-frame / Per-draw common (b0)
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
// Blinn-Phong legacy params (b1)
// - 일부 패스/셰이더 호환용
// ============================================================================
cbuffer BP : register(b1)
{
    float4 EyePosW;
    float4 kA;
    float4 kSAlpha; // x=ks, y=shininess / (일부 셰이더에서는 alphaCut로 쓰기도 함)
    float4 I_ambient;
}

// ============================================================================
// Texture usage flags (b2)
// - 엔진 쪽에서 텍스처 존재 여부/사용 여부 토글
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
// Texture / Sampler bindings
// ============================================================================
Texture2D txDiffuse : register(t0);
Texture2D txNormal : register(t1);
Texture2D txSpecular : register(t2);
Texture2D txEmissive : register(t3);
Texture2D txOpacity : register(t4);

SamplerState samLinear : register(s0);

// ============================================================================
// Shadow map (t5 / s1 / b6)
// ============================================================================
cbuffer ShadowCB : register(b6)
{
    float4x4 gLightViewProj;
    float4 gShadowParams; // x=CmpBias, y=1/ShadowW, z=1/ShadowH, w=unused
}

Texture2D<float> txShadow : register(t5);
SamplerComparisonState samShadow : register(s1);

// ============================================================================
// Toon shading (t6 / b7 / s2)
// ============================================================================
Texture2D txRamp : register(t6); // 1D처럼 사용(가로 0..1)

cbuffer ToonCB : register(b7)
{
    uint gUseToon; // 0=Off, 1=On
    uint gToonHalfLambert; // 0/1
    float gToonSpecStep; // spec 단계 임계값(0~1)
    float gToonSpecBoost; // spec 부스트(1~2+)
    float gToonShadowMin; // 램프 최저 밝기 바닥
    float3 _padTo16; // 16-byte align
}

// 램프 텍스처는 밴딩을 또렷하게 하려면 POINT가 더 낫다
SamplerState samRampPointClamp : register(s2)
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

// ============================================================================
// IBL (t7,t8,t9 / s3)
// - txIrr  : diffuse irradiance cubemap
// - txPref : spec prefiltered cubemap (mip 사용)
// - txBRDF : BRDF LUT (2D)
// ============================================================================
TextureCube txIrr : register(t7);
TextureCube txPref : register(t8);
Texture2D txBRDF : register(t9);

SamplerState samClampLinear : register(s3);

// ============================================================================
// Shared vertex/pixel interfaces
// ============================================================================
struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Tang : TANGENT; // (=TANGENT0)

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
    // Gram-Schmidt: T를 N에 직교화 후 정규화
    T = normalize(T - N * dot(T, N));
    float3 B = normalize(cross(N, T));
    // 수치 안정성: 다시 직교화
    T = normalize(cross(B, N));
    return T;
}

// Normal map (tangent space) -> world space
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

// Alpha cutout helper (간단 버전)
// NOTE: PBR_PS 쪽처럼 “alphaCut < 0이면 투명” 같은 정책이 필요하면 여기 수정해야 함.
//       현재는 useOpacity != 0이면 무조건 clip(alpha - alphaCut) 형태.
inline void AlphaClip(float2 uv)
{
    if (useOpacity != 0)
    {
        float a = txOpacity.Sample(samLinear, uv).r;
        clip(a - alphaCut);
    }
}

// Shadow PCF (3x3)
// - worldPos를 light VP로 변환 후, uv/z 계산
// - 기울기 기반 bias 적용
float SampleShadow_PCF(float3 worldPos, float3 Nw)
{
    float4 lp = mul(float4(worldPos, 1.0f), gLightViewProj);

    // 라이트 뒤쪽이면 그림자 없음 처리
    if (lp.w <= 0.0f)
        return 1.0f;

    float3 ndc = lp.xyz / lp.w; // perspective divide
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f; // Y flip 포함
    float z = ndc.z;

    // 화면 밖 / depth 밖이면 그림자 없음
    if (any(uv < 0.0f) || any(uv > 1.0f) || z < 0.0f || z > 1.0f)
        return 1.0f;

    // slope-scaled bias (크면 peter-panning / 작으면 acne)
    float ndotl = saturate(dot(Nw, normalize(-vLightDir.xyz)));
    float bias = max(0.0005f, gShadowParams.x * (1.0f - ndotl));

    // 3x3 PCF
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
// Skinning bones palette (b4)
// - SKINNED 정의된 셰이더에서만 사용
// ============================================================================
static const uint kMaxBones = 256;

#if defined(SKINNED)
cbuffer Bones : register(b4)
{
    float4x4 BonePalette[kMaxBones];
}
#endif

#endif // SHARED_HLSLI_INCLUDED
