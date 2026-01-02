// ===== DebugArrow.h =====
#pragma once
#include "Helper.h"
#include <d3d11.h>
#include <DirectXTK/SimpleMath.h>

struct DebugArrow {
    ID3D11InputLayout* il = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cbWVP = nullptr; // b0
    ID3D11Buffer* cbColor = nullptr; // b3
    ID3D11RasterizerState* rsNone = nullptr; // Cull None
    UINT                    indexCount = 0;
};

constexpr DirectX::XMFLOAT4 kArrowYellow = { 1.0f, 0.95f, 0.2f, 1.0f };

inline bool DebugArrow_Init(ID3D11Device* dev, DebugArrow& A)
{
    using namespace DirectX::SimpleMath;

    // 1) 셰이더 + IL
    ID3D10Blob* vsb = nullptr; ID3D10Blob* psb = nullptr;
    HR_T(CompileShaderFromFile(L"../Resource/DebugColor_VS.hlsl", "main", "vs_5_0", &vsb));
    HR_T(CompileShaderFromFile(L"../Resource/DebugColor_PS.hlsl", "main", "ps_5_0", &psb));
    HR_T(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &A.vs));
    HR_T(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &A.ps));

    D3D11_INPUT_ELEMENT_DESC ild[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,   0, 0, D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12, D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    HR_T(dev->CreateInputLayout(ild, _countof(ild), vsb->GetBufferPointer(), vsb->GetBufferSize(), &A.il));
    SAFE_RELEASE(vsb); SAFE_RELEASE(psb);

    // 2) 지오메트리(+Z 화살표): 두툼한 박스 샤프트 + 피라밋 헤드
    struct V { DirectX::XMFLOAT3 p; DirectX::XMFLOAT4 c; };
    const float halfT = 6.0f, shaftLen = 120.0f, headLen = 30.0f, headHalf = 10.0f;
    const DirectX::XMFLOAT4 Y = { 1.0f,0.9f,0.1f,1.0f };

    enum { s0, s1, s2, s3, s4, s5, s6, s7, h0, h1, h2, h3, tip, COUNT };
    V v[COUNT] = {
        {{-halfT,-halfT,0},Y}, {{+halfT,-halfT,0},Y}, {{+halfT,+halfT,0},Y}, {{-halfT,+halfT,0},Y},
        {{-halfT,-halfT,shaftLen},Y}, {{+halfT,-halfT,shaftLen},Y}, {{+halfT,+halfT,shaftLen},Y}, {{-halfT,+halfT,shaftLen},Y},
        {{-headHalf,-headHalf,shaftLen},Y}, {{+headHalf,-headHalf,shaftLen},Y},
        {{+headHalf,+headHalf,shaftLen},Y}, {{-headHalf,+headHalf,shaftLen},Y},
        {{0,0,shaftLen + headLen},Y},
    };

    uint16_t idx[] = {
        // shaft 6면
        s0,s2,s1, s0,s3,s2,
        s0,s1,s5, s0,s5,s4,
        s1,s2,s6, s1,s6,s5,
        s3,s7,s6, s3,s6,s2,
        s0,s4,s7, s0,s7,s3,
        s4,s6,s7, s4,s5,s6,
        // head
        h2,h1,h0, h3,h2,h0,
        h0,h1,tip, h1,h2,tip, h2,h3,tip, h3,h0,tip,
    };
    A.indexCount = _countof(idx);

    // VB
    D3D11_BUFFER_DESC vbd{}; vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.Usage = D3D11_USAGE_IMMUTABLE; vbd.ByteWidth = sizeof(v);
    D3D11_SUBRESOURCE_DATA vinit{ v };
    HR_T(dev->CreateBuffer(&vbd, &vinit, &A.vb));

    // IB
    D3D11_BUFFER_DESC ibd{}; ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.Usage = D3D11_USAGE_IMMUTABLE; ibd.ByteWidth = sizeof(idx);
    D3D11_SUBRESOURCE_DATA iinit{ idx };
    HR_T(dev->CreateBuffer(&ibd, &iinit, &A.ib));
    {
        D3D11_BUFFER_DESC cbd0{};
        cbd0.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd0.Usage = D3D11_USAGE_DEFAULT;

        // VS에서 W, V, P (3개)만 써도 되지만, 16바이트 정렬 여유 겸해서 4개로 패딩
        cbd0.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 4; // 64 * 4 = 256

        HR_T(dev->CreateBuffer(&cbd0, nullptr, &A.cbWVP));
    }

    // 3) Color 상수버퍼(b3)
    D3D11_BUFFER_DESC cbdC{};
    cbdC.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbdC.Usage = D3D11_USAGE_DEFAULT;
    cbdC.ByteWidth = 16;

    // 초기 데이터 = 기본 노랑
    D3D11_SUBRESOURCE_DATA iC{};
    iC.pSysMem = &kArrowYellow;

    HR_T(dev->CreateBuffer(&cbdC, &iC, &A.cbColor));

    // 4) Cull None
    D3D11_RASTERIZER_DESC rs{}; rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE; rs.DepthClipEnable = TRUE;
    HR_T(dev->CreateRasterizerState(&rs, &A.rsNone));
    return true;
}

