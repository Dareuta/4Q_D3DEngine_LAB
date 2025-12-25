cbuffer CB0 : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldInvTranspose;
    float4 vLightDir;
    float4 vLightColor;
};

struct VS_IN
{
    float3 Pos : POSITION;
    float4 Col : COLOR;
};

struct VS_OUT
{
    float4 SvPos : SV_Position;
    float4 Col : COLOR;
};

VS_OUT main(VS_IN i)
{
    VS_OUT o;

    o.SvPos = mul(mul(mul(float4(i.Pos, 1.0f), World), View), Projection);
    o.Col = i.Col;

    return o;
}
