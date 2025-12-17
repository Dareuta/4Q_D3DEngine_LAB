//================================================================================================
// TutorialApp.h
// - TutorialApp을 구성하는 5개의 CPP로 역할을 분리
//
//   TutorialApp_Lifecycle.cpp    // OnInitialize / OnUninitialize / OnUpdate / OnRender / WndProc
//   TutorialApp_D3DInit.cpp      // InitD3D / UninitD3D
//   TutorialApp_SceneInit.cpp    // InitScene / UninitScene + 섀도우 리소스/상태 생성
//   TutorialApp_RenderPass.cpp   // 렌더링 패스 세분화 (섀도우 / 스카이 / 불투명 / 투명 / 디버그)
//   TutorialApp_ImGui.cpp        // InitImGUI / UninitImGUI / UpdateImGUI / AnimUI
//================================================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "RenderSharedCB.h" 

#include <directxtk/SimpleMath.h>
#include <DirectXTK/DDSTextureLoader.h>   // CreateDDSTextureFromFile
#include <DirectXTK/WICTextureLoader.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "../D3D_Core/GameApp.h"
#include "../D3D_Core/Helper.h"

#include "StaticMesh.h"
#include "Material.h"
#include "RigidSkeletal.h"
#include "SkinnedSkeletal.h"
#include "AssimpImporterEx.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

//================================================================================================
// TutorialApp
//================================================================================================
class TutorialApp : public GameApp
{
public:
	using Vector2 = DirectX::SimpleMath::Vector2;
	using Vector3 = DirectX::SimpleMath::Vector3;
	using Vector4 = DirectX::SimpleMath::Vector4;
	using Matrix = DirectX::SimpleMath::Matrix;

	ID3D11Device* GetDevice()     const noexcept { return m_pDevice; }
	ID3D11DeviceContext* GetContext()    const noexcept { return m_pDeviceContext; }
	const Matrix& GetProjection() const noexcept { return m_Projection; }

protected:
	//==========================================================================================
	// GameApp hooks
	//==========================================================================================
	bool    OnInitialize() override;
	void    OnUninitialize() override;
	void    OnUpdate() override;
	void    OnRender() override;
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	//==========================================================================================
	// 초기화 / 해제 유틸리티
	//==========================================================================================
	bool InitD3D();
	void UninitD3D();

	bool InitImGUI();
	void UninitImGUI();
	void UpdateImGUI();

	bool InitScene();
	void UninitScene();

	// Shadow & DepthOnly
	void UpdateLightCameraAndShadowCB(ID3D11DeviceContext* ctx);
	bool CreateShadowResources(ID3D11Device* dev);
	bool CreateDepthOnlyShaders(ID3D11Device* dev);

	//==========================================================================================
	// 렌더 패스
	//==========================================================================================
	void RenderShadowPass_Main(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB);

	void RenderSkyPass(
		ID3D11DeviceContext* ctx,
		const Matrix& viewNoTrans);

