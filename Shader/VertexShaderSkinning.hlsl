#include "Shared.hlsli"

PS_INPUT main(VS_INPUT i)
{
    float4 Pobj = float4(i.Pos, 1.0f);
    float3 Nobj = i.Norm;
    float3 Tobj = i.Tang.xyz;
    float signT = i.Tang.w;

#if defined(SKINNED)
    uint4  bi = i.BlendIndices;
    float4 bw = i.BlendWeights;

    // 행렬 저장/곱 순서에 따라 전치가 필요할 수 있음 (틀어지면 transpose 시도)
    float4x4 B =
          bw.x * BonePalette[bi.x]
        + bw.y * BonePalette[bi.y]
        + bw.z * BonePalette[bi.z]
        + bw.w * BonePalette[bi.w];

    Pobj = mul(Pobj, B);
    float3x3 B3 = (float3x3)B;
    Nobj = mul(Nobj, B3);
    Tobj = mul(Tobj, B3);
#endif

    float4 Pw = mul(Pobj, World);
    float3 Nw = normalize(mul(Nobj, (float3x3) WorldInvTranspose));
    float3 Tw = normalize(mul(Tobj, (float3x3) World));

    float4 Pv = mul(Pw, View);
    float4 Pc = mul(Pv, Projection);

    PS_INPUT o;
    o.PosH = Pc;
    o.WorldPos = Pw.xyz;
    o.Tex = i.Tex;
    o.TangentW = float4(Tw, signT);
    o.NormalW = Nw;
    return o;
}
