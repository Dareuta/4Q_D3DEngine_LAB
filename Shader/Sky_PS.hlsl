TextureCube SkyTex : register(t0);
SamplerState SkySamp : register(s0);

struct PS_IN
{
    float4 SvPos : SV_POSITION;
    float3 Dir : TEXCOORD0;
};

float4 main(PS_IN i) : SV_Target
{
    //return float4(1, 0, 1, 1); // 마젠타
    
    float3 dir = normalize(i.Dir);
    float3 c = SkyTex.SampleLevel(SkySamp, dir, 0).rgb; 
    return float4(c, 1);
}
