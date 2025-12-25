// ============================================================================
// SkinnedSkeletal.h
//  - SkinnedSkeletal 선언부 (스키닝 + 애니메이션 클립 + 파츠/머티리얼)
//  - "데이터 구조(노드/본/클립)" + "렌더/업데이트 API" 를 한 파일에 모아둔 헤더
// ============================================================================

#pragma once

// ---------------------------------------------------------------------------
// Includes
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>                   // std::unique_ptr
#include <d3d11.h>
#include <directxtk/SimpleMath.h>

#include "SkinnedMesh.h"
#include "Material.h"

// 주의: 헤더에서 using namespace는 전역 오염이라 보통 피하는 편.
// (지금은 기존 스타일 유지하되, 아래에서 타입 alias도 같이 둠)
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// Scene Graph (노드 트리)
//  - FBX 노드 계층을 그대로 들고 있음
//  - 각 노드는 "바인드 로컬 / 포즈 로컬 / 포즈 글로벌" 행렬을 가진다
// ---------------------------------------------------------------------------
struct SK_Node
{
    std::string name;
    int parent = -1;
    std::vector<int> children;

    // 바인드 포즈(local): FBX 로딩 시점의 로컬 변환
    Matrix bindLocal = Matrix::Identity;

    // 애니메이션 샘플 결과(local)
    Matrix poseLocal = Matrix::Identity;

    // poseLocal을 부모부터 누적한 글로벌 변환
    Matrix poseGlobal = Matrix::Identity;

    // 이 노드에 붙어있는 "파트(메시)" 인덱스 목록
    std::vector<int> partIndices;
};

// ---------------------------------------------------------------------------
// Animation Keys / Channel / Clip
//  - 키프레임: (time, value)
//  - 채널: 특정 노드(target)에 대해 T/R/S 키를 가진다
//  - 클립: 채널 집합 + (duration, ticksPerSecond)
// ---------------------------------------------------------------------------
struct SK_KeyT { double t; Vector3    v; };   // Translation key
struct SK_KeyR { double t; Quaternion q; };   // Rotation key
struct SK_KeyS { double t; Vector3    v; };   // Scale key

struct SK_Channel
{
    std::string target;              // 애니메이션이 적용될 노드 이름
    std::vector<SK_KeyT> T;
    std::vector<SK_KeyR> R;
    std::vector<SK_KeyS> S;
};

struct SK_Clip
{
    std::string name;
    double duration = 0.0;           // tick 단위 (Assimp aiAnimation::mDuration)
    double tps = 25.0;          // ticks per second (0이면 관례적으로 25로 취급)

    std::vector<SK_Channel> channels;

    // 빠른 lookup: target(node name) -> channels index
    std::unordered_map<std::string, int> map;
};

// ---------------------------------------------------------------------------
// Bone (스키닝용 본 정보)
//  - name: 본 이름 (대개 aiBone::mName)
//  - node: 해당 본이 매핑되는 노드 인덱스
//  - offset: inverse bind matrix (aiBone::mOffsetMatrix)
// ---------------------------------------------------------------------------
struct SK_Bone
{
    std::string name;
    int node = -1;
    Matrix offset = Matrix::Identity;
};

// ---------------------------------------------------------------------------
// Part (메시 파트 단위)
//  - 파트 = 한 덩어리의 스키닝 메시 + 머티리얼 배열
//  - ownerNode: 이 파트가 붙어있는 노드 인덱스(월드 변환의 기준)
// ---------------------------------------------------------------------------
struct SK_Part
{
    SkinnedMesh mesh;
    std::vector<MaterialGPU> materials;
    int ownerNode = -1;
};

// ===========================================================================
// SkinnedSkeletal
//  - FBX 로드: 노드/본/파트/클립 구성
//  - 포즈 평가: EvaluatePose()
//  - 렌더: Opaque / AlphaCut / Transparent / DepthOnly
//  - 본 팔레트 업데이트: UpdateBonePalette()
// ===========================================================================
class SkinnedSkeletal
{
public:
    // --- type aliases (호출부에서 DirectX::SimpleMath 반복 줄이기) ---
    using Matrix = DirectX::SimpleMath::Matrix;
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Quaternion = DirectX::SimpleMath::Quaternion;
    using Vector4 = DirectX::SimpleMath::Vector4;

public:
    // -----------------------------------------------------------------------
    // Load / Basic info
    // -----------------------------------------------------------------------
    static std::unique_ptr<SkinnedSkeletal> LoadFromFBX(
        ID3D11Device* dev,
        const std::wstring& fbxPath,
        const std::wstring& texDir);

