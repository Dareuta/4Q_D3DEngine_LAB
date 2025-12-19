// RenderSharedCB.h
#pragma once

#include <cstdint>
#include <directxtk/SimpleMath.h>

namespace RenderCB
{
	using Matrix = DirectX::SimpleMath::Matrix;
	using Vector4 = DirectX::SimpleMath::Vector4;

	// =========================================================
	// b0: Per-object (World/View/Proj + 라이트 방향/색)
	// HLSL: cbuffer PerObject : register(b0)
	// =========================================================
	struct PerObject
	{
		Matrix mWorld;
		Matrix mView;
		Matrix mProjection;
		Matrix mWorldInvTranspose;

		Vector4 vLightDir;
		Vector4 vLightColor;
	};

	// =========================================================
	// b1: Blinn-Phong 재질/조명 파라미터
	// HLSL: cbuffer BlinnPhongCB : register(b1)
	// =========================================================
	struct BlinnPhong
	{
		Vector4 EyePosW;   // (ex,ey,ez,1)
		Vector4 kA;        // (ka.r,ka.g,ka.b,0)
		Vector4 kSAlpha;   // (ks, alpha, 0, 0)
		Vector4 I_ambient; // (Ia.r,Ia.g,Ia.b,0)
	};

	// =========================================================
	// b2: 텍스처 사용 플래그 + 알파컷
	// HLSL: cbuffer UseCB : register(b2)
	// =========================================================
	struct Use
	{
		std::uint32_t useDiffuse;
		std::uint32_t useNormal;
		std::uint32_t useSpecular;
		std::uint32_t useEmissive;

		std::uint32_t useOpacity;
		float         alphaCut;
		float         pad[2];   // 16B 정렬 맞춤
	};

	// =========================================================
	// b6: ShadowCB – LightViewProj + 그림자 파라미터
	// HLSL: cbuffer ShadowCB : register(b6)
	// =========================================================
	struct Shadow
	{
		Matrix  LVP;     // LightViewProj (transpose 여부는 C++ 쪽 처리)
		Vector4 Params;  // x: compareBias, y: 1/width, z: 1/height, w: reserve
	};

	// =========================================================
	// b7: ToonCB – Ramp/Toon 파라미터
	// HLSL: cbuffer ToonCB : register(b7)
	// =========================================================
	struct Toon
	{
		std::uint32_t useToon;
		std::uint32_t halfLambert;
		float         specStep;
		float         specBoost;
		float         shadowMin;
		float         pad0, pad1, pad2; // 16B 정렬
	};
}

// -------------------------------------------------------------
// 기존 이름과의 호환용 alias (원하는 만큼만 써도 됨)
// -------------------------------------------------------------
using ConstantBuffer = RenderCB::PerObject;
using BlinnPhongCB = RenderCB::BlinnPhong;
using UseCB = RenderCB::Use;
using ShadowCB = RenderCB::Shadow;
using ToonCB_ = RenderCB::Toon;

struct CB_PBRParams
{
	UINT useBaseColorTex;
	UINT useNormalTex;
	UINT useMetalTex;
	UINT useRoughTex;

	DirectX::XMFLOAT4 baseColorOverride; // rgb
	DirectX::XMFLOAT4 m_r_n_flags;       // x=metallic, y=roughness, z=normalStrength, w=flipNormalY(0/1)
};
static_assert((sizeof(CB_PBRParams) % 16) == 0);


struct CB_Proc
{
	DirectX::XMFLOAT4 uProc1; // x=timeSec, y=cellScale, z=warp1, w=warp2
	DirectX::XMFLOAT4 uProc2; // x=scrollX, y=scrollY, z=gridMix, w=unused
};
static_assert((sizeof(CB_Proc) % 16) == 0);