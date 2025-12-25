#define SKINNED 1
#include "Shared.hlsli"

struct VS_IN
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Tang : TANGENT;
    uint4 BlendIndices : BLENDINDICES;
    float4 BlendWeight : BLENDWEIGHT;
};

struct VS_OUT
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
    float3 Pw : TEXCOORD1;
};

VS_OUT main(VS_IN i)
{
    // 스키닝(4본 가정. 너의 Shared.hlsli에 BonePalette[b4]가 있을 것)
    uint4 bi = i.BlendIndices;
    float4 bw = i.BlendWeight;

    float4x4 B =
          bw.x * BonePalette[bi.x]
        + bw.y * BonePalette[bi.y]
        + bw.z * BonePalette[bi.z]
        + bw.w * BonePalette[bi.w];

    float4 P = mul(float4(i.Pos, 1), B);
    float4 Pw = mul(P, World);

    VS_OUT o;
    o.PosH = mul(mul(Pw, View), Projection);
    o.Tex = i.Tex;
    o.Pw = Pw.xyz;
    return o;
}
