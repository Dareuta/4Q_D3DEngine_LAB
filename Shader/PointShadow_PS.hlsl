// PointShadow_PS.hlsl
// Cube shadow map for point light
// - Color cube stores distNorm = distance(lightPos, worldPos) / range
// - Depth buffer is used to keep nearest surface per direction

#include "Shared.hlsli"

cbuffer PointShadowCB : register(b13)
{
    float4 gPointShadowPosRange; // xyz=lightPos, w=range
    float4 gPointShadowParams;   // x=bias(사용X), y=enable(사용X), z/w=unused
}

struct PS_IN
{
    float4 PosH : SV_Position;
    float2 Tex  : TEXCOORD0;
    float3 Pw   : TEXCOORD1;
};

float main(PS_IN IN) : SV_Target0
{
    // alpha cut (나뭇잎 같은 cutout)
    if (useOpacity != 0)
    {
        float a = txOpacity.Sample(samLinear, IN.Tex).a;
        clip(a - alphaCut);
    }

    float range = max(gPointShadowPosRange.w, 1e-4);
    float dist  = length(IN.Pw - gPointShadowPosRange.xyz);

    // 0~1로 정규화된 거리 저장
    return saturate(dist / range);
}
