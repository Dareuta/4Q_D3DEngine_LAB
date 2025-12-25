// ============================================================================
// MeshDataEx.h
// - 메쉬 데이터 구조: Vertex/SubMesh/Range/Material 등
// ============================================================================

// ---- includes ----

#pragma once

#include <vector>
#include <cstdint>
#include <string>

struct VertexCPU_PNTT {
	float px, py, pz;
	float nx, ny, nz;
	float u, v;
	float tx, ty, tz, tw;
};

struct VertexCPU_PNTT_BW {
	float px, py, pz;
	float nx, ny, nz;
	float u, v;
	float tx, ty, tz, tw; // handedness
	uint8_t  bi[4];       // Bone indices (0~255)
	float    bw[4];       // Bone weights (정규화)
};

struct SubMeshCPU {
	uint32_t baseVertex = 0, indexStart = 0, indexCount = 0, materialIndex = 0;
};

struct MaterialCPU {
	std::wstring diffuse, normal, specular, emissive, opacity;
	float diffuseColor[3] = { 1,1,1 };
};

struct MeshData_PNTT {
	std::vector<VertexCPU_PNTT> vertices;
	std::vector<uint32_t> indices;
	std::vector<SubMeshCPU> submeshes;
	std::vector<MaterialCPU> materials;
};
