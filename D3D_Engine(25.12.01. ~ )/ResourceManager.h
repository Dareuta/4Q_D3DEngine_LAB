// ResourceManager.h
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <functional>

struct ID3D11Device;

class Texture2DResource;
class StaticMeshResource;
class SkinnedModelResource;

enum class TextureColorSpace : uint8_t
{
    SRGB = 0,
    Linear = 1
};

struct TextureKey
{
    std::wstring path;          // 정규화된 전체 경로
    TextureColorSpace cs = TextureColorSpace::SRGB;
};

struct TextureKeyHash
{
    size_t operator()(const TextureKey& k) const noexcept
    {
        size_t h1 = std::hash<std::wstring>{}(k.path);
        size_t h2 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.cs));
        // hash combine
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};
struct TextureKeyEq
{
    bool operator()(const TextureKey& a, const TextureKey& b) const noexcept
    {
        return a.cs == b.cs && a.path == b.path;
    }
};

struct ModelKey
{
    std::wstring fbxPath;   // 정규화된 FBX 전체 경로
    std::wstring texRoot;   // 정규화된 텍스처 루트(디렉토리)
};

struct ModelKeyHash
{
    size_t operator()(const ModelKey& k) const noexcept
    {
        size_t h1 = std::hash<std::wstring>{}(k.fbxPath);
        size_t h2 = std::hash<std::wstring>{}(k.texRoot);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};
struct ModelKeyEq
{
    bool operator()(const ModelKey& a, const ModelKey& b) const noexcept
    {
        return a.fbxPath == b.fbxPath && a.texRoot == b.texRoot;
    }
};

class ResourceManager final
{
public:
    static ResourceManager& Instance();

    void Initialize(ID3D11Device* device);
    void Shutdown();

    // 기존 호출들 깨지지 않게 유지(기본 SRGB)
    std::shared_ptr<Texture2DResource>
        LoadTexture2D(const std::wstring& path)
    {
        return LoadTexture2D(path, TextureColorSpace::SRGB);
    }

    // 새 오버로드(캐시 키 분리 가능)
    std::shared_ptr<Texture2DResource>
        LoadTexture2D(const std::wstring& path, TextureColorSpace cs);

    std::shared_ptr<StaticMeshResource>
        LoadStaticMesh(const std::wstring& fbxPath, const std::wstring& texDir);

    std::shared_ptr<SkinnedModelResource>
        LoadSkinnedModel(const std::wstring& fbxPath, const std::wstring& texDir);

private:
    ResourceManager() = default;

    ID3D11Device* m_device = nullptr;

    using TexCache = std::unordered_map<TextureKey, std::weak_ptr<Texture2DResource>, TextureKeyHash, TextureKeyEq>;
    using StaticMeshCache = std::unordered_map<ModelKey, std::weak_ptr<StaticMeshResource>, ModelKeyHash, ModelKeyEq>;
    using SkinnedMeshCache = std::unordered_map<ModelKey, std::weak_ptr<SkinnedModelResource>, ModelKeyHash, ModelKeyEq>;

    TexCache         m_texCache;
    StaticMeshCache  m_staticCache;
    SkinnedMeshCache m_skinnedCache;
};
