// Material.cpp
#include "../D3D_Core/pch.h"
#include "Material.h"
#include "../D3D_Core/Helper.h"
#include <filesystem>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11ShaderResourceView> LoadSRV(ID3D11Device* dev, const std::wstring& fullpath)
{
	ComPtr<ID3D11ShaderResourceView> srv;
	if (FAILED(CreateTextureFromFile(dev, fullpath.c_str(), srv.GetAddressOf())))
		srv.Reset();
	return srv;
}

void MaterialGPU::Build(ID3D11Device* dev, const MaterialCPU& cpu, const std::wstring& texRoot)
{
	ResetAll();

	auto join = [&](const std::wstring& f)->std::wstring {
		namespace fs = std::filesystem;
		fs::path file(f);
		if (file.is_absolute()) return file.wstring();
		return (fs::path(texRoot) / file).wstring();
		};

	if (!cpu.diffuse.empty()) { texDiffuse = LoadSRV(dev, join(cpu.diffuse));   hasDiffuse = (texDiffuse.Get() != nullptr); }
	if (!cpu.normal.empty()) { texNormal = LoadSRV(dev, join(cpu.normal));    hasNormal = (texNormal.Get() != nullptr); }
	if (!cpu.specular.empty()) { texSpecular = LoadSRV(dev, join(cpu.specular));  hasSpecular = (texSpecular.Get() != nullptr); }
	if (!cpu.emissive.empty()) { texEmissive = LoadSRV(dev, join(cpu.emissive));  hasEmissive = (texEmissive.Get() != nullptr); }
	if (!cpu.opacity.empty()) { texOpacity = LoadSRV(dev, join(cpu.opacity));   hasOpacity = (texOpacity.Get() != nullptr); }

	// FBX diffuseColor -> baseColor
	baseColor[0] = cpu.diffuseColor[0];
	baseColor[1] = cpu.diffuseColor[1];
	baseColor[2] = cpu.diffuseColor[2];
	baseColor[3] = 1.0f;

	// 디퓨즈 텍스처 없으면 baseColor 쓰는 정책
	useBaseColor = !hasDiffuse;

	if (!cbMat)
	{
		struct CBMat { float baseColor[4]; UINT useBaseColor; UINT pad[3]; };

		D3D11_BUFFER_DESC bd{};
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CBMat);

		HR_T(dev->CreateBuffer(&bd, nullptr, cbMat.GetAddressOf()));
	}
}

void MaterialGPU::Bind(ID3D11DeviceContext* ctx) const
{
	ID3D11ShaderResourceView* srvs[5] = {
		texDiffuse.Get(), texNormal.Get(), texSpecular.Get(), texEmissive.Get(), texOpacity.Get()
	};
	ctx->PSSetShaderResources(0, 5, srvs);

	if (!cbMat) return;

	struct CBMat { float baseColor[4]; UINT useBaseColor; UINT pad[3]; } cb{};
	cb.baseColor[0] = baseColor[0];
	cb.baseColor[1] = baseColor[1];
	cb.baseColor[2] = baseColor[2];
	cb.baseColor[3] = baseColor[3];
	cb.useBaseColor = useBaseColor ? 1u : 0u;

	ctx->UpdateSubresource(cbMat.Get(), 0, nullptr, &cb, 0, 0);

	ID3D11Buffer* b = cbMat.Get();
	ctx->PSSetConstantBuffers(5, 1, &b);
}

void MaterialGPU::Unbind(ID3D11DeviceContext* ctx)
{
	ID3D11ShaderResourceView* nullSRVs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
	ctx->PSSetShaderResources(0, 5, nullSRVs);

	ID3D11Buffer* nullCB = nullptr;
	ctx->PSSetConstantBuffers(5, 1, &nullCB);
}
