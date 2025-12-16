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

#include <filesystem>
#include <algorithm>
#include <cwctype>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace
{
    std::wstring NormalizePath(const std::wstring& in)
    {
        namespace fs = std::filesystem;
        fs::path p(in);

        try
        {
            p = fs::absolute(p);
            p = fs::weakly_canonical(p);
        }
        catch (...)
        {
            p = fs::absolute(p);
        }

        // generic_wstring() => 슬래시 통일(/)
        std::wstring out = p.generic_wstring();

#if defined(_WIN32)
        // Windows는 대소문자 무시가 일반적이라 키도 통일(캐시 중복 방지)
        std::transform(out.begin(), out.end(), out.begin(),
            [](wchar_t c) { return (wchar_t)towlower(c); });
#endif

        return out;
    }

    std::wstring ResolveTexRoot(const std::wstring& fbxPath, const std::wstring& texDir)
    {
        namespace fs = std::filesystem;
        fs::path root = texDir.empty() ? fs::path(fbxPath).parent_path() : fs::path(texDir);
        // 디렉토리로 취급(끝이 파일명이어도 그냥 넘어가는데, 여기선 사용자가 디렉토리 준다고 가정)
        return NormalizePath(root.wstring());
    }
}


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

// ---------------------------------------------------------
// 1) Texture2D
// ---------------------------------------------------------
std::shared_ptr<Texture2DResource>
ResourceManager::LoadTexture2D(const std::wstring& path, TextureColorSpace cs)
{
    if (!m_device)
        throw std::runtime_error("ResourceManager::LoadTexture2D - not initialized.");

    TextureKey key{ NormalizePath(path), cs };

    // 캐시 확인
    {
        auto it = m_texCache.find(key);
        if (it != m_texCache.end())
        {
            if (auto sp = it->second.lock())
                return sp;
            m_texCache.erase(it);
        }
    }

    // 새로 로드
    ComPtr<ID3D11ShaderResourceView> srv;
    HRESULT hr = CreateTextureFromFile(m_device, key.path.c_str(), srv.GetAddressOf());
    if (FAILED(hr))
    {
        std::string msg = "ResourceManager::LoadTexture2D - failed: ";
        msg.append(std::string(key.path.begin(), key.path.end()));
        throw std::runtime_error(msg);
    }

    // width/height 추출(기존 코드 그대로 두되 srv 기반)
    ComPtr<ID3D11Resource> res;
    srv->GetResource(res.GetAddressOf());

    ComPtr<ID3D11Texture2D> tex2d;
    res.As(&tex2d);

    D3D11_TEXTURE2D_DESC td{};
    tex2d->GetDesc(&td);

    auto sp = std::make_shared<Texture2DResource>(srv.Detach(), td.Width, td.Height);
    m_texCache[key] = sp;
    return sp;
}

// ---------------------------------------------------------
// 2) StaticMesh + Materials
// ---------------------------------------------------------

std::shared_ptr<StaticMeshResource>
ResourceManager::LoadStaticMesh(const std::wstring& fbxPath, const std::wstring& texDir)
{
    if (!m_device)
        throw std::runtime_error("ResourceManager::LoadStaticMesh - not initialized.");

    ModelKey key{ NormalizePath(fbxPath), ResolveTexRoot(fbxPath, texDir) };

    {
        auto it = m_staticCache.find(key);
        if (it != m_staticCache.end())
        {
            if (auto sp = it->second.lock())
                return sp;
            m_staticCache.erase(it);
        }
    }

    MeshData_PNTT cpu;
    if (!AssimpImporterEx::LoadFBX_PNTT_AndMaterials(key.fbxPath, cpu, false, true))
        throw std::runtime_error("ResourceManager::LoadStaticMesh - Assimp load failed.");
    
    StaticMesh mesh;
    if (!mesh.Build(m_device, cpu))
        throw std::runtime_error("ResourceManager::LoadStaticMesh - build failed.");

    std::vector<MaterialGPU> materials(cpu.materials.size());
    for (size_t i = 0; i < cpu.materials.size(); ++i)
        materials[i].Build(m_device, cpu.materials[i], key.texRoot);

    auto res = std::make_shared<StaticMeshResource>(std::move(mesh), std::move(materials));
    m_staticCache[key] = res;
    return res;
}


