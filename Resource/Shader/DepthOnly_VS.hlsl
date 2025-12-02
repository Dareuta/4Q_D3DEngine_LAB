#include "Shared.hlsli"

// 네가 쓰는 정점 포맷의 "시맨틱"만 맞으면 이름은 달라도 됨.
// (PNTT: POSITION/NORMAL/TEXCOORD0/TANGENT)
struct VS_IN
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Tang : TANGENT;
};

struct VS_OUT
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0; // cutout 처리 위해 전달
    float3 Pw : TEXCOORD1; // (선택) 월드 위치 필요시
};

VS_OUT main(VS_IN i)
{
    VS_OUT o;

    // 월드로 변환
    float4 Pw = mul(float4(i.Pos, 1.0f), World);

    // *** 중요 ***
    // 이 VS는 나중에 C++에서 View/Projection에 "라이트 카메라"를 넣고 돌릴 거야.
    // 그러면 아래 곱이 곧 LightViewProj가 됨.
    o.PosH = mul(mul(Pw, View), Projection);

    o.Tex = i.Tex;
    o.Pw = Pw.xyz; // (선택) 나중에 디버깅용/확장용
    return o;
}
