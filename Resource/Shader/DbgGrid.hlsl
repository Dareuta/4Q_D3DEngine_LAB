// Shader/DbgGrid.hlsl
#define DGBGRID 1
#include "Shared.hlsli"



// ===== Procedural Params (PS b9) =====
cbuffer ProcCB : register(b9)
{
    float4 uProc1; // x=timeSec, y=cellScale, z=warp1, w=warp2
    float4 uProc2; // x=scrollX, y=scrollY, z=gridMix(0~1), w=unused
}

// ---- hash random (sin*fract) ----
float random(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// ---- Perlin fade (6t^5 - 15t^4 + 10t^3) ----
float2 perlinFade(float2 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// ---- 2D Value Noise ----
float noise2(float2 uv)
{
    float2 cell = floor(uv);
    float2 f = frac(uv);

    float a = random(cell);
    float b = random(cell + float2(1, 0));
    float c = random(cell + float2(0, 1));
    float d = random(cell + float2(1, 1));

    float2 u = perlinFade(f);

    float top = lerp(a, b, u.x);
    float bottom = lerp(c, d, u.x);
    return lerp(top, bottom, u.y);
}

// ---- FBM (5~6 octave 권장) ----
float fbm(float2 uv)
{
    float v = 0.0;
    float a = 0.5;
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        v += a * noise2(uv);
        uv *= 2.0;
        a *= 0.5;
    }
    return v;
}

// ---- Domain warp helper ----
float2 warp(float2 uv)
{
    // 서로 다른 오프셋으로 2채널 만들기
    float x = fbm(uv + float2(5.2, 1.3));
    float y = fbm(uv + float2(1.7, 9.2));
    return float2(x, y) * 2.0 - 1.0; // [-1,1]
}

float3 SmokeColor(float2 uv, float t)
{
    // 위로 올라가는 드리프트 + 약간의 좌우 흔들림
    float2 drift = float2(0.15 * sin(t * 0.3), -0.35) * t;
    float2 uv0 = uv + drift;

    // domain warp로 소용돌이
    float2 w1 = warp(uv0) * uProc1.z;
    float2 w2 = warp(uv0 + w1 + 7.7) * uProc1.w;
    float d = fbm(uv0 + w1 + w2);

    // 연기는 “부드러운 덩어리 + 가장자리 흐림”
    d = smoothstep(0.35, 0.75, d);
    d = pow(d, 1.6);

    float3 dark = float3(0.05, 0.06, 0.07);
    float3 light = float3(0.55, 0.58, 0.62);
    return lerp(dark, light, d);
}

float3 CloudColor(float2 uv, float t)
{
    float2 uv0 = uv + float2(0.02, -0.01) * t;

    float2 w = warp(uv0) * uProc1.z;
    float d = fbm(uv0 + w);

    // 구름은 “덩어리”가 중요: 콘트라스트 올리기
    d = smoothstep(0.45, 0.80, d);

    // 간단한 셀프 섀도우(밀도 방향으로 한 번 더 샘플)
    float2 lightDir = normalize(float2(-0.7, -0.3));
    float d2 = fbm(uv0 + w + lightDir * 0.6);
    float shade = saturate(1.0 - (d2 - d) * 1.5);

    float3 base = float3(0.85, 0.87, 0.90);
    float3 col = base * (0.6 + 0.4 * shade);
    return col * (0.2 + 0.8 * d); // 투명도 대신 밝기 스케일
}

float3 PlasmaColor(float2 uv, float t)
{
    float2 uv0 = uv + float2(0.05, 0.02) * t;

    float2 w1 = warp(uv0) * uProc1.z;
    float2 w2 = warp(uv0 + w1 + 13.1) * uProc1.w;
    float n = fbm(uv0 + w1 + w2);

    // 밴드(등고선) 느낌: sin으로 띠 만들기
    float bands = 0.5 + 0.5 * sin((n * 6.0 + t * 0.8) * 6.28318);
    bands = pow(bands, 2.2);

    // 간단 팔레트(자체 발광 느낌)
    float3 a = float3(0.10, 0.00, 0.20);
    float3 b = float3(0.10, 0.60, 1.20);
    float3 c = float3(1.80, 0.40, 0.10);

    float3 col = lerp(a, b, bands);
    col = lerp(col, c, smoothstep(0.65, 0.95, bands));
    return col; // 밝게 나오는 게 정상(발광체)
}

float3 WaterRippleCentral(float2 worldXZ, float t)
{
    // 중앙(그리드 중심). 원점이 그리드 중심이면 이대로 OK.
    float2 center = float2(0.0, 0.0);

    float radius = max(uProc2.w, 1e-3); // 그리드 반경(월드)
    float2 pW = worldXZ - center;

    // 0~1 정규화 거리: 그리드 크기에 "비례"하는 핵심
    float r01 = saturate(length(pW) / radius);

    // 워핑도 월드가 아니라 정규화 좌표에서 해야 크기 따라감
    float2 pn = pW / radius; // [-1..1] 근처
    float2 w = warp(pn * 2.0 + t * 0.05) * (uProc1.z * 0.15);
    pn += w;

    float r = length(pn); // 이것도 정규화 거리 기반

    // "링 개수"로 주파수 결정: 그리드가 커져도 링 개수는 동일
    float ringCount = max(uProc1.y, 1.0); // uProc1.y를 링 개수로 쓰자(예: 12~30)
    float phase = r * (ringCount * 6.28318) - t * 3.0;
    float wave = sin(phase);

    // 감쇠: 바깥으로 갈수록 약해지되 0으로 너무 빨리 죽지 않게
    float atten = 1.0 - smoothstep(0.0, 1.0, r01);
    float h = wave * atten;

    float3 deep = float3(0.02, 0.06, 0.10);
    float3 shallow = float3(0.05, 0.22, 0.25);

    float3 col = lerp(deep, shallow, 0.5 + 0.5 * h);

    // 링 하이라이트(물결 윤곽)
    float rim = smoothstep(0.90, 1.00, abs(wave)) * atten;
    col += rim * 0.25;

    return col;
}


float3 WaterRippleColor(float2 uv, float t)
{
    // 큰 링 만들려면 uv를 "줄여야" 함 (덩어리 크게)
    float2 p = uv * 0.35;

    // domain warp로 바람/흐름 느낌
    float2 w = warp(p + float2(t * 0.05, t * 0.03)) * uProc1.z;
    p += w;

    // 4x4 단위마다 중심이 반복되는 리플 (어디서 봐도 보임)
    float2 cell = frac(p / 4.0) - 0.5; // [-0.5, 0.5]
    float r = length(cell); // 0 ~ 약 0.707

    // 링: r에 대한 사인파, 감쇠는 너무 세지 않게
    float phase = (r * 18.0) - (t * 3.0);
    float wave = sin(phase);

    float atten = 1.0 / (1.0 + r * 6.0); // exp보다 덜 죽음(중요)
    float h = wave * atten;

    float3 deep = float3(0.02, 0.06, 0.10);
    float3 shallow = float3(0.05, 0.22, 0.25);

    float3 col = lerp(deep, shallow, 0.5 + 0.5 * h);

    // 물결 하이라이트(거품 같은 느낌)
    float foam = smoothstep(0.85, 1.0, abs(wave)) * atten;
    col += foam * 0.25;

    return col;
}




// ---- Lava color (Double Warp + turbulence shaping) ----
float3 LavaColor(float2 uv, float t)
{
    // "boiling" 느낌: x,y 둘 다 시간에 따라 이동 (슬라이드 설명 그대로):contentReference[oaicite:5]{index=5}
    float2 uvTime = uv + float2(t * 0.05, t * 0.07);

    float2 w1 = warp(uvTime) * uProc1.z; // warp1
    float2 uv1 = uvTime + w1;

    float2 w2 = warp(uv1 + 11.0) * uProc1.w; // warp2 (double warp)
    float2 uv2 = uv1 + w2;

    float n = fbm(uv2);

    // turbulence 느낌(절댓값 기반)
    float turb = abs(2.0 * n - 1.0);

    // 뜨거운 용암 "틈" 마스크: 높은 turb 영역만 살림 (얇은 밝은 vein)
    float cracks = smoothstep(0.70, 0.95, turb);
    cracks = pow(cracks, 2.0);

    // 바위 베이스 + 용암 발광
    float3 rock = float3(0.05, 0.04, 0.03);
    float3 magma = float3(1.8, 0.55, 0.05);

    // rock에 미세 노이즈
    float grit = fbm(uvTime * 2.0);
    rock *= lerp(0.8, 1.2, grit);

    float3 col = lerp(rock, magma, cracks);
    return col;
}


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

    //float3 gridColor = baseCol;
    //gridColor = lerp(gridColor, lineCol, m1);
    //gridColor = lerp(gridColor, lineCol10, saturate(m10));
    
    float t = uProc1.x;

// 월드 XZ를 UV로: cellScale로 밀도 조절 (슬라이드에서도 UV*CellScale 강조):contentReference[oaicite:6]{index=6}
    float2 uvP = IN.WorldPos.xz * uProc1.y + uProc2.xy * t;
    //float2 uvP = (IN.WorldPos.xz * 0.05) * uProc1.y + uProc2.xy * t;

    //float3 procCol = LavaColor(uvP, t);
    //float3 procCol = WaterRippleColor(uvP, t);
    float3 procCol = WaterRippleCentral(IN.WorldPos.xz, t);
    //float3 procCol = CloudColor(uvP, t);
    //float3 procCol = PlasmaColor(uvP, t);       
    //float3 procCol = SmokeColor(uvP, t);


// 기존 그리드 라인(m1/m10) 위에 섞기
    float3 gridLine = baseCol;
    gridLine = lerp(gridLine, lineCol, m1);
    gridLine = lerp(gridLine, lineCol10, saturate(m10));

// uProc2.z = 0이면 순수 proc, 1이면 순수 grid
    float3 gridColor = lerp(procCol, gridLine, saturate(uProc2.z));


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
