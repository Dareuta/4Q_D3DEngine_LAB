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

    // 밉 0 강제 (LOD 이상 선택으로 인한 뭉개짐/밴딩 먼저 제거)
    float3 c = SkyTex.SampleLevel(SkySamp, dir, 0).rgb;

    // 지금 네 렌더 파이프라인(UNORM 백버퍼 + 출력 감마 안함)과 "겉보기" 맞추려면
    // 스카이에서 sRGB->linear 디코드는 일단 하지 말아야 함
    return float4(c, 1);
}
