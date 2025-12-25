// ============================================================================
// Constants
// ============================================================================

static const float PI = 3.14159265f;

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
// Directional Shadow CB + Resources
// ============================================================================

cbuffer ShadowCB : register(b6)
{
    float4x4 LVP;
    float4 Params; // x: bias
};

Texture2D<float> gShadowMap : register(t5);

// ============================================================================
// Point Shadow (Cube) Resource
// ============================================================================

TextureCube<float> gPointShadowCube : register(t10);

// ============================================================================
// Samplers
// ============================================================================

SamplerState s0 : register(s0);
SamplerComparisonState s1 : register(s1);

// ============================================================================
// Fullscreen VS (Deferred Lighting Pass)
// ============================================================================

struct VS_OUT
{
    float4 PosH : SV_Position;
    float2 UV : TEXCOORD0;
};

VS_OUT VS_Main(uint vid : SV_VertexID)
{
    float2 p =
        (vid == 2) ? float2(3, -1) :
        (vid == 1) ? float2(-1, 3) :
                     float2(-1, -1);

    VS_OUT o;
    o.PosH = float4(p, 0, 1);
    o.UV = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}

// ============================================================================
// PBR / IBL Params + Resources
// ============================================================================

cbuffer PBRParams : register(b8)
{
    uint pUseBaseColorTex;
    uint pUseNormalTex;
    uint pUseMetallicTex;
    uint pUseRoughnessTex;

    float4 pBaseColor;
    float4 pParams;

    float4 pEnvDiff;
    float4 pEnvSpec;
    float4 pEnvInfo; // x = prefilterMaxMip
}

TextureCube txIrr : register(t7);
TextureCube txPref : register(t8);
Texture2D txBRDF : register(t9);

SamplerState s3 : register(s3);

// ============================================================================
// Point Shadow Params (Cube distNorm)
// ============================================================================

cbuffer PointShadowCB : register(b13)
{
    float4 gPointShadowPosRange;
    float4 gPointShadowParams;
}

// ============================================================================
// Deferred Point Lights (Array)
// ============================================================================

#define MAX_POINT_LIGHTS 8

cbuffer DeferredLightsCB : register(b12)
{
    float4 gEyePosW;
    uint4 gPointMeta;
    float4 gPointPosRange[MAX_POINT_LIGHTS];
    float4 gPointColorInt[MAX_POINT_LIGHTS];
}

// ============================================================================
// Attenuation Helpers
// ============================================================================

float AttenSmooth(float dist, float range)
{
    float t = saturate(1.0f - dist / max(range, 1e-4f));
    return t * t;
}

float AttenInvSq(float dist, float range)
{
    float inv = 1.0f / max(dist * dist, 1e-4f);

    float t = saturate(1.0f - dist / max(range, 1e-4f));
    return inv * (t * t);
}

// ============================================================================
// Point Shadow Term (Cube distNorm)
// ============================================================================

float PointShadowTerm(float3 worldPos, float3 lightPos, float range)
{
    if (gPointShadowParams.y == 0.0f)
        return 1.0f;

    float3 v = worldPos - lightPos;
    float dist = length(v);
    if (dist >= range)
        return 1.0f;

    float invDist = 1.0f / max(dist, 1e-4f);
    float3 dir = v * invDist;

    float distN = dist / max(range, 1e-4f);
    float stored = gPointShadowCube.SampleLevel(s3, dir, 0).r;

    return (distN - gPointShadowParams.x <= stored) ? 1.0f : 0.0f;
}

// ============================================================================
// Microfacet BRDF Helpers (GGX / Smith / Schlick)
// ============================================================================

float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-6f);
}

float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-6f);
}

float G_Smith(float NdotV, float NdotL, float rough)
{
    float r = rough + 1.0f;
    float k = (r * r) / 8.0f;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// ============================================================================
// Directional Shadow Term (PCF)
// ============================================================================

float ShadowTerm(float3 worldPos, float3 Nw)
{
    float4 lp = mul(float4(worldPos, 1.0f), LVP);

    if (lp.w <= 0.0f)
        return 1.0f;

    float3 ndc = lp.xyz / lp.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float z = ndc.z;

    if (any(uv < 0.0f) || any(uv > 1.0f) || z < 0.0f || z > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(Nw, normalize(-vLightDir.xyz)));
    float bias = max(0.0005f, Params.x * (1.0f - ndotl));

    float2 texel = Params.yz;
    float acc = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            acc += gShadowMap.SampleCmpLevelZero(
                s1,
                uv + float2(dx, dy) * texel,
                z - bias
            );
        }
    }

    return acc / 9.0f;
}

// ============================================================================
// IBL Fresnel Helper
// ============================================================================

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0)
        * pow(1.0f - cosTheta, 5.0f);
}

