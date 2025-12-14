#pragma once

#include <vector>
#include "SkinnedMesh.h"
#include "Material.h"

// FBX 한 개 = 여러 파트(메쉬들) + 공용 머티리얼 테이블
struct SkinnedMeshPartResource
{
    SkinnedMesh mesh;
};

class SkinnedModelResource
{
public:
    SkinnedModelResource() = default;

    SkinnedModelResource(std::vector<SkinnedMeshPartResource>&& parts,
        std::vector<MaterialGPU>&& materials)
        : m_parts(std::move(parts)), m_materials(std::move(materials))
    {
    }

    SkinnedModelResource(const SkinnedModelResource&) = delete;
    SkinnedModelResource& operator=(const SkinnedModelResource&) = delete;
    SkinnedModelResource(SkinnedModelResource&&) noexcept = default;
    SkinnedModelResource& operator=(SkinnedModelResource&&) noexcept = default;

    const std::vector<SkinnedMeshPartResource>& GetParts() const { return m_parts; }
    std::vector<SkinnedMeshPartResource>& GetParts() { return m_parts; }

    const std::vector<MaterialGPU>& GetMaterials() const { return m_materials; }
    std::vector<MaterialGPU>& GetMaterials() { return m_materials; }

    bool Empty() const { return m_parts.empty(); }

private:
    std::vector<SkinnedMeshPartResource> m_parts;
    std::vector<MaterialGPU> m_materials;
};
