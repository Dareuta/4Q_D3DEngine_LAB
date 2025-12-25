// ============================================================================
// AssimpImporterEX.h
// - AssimpImporterEx 선언: MeshData_PNTT/MaterialCPU 로딩 인터페이스
// ============================================================================

// ---- includes ----

#pragma once
#include <string>
#include "MeshDataEx.h"

// Assimp 전방 선언(헤더 의존 최소화)
struct aiScene;
struct aiMesh;

class AssimpImporterEx {
public:
    static bool LoadFBX_PNTT_AndMaterials(
        const std::wstring& path,
        MeshData_PNTT& out,
        bool flipUV = false,
        bool leftHanded = true);

    static void ConvertAiMeshToPNTT(const aiMesh* am, MeshData_PNTT& out);

    static void ExtractMaterials(const aiScene* sc, std::vector<MaterialCPU>& out);
};
