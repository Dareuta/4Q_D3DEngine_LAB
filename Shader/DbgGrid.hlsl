// Shader/DbgGrid.hlsl
#define DGBGRID 1
#include "Shared.hlsli"

// ============================================================================
// Point Light Shadow / Deferred Point Lights
// ============================================================================

TextureCube<float> gPointShadowCube : register(t10);

cbuffer PointShadowCB : register(b13)
{
    float4 gPointShadowPosRange; // xyz = lightPos, w = range
    float4 gPointShadowParams; // x = biasWorld, y = enable(1/0), z,w unused
};

#ifndef MAX_POINT_LIGHTS
#define MAX_POINT_LIGHTS 8
#endif

cbuffer DeferredLightsCB : register(b12)
{
    float4 gEyePosW; // xyz = eye
    uint4 gPointMeta; // x=count, y=enable, z=falloffMode(0 smooth, 1 invSq), w pad
    float4 gPointPosRange[MAX_POINT_LIGHTS]; // xyz pos, w range
    float4 gPointColorInt[MAX_POINT_LIGHTS]; // rgb color, w intensity
};

// ============================================================================
// Procedural Params
// ============================================================================

cbuffer ProcCB : register(b9)
{
    float4 uProc1; // x=timeSec, y=cellScale, z=warp1, w=warp2
    float4 uProc2; // x=scrollX, y=scrollY, z=gridMix(0~1), w=radius
};

// ============================================================================
// Noise / FBM Helpers
// ============================================================================