inline void DebugArrow_Draw(
    ID3D11DeviceContext* ctx, const DebugArrow& A,
    const DirectX::SimpleMath::Matrix& world,
    const DirectX::SimpleMath::Matrix& view,
    const DirectX::SimpleMath::Matrix& proj,
    const DirectX::XMFLOAT4& color = kArrowYellow   
)
{
    using namespace DirectX::SimpleMath;

    // --- 상태 백업 ---
    ID3D11RasterizerState* oldRS = nullptr;   ctx->RSGetState(&oldRS);
    ID3D11InputLayout* oldIL = nullptr;       ctx->IAGetInputLayout(&oldIL);
    ID3D11VertexShader* oldVS = nullptr;      ctx->VSGetShader(&oldVS, nullptr, 0);
    ID3D11PixelShader* oldPS = nullptr;       ctx->PSGetShader(&oldPS, nullptr, 0);
    ID3D11Buffer* oldPSCBs[4] = { nullptr,nullptr,nullptr,nullptr };
    ctx->PSGetConstantBuffers(0, 4, oldPSCBs);

    // --- CB0: WVP ---
    struct CB { Matrix W, V, P, _pad; } cb;
    cb.W = world.Transpose();
    cb.V = view.Transpose();
    cb.P = proj.Transpose();
    ctx->UpdateSubresource(A.cbWVP, 0, nullptr, &cb, 0, 0);
    
    ctx->UpdateSubresource(A.cbColor, 0, nullptr, &color, 0, 0);

    // --- 파이프라인 바인드 ---
    UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT4), offset = 0;
    ctx->IASetInputLayout(A.il);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, &A.vb, &stride, &offset);
    ctx->IASetIndexBuffer(A.ib, DXGI_FORMAT_R16_UINT, 0);

    if (A.rsNone) ctx->RSSetState(A.rsNone);

    // 불투명, 깊이 ON 권장 (원하면 변경)
    float bf[4] = { 0,0,0,0 };
    ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);

    ctx->VSSetShader(A.vs, nullptr, 0);
    ctx->PSSetShader(A.ps, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &A.cbWVP);
    ctx->PSSetConstantBuffers(3, 1, &A.cbColor); // ★ b3

    // Draw
    ctx->DrawIndexed(A.indexCount, 0, 0);

    // --- 복원 ---
    ctx->VSSetShader(oldVS, nullptr, 0);
    ctx->PSSetShader(oldPS, nullptr, 0);
    ctx->IASetInputLayout(oldIL);
    ctx->RSSetState(oldRS);
    ctx->PSSetConstantBuffers(0, 4, oldPSCBs);
    SAFE_RELEASE(oldVS); SAFE_RELEASE(oldPS); SAFE_RELEASE(oldIL); SAFE_RELEASE(oldRS);
    for (auto& p : oldPSCBs) SAFE_RELEASE(p);
}

inline void DebugArrow_Release(DebugArrow& A)
{
    SAFE_RELEASE(A.il);  SAFE_RELEASE(A.vs); SAFE_RELEASE(A.ps);
    SAFE_RELEASE(A.vb);  SAFE_RELEASE(A.ib); SAFE_RELEASE(A.cbWVP);
    SAFE_RELEASE(A.cbColor);           
    SAFE_RELEASE(A.rsNone);
    A.indexCount = 0;
}

// ===== 도움 함수: 방향 벡터로 World 구성(+Z 정렬) =====
inline DirectX::SimpleMath::Matrix MakeWorldFromDir(
    const DirectX::SimpleMath::Vector3& pos,
    const DirectX::SimpleMath::Vector3& dir,   // 보고 싶은 방향(정규화 권장)
    const DirectX::SimpleMath::Vector3& up,
    const DirectX::SimpleMath::Vector3& scale) // 화살표 크기
{
    using namespace DirectX::SimpleMath;
    Vector3 d = dir; d.Normalize();
    return Matrix::CreateScale(scale) * Matrix::CreateWorld(pos, d, up);
}
