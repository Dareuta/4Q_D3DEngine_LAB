// ============================================================================
// RenderSharedCB.h
// - C++ <-> HLSL 공유 ConstantBuffer 레이아웃 모음
// - "cbuffer register(b#)" 슬롯 기준으로 정리
//
// 주의:
// - Matrix는 HLSL에서 column-major가 기본. C++에서 XMMatrixTranspose 해서 넣는 정책이면
//   여기 구조체는 그냥 "그대로 저장"하고, 업로드 쪽에서 transpose만 일관되게 유지하면 됨.
// - 모든 CB는 16-byte 정렬(패딩 포함) 필수.
// ============================================================================

#pragma once

#include <cstdint>
#include <directxtk/SimpleMath.h>

// 필요하다면 여기서만 공용으로 쓰는 assert 매크로
#define CB_STATIC_ASSERT_16B(T) static_assert((sizeof(T) % 16) == 0, #T " must be 16-byte aligned")

namespace RenderCB
{
    using Matrix = DirectX::SimpleMath::Matrix;
    using Vector4 = DirectX::SimpleMath::Vector4;

    // =========================================================================
    // b0 : Per-Object (W/V/P + 라이트 방향/색)
    // HLSL: cbuffer PerObject : register(b0)
    // =========================================================================
    struct PerObject
    {
        Matrix  mWorld;
        Matrix  mView;
        Matrix  mProjection;
        Matrix  mWorldInvTranspose;

        Vector4 vLightDir;    // xyz = direction, w = unused/1
        Vector4 vLightColor;  // rgb = color,     w = intensity(or 1)
    };
    CB_STATIC_ASSERT_16B(PerObject);

    // =========================================================================
    // b1 : Blinn-Phong 조명/재질 파라미터
    // HLSL: cbuffer BlinnPhongCB : register(b1)
    // =========================================================================
    struct BlinnPhong
    {
        Vector4 EyePosW;      // (ex, ey, ez, 1)
        Vector4 kA;           // (ka.r, ka.g, ka.b, 0)
        Vector4 kSAlpha;      // (ks, alpha/shininess, 0, 0)  <-- 네 엔진 정책에 맞춤
        Vector4 I_ambient;    // (Ia.r, Ia.g, Ia.b, 0)
    };
    CB_STATIC_ASSERT_16B(BlinnPhong);

    // =========================================================================
    // b2 : 텍스처 사용 플래그 + 알파 컷 (Cutout)
    // HLSL: cbuffer UseCB : register(b2)
    // =========================================================================
    struct Use
    {
        std::uint32_t useDiffuse;
        std::uint32_t useNormal;
        std::uint32_t useSpecular;
        std::uint32_t useEmissive;

        std::uint32_t useOpacity;
        float         alphaCut;    // -1이면 비활성, 그 외 clip(alpha - alphaCut)
        float         pad[2];      // 16B 정렬
    };
    CB_STATIC_ASSERT_16B(Use);

    // =========================================================================
    // b6 : Shadow (Directional / Spot 등 2D ShadowMap)
    // HLSL: cbuffer ShadowCB : register(b6)
    // =========================================================================
    struct Shadow
    {
        Matrix  LVP;      // LightViewProj (transpose는 C++ 업로드 정책에 따름)
        Vector4 Params;   // x: compareBias, y: 1/width, z: 1/height, w: reserved
    };
    CB_STATIC_ASSERT_16B(Shadow);

    // =========================================================================
    // b7 : Toon 파라미터
    // HLSL: cbuffer ToonCB : register(b7)
    // =========================================================================
    struct Toon
    {
        std::uint32_t useToon;       // 0/1
        std::uint32_t halfLambert;   // 0/1
        float         specStep;
        float         specBoost;

        float         shadowMin;     // toon shadow floor
        float         pad0, pad1, pad2; // 16B 정렬
    };
    CB_STATIC_ASSERT_16B(Toon);
} // namespace RenderCB

// ----------------------------------------------------------------------------
// 기존 코드 호환용 alias
// ----------------------------------------------------------------------------
using ConstantBuffer = RenderCB::PerObject;
using BlinnPhongCB = RenderCB::BlinnPhong;
using UseCB = RenderCB::Use;
using ShadowCB = RenderCB::Shadow;
using ToonCB_ = RenderCB::Toon;


// ============================================================================
// b8 : PBR 파라미터 (PBR_PS.hlsl 과 매칭)
// - 여기만 DirectX::XMFLOAT4를 쓰는 이유: POD/레이아웃 안정 + HLSL float4와 1:1 매핑
// ============================================================================
struct CB_PBRParams
{
    std::uint32_t useBaseColorTex;
    std::uint32_t useNormalTex;
    std::uint32_t useMetalTex;
    std::uint32_t useRoughTex;

    DirectX::XMFLOAT4 baseColorOverride; // rgb = override, a = unused

    // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)
    DirectX::XMFLOAT4 m_r_n_flags;

    DirectX::XMFLOAT4 envDiff; // rgb=color, w=intensity
    DirectX::XMFLOAT4 envSpec; // rgb=color, w=intensity

    // x=prefilterMaxMip, yzw=unused (정렬 유지)
    DirectX::XMFLOAT4 envInfo;
};
CB_STATIC_ASSERT_16B(CB_PBRParams);


// ============================================================================
// b9 : Procedural(그리드/워프 등) 파라미터
// ============================================================================
struct CB_Proc
{
    DirectX::XMFLOAT4 uProc1; // x=timeSec, y=cellScale, z=warp1, w=warp2
    DirectX::XMFLOAT4 uProc2; // x=scrollX, y=scrollY, z=gridMix, w=unused
};
CB_STATIC_ASSERT_16B(CB_Proc);


// ============================================================================
// b12 : Deferred Point Lights (Lighting Pass 전용)
// HLSL: cbuffer DeferredLightsCB : register(b12)
// ============================================================================
static constexpr std::uint32_t MAX_POINT_LIGHTS = 8;

struct CB_DeferredLights
{
    DirectX::XMFLOAT4 eyePosW;     // xyz = eye pos, w = 1
    std::uint32_t     meta[4];     // x=numPoint, y=enablePoint, z=falloffMode(0:smooth,1:invSq), w=pad

    DirectX::XMFLOAT4 pointPosRange[MAX_POINT_LIGHTS]; // xyz=pos,  w=range
    DirectX::XMFLOAT4 pointColorInt[MAX_POINT_LIGHTS]; // rgb=color,w=intensity
};
CB_STATIC_ASSERT_16B(CB_DeferredLights);


// ============================================================================
// b13 : Point Shadow (Cube) 파라미터
// HLSL: cbuffer PointShadowCB : register(b13)
// ============================================================================
struct CB_PointShadow
{
    DirectX::XMFLOAT4 posRange; // xyz = light pos, w = range
    DirectX::XMFLOAT4 params;   // x=bias(dist/range), y=enable(0/1), z/w=reserved
};
CB_STATIC_ASSERT_16B(CB_PointShadow);


// ----------------------------------------------------------------------------
// cleanup
// ----------------------------------------------------------------------------
#undef CB_STATIC_ASSERT_16B