// ---------------------------------------------------------
// 3) SkinnedModel + Materials
// ---------------------------------------------------------
std::shared_ptr<SkinnedModelResource>
ResourceManager::LoadSkinnedModel(const std::wstring& fbxPath, const std::wstring& texDir)
{
    if (!m_device)
        throw std::runtime_error("ResourceManager::LoadSkinnedModel - not initialized.");

    ModelKey key{ NormalizePath(fbxPath), ResolveTexRoot(fbxPath, texDir) };

    {
        auto it = m_skinnedCache.find(key);
        if (it != m_skinnedCache.end())
        {
            if (auto sp = it->second.lock())
                return sp;
            m_skinnedCache.erase(it);
        }
    }

    Assimp::Importer imp;
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    imp.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

    unsigned flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace |
        aiProcess_LimitBoneWeights |
        aiProcess_ConvertToLeftHanded |
        aiProcess_FlipUVs;

    const std::string fbxPathA(key.fbxPath.begin(), key.fbxPath.end());
    const aiScene* sc = imp.ReadFile(fbxPathA.c_str(), flags);
    if (!sc || !sc->mRootNode)
        throw std::runtime_error("ResourceManager::LoadSkinnedModel - Assimp load failed.");

    // 1) 재질 CPU 추출
    std::vector<MaterialCPU> sceneMaterials;
    AssimpImporterEx::ExtractMaterials(sc, sceneMaterials);

    // 2) 재질 GPU는 "한 번만" 빌드
    std::vector<MaterialGPU> materials(sceneMaterials.size());
    for (size_t i = 0; i < sceneMaterials.size(); ++i)
        materials[i].Build(m_device, sceneMaterials[i], key.texRoot);

    // 3) 파트 빌드
    std::vector<SkinnedMeshPartResource> parts;
    parts.reserve(sc->mNumMeshes ? sc->mNumMeshes : 1);

    std::unordered_map<std::string, int> boneNameToIndex;
    int nextBoneIndex = 0;

    struct Influences
    {
        std::vector<std::pair<int, float>> inf;

        void add(int b, float w)
        {
            if (w <= 0.0f) return;
            inf.emplace_back(b, w);
        }

        void finalize(uint8_t bi[4], float bw[4]) const
        {
            if (inf.empty())
            {
                bi[0] = 0; bw[0] = 1.0f;
                for (int i = 1; i < 4; ++i) { bi[i] = 0; bw[i] = 0.0f; }
                return;
            }

            // 큰 가중치 우선
            std::vector<std::pair<int, float>> tmp = inf;
            std::sort(tmp.begin(), tmp.end(),
                [](auto& a, auto& b) { return a.second > b.second; });

            float sum = 0.0f;
            int count = (int)std::min<size_t>(4, tmp.size());
            for (int i = 0; i < count; ++i) sum += tmp[i].second;
            if (sum <= 0.0f) sum = 1.0f;

            for (int i = 0; i < 4; ++i)
            {
                if (i < count)
                {
                    if (tmp[i].first > 255)
                        throw std::runtime_error("LoadSkinnedModel - bone index overflow (vertex uses uint8).");

                    bi[i] = (uint8_t)tmp[i].first;
                    bw[i] = tmp[i].second / sum;
                }
                else
                {
                    bi[i] = 0;
                    bw[i] = 0.0f;
                }
            }
        }
    };

    auto buildPartFromAiMesh =
        [&](unsigned meshIndex) -> SkinnedMeshPartResource
        {
            const aiMesh* am = sc->mMeshes[meshIndex];
            if (!am)
                throw std::runtime_error("LoadSkinnedModel - aiMesh is null.");

            std::vector<VertexCPU_PNTT_BW> vtx(am->mNumVertices);
            std::vector<uint32_t> idx;
            idx.reserve(am->mNumFaces * 3);

            std::vector<SubMeshCPU> submeshes;
            submeshes.push_back({ 0, 0, static_cast<uint32_t>(am->mNumFaces * 3), am->mMaterialIndex });

            // 기본 버텍스 데이터
            for (unsigned v = 0; v < am->mNumVertices; ++v)
            {
                auto& o = vtx[v];

                o.px = am->mVertices[v].x;
                o.py = am->mVertices[v].y;
                o.pz = am->mVertices[v].z;

                if (am->HasNormals())
                {
                    o.nx = am->mNormals[v].x;
                    o.ny = am->mNormals[v].y;
                    o.nz = am->mNormals[v].z;
                }
                else { o.nx = 0; o.ny = 1; o.nz = 0; }

                if (am->HasTextureCoords(0))
                {
                    o.u = am->mTextureCoords[0][v].x;
                    o.v = am->mTextureCoords[0][v].y;
                }
                else { o.u = 0; o.v = 0; }

                // tangent
                if (am->HasTangentsAndBitangents())
                {
                    aiVector3D t = am->mTangents[v];
                    aiVector3D b = am->mBitangents[v];
                    aiVector3D n = am->HasNormals() ? am->mNormals[v] : aiVector3D(0, 1, 0);

                    aiVector3D c(
                        n.y * t.z - n.z * t.y,
                        n.z * t.x - n.x * t.z,
                        n.x * t.y - n.y * t.x
                    );
                    float sign = (c.x * b.x + c.y * b.y + c.z * b.z) < 0.0f ? -1.0f : 1.0f;

                    o.tx = t.x; o.ty = t.y; o.tz = t.z; o.tw = sign;
                }
                else
                {
                    o.tx = 1; o.ty = 0; o.tz = 0; o.tw = 1;
                }

                // bone init
                for (int i = 0; i < 4; ++i) { o.bi[i] = 0; o.bw[i] = 0.0f; }
            }

            // 인덱스
            for (unsigned f = 0; f < am->mNumFaces; ++f)
            {
                const aiFace& face = am->mFaces[f];
                if (face.mNumIndices != 3) continue;
                idx.push_back(face.mIndices[0]);
                idx.push_back(face.mIndices[1]);
                idx.push_back(face.mIndices[2]);
            }

            // 본 영향 수집
            std::vector<Influences> influences(am->mNumVertices);

            for (unsigned b = 0; b < am->mNumBones; ++b)
            {
                const aiBone* bone = am->mBones[b];
                if (!bone) continue;

                std::string bName = bone->mName.C_Str();

                int boneIndex = -1;
                auto it = boneNameToIndex.find(bName);
                if (it == boneNameToIndex.end())
                {
                    if (nextBoneIndex >= 256)
                        throw std::runtime_error("LoadSkinnedModel - too many bones (vertex uses uint8, max 256).");

                    boneIndex = nextBoneIndex++;
                    boneNameToIndex.emplace(bName, boneIndex);
                }
                else
                {
                    boneIndex = it->second;
                }

                for (unsigned w = 0; w < bone->mNumWeights; ++w)
                {
                    const aiVertexWeight& vw = bone->mWeights[w];
                    if (vw.mVertexId < influences.size())
                        influences[vw.mVertexId].add(boneIndex, vw.mWeight);
                }
            }

            // 4개로 정리 + 정규화
            for (unsigned v = 0; v < am->mNumVertices; ++v)
                influences[v].finalize(vtx[v].bi, vtx[v].bw);

            SkinnedMesh mesh;
            if (!mesh.Build(m_device, vtx, idx, submeshes))
                throw std::runtime_error("LoadSkinnedModel - SkinnedMesh build failed.");

            SkinnedMeshPartResource part;
            part.mesh = std::move(mesh);
            return part;
        };

    for (unsigned m = 0; m < sc->mNumMeshes; ++m)
        parts.push_back(buildPartFromAiMesh(m));

    auto res = std::make_shared<SkinnedModelResource>(std::move(parts), std::move(materials));
    m_skinnedCache[key] = res;
    return res;
}