	void RenderOpaquePass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const DirectX::SimpleMath::Vector3& eye);

	void RenderCutoutPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const DirectX::SimpleMath::Vector3& eye);

	void RenderTransparentPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const DirectX::SimpleMath::Vector3& eye);

	void RenderDebugPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const DirectX::SimpleMath::Vector3& lightDir);

	//==========================================================================================
	// Render helpers (정적/스키닝 파이프라인 + 정적 드로우)
	//==========================================================================================
	void BindStaticMeshPipeline(ID3D11DeviceContext* ctx);
	void BindStaticMeshPipeline_PBR(ID3D11DeviceContext* ctx);

	void BindSkinnedMeshPipeline(ID3D11DeviceContext* ctx);

	void DrawStaticOpaqueOnly(
		ID3D11DeviceContext* ctx,
		StaticMesh& mesh,
		const std::vector<MaterialGPU>& mtls,
		const Matrix& world,
		const ConstantBuffer& cb);

	void DrawStaticAlphaCutOnly(
		ID3D11DeviceContext* ctx,
		StaticMesh& mesh,
		const std::vector<MaterialGPU>& mtls,
		const Matrix& world,
		const ConstantBuffer& cb);

	void DrawStaticTransparentOnly(
		ID3D11DeviceContext* ctx,
		StaticMesh& mesh,
		const std::vector<MaterialGPU>& mtls,
		const Matrix& world,
		const ConstantBuffer& cb);

	//==========================================================================================
	// D3D 핵심 객체
	//==========================================================================================
	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;
	IDXGISwapChain* m_pSwapChain = nullptr;
	ID3D11RenderTargetView* m_pRenderTargetView = nullptr;

	// Depth / Stencil
	ID3D11Texture2D* m_pDepthStencil = nullptr;
	ID3D11DepthStencilView* m_pDepthStencilView = nullptr;
	ID3D11DepthStencilState* m_pDepthStencilState = nullptr;

	// 공용 리소스 (샘플러, 상수버퍼, 투영 행렬)
	ID3D11SamplerState* m_pSamplerLinear = nullptr;
	ID3D11Buffer* m_pConstantBuffer = nullptr; // b0
	ID3D11Buffer* m_pBlinnCB = nullptr; // b1

	Matrix              m_Projection = Matrix::Identity;
	DirectX::XMMATRIX   m_World = DirectX::XMMatrixIdentity();

	//==========================================================================================
	// 렌더 상태 (Rasterizer / DepthStencil / Blend)
	//==========================================================================================
	// 일반
	ID3D11RasterizerState* m_pCullBackRS = nullptr; // 기본 Back
	ID3D11DepthStencilState* m_pDSS_Opaque = nullptr; // Depth write ON
	ID3D11DepthStencilState* m_pDSS_Trans = nullptr; // Depth write OFF
	ID3D11BlendState* m_pBS_Alpha = nullptr; // Straight Alpha

	// 디버그 / 테스트
	ID3D11RasterizerState* m_pNoCullRS = nullptr; // (미사용 가능)
	ID3D11RasterizerState* m_pWireRS = nullptr; // Wireframe + Cull None
	ID3D11DepthStencilState* m_pDSS_Disabled = nullptr; // DepthEnable = FALSE

	//==========================================================================================
	// 스카이박스
	//==========================================================================================
	ID3D11VertexShader* m_pSkyVS = nullptr;
	ID3D11PixelShader* m_pSkyPS = nullptr;
	ID3D11InputLayout* m_pSkyIL = nullptr;
	ID3D11Buffer* m_pSkyVB = nullptr;
	ID3D11Buffer* m_pSkyIB = nullptr;
	ID3D11ShaderResourceView* m_pSkySRV = nullptr;
	ID3D11SamplerState* m_pSkySampler = nullptr;
	ID3D11DepthStencilState* m_pSkyDSS = nullptr; // Depth write ZERO + LEQUAL
	ID3D11RasterizerState* m_pSkyRS = nullptr; // Cull FRONT (내부면 렌더)

	//==========================================================================================
	// 메쉬 파이프라인 (정적)
	//==========================================================================================
	ID3D11VertexShader* m_pMeshVS = nullptr;
	ID3D11PixelShader* m_pMeshPS = nullptr;
	ID3D11InputLayout* m_pMeshIL = nullptr;
	ID3D11Buffer* m_pUseCB = nullptr; // b2

	// FBX / 머티리얼 (정적)
	StaticMesh               gTree;
	StaticMesh               gChar;
	StaticMesh               gZelda;
	StaticMesh				 gFemale;

	std::vector<MaterialGPU> gTreeMtls;
	std::vector<MaterialGPU> gCharMtls;
	std::vector<MaterialGPU> gZeldaMtls;
	std::vector<MaterialGPU> gFemaleMtls;

	// 박스 휴먼 (정적 메쉬 + 리지드 스켈레톤)
	StaticMesh               gBoxHuman;
	std::vector<MaterialGPU> gBoxMtls;

	//==========================================================================================
	// 스키닝 파이프라인
	//==========================================================================================
	ID3D11VertexShader* m_pSkinnedVS = nullptr;
	ID3D11InputLayout* m_pSkinnedIL = nullptr;
	ID3D11Buffer* m_pBoneCB = nullptr; // VS b4
	std::unique_ptr<SkinnedSkeletal>     mSkinRig;               // SkinningTest.fbx

	//==========================================================================================
	// 섀도우 리소스
	//==========================================================================================
	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mShadowTex;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   mShadowDSV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mShadowSRV;
	Microsoft::WRL::ComPtr<ID3D11SamplerState>       mSamShadowCmp;   // s1 (Comparison)

	Microsoft::WRL::ComPtr<ID3D11RasterizerState>    mRS_ShadowBias;  // 그림자 패스용 깊이 바이어스 RS
	D3D11_VIEWPORT                                   mShadowVP{};

	// Depth-only shaders & IL
	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_Depth;        // Static
	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_DepthSkinned; // Skinned
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_Depth;        // Alpha-cut clip()
	Microsoft::WRL::ComPtr<ID3D11InputLayout>        mIL_PNTT;         // 정적 (PNTT)
	Microsoft::WRL::ComPtr<ID3D11InputLayout>        mIL_PNTT_BW;      // 스키닝 (PNTT + Bone)

	// Shadow CB (b6) 및 라이트 카메라 행렬
	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_Shadow;       // LVP, Params
	Matrix                                           mLightView = Matrix::Identity;
	Matrix                                           mLightProj = Matrix::Identity;

	// 섀도우 설정값
	UINT  mShadowW = 2048;
	UINT  mShadowH = 2048;
	float mShadowCmpBias = 0.0015f; // 비교 바이어스(PS)
	float mShadowFovY = DirectX::XMConvertToRadians(60.0f);
	float mShadowNear = 0.01f;
	float mShadowFar = 1000.0f;
	int   mShadowDepthBias = 1000;    // RS DepthBias
	float mShadowSlopeBias = 1.5f;    // RS SlopeScaledDepthBias
	float mShadowAlphaCut = 0.4f;    // DepthOnly clip 임계

	struct ShadowUI
	{
		bool  showSRV = true;  // ImGui 프리뷰
		bool  followCamera = true;  // 카메라 정면을 focus
		bool  useManualPos = false; // 라이트 위치 수동
		bool  autoCover = true;  // 카메라 화면 자동 커버
		bool  useOrtho = false; // 직교 투영 여부

		float focusDist = 500.0f;    // 카메라 정면 기준
		float lightDist = 5000.0f;   // lookAt ~ light
		float coverMargin = 1.3f;      // 여유 치수 (> 1)

		DirectX::XMFLOAT3 manualPos = { 0, 30, -30 };
		DirectX::XMFLOAT3 manualTarget = { 0,  0,   0 };
	} mShUI;

	//==========================================================================================
	// 애니메이션 컨트롤 (디버그)
	//==========================================================================================
	struct AnimCtrl
	{
		bool   play = true;
		bool   loop = true;
		float  speed = 1.0f;
		double t = 0.0;
	};

	AnimCtrl mBoxAC;
	AnimCtrl mSkinAC;

	//==========================================================================================
	// 디버그 화살표
	//==========================================================================================
	ID3D11VertexShader* m_pDbgVS = nullptr;
	ID3D11PixelShader* m_pDbgPS = nullptr;
	ID3D11InputLayout* m_pDbgIL = nullptr;
	ID3D11Buffer* m_pArrowVB = nullptr;
	ID3D11Buffer* m_pArrowIB = nullptr;
	ID3D11RasterizerState* m_pDbgRS = nullptr; // Cull None
	ID3D11Buffer* m_pDbgCB = nullptr;    // PS b3 (색상)

	//==========================================================================================
	// 디버그 그리드
	//==========================================================================================
	Microsoft::WRL::ComPtr<ID3D11Buffer>       mGridVB;
	Microsoft::WRL::ComPtr<ID3D11Buffer>       mGridIB;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>  mGridIL;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> mGridVS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  mGridPS;

	UINT  mGridIndexCount = 0;
	float mGridHalfSize = 1500.0f; // +- 범위
	float mGridY = -200.0f; // y 높이

	//==========================================================================================
	// 변환 유틸 / 디버그 토글
	//==========================================================================================
	struct XformUI
	{
		DirectX::SimpleMath::Vector3 pos{ 0, 0, 0 };
		DirectX::SimpleMath::Vector3 rotD{ 0, 0, 0 }; // degrees
		DirectX::SimpleMath::Vector3 scl{ 1, 1, 1 };

		DirectX::SimpleMath::Vector3 initPos{ 0, 0, 0 };
		DirectX::SimpleMath::Vector3 initRotD{ 0, 0, 0 };
		DirectX::SimpleMath::Vector3 initScl{ 1, 1, 1 };

		bool enabled = true;
	};

	struct DebugToggles
	{
		bool showSky = true;
		bool showOpaque = true;
		bool showTransparent = true;
		bool showLightArrow = true;

		bool wireframe = false;
		bool cullNone = true;
		bool depthWriteOff = false;
		bool freezeTime = false;

		bool disableNormal = false;
		bool disableSpecular = false;
		bool disableEmissive = false;

		bool  forceAlphaClip = true; // 테스트 강제
		bool  showGrid = true;

		float alphaCut = 0.4f;

		bool  useToon = true;
		bool  toonHalfLambert = true;
		float toonSpecStep = 0.55f;
		float toonSpecBoost = 1.0f;
		float toonShadowMin = 0.02f;
	};

	static Matrix ComposeSRT(const XformUI& xf)
	{
		using namespace DirectX;
		using namespace DirectX::SimpleMath;

		const Matrix S = Matrix::CreateScale(xf.scl);
		const Matrix R = Matrix::CreateFromYawPitchRoll(
			XMConvertToRadians(xf.rotD.y),
			XMConvertToRadians(xf.rotD.x),
			XMConvertToRadians(xf.rotD.z));
		const Matrix T = Matrix::CreateTranslation(xf.pos);
		return S * R * T;
	}

	//==========================================================================================
	// 씬 / 조명 / 카메라 파라미터
	//==========================================================================================
	Matrix view; // 카메라 View (렌더 프레임용 캐시)

	float color[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
	float spinSpeed = 0.0f;

	float m_FovDegree = 60.0f; // deg
	float m_Near = 0.1f;
	float m_Far = 5000.0f;

	float   m_LightYaw = DirectX::XMConvertToRadians(-90.0f);
	float   m_LightPitch = DirectX::XMConvertToRadians(60.0f);
	Vector3 m_LightColor{ 1, 1, 1 };
	float   m_LightIntensity = 1.0f;

	Vector3 cubeScale{ 5.0f,  5.0f,  5.0f };
	Vector3 cubeTransformA{ 0.0f,  0.0f, -20.0f };
	Vector3 cubeTransformB{ 5.0f,  0.0f,   0.0f };
	Vector3 cubeTransformC{ 3.0f,  0.0f,   0.0f };

	Vector3 m_Ia{ 0.1f, 0.1f, 0.1f };
	Vector3 m_Ka{ 1.0f, 1.0f, 1.0f };
	float   m_Ks = 0.9f;
	float   m_Shininess = 64.0f;

	XformUI      mTreeX;
	XformUI      mCharX;
	XformUI      mZeldaX;
	XformUI		 mFemaleX;

	XformUI      mBoxX;
	XformUI      mSkinX;
	DebugToggles mDbg;

	Vector4 vLightDir;
	Vector4 vLightColor;

	// 디버그 화살표(표시 위치/스케일)
	Vector3 m_ArrowPos{ 100.0f, 200.0f, 100.0f };
	Vector3 m_ArrowScale{ 1.0f,   1.0f,   1.0f };

	//==========================================================================================
	// 리지드 스켈레톤(박스 휴먼) 컨트롤
	//==========================================================================================
	std::unique_ptr<RigidSkeletal> mBoxRig;

	double mAnimT = 0.0;  // 애니메이션 시간(초)
	double mAnimSpeed = 1.0;  // 재생 속도 배율
	bool   mBox_Play = true; // 재생/정지
	bool   mBox_Loop = true; // 루프 여부
	float  mBox_Speed = 1.0f; // 재생 배수(음수 = 역재생)

	//==========================================================================================
	// 툰 셰이딩
	//==========================================================================================
	ID3D11ShaderResourceView* m_pRampSRV = nullptr; // PS t6
	ID3D11Buffer* m_pToonCB = nullptr; // PS b7			


	//==========================================================================================
	
	ID3D11PixelShader* m_pPBRPS = nullptr;
	ID3D11Buffer* m_pPBRParamsCB = nullptr;

	struct PBRUI
	{
		bool enable = true;           // PBR 켜기/끄기
		bool charOnly = true;         // char만 PBR 적용(기존 모델들 안 망가뜨리기)

		bool useBaseColorTex = true;
		bool useNormalTex = true;
		bool useMetalTex = true;
		bool useRoughTex = true;

		bool flipNormalY = false; // 노말맵 녹색 채널 뒤집기용(에셋에 따라 필요할 때가 있음)
		float normalStrength = 1.0f;

		DirectX::SimpleMath::Vector3 baseColor = { 1,1,1 };
		float metallic = 0.0f;
		float roughness = 0.5f;       // 0에 가까울수록 거울
	} mPbr;

};
