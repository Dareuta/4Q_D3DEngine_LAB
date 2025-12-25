// ============================================================================
// StaticMesh.cpp
// - StaticMesh 구현: 정적 메쉬 GPU 빌드/드로우 + 서브메쉬 범위
// ============================================================================

// ---- includes ----

#include "../D3D_Core/pch.h"
#include "StaticMesh.h"

bool StaticMesh::Build(ID3D11Device* dev, const MeshData_PNTT& src)
{
	assert(!src.vertices.empty());
	assert(!src.indices.empty());

    D3D11_BUFFER_DESC vb{};

    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb.ByteWidth = (UINT)(src.vertices.size() * sizeof(VertexCPU_PNTT));
    vb.Usage = D3D11_USAGE_IMMUTABLE;

    D3D11_SUBRESOURCE_DATA vsd{ src.vertices.data(),0,0 };
    if (FAILED(dev->CreateBuffer(&vb, &vsd, mVB.GetAddressOf()))) return false;

    D3D11_BUFFER_DESC ib{};
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ib.ByteWidth = (UINT)(src.indices.size() * sizeof(uint32_t));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA isd{ src.indices.data(),0,0 };
    if (FAILED(dev->CreateBuffer(&ib, &isd, mIB.GetAddressOf()))) return false;

    mRanges.clear(); mRanges.reserve(src.submeshes.size());
    for (auto& sm : src.submeshes)
        mRanges.push_back({ sm.indexStart, sm.indexCount, sm.materialIndex });
    return true;
}

void StaticMesh::DrawSubmesh(ID3D11DeviceContext* ctx, size_t i) const
{
    UINT offset = 0; ID3D11Buffer* vb = mVB.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &mStride, &offset);
    ctx->IASetIndexBuffer(mIB.Get(), DXGI_FORMAT_R32_UINT, 0);

    assert(i < mRanges.size());
    auto& r = mRanges[i];
    ctx->DrawIndexed(r.indexCount, r.indexStart, 0);
}
