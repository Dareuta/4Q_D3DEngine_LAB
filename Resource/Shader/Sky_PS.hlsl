TextureCube SkyTex : register(t0);
SamplerState SkySamp : register(s0);
struct PS_IN
{
    float4 SvPos : SV_POSITION;
    float3 Dir : TEXCOORD0;
};
float4 main(PS_IN i) : SV_Target
{
    return SkyTex.Sample(SkySamp, normalize(i.Dir));
}
