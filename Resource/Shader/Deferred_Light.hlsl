

static const float PI = 3.14159265f;

cbuffer CB0 : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldInvTranspose;
    float4 vLightDir; // "광원 방향" (너 코드 주석대로 dot(N, -vLightDir))
    float4 vLightColor; // rgb = color * intensity
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

cbuffer ShadowCB : register(b6)
{
    float4x4 LVP;
    float4 Params; // x: bias
};

Texture2D gPos : register(t0);
Texture2D gNrm : register(t1);
Texture2D gAlb : register(t2);
Texture2D gMR : register(t3);

Texture2D<float> gShadowMap : register(t5);

SamplerState s0 : register(s0);
SamplerComparisonState s1 : register(s1);

struct VS_OUT
{
    float4 PosH : SV_Position;
    float2 UV : TEXCOORD0;
};

VS_OUT VS_Main(uint vid : SV_VertexID)
{
    // fullscreen triangle
    float2 p = (vid == 2) ? float2(3, -1) : (vid == 1) ? float2(-1, 3) : float2(-1, -1);

    VS_OUT o;
    o.PosH = float4(p, 0, 1);
    o.UV = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}

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
    float k = (r * r) / 8.0f; // UE4 방식
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float ShadowTerm(float3 worldPos)
{
    float4 lp = mul(float4(worldPos, 1), LVP);
    float3 ndc = lp.xyz / max(lp.w, 1e-6f);

    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float z = ndc.z;

    // 밖이면 그냥 lit
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return 1.0f;

    float bias = Params.x;
    return gShadowMap.SampleCmpLevelZero(s1, uv, z - bias);
}

float4 PS_Main(VS_OUT i) : SV_Target
{
    float4 wp = gPos.Sample(s0, i.UV);
    if (wp.w == 0.0f)
        return float4(0, 0, 0, 1); // geometry 없음

    float3 worldPos = wp.xyz;

    float3 N = gNrm.Sample(s0, i.UV).xyz * 2.0f - 1.0f; // decode
    N = normalize(N);

    float3 albedo = gAlb.Sample(s0, i.UV).rgb;
    float2 mr = gMR.Sample(s0, i.UV).rg;
    float metallic = saturate(mr.x);
    float roughness = clamp(mr.y, 0.02f, 1.0f);

    float3 V = normalize(EyePosW - worldPos);
    float3 L = normalize(-vLightDir.xyz);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float a = roughness * roughness;

    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-6f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diff = kD * albedo / PI;

    float shadow = ShadowTerm(worldPos);

    float3 radiance = vLightColor.rgb; // 이미 intensity 포함
    float3 color = (diff + spec) * radiance * NdotL * shadow;

    // (선택) 너무 깜깜하면 최소 ambient 한 줄만 살려도 됨
    color += 0.02f * albedo;

    return float4(color, 1);
}
