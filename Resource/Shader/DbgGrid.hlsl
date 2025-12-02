// Shader/DbgGrid.hlsl
#define DGBGRID 1
#include "Shared.hlsli"

struct VS_IN
{
    float3 Pos : POSITION; // XZ 평면용 위치만
};

struct VS_OUT
{
    float4 PosH : SV_Position;
    float3 WorldPos : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
};

VS_OUT VS_Main(VS_IN IN)
{
    VS_OUT O;
    float4 Pw = float4(IN.Pos, 1.0);
    float4 PwW = mul(Pw, World);
    O.WorldPos = PwW.xyz;
    O.NormalW = mul(float4(0, 1, 0, 0), WorldInvTranspose).xyz; // 고정 Y+
    float4 Pv = mul(PwW, View);
    O.PosH = mul(Pv, Projection);
    return O;
}

// 부드러운 그리드 마스크 (AA)
float gridMask(float2 uv, float thickness)
{
    float2 g = abs(frac(uv) - 0.5);
    float a = max(g.x, g.y);
    float t = thickness; // 0.0~0.5
    return 1.0 - smoothstep(0.5 - t, 0.5, a);
}

float4 PS_Main(VS_OUT IN) : SV_Target
{
    // 그리드 파라미터
    const float cell = 1.0; // 1m 간격
    const float thick = 0.03; // 얇은 선 굵기
    const float thick10 = 0.06; // 10칸마다 굵은 선
    const float3 baseCol = float3(0.10, 0.11, 0.12);
    const float3 lineCol = float3(0.20, 0.22, 0.26);
    const float3 lineCol10 = float3(0.35, 0.40, 0.45);

    float2 uv = IN.WorldPos.xz / cell;

    // 기본 + 10칸 선
    float m1 = gridMask(uv, thick);
    float m10 = gridMask(uv / 10.0, thick10);

    float3 gridColor = baseCol;
    gridColor = lerp(gridColor, lineCol, m1);
    gridColor = lerp(gridColor, lineCol10, saturate(m10));

    // 간단한 램버트 + 그림자(네 Shared의 샘플러/CB 사용)
    float3 N = normalize(IN.NormalW);
    float3 L = normalize(-vLightDir.xyz);
    float ndotl = max(0.0, dot(N, L));

    float shadow = SampleShadow_PCF(IN.WorldPos, N); // 우리가 이전에 추가한 함수
    float3 ambient = I_ambient.rgb * kA.rgb; // b1 사용 (이미 바인딩하고 있음)
    float3 direct = vLightColor.rgb * ndotl * shadow;

    float3 final = gridColor * (ambient + direct);
    return float4(final, 1.0);
}
