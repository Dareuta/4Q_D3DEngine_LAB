// ============================================================================
// StaticMesh.h
// - StaticMesh 선언
// ============================================================================

// ---- includes ----

#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include "MeshDataEx.h"

class StaticMesh {
public:
    bool Build(ID3D11Device* dev, const MeshData_PNTT& src);
    void DrawSubmesh(ID3D11DeviceContext* ctx, size_t smIdx) const;

    struct Range { UINT indexStart, indexCount, materialIndex; };
    const std::vector<Range>& Ranges() const { return mRanges; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> mVB, mIB;
    UINT mStride = sizeof(VertexCPU_PNTT);
    std::vector<Range> mRanges;
};