// ============================================================================
// GBuffer Resources
// ============================================================================

Texture2D<float4> gPos : register(t0);
Texture2D<float4> gNrm : register(t1);
Texture2D<float4> gAlb : register(t2);
Texture2D<float4> gMR : register(t3);

// ============================================================================
// PS: Deferred Lighting (Dir + Point + IBL)
// ============================================================================

float4 PS_Main(VS_OUT i) : SV_Target
{
    // --- Pixel Coord Clamp ---
    uint w, h;
    gPos.GetDimensions(w, h);

    int2 pix = int2(i.PosH.xy);
    pix = clamp(pix, int2(0, 0), int2((int) w - 1, (int) h - 1));

    // --- GBuffer Fetch (Load, no filtering) ---
    float4 wp = gPos.Load(int3(pix, 0));
    if (wp.w == 0.0f)
        return float4(0, 0, 0, 1);

    float3 worldPos = wp.xyz;

    float3 Nw = gNrm.Load(int3(pix, 0)).xyz;
    Nw = (dot(Nw, Nw) > 1e-6f) ? normalize(Nw) : float3(0, 1, 0);

    float3 baseColor = gAlb.Load(int3(pix, 0)).rgb;

    float2 mr = gMR.Load(int3(pix, 0)).rg;
    float metallic = saturate(mr.r);
    float roughness = clamp(mr.g, 0.04f, 1.0f);

    // --- View / Light Vectors ---
    float3 V = normalize(EyePosW - worldPos);
    float3 L = normalize(-vLightDir.xyz);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(Nw, L));
    float NdotV = saturate(dot(Nw, V));
    float NdotH = saturate(dot(Nw, H));
    float VdotH = saturate(dot(V, H));

    // --- BRDF (Direct: Directional) ---
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float a = roughness * roughness;

    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-6f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diff = kD * baseColor / PI;

    // --- Directional Shadow ---
    float shadow = ShadowTerm(worldPos, Nw);

    float3 radiance = vLightColor.rgb;
    float3 direct = (diff + spec) * radiance * NdotL * shadow;

    // --- Point Lights (Deferred) ---
    if (gPointMeta.y != 0u)
    {
        uint count = min(gPointMeta.x, (uint) MAX_POINT_LIGHTS);

        [loop]
        for (uint li = 0u; li < count; ++li)
        {
            float3 lp = gPointPosRange[li].xyz;
            float range = gPointPosRange[li].w;

            float3 Lvec = lp - worldPos;
            float dist = length(Lvec);
            if (dist >= range || dist < 1e-4f)
                continue;

            float3 Lp = Lvec / dist;

            float NdotLp = saturate(dot(Nw, Lp));
            if (NdotLp <= 0.0f)
                continue;

            float3 Hp = normalize(V + Lp);
            float NdotHp = saturate(dot(Nw, Hp));
            float VdotHp = saturate(dot(V, Hp));

            float Dp = D_GGX(NdotHp, a);
            float Gp = G_Smith(NdotV, NdotLp, roughness);
            float3 Fp = F_Schlick(VdotHp, F0);

            float3 specP = (Dp * Gp * Fp) / max(4.0f * NdotV * NdotLp, 1e-6f);

            float3 kS_p = Fp;
            float3 kD_p = (1.0f - kS_p) * (1.0f - metallic);
            float3 diffP = kD_p * baseColor / PI;

            float atten = (gPointMeta.z == 0u) ? AttenSmooth(dist, range) : AttenInvSq(dist, range);

            float3 lightColor = gPointColorInt[li].rgb * gPointColorInt[li].w;
            float3 radianceP = lightColor * atten;

            float shadowP = 1.0f;
            if (li == 0u)
                shadowP = PointShadowTerm(worldPos, lp, range);

            direct += (diffP + specP) * radianceP * NdotLp * shadowP;
        }
    }

    // --- IBL ---
    float ao = 1.0f;
    float3 R = reflect(-V, Nw);

    float3 envDiff = pEnvDiff.rgb * pEnvDiff.w;
    float3 envSpec = pEnvSpec.rgb * pEnvSpec.w;

    float3 irradiance = txIrr.Sample(s3, Nw).rgb * envDiff;

    float maxMip = max(pEnvInfo.x, 0.0f);
    float3 prefiltered = txPref.SampleLevel(s3, R, roughness * maxMip).rgb * envSpec;

    float2 brdf = txBRDF.Sample(s3, float2(NdotV, roughness)).rg;

    float3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_amb = (1.0f - F_ibl) * (1.0f - metallic);

    float3 diffIBL = irradiance * baseColor / PI;
    float3 specIBL = prefiltered * (F_ibl * brdf.x + brdf.y);

    float3 ambient = (kD_amb * diffIBL + specIBL) * ao;

    float3 color = direct + ambient;
    return float4(color, 1);
}
