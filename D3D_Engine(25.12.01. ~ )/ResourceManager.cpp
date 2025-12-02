// ResourceManager.cpp
#include "../D3D_Core/pch.h"
#include "ResourceManager.h"

#include "../D3D_Core/Helper.h"              // CreateTextureFromFile
#include "Texture2DResource.h"
#include "StaticMeshResource.h"
#include "SkinnedModelResource.h"

// Assimp + MeshData 쪽은 Resource 클래스 내부에서 이미 include 했다면
// 여기서까지는 굳이 안 써도 됨. (StaticMeshResource/SkinnedModelResource가 처리)
// 필요하면 아래 주석 풀어서 사용해도 됨.
#include "AssimpImporterEX.h"
#include "MeshDataEx.h"
#include "StaticMesh.h"
#include "SkinnedMesh.h"
#include "Material.h"

ResourceManager& ResourceManager::Instance()
{
	static ResourceManager s_instance;
	return s_instance;
}

void ResourceManager::Initialize(ID3D11Device* device)
{
	if (m_device && m_device != device)
		throw std::runtime_error("ResourceManager::Initialize called twice with different devices.");
	m_device = device;
}

void ResourceManager::Shutdown()
{
	m_texCache.clear();
	m_staticCache.clear();
	m_skinnedCache.clear();

	m_device = nullptr;
}

std::wstring ResourceManager::MakeKey(const std::wstring& a,
	const std::wstring& b)
{
	// 단순 문자열 합치기. 필요하면 나중에 더 튼튼하게 만들면 됨.
	std::wstring key = a;
	key.push_back(L'|');
	key.append(b);
	return key;
}

// ---------------------------------------------------------
// 1) Texture2D
// ---------------------------------------------------------
std::shared_ptr<Texture2DResource>
ResourceManager::LoadTexture2D(const std::wstring& path)
{
	if (!m_device)
		throw std::runtime_error("ResourceManager::LoadTexture2D - not initialized.");

	// 이미 있는지 확인
	{
		auto it = m_texCache.find(path);
		if (it != m_texCache.end())
		{
			if (auto sp = it->second.lock())
				return sp;      // 살아있으면 재사용
			m_texCache.erase(it); // 죽은 weak_ptr은 정리
		}
	}

	// 새로 로드
	ComPtr<ID3D11ShaderResourceView> srv;
	HRESULT hr = CreateTextureFromFile(m_device, path.c_str(), srv.GetAddressOf());
	if (FAILED(hr))
	{
		// 여기서 그냥 nullptr 리턴할건지, 예외 던질건지 정책은 네가 결정하면 됨.
		// 일단 강하게 예외로 던지게 해둠.
		throw std::runtime_error("ResourceManager::LoadTexture2D - failed to load texture.");
	}

	// width/height 알아내기 (원하면)
	UINT width = 0, height = 0;
	{
		ComPtr<ID3D11Resource> res;
		srv->GetResource(res.GetAddressOf());
		if (res)
		{
			ComPtr<ID3D11Texture2D> tex2D;
			if (SUCCEEDED(res.As(&tex2D)))
			{
				D3D11_TEXTURE2D_DESC desc{};
				tex2D->GetDesc(&desc);
				width = desc.Width;
				height = desc.Height;
			}
		}
	}

	// Texture2DResource 쪽 설계에 맞게 생성자 파라미터 맞춰라.
	// (우리가 앞에서 만든 버전 기준: (ID3D11ShaderResourceView*, unsigned, unsigned))
	auto texRes = std::make_shared<Texture2DResource>(srv.Detach(), width, height);

	m_texCache[path] = texRes;
	return texRes;
}

// ---------------------------------------------------------
// 2) StaticMesh + Materials
// ---------------------------------------------------------
std::shared_ptr<StaticMeshResource>
ResourceManager::LoadStaticMesh(const std::wstring& fbxPath,
	const std::wstring& texDir)
{
	if (!m_device)
		throw std::runtime_error("ResourceManager::LoadStaticMesh - not initialized.");

	const std::wstring key = MakeKey(fbxPath, texDir);

	// 캐시 확인
	{
		auto it = m_staticCache.find(key);
		if (it != m_staticCache.end())
		{
			if (auto sp = it->second.lock())
				return sp;
			m_staticCache.erase(it);
		}
	}

	// ----- 실제 FBX 로딩 + GPU 빌드 -----
	MeshData_PNTT cpu;
	if (!AssimpImporterEx::LoadFBX_PNTT_AndMaterials(
		fbxPath, cpu, /*flipUV*/true, /*leftHanded*/true))
	{
		throw std::runtime_error("ResourceManager::LoadStaticMesh - FBX load failed.");
	}

	StaticMesh mesh;
	if (!mesh.Build(m_device, cpu))
	{
		throw std::runtime_error("ResourceManager::LoadStaticMesh - mesh build failed.");
	}

	std::vector<MaterialGPU> materials(cpu.materials.size());
	for (size_t i = 0; i < cpu.materials.size(); ++i)
	{
		materials[i].Build(m_device, cpu.materials[i], texDir);
	}

	auto res = std::make_shared<StaticMeshResource>(
		std::move(mesh),
		std::move(materials));

	m_staticCache[key] = res;
	return res;
}
// ---------------------------------------------------------
// 3) SkinnedModel + Materials
// ---------------------------------------------------------
std::shared_ptr<SkinnedModelResource>
ResourceManager::LoadSkinnedModel(const std::wstring& fbxPath,
	const std::wstring& texDir)
{
	if (!m_device)
		throw std::runtime_error("ResourceManager::LoadSkinnedModel - not initialized.");

	const std::wstring key = MakeKey(fbxPath, texDir);

	// 캐시 확인 (혹시 나중에 진짜 구현할 때를 대비해서 남겨둠)
	{
		auto it = m_skinnedCache.find(key);
		if (it != m_skinnedCache.end())
		{
			if (auto sp = it->second.lock())
				return sp;
			m_skinnedCache.erase(it);
		}
	}

	// TODO: 나중에 SkinnedMeshPartResource 벡터를 채워서
	//   auto res = std::make_shared<SkinnedModelResource>(std::move(parts));
	//   m_skinnedCache[key] = res;
	//   return res;
	//
	// 지금은 스키닝 리소스를 안 쓰므로, 잘못 호출되면 바로 티 나게 예외만 던져둠.

	throw std::runtime_error("ResourceManager::LoadSkinnedModel - not implemented yet.");
}
