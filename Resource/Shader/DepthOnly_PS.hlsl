#include "Shared.hlsli"

struct PS_IN
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

void main(PS_IN input)
{
    // 기존 Shared.hlsli에서 쓰는 명칭을 그대로 사용:
    // - useOpacity (uint), alphaCut (float), txOpacity (Texture2D)
    // 프로젝트의 실제 이름이 다르면 여기만 맞춰줘.
    if (useOpacity != 0)
    {
        float a = txOpacity.Sample(samLinear, input.Tex).a;

        // 만약 너의 프로젝트가 "흰 = 투명" 규칙이면 아래 한 줄 활성화
        // a = 1.0 - a;

        // alphaCut은 머티리얼이나 UseCB에 이미 있을 확률 큼(없다면 0.5 고정도 가능)
        clip(a - alphaCut);
    }
    // 색상 출력 없음. 깊이는 VS의 SV_Position.zw로 기록됨.
}
