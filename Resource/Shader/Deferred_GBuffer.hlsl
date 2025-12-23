
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

cbuffer UseCB : register(b2)
{
    uint useDiffuse;
    uint useNormal;
    uint useSpecular; // (너 프로젝트에선 Metallic)
    uint useEmissive; // (너 프로젝트에선 Roughness)
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

    float4 pBaseColor; // rgb
    float4 pParams; // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)

    float4 pEnvDiff; // unused here
    float4 pEnvSpec; // unused here
    float4 pEnvInfo; // unused here
};

Texture2D tBaseColor : register(t0);
Texture2D tNormal : register(t1);
Texture2D tMetallic : register(t2); // (AssimpImporterEX: specular 슬롯을 metallic로)
Texture2D tRoughness : register(t3); // (AssimpImporterEX: emissive 슬롯을 roughness로)
Texture2D tOpacity : register(t4);

SamplerState s0 : register(s0);

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
};

VS_OUT VS_Main(VS_IN i)
{
    VS_OUT o;
    float4 Pw = mul(float4(i.Pos, 1), World);
    o.WorldPos = Pw.xyz;

    float3 Nw = mul(float4(i.Nor, 0), WorldInvTranspose).xyz;
    float3 Tw = mul(float4(i.Tan.xyz, 0), World).xyz;
    Nw = normalize(Nw);
    Tw = normalize(Tw);
    float3 Bw = normalize(cross(Nw, Tw) * i.Tan.w);

    o.Nw = Nw;
    o.Tw = Tw;
    o.Bw = Bw;
    o.UV = i.UV;

    float4 Pv = mul(Pw, View);
    o.PosH = mul(Pv, Projection);
    return o;
}

float3 DecodeNormalTS(float3 enc)
{
    // enc: [0,1] -> [-1,1]
    float3 n = enc * 2.0f - 1.0f;
    if (pParams.w > 0.5f)
        n.y = -n.y;
    return n;
}

struct PS_OUT
{
    float4 G0 : SV_Target0; // WorldPos (xyz), valid(w)
    float4 G1 : SV_Target1; // WorldNormal encoded 0..1 (xyz), valid(w)
    float4 G2 : SV_Target2; // BaseColor (rgb)
    float4 G3 : SV_Target3; // Metallic (r), Roughness (g)
};

PS_OUT PS_Main(VS_OUT i)
{
    PS_OUT o;

    // alpha cut (선택)
    if (useOpacity == 1u)
    {
        float a = tOpacity.Sample(s0, i.UV).r;
        clip(a - alphaCut);
    }

    // baseColor
    float3 baseColor = (matUseBaseColor != 0) ? matBaseColor.rgb : pBaseColor.rgb;
    if (pUseBaseColorTex != 0u && useDiffuse == 1u)
        baseColor = tBaseColor.Sample(s0, i.UV).rgb;

    // metallic / roughness
    float metallic = pParams.x;
    float rough = pParams.y;

    if (pUseMetalTex != 0u && useSpecular == 1u)
        metallic = tMetallic.Sample(s0, i.UV).r;

    if (pUseRoughTex != 0u && useEmissive == 1u)
        rough = tRoughness.Sample(s0, i.UV).r;

    rough = clamp(rough, 0.02f, 1.0f);
    metallic = saturate(metallic);

    // normal
    float3 Nw = normalize(i.Nw);
    if (pUseNormalTex != 0u && useNormal == 1u)
    {
        float3 nts = DecodeNormalTS(tNormal.Sample(s0, i.UV).xyz);
        nts.xy *= pParams.z; // strength
        nts = normalize(nts);

        float3x3 TBN = float3x3(normalize(i.Tw), normalize(i.Bw), normalize(i.Nw));
        Nw = normalize(mul(nts, TBN));
    }

    // outputs
    o.G0 = float4(i.WorldPos, 1.0f);
    o.G1 = float4(Nw * 0.5f + 0.5f, 1.0f); // ImGui로 보기 좋게 0..1 인코딩
    o.G2 = float4(baseColor, 1.0f);
    o.G3 = float4(metallic, rough, 0, 1.0f);
    return o;
}
