TextureCube SkyTex : register(t0);
SamplerState SkySamp : register(s0);
struct PS_IN
{
    float4 SvPos : SV_POSITION;
    float3 Dir : TEXCOORD0;
};
float4 main(PS_IN i) : SV_Target
{    
//    return SkyTex.Sample(SkySamp, normalize(i.Dir));
    
    float3 c = SkyTex.Sample(SkySamp, normalize(i.Dir)).rgb;

    // MDR이 sRGB(감마)로 저장돼 있다고 가정하고 linear로 디코드
    c = pow(c, 2.2);

    return float4(c, 1);
    
}
