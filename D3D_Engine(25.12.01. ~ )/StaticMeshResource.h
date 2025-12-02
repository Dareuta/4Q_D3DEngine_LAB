#pragma once

#include <vector>

#include "StaticMesh.h"
#include "Material.h"

// FBX 하나에 대한 정적 메쉬 + 머티리얼 묶음
class StaticMeshResource
{
public:
	StaticMeshResource() = default;

	// FBX 로딩 후 한 번에 만들어 쓸 생성자
	StaticMeshResource(StaticMesh&& mesh,
		std::vector<MaterialGPU>&& materials) : m_mesh(std::move(mesh))
		, m_materials(std::move(materials)) {

		//아무것도 없지롱

	}

	// 복사는 막고, 이동만 허용 (MaterialGPU가 원래 복사 금지라 자연스럽게 맞음)
	StaticMeshResource(const StaticMeshResource&) = delete;
	StaticMeshResource& operator=(const StaticMeshResource&) = delete;
	StaticMeshResource(StaticMeshResource&&) noexcept = default;
	StaticMeshResource& operator=(StaticMeshResource&&) noexcept = default;

	// 나중에 ResourceManager에서 쓸 접근용 헬퍼들
	const StaticMesh& GetMesh()      const { return m_mesh; }
	StaticMesh& GetMesh() { return m_mesh; }

	const std::vector<MaterialGPU>& GetMaterials() const { return m_materials; }
	std::vector<MaterialGPU>& GetMaterials() { return m_materials; }

private:
	StaticMesh               m_mesh;
	std::vector<MaterialGPU> m_materials;
};
