// ============================================================================
// Material.cpp
// - MaterialGPU 구현: 텍스처 경로 해결 + SRV/CB 바인딩
// ============================================================================

// ---- includes ----

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
		if (f.empty()) return L"";

		fs::path p(f);

		// FBX embedded texture (*0 같은 형태)면 지금 엔진 구조에선 포기
		if (!f.empty() && f[0] == L'*')
			return L"";

		auto exists = [](const fs::path& x)->bool {
			std::error_code ec;
			return !x.empty() && fs::exists(x, ec);
			};

		fs::path root(texRoot);

		// 1) 절대경로가 실제로 존재하면 그대로 사용
		if (p.is_absolute() && exists(p))
			return p.wstring();

		// 2) 절대경로인데 존재 안 하면: 파일명만 떼서 texRoot에서 찾기
		// (다른 PC에서 export된 FBX 절대경로 대응)
		if (p.is_absolute()) {
			fs::path c1 = root / p.filename();
			if (exists(c1)) return c1.wstring();

			// "/Textures/a.png" 같은 "루트 붙은 경로"는 relative_path로 뽑아서 texRoot에 붙여보기
			fs::path c2 = root / p.relative_path();
			if (exists(c2)) return c2.wstring();

			// 마지막: 어차피 실패할 거지만 에러 메시지에 뜨게 경로 반환
			return c1.wstring();
		}

		// 3) 상대경로: texRoot/상대경로
		fs::path c = root / p;
		if (exists(c)) return c.wstring();

		// 4) 상대경로인데 서브폴더가 깨진 케이스 대비: 파일명만으로도 찾아보기
		fs::path c3 = root / p.filename();
		if (exists(c3)) return c3.wstring();

		return c.wstring();
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