float random(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float2 perlinFade(float2 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

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

float2 warp(float2 uv)
{
    float x = fbm(uv + float2(5.2, 1.3));
    float y = fbm(uv + float2(1.7, 9.2));
    return float2(x, y) * 2.0 - 1.0;
}

// ============================================================================
// Procedural Materials
// ============================================================================

float3 SmokeColor(float2 uv, float t)
{
    float2 drift = float2(0.15 * sin(t * 0.3), -0.35) * t;
    float2 uv0 = uv + drift;

    float2 w1 = warp(uv0) * uProc1.z;
    float2 w2 = warp(uv0 + w1 + 7.7) * uProc1.w;
    float d = fbm(uv0 + w1 + w2);

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

    d = smoothstep(0.45, 0.80, d);

    float2 lightDir = normalize(float2(-0.7, -0.3));
    float d2 = fbm(uv0 + w + lightDir * 0.6);
    float shade = saturate(1.0 - (d2 - d) * 1.5);

    float3 base = float3(0.85, 0.87, 0.90);
    float3 col = base * (0.6 + 0.4 * shade);
    return col * (0.2 + 0.8 * d);
}

float3 PlasmaColor(float2 uv, float t)
{
    float2 uv0 = uv + float2(0.05, 0.02) * t;

    float2 w1 = warp(uv0) * uProc1.z;
    float2 w2 = warp(uv0 + w1 + 13.1) * uProc1.w;
    float n = fbm(uv0 + w1 + w2);

    float bands = 0.5 + 0.5 * sin((n * 6.0 + t * 0.8) * 6.28318);
    bands = pow(bands, 2.2);

    float3 a = float3(0.10, 0.00, 0.20);
    float3 b = float3(0.10, 0.60, 1.20);
    float3 c = float3(1.80, 0.40, 0.10);

    float3 col = lerp(a, b, bands);
    col = lerp(col, c, smoothstep(0.65, 0.95, bands));
    return col;
}

float3 WaterRippleCentral(float2 worldXZ, float t)
{
    float2 center = float2(0.0, 0.0);

    float radius = max(uProc2.w, 1e-3);
    float2 pW = worldXZ - center;

    float r01 = saturate(length(pW) / radius);

    float2 pn = pW / radius;
    float2 w = warp(pn * 2.0 + t * 0.05) * (uProc1.z * 0.15);
    pn += w;

    float r = length(pn);

    float ringCount = max(uProc1.y, 1.0);
    float phase = r * (ringCount * 6.28318) - t * 3.0;
    float wave = sin(phase);

    float atten = 1.0 - smoothstep(0.0, 1.0, r01);
    float h = wave * atten;

    float3 deep = float3(0.02, 0.06, 0.10);
    float3 shallow = float3(0.05, 0.22, 0.25);

    float3 col = lerp(deep, shallow, 0.5 + 0.5 * h);

    float rim = smoothstep(0.90, 1.00, abs(wave)) * atten;
    col += rim * 0.25;

    return col;
}

float3 WaterRippleColor(float2 uv, float t)
{
    float2 p = uv * 0.35;

    float2 w = warp(p + float2(t * 0.05, t * 0.03)) * uProc1.z;
    p += w;

    float2 cell = frac(p / 4.0) - 0.5;
    float r = length(cell);

    float phase = (r * 18.0) - (t * 3.0);
    float wave = sin(phase);

    float atten = 1.0 / (1.0 + r * 6.0);
    float h = wave * atten;

    float3 deep = float3(0.02, 0.06, 0.10);
    float3 shallow = float3(0.05, 0.22, 0.25);

    float3 col = lerp(deep, shallow, 0.5 + 0.5 * h);

    float foam = smoothstep(0.85, 1.0, abs(wave)) * atten;
    col += foam * 0.25;

    return col;
}

float3 LavaColor(float2 uv, float t)
{
    float2 uvTime = uv + float2(t * 0.05, t * 0.07);

    float2 w1 = warp(uvTime) * uProc1.z;
    float2 uv1 = uvTime + w1;

    float2 w2 = warp(uv1 + 11.0) * uProc1.w;
    float2 uv2 = uv1 + w2;

    float n = fbm(uv2);

    float turb = abs(2.0 * n - 1.0);

    float cracks = smoothstep(0.70, 0.95, turb);
    cracks = pow(cracks, 2.0);

    float3 rock = float3(0.05, 0.04, 0.03);
    float3 magma = float3(1.8, 0.55, 0.05);

    float grit = fbm(uvTime * 2.0);
    rock *= lerp(0.8, 1.2, grit);

    float3 col = lerp(rock, magma, cracks);
    return col;
}

// ============================================================================
// Point Light Shadow + Accumulation
// ============================================================================

float PointShadowTerm(float3 worldPos)
{
    if (gPointShadowParams.y < 0.5f)
        return 1.0f;

    float3 Lvec = worldPos - gPointShadowPosRange.xyz;
    float dist = length(Lvec);
    float range = max(gPointShadowPosRange.w, 1e-4f);

    if (dist >= range || dist < 1e-4f)
        return 1.0f;

    float3 dir = Lvec / dist;

    float distNorm = dist / range;
    float storedNorm = gPointShadowCube.SampleLevel(samLinear, dir, 0).r;

    float biasNorm = (gPointShadowParams.x / range);
    return (distNorm - biasNorm) <= storedNorm ? 1.0f : 0.0f;
}

float3 AddPointLight(float3 worldPos, float3 N, float3 baseCol)
{
    if (gPointMeta.y == 0u || gPointMeta.x == 0u)
        return 0;

    float3 lp = gPointPosRange[0].xyz;
    float range = gPointPosRange[0].w;

    float3 Lvec = lp - worldPos;
    float dist = length(Lvec);

    if (dist >= range || dist < 1e-4f)
        return 0;

    float3 L = Lvec / dist;

    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0)
        return 0;

    float atten;
    if (gPointMeta.z == 0u)
    {
        float x = saturate(1.0f - dist / range);
        atten = x * x;
    }
    else
    {
        float d2 = max(dist * dist, 1e-4f);
        atten = 1.0f / d2;

        float x = saturate(1.0f - dist / range);
        atten *= x * x;
    }

    float shadow = PointShadowTerm(worldPos);

    float3 light = gPointColorInt[0].rgb * gPointColorInt[0].w;
    return baseCol * light * (atten * NdotL * shadow);
}

// ============================================================================
// VS/PS IO
// ============================================================================

struct VS_IN
{
    float3 Pos : POSITION;
};

struct VS_OUT
{
    float4 PosH : SV_Position;
    float3 WorldPos : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
};

// ============================================================================
// VS
// ============================================================================

VS_OUT VS_Main(VS_IN IN)
{
    VS_OUT O;

    float4 Pw = float4(IN.Pos, 1.0);
    float4 PwW = mul(Pw, World);

    O.WorldPos = PwW.xyz;
    O.NormalW = mul(float4(0, 1, 0, 0), WorldInvTranspose).xyz;

    float4 Pv = mul(PwW, View);
    O.PosH = mul(Pv, Projection);

    return O;
}

// ============================================================================
// Grid Helpers
// ============================================================================

float gridMask(float2 uv, float thickness)
{
    float2 g = abs(frac(uv) - 0.5);
    float a = max(g.x, g.y);
    float t = thickness;
    return 1.0 - smoothstep(0.5 - t, 0.5, a);
}

// ============================================================================
// PS
// ============================================================================

float4 PS_Main(VS_OUT IN) : SV_Target
{
    // --- Grid Params ---
    const float cell = 1.0;
    const float thick = 0.03;
    const float thick10 = 0.06;

    const float3 baseCol = float3(0.10, 0.11, 0.12);
    const float3 lineCol = float3(0.20, 0.22, 0.26);
    const float3 lineCol10 = float3(0.35, 0.40, 0.45);

    float2 uv = IN.WorldPos.xz / cell;

    float m1 = gridMask(uv, thick);
    float m10 = gridMask(uv / 10.0, thick10);

    // --- Procedural Color ---
    float t = uProc1.x;
    float2 uvP = IN.WorldPos.xz * uProc1.y + uProc2.xy * t;

    float3 procCol = WaterRippleCentral(IN.WorldPos.xz, t);

    // --- Grid Line Mix ---
    float3 gridLine = baseCol;
    gridLine = lerp(gridLine, lineCol, m1);
    gridLine = lerp(gridLine, lineCol10, saturate(m10));

    float3 gridColor = lerp(procCol, gridLine, saturate(uProc2.z));

    // --- Directional Light + Shadow ---
    float3 N = normalize(IN.NormalW);
    float3 L = normalize(-vLightDir.xyz);
    float ndotl = max(0.0, dot(N, L));

    float shadow = SampleShadow_PCF(IN.WorldPos, N);
    float3 ambient = I_ambient.rgb * kA.rgb;
    float3 direct = vLightColor.rgb * ndotl * shadow;

    // --- Final ---
    float3 final = gridColor * (ambient + direct);
    final += AddPointLight(IN.WorldPos, normalize(IN.NormalW), final);

    return float4(final, 1.0);
}
