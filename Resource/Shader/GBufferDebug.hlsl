// ../Resource/Shader/GBufferDebug.hlsl

cbuffer GBufferDebugCB : register(b11)
{
    uint mode; // 1=WorldPos, 2=Normal, 3=BaseColor, 4=Metal/Rough
    float posRange; // WorldPos remap 범위
    float2 _pad;
};

Texture2D gPos : register(t0);
Texture2D gNrm : register(t1);
Texture2D gAlb : register(t2);
Texture2D gMR : register(t3);

SamplerState s0 : register(s0);

struct VS_OUT
{
    float4 PosH : SV_Position;
    float2 UV : TEXCOORD0;
};

float4 PS_Main(VS_OUT i) : SV_Target
{
    float4 wp = gPos.Sample(s0, i.UV);
    if (wp.w == 0.0f)
        return float4(0, 0, 0, 1);

    if (mode == 1u)
    {
        float r = max(posRange, 1e-3f);
        float3 c = wp.xyz / r * 0.5f + 0.5f;
        return float4(saturate(c), 1);
    }
    if (mode == 2u)
    {        
        float3 n = normalize(gNrm.Sample(s0, i.UV).xyz);        
        return float4(n * 0.5f + 0.5f, 1);

    }
    if (mode == 3u)
    {
        return float4(gAlb.Sample(s0, i.UV).rgb, 1);
    }
    if (mode == 4u)
    {
        float2 mr = gMR.Sample(s0, i.UV).rg;
        return float4(mr.x, mr.y, 0, 1);
    }

    return float4(0, 0, 0, 1);
}
