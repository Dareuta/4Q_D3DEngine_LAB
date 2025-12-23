// ToneMap.hlsl
// Input : t0 = SceneHDR (R16G16B16A16_FLOAT)
// Output: BackBuffer (LDR)
// CB    : b10

Texture2D gSceneHDR : register(t0);
SamplerState gSamp : register(s0);

cbuffer ToneMapCB : register(b10)
{
    float gExposureEV; // exp2(EV)
    float gGamma; // usually 2.2
    uint gOperatorId; // 0=None, 1=Reinhard, 2=ACES(Fitted)
    uint gFlags; // bit0: apply gamma
};

struct VS_OUT
{
    float4 PosH : SV_Position;
    float2 UV : TEXCOORD0;
};

VS_OUT VS_Main(uint vid : SV_VertexID)
{
    VS_OUT o;

    // Fullscreen triangle
    float2 pos[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };

    float2 uv[3] =
    {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    o.PosH = float4(pos[vid], 0.0, 1.0);
    o.UV = uv[vid];
    return o;
}

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFitted(float3 v)
{
    const float3x3 ACESInputMat =
    {
        { 0.59719, 0.35458, 0.04823 },
        { 0.07600, 0.90834, 0.01566 },
        { 0.02840, 0.13383, 0.83777 }
    };

    const float3x3 ACESOutputMat =
    {
        { 1.60475, -0.53108, -0.07367 },
        { -0.10208, 1.10813, -0.00605 },
        { -0.00327, -0.07276, 1.07602 }
    };

    v = mul(ACESInputMat, v);
    v = RRTAndODTFit(v);
    v = mul(ACESOutputMat, v);
    return saturate(v);
}

float3 ToneMap(float3 c)
{
    float exposure = exp2(gExposureEV);
    c *= exposure;

    if (gOperatorId == 1)
    {
        // Reinhard
        c = c / (1.0 + c);
        return c;
    }
    else if (gOperatorId == 2)
    {
        // ACES fitted
        return ACESFitted(c);
    }

    // None
    return c;
}

float4 PS_Main(VS_OUT i) : SV_Target
{
    float3 c = gSceneHDR.Sample(gSamp, i.UV).rgb;
    c = ToneMap(c);

    // backbuffer가 UNORM이면 보통 gamma 인코딩이 필요함
    if ((gFlags & 1u) != 0u)
    {
        c = pow(max(c, 0.0), 1.0 / max(gGamma, 0.0001));
    }

    return float4(c, 1.0);
}