    // 글로벌 인버스(스키닝에서 root 보정 등에 사용)
    const Matrix& GlobalInverse() const { return mGlobalInv; }

    // 클립 길이(초 단위)
    double DurationSec() const
    {
        return (mClip.tps > 0.0) ? (mClip.duration / mClip.tps)
            : (mClip.duration / 25.0);
    }

public:
    // -----------------------------------------------------------------------
    // Animation
    // -----------------------------------------------------------------------
    void EvaluatePose(double tSec);                 // 기본: loop = true
    void EvaluatePose(double tSec, bool loop);      // loop 여부 선택

public:
    // -----------------------------------------------------------------------
    // Rendering (패스 분리)
    //  - 각 Draw*는 "현재 poseGlobal"을 기준으로 파트를 렌더링한다
    //  - boneCB는 UpdateBonePalette() 결과를 담는 팔레트 상수버퍼
    // -----------------------------------------------------------------------
    void DrawOpaqueOnly(
        ID3D11DeviceContext* ctx,
        const Matrix& worldModel, const Matrix& view, const Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
        const Vector4& vLightDir, const Vector4& vLightColor,
        const Vector3& eyePos,
        const Vector3& kA, float ks, float shininess, const Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawAlphaCutOnly(
        ID3D11DeviceContext* ctx,
        const Matrix& worldModel, const Matrix& view, const Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
        const Vector4& vLightDir, const Vector4& vLightColor,
        const Vector3& eyePos,
        const Vector3& kA, float ks, float shininess, const Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawTransparentOnly(
        ID3D11DeviceContext* ctx,
        const Matrix& worldModel, const Matrix& view, const Matrix& proj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
        const Vector4& vLightDir, const Vector4& vLightColor,
        const Vector3& eyePos,
        const Vector3& kA, float ks, float shininess, const Vector3& Ia,
        bool disableNormal, bool disableSpecular, bool disableEmissive);

    void DrawDepthOnly(
        ID3D11DeviceContext* ctx,
        const Matrix& worldModel,
        const Matrix& lightView,
        const Matrix& lightProj,
        ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
        ID3D11VertexShader* vsDepthSkinned,
        ID3D11PixelShader* psDepth,
        ID3D11InputLayout* ilPNTT_BW,
        float alphaCut);

public:
    // -----------------------------------------------------------------------
    // Skinning (bone palette)
    //  - UpdateBonePalette: CPU 팔레트 계산 + boneCB 업로드
    //  - WarmupBoneCB     : 초기 1회 업로드(디버그/안전용)
    // -----------------------------------------------------------------------
    void UpdateBonePalette(ID3D11DeviceContext* ctx, ID3D11Buffer* boneCB, const Matrix& worldModel);
    void WarmupBoneCB(ID3D11DeviceContext* ctx, ID3D11Buffer* boneCB);

private:
    SkinnedSkeletal() = default;

    // -----------------------------------------------------------------------
    // Key sampling helpers (upper-bound)
    //  - t 기준으로 "처음으로 t보다 큰 원소의 인덱스"를 찾는 용도
    // -----------------------------------------------------------------------
    static int UB_T(double t, const std::vector<SK_KeyT>& v);
    static int UB_R(double t, const std::vector<SK_KeyR>& v);
    static int UB_S(double t, const std::vector<SK_KeyS>& v);

    // 노드 하나의 로컬 변환을 (T/R/S) 채널에서 샘플링
    Matrix SampleLocalOf(int nodeIdx, double tTick) const;

private:
    // -----------------------------------------------------------------------
    // Core data
    // -----------------------------------------------------------------------
    std::vector<SK_Node> mNodes;
    std::vector<SK_Part> mParts;
    std::vector<SK_Bone> mBones;

    SK_Clip mClip;
    int mRoot = 0;
    std::unordered_map<std::string, int> mNameToNode;

    // 스키닝 팔레트(본 개수만큼):
    //   finalBone = offset * poseGlobal(node)  (혹은 여기에 globalInv/worldModel 보정 포함)
    std::vector<Matrix> mBonePalette;

    // 루트 보정 등에 사용하는 글로벌 인버스
    Matrix mGlobalInv = Matrix::Identity;
};
