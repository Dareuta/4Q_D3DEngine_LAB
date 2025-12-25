#include "Shared.hlsli"

// ============================================================================
// Feature Switches
// ============================================================================

#ifndef NORMALMAP_FLIP_GREEN
#define NORMALMAP_FLIP_GREEN 0
#endif

#ifndef SWAPCHAIN_SRGB
#define SWAPCHAIN_SRGB 1
#endif

#ifndef OPACITY_MAP_IS_TRANSPARENCY
#define OPACITY_MAP_IS_TRANSPARENCY 0
#endif

// ============================================================================
// PS
// ============================================================================

float4 main(PS_INPUT input) : SV_Target
{
    // ------------------------------------------------------------------------
    // Opacity / Cutout
    // ------------------------------------------------------------------------
    float a = 1.0f;

    if (useOpacity != 0)
    {
        a = txOpacity.Sample(samLinear, input.Tex).a;

#if OPACITY_MAP_IS_TRANSPARENCY
        a = 1.0f - a;
#endif

        const float MIN_ALPHA = 1e-3f;
        if (a <= MIN_ALPHA)
            clip(-1);

        if (alphaCut >= 0.0f)
        {
            clip(a - alphaCut);
            a = 1.0f;
        }
    }

    // ------------------------------------------------------------------------
    // Material Sampling
    // ------------------------------------------------------------------------
    float3 texCol = (useDiffuse != 0) ? txDiffuse.Sample(samLinear, input.Tex).rgb : float3(1, 1, 1);
    float3 baseCol = (matUseBaseColor != 0) ? matBaseColor.rgb : float3(1, 1, 1);
    float3 albedo = texCol * baseCol;

    uint uSpec = useSpecular;
    bool specOn = (uSpec != 0);

    float specMask =
        (uSpec == 1) ? txSpecular.Sample(samLinear, input.Tex).r :
        (uSpec == 2) ? 1.0f :
                       0.0f;

    float3 emissive =
        (useEmissive != 0) ? txEmissive.Sample(samLinear, input.Tex).rgb : float3(0, 0, 0);

    // ------------------------------------------------------------------------
    // World Basis (N/T/B) + Normal Map
    // ------------------------------------------------------------------------
    float3 Nw_base = normalize(input.NormalW);

    float3 T = normalize(input.TangentW.xyz);
    T = normalize(T - Nw_base * dot(T, Nw_base));
    float3 B = normalize(cross(Nw_base, T) * input.TangentW.w);

    float3 Nw = Nw_base;

    if (useNormal != 0)
    {
        float3 nts = txNormal.Sample(samLinear, input.Tex).xyz * 2.0f - 1.0f;

#if NORMALMAP_FLIP_GREEN
        nts.y = -nts.y;
#endif

        nts = normalize(nts);
        Nw = normalize(nts.x * T + nts.y * B + nts.z * Nw_base);
    }

    // ------------------------------------------------------------------------
    // Lighting Vectors / Params
    // ------------------------------------------------------------------------
    float3 L = normalize(-vLightDir.xyz);
    float3 V = normalize(EyePosW.xyz - input.WorldPos);
    float3 H = normalize(L + V);

    float ks = kSAlpha.x;
    float shin = max(1.0f, kSAlpha.y);

    float NdotL = dot(Nw, L);

    float3 diff;
    float3 spec;

    float shadow = SampleShadow_PCF(input.WorldPos, Nw);

    // ------------------------------------------------------------------------
    // Shading (Toon / Standard)
    // ------------------------------------------------------------------------
    if (gUseToon != 0)
    {
        float ndl =
            (gToonHalfLambert != 0) ? (NdotL * 0.5f + 0.5f) :
                                     saturate(NdotL);

        ndl = saturate(ndl);

        float shadowToon = (shadow > 0.5f) ? 1.0f : gToonShadowMin;
        float litForRamp = ndl * shadowToon;

        uint w, h;
        txRamp.GetDimensions(w, h);

        float u = (litForRamp * (float) (w - 1) + 0.5f) / (float) w;
        float3 ramp = txRamp.SampleLevel(samRampPointClamp, float2(u, 0.5f), 0).rgb;

        diff = albedo * ramp;

        if (specOn)
        {
            float shadowRaw = shadow;
            float shadowSpec = (shadowRaw > 0.5f) ? 1.0f : 0.0f;

            float s = pow(saturate(dot(Nw, H)), shin);
            s = step(gToonSpecStep, s) * gToonSpecBoost;

            spec = specMask * ks * s * shadowSpec;
        }
        else
        {
            spec = 0.0.xxx;
        }
    }
    else
    {
        NdotL = saturate(NdotL);
        diff = albedo * NdotL * shadow;
        spec = specOn ? (specMask * ks * pow(saturate(dot(Nw, H)), shin) * shadow) : 0.0.xxx;
    }

    // ------------------------------------------------------------------------
    // Ambient / Final Compose
    // ------------------------------------------------------------------------
    float3 amb = I_ambient.rgb * kA.rgb * albedo;
    float3 direct = (diff + spec);

    float3 color = amb + emissive + vLightColor.rgb * direct;

#if SWAPCHAIN_SRGB
    return float4(color, a);
#else
    float3 color_srgb = pow(saturate(color), 1.0 / 2.2);
    return float4(color_srgb, a);
#endif
}
