// ============================================================================
// RigidSkeletal.h
// - RigidSkeletal 선언
// ============================================================================

// ---- includes ----

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <directxtk/SimpleMath.h>
#include <d3d11.h>

#include "StaticMesh.h"
#include "Material.h"

using namespace DirectX::SimpleMath;

struct RS_Node
{
    std::string name;
    int parent = -1;
    std::vector<int> children;

    Matrix bindLocal = Matrix::Identity;     // FBX 노드의 로컬 바인드
    Matrix poseLocal = Matrix::Identity;     // 애니메이션으로 계산된 로컬 포즈
    Matrix poseGlobal = Matrix::Identity;    // 부모 누적된 글로벌 포즈

    // 이 노드에 붙은 '부분 메시' 인덱스(여러 개일 수 있음, 보통 0~1개)
    std::vector<int> partIndices;
};

struct RS_KeyT { double t; Vector3 v; };
struct RS_KeyR { double t; Quaternion q; };
struct RS_KeyS { double t; Vector3 v; };

struct RS_Channel
{
    std::string target;          // 노드 이름
    std::vector<RS_KeyT> T;
    std::vector<RS_KeyR> R;
    std::vector<RS_KeyS> S;
};

struct RS_Clip
{
    std::string name;
    double duration = 0.0;       // ticks
    double ticksPerSec = 25.0;   // 기본 25
    std::vector<RS_Channel> channels;

    // targetName -> index
    std::unordered_map<std::string, int> map;
};

struct RS_Part
{
    // 각 파트는 독립 StaticMesh (간단하게 구현; BoxHuman는 파트 수 적음)
    StaticMesh mesh;
    std::vector<MaterialGPU> materials; // 해당 파트에 쓰이는 머티리얼들
    int ownerNode = -1;                 // 이 파트의 노드 인덱스
};

class RigidSkeletal
{
public:
    // FBX에서 계층/애니메이션/파트 추출 후 GPU 빌드까지
    static std::unique_ptr<RigidSkeletal> LoadFromFBX(
        ID3D11Device* dev,
        const std::wstring& fbxPath,
        const std::wstring& texDir);

    // 시간 업데이트(tSec = 초). 첫 애니메이션(보통 Walk)을 사용
    void EvaluatePose(double tSec);
    void EvaluatePose(double tSec, bool loop);      //

    // Opaque / Cutout / Transparent 렌더(기존 파이프라인에 그대로 맞춤)
    void DrawOpaqueOnly(
        ID3D11DeviceContext* ctx,
        const DirectX::SimpleMath::Matrix& worldModel,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB,
        const DirectX::SimpleMath::Vector4& vLightDir,
        const DirectX::SimpleMath::Vector4& vLightColor,
        const DirectX::SimpleMath::Vector3& eyePos,
        const DirectX::SimpleMath::Vector3& kA, float ks, float shininess,
        const DirectX::SimpleMath::Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawAlphaCutOnly(
        ID3D11DeviceContext* ctx,
        const DirectX::SimpleMath::Matrix& worldModel,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB, float alphaCut,
        const DirectX::SimpleMath::Vector4& vLightDir,
        const DirectX::SimpleMath::Vector4& vLightColor,
        const DirectX::SimpleMath::Vector3& eyePos,
        const DirectX::SimpleMath::Vector3& kA, float ks, float shininess,
        const DirectX::SimpleMath::Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawTransparentOnly(
        ID3D11DeviceContext* ctx,
        const DirectX::SimpleMath::Matrix& worldModel,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB,
        const DirectX::SimpleMath::Vector4& vLightDir,
        const DirectX::SimpleMath::Vector4& vLightColor,
        const DirectX::SimpleMath::Vector3& eyePos,
        const DirectX::SimpleMath::Vector3& kA, float ks, float shininess,
        const DirectX::SimpleMath::Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawDepthOnly(
        ID3D11DeviceContext* ctx,
        const DirectX::SimpleMath::Matrix& worldModel,
        const DirectX::SimpleMath::Matrix& lightView,
        const DirectX::SimpleMath::Matrix& lightProj,
        ID3D11Buffer* cb0,        // b0(월드/뷰/프로젝션)
        ID3D11Buffer* useCB,      // b2(UseCB) — alphaCut용
        ID3D11VertexShader* vsDepth,
        ID3D11PixelShader* psDepth,
        ID3D11InputLayout* ilPNTT,
        float alphaCut);


public:
    // ----------------------------------------------------------------------------
    // IMGUI/타이밍용 간단 Getter
    // ----------------------------------------------------------------------------
    double GetClipDurationTicks() const noexcept { return mClip.duration; }
    double GetTicksPerSecond()   const noexcept { return (mClip.ticksPerSec > 0.0) ? mClip.ticksPerSec : 25.0; }
    double GetClipDurationSec()  const noexcept { return GetClipDurationTicks() / GetTicksPerSecond(); }
    const std::string& GetClipName() const noexcept { return mClip.name; }


private:
    RigidSkeletal() = default;

    // 내부 유틸(보간)
    static int UpperBoundT(double t, const std::vector<RS_KeyT>& v);
    static int UpperBoundR(double t, const std::vector<RS_KeyR>& v);
    static int UpperBoundS(double t, const std::vector<RS_KeyS>& v);

    Matrix SampleLocalOf(int nodeIdx, double tTick) const;

private:
    std::vector<RS_Node> mNodes;
    std::vector<RS_Part> mParts;

    RS_Clip mClip;     // 첫 번째 클립 사용(예: Walk)
    int mRoot = 0;

    // 캐시: 이름->노드
    std::unordered_map<std::string, int> mNameToNode;
};
