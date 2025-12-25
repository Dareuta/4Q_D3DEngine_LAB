// ============================================================================
// SkinnedMesh.h
// - SkinnedMesh 선언
// ============================================================================

// ---- includes ----

#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

#include "MeshDataEx.h"

class SkinnedMesh {
public:
    bool Build(ID3D11Device* dev,
        const std::vector<VertexCPU_PNTT_BW>& vtx,
        const std::vector<uint32_t>& idx,
        const std::vector<SubMeshCPU>& submeshes);
    void DrawSubmesh(ID3D11DeviceContext* ctx, size_t smIdx) const;
    const std::vector<SubMeshCPU>& Ranges() const { return mRanges; }
    UINT Stride() const { return mStride; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> mVB, mIB;
    UINT mStride = sizeof(VertexCPU_PNTT_BW);
    std::vector<SubMeshCPU> mRanges;
};
