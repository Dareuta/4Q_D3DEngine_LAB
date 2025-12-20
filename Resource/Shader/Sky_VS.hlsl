cbuffer CB : register(b0)
{
    float4x4 World;
    float4x4 View;        
    float4x4 Projection;
}

struct VS_IN  { float3 Pos : POSITION; };
//struct VS_OUT { float4 SvPos : SV_POSITION; float3 Dir : TEXCOORD0; };
struct VS_OUT
{
    float4 SvPos : SV_POSITION;
    noperspective float3 Dir : TEXCOORD0;
};

VS_OUT main(VS_IN i)
{
    VS_OUT o;
    
    float4 p    = float4(i.Pos, 1.0f);
    float4 clip = mul(mul(p, View), Projection);
    clip.z = clip.w;              
    o.SvPos = clip;
    o.Dir = i.Pos;

    return o;
}
