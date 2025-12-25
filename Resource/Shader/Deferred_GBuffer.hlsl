// ============================================================================
// Frame / Camera / Light CBs
// ============================================================================

cbuffer CB0 : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldInvTranspose;
    float4 vLightDir;
    float4 vLightColor;
};

cbuffer CB1 : register(b1)
{
    float3 EyePosW;
    float _pad1;
    float4 Ambient;
    float4 Ka;
    float4 Ks;
    float Shininess;
    float3 _pad1b;
};

// ============================================================================
// Material / Texture Usage Flags
// ============================================================================

cbuffer UseCB : register(b2)
{
    uint useDiffuse;
    uint useNormal;
    uint useSpecular;
    uint useEmissive;
    uint useOpacity;
    float alphaCut;
    float2 _pad2;
};

cbuffer MAT : register(b5)
{
    float4 matBaseColor;
    uint matUseBaseColor;
    uint3 _matPad;
};

cbuffer PBRParams : register(b8)
{
    uint pUseBaseColorTex;
    uint pUseNormalTex;
    uint pUseMetalTex;
    uint pUseRoughTex;

    float4 pBaseColor;
    float4 pParams;

    float4 pEnvDiff;
    float4 pEnvSpec;
    float4 pEnvInfo;
};

// ============================================================================
// Textures / Samplers
// ============================================================================

Texture2D tBaseColor : register(t0);
Texture2D tNormal : register(t1);
Texture2D tMetallic : register(t2);
Texture2D tRoughness : register(t3);
Texture2D tOpacity : register(t4);

SamplerState s0 : register(s0);

// ============================================================================
// VS/PS IO
// ============================================================================

struct VS_IN
{
    float3 Pos : POSITION;
    float3 Nor : NORMAL;
    float2 UV : TEXCOORD0;
    float4 Tan : TANGENT;
};

struct VS_OUT
{
    float4 PosH : SV_Position;
    float3 WorldPos : TEXCOORD0;
    float3 Nw : TEXCOORD1;
    float3 Tw : TEXCOORD2;
    float3 Bw : TEXCOORD3;
    float2 UV : TEXCOORD4;
    float TanSign : TEXCOORD5;
};

struct PS_OUT
{
    float4 G0 : SV_Target0;
    float4 G1 : SV_Target1;
    float4 G2 : SV_Target2;
    float4 G3 : SV_Target3;
};

// ============================================================================
// VS
// ============================================================================

VS_OUT VS_Main(VS_IN i)
{
    VS_OUT o;

    float4 wpos = mul(float4(i.Pos, 1), World);
    o.PosH = mul(mul(wpos, View), Projection);
    o.WorldPos = wpos.xyz;
    o.TanSign = i.Tan.w;

    float3 Nw = normalize(mul(float4(i.Nor, 0), WorldInvTranspose).xyz);

    float3 Tw = mul(float4(i.Tan.xyz, 0), World).xyz;
    Tw = normalize(Tw);

    Tw = Tw - Nw * dot(Tw, Nw);
    Tw = normalize(Tw);

    float3 Bw = normalize(cross(Nw, Tw) * i.Tan.w);

    o.Nw = Nw;
    o.Tw = Tw;
    o.Bw = Bw;
    o.UV = i.UV;

    return o;
}

// ============================================================================
// Helpers
// ============================================================================

float3 DecodeNormalTS(float3 enc)
{
    float3 n = enc * 2.0f - 1.0f;

    if (pParams.w > 0.5f)
        n.y = -n.y;

    return n;
}

// ============================================================================
// PS (GBuffer Write)
// ============================================================================

PS_OUT PS_Main(VS_OUT i)
{
    PS_OUT o;

    // --- Opacity / Alpha Cut ---
    if (useOpacity == 1u)
    {
        float a = tOpacity.Sample(s0, i.UV).a;
        clip(a - alphaCut);
    }

    // --- Base Color ---
    float3 baseColor = (matUseBaseColor != 0) ? matBaseColor.rgb : pBaseColor.rgb;
    if (pUseBaseColorTex != 0u && useDiffuse == 1u)
        baseColor = tBaseColor.Sample(s0, i.UV).rgb;

    // --- Metallic / Roughness ---
    float metallic = pParams.x;
    float rough = pParams.y;

    if (pUseMetalTex != 0u && useSpecular == 1u)
        metallic = tMetallic.Sample(s0, i.UV).r;

    if (pUseRoughTex != 0u && useEmissive == 1u)
        rough = tRoughness.Sample(s0, i.UV).r;

    rough = clamp(rough, 0.04f, 1.0f);
    metallic = saturate(metallic);

    // --- Normal (World) ---
    float3 Nw_base = normalize(i.Nw);
    float3 Nw = Nw_base;

    if (pUseNormalTex != 0u && useNormal == 1u)
    {
        float3 nts = DecodeNormalTS(tNormal.Sample(s0, i.UV).xyz);
        nts.xy *= pParams.z;
        nts = normalize(nts);

        float3 T = normalize(i.Tw);
        T = normalize(T - Nw_base * dot(T, Nw_base));
        float3 B = normalize(cross(Nw_base, T) * i.TanSign);

        Nw = normalize(nts.x * T + nts.y * B + nts.z * Nw_base);
    }

    // --- Outputs ---
    o.G0 = float4(i.WorldPos, 1.0f);
    o.G1 = float4(Nw, 1.0f);
    o.G2 = float4(baseColor, 1.0f);
    o.G3 = float4(metallic, rough, 0, 1.0f);

    return o;
}
