// ============================================================================
// Material.h
// - MaterialCPU/GPU 구조체 및 바인딩 인터페이스
// ============================================================================

// ---- includes ----

#pragma once

#include <string>
#include <d3d11.h>
#include <wrl/client.h>   // ComPtr
#include "MeshDataEx.h"

struct MaterialGPU
{
	MaterialGPU() = default;
	~MaterialGPU() = default;

	MaterialGPU(const MaterialGPU&) = delete;
	MaterialGPU& operator=(const MaterialGPU&) = delete;

	MaterialGPU(MaterialGPU&&) noexcept = default;
	MaterialGPU& operator=(MaterialGPU&&) noexcept = default;

	void Build(ID3D11Device* dev, const MaterialCPU& cpu, const std::wstring& texRoot);
	void Bind(ID3D11DeviceContext* ctx) const;
	static void Unbind(ID3D11DeviceContext* ctx);

	// 텍스처 플래그
	bool hasDiffuse = false;
	bool hasNormal = false;
	bool hasSpecular = false;
	bool hasEmissive = false;
	bool hasOpacity = false;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texDiffuse;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texNormal;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texSpecular;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texEmissive;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texOpacity;

	float baseColor[4] = { 1.f, 1.f, 1.f, 1.f };
	bool  useBaseColor = false;

	Microsoft::WRL::ComPtr<ID3D11Buffer> cbMat;

	// Build 시작할 때 이거 한 번 호출하면 됨
	void ResetAll()
	{
		cbMat.Reset();
		texDiffuse.Reset();
		texNormal.Reset();
		texSpecular.Reset();
		texEmissive.Reset();
		texOpacity.Reset();

		hasDiffuse = hasNormal = hasSpecular = hasEmissive = hasOpacity = false;
		useBaseColor = false;
	}
};
