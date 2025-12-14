// Material.cpp
#include "../D3D_Core/pch.h"
#include "Material.h"

#include <algorithm>

#include "ResourceManager.h"      // ResourceManager::LoadTexture2D
#include "Texture2DResource.h"
#include <filesystem>

// 텍스처 로딩은 전부 ResourceManager를 통해 진행
void MaterialGPU::Build(ID3D11Device* dev, const MaterialCPU& cpu, const std::wstring& texRoot)
{
    ReleaseAll();

    auto join = [&](const std::wstring& f)->std::wstring
        {
            namespace fs = std::filesystem;
            fs::path file(f);

            // f가 절대경로면 그대로 사용
            if (file.is_absolute())
                return file.wstring();

            fs::path root(texRoot);
            return (root / file).wstring();
        };

    auto& rm = ResourceManager::Instance();

    if (!cpu.diffuse.empty())
    {
        texDiffuse = rm.LoadTexture2D(join(cpu.diffuse));
        hasDiffuse = (texDiffuse != nullptr);
    }
    if (!cpu.normal.empty())
    {
        texNormal = rm.LoadTexture2D(join(cpu.normal));
        hasNormal = (texNormal != nullptr);
    }
    if (!cpu.specular.empty())
    {
        texSpecular = rm.LoadTexture2D(join(cpu.specular));
        hasSpecular = (texSpecular != nullptr);
    }
    if (!cpu.emissive.empty())
    {
        texEmissive = rm.LoadTexture2D(join(cpu.emissive));
        hasEmissive = (texEmissive != nullptr);
    }
    if (!cpu.opacity.empty())
    {
        texOpacity = rm.LoadTexture2D(join(cpu.opacity));
        hasOpacity = (texOpacity != nullptr);
    }

    // FBX diffuseColor -> baseColor
    baseColor[0] = cpu.diffuseColor[0];
    baseColor[1] = cpu.diffuseColor[1];
    baseColor[2] = cpu.diffuseColor[2];
    baseColor[3] = 1.0f;

    // 디퓨즈 맵이 없으면 FBX 색을 사용
    useBaseColor = !hasDiffuse;

    // PS b5용 상수버퍼 (한번만 만들어도 되지만, 구현 단순하게 매 Build마다 체크)
    struct CBMat
    {
        float baseColor[4];
        UINT  useBaseColor;
        UINT  pad[3];
    };

    if (!cbMat)
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CBMat);
        HRESULT hr = dev->CreateBuffer(&bd, nullptr, &cbMat);
        if (FAILED(hr) || !cbMat)
            throw std::runtime_error("MaterialGPU::Build - CreateBuffer failed.");
    }
}

void MaterialGPU::Bind(ID3D11DeviceContext* ctx) const
{
    // t0~t4 에 텍스처 바인딩
    ID3D11ShaderResourceView* srvs[5] =
    {
        texDiffuse ? texDiffuse->GetSRV() : nullptr,
        texNormal ? texNormal->GetSRV() : nullptr,
        texSpecular ? texSpecular->GetSRV() : nullptr,
        texEmissive ? texEmissive->GetSRV() : nullptr,
        texOpacity ? texOpacity->GetSRV() : nullptr,
    };
    ctx->PSSetShaderResources(0, 5, srvs);

    if (!cbMat)
        return;

    // b5 업데이트
    struct CBMat
    {
        float baseColor[4];
        UINT  useBaseColor;
        UINT  pad[3];
    } cb{};

    cb.baseColor[0] = baseColor[0];
    cb.baseColor[1] = baseColor[1];
    cb.baseColor[2] = baseColor[2];
    cb.baseColor[3] = baseColor[3];
    cb.useBaseColor = useBaseColor ? 1u : 0u;

    ctx->UpdateSubresource(cbMat, 0, nullptr, &cb, 0, 0);
    ctx->PSSetConstantBuffers(5, 1, &cbMat);
}

void MaterialGPU::Unbind(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* nulls[5] =
    {
        nullptr, nullptr, nullptr, nullptr, nullptr
    };
    ctx->PSSetShaderResources(0, 5, nulls);
}

MaterialGPU::~MaterialGPU()
{
    ReleaseAll();
}
