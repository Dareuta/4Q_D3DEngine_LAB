//=================================================================================================
// TutorialApp.h
// - TutorialApp을 구성하는 5개의 CPP로 역할 분리
//
//   TutorialApp_Lifecycle.cpp    // OnInitialize / OnUninitialize / OnUpdate / OnRender / WndProc
//   TutorialApp_D3DInit.cpp      // InitD3D / UninitD3D
//   TutorialApp_SceneInit.cpp    // InitScene / UninitScene + 섀도우 리소스/상태 생성
//   TutorialApp_RenderPass.cpp   // 렌더 패스 세분화 (섀도우 / 스카이 / 불투명 / 투명 / 디버그)
//   TutorialApp_ImGui.cpp        // InitImGUI / UninitImGUI / UpdateImGUI / AnimUI
//=================================================================================================
#pragma once

// ============================================================================
// System / D3D
// ============================================================================

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxtk/SimpleMath.h>

// ============================================================================
// DirectXTK
// ============================================================================

#include <DirectXTK/DDSTextureLoader.h>   // CreateDDSTextureFromFile
#include <DirectXTK/WICTextureLoader.h>

// ============================================================================
// ImGui
// ============================================================================

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

// ============================================================================
// Project
// ============================================================================

#include "../../D3D_Core/GameApp.h"
#include "../../D3D_Core/Helper.h"

#include "../RenderSharedCB.h"
#include "../StaticMesh.h"
#include "../Material.h"
#include "../RigidSkeletal.h"
#include "../SkinnedSkeletal.h"
#include "../AssimpImporterEx.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "../PhysX/PhysXContext.h"
#include "../PhysX/PhysXWorld.h"

// ============================================================================
// TutorialApp
// ============================================================================

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
	// =========================================================================
	// GameApp hooks
	// =========================================================================

	bool    OnInitialize() override;
	void    OnUninitialize() override;
	void    OnUpdate() override;
	void    OnRender() override;
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	// =========================================================================
	// Initialize / Shutdown
	// =========================================================================

	bool InitD3D();
	void UninitD3D();

	bool InitImGUI();
	void UninitImGUI();
	void UpdateImGUI();

	bool InitScene();
	void UninitScene();

	// =========================================================================
	// Shadow / DepthOnly
	// =========================================================================

	void UpdateLightCameraAndShadowCB(ID3D11DeviceContext* ctx);
	bool CreateShadowResources(ID3D11Device* dev);
	bool CreateDepthOnlyShaders(ID3D11Device* dev);
	bool CreatePointShadowResources(ID3D11Device* dev);

	// =========================================================================
	// Render Passes
	// =========================================================================

	void RenderShadowPass_Main(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB);

	void RenderPointShadowPass_Cube(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB);

	void RenderSkyPass(
		ID3D11DeviceContext* ctx,
		const Matrix& viewNoTrans);

	void RenderOpaquePass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const Vector3& eye);

	void RenderCutoutPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const Vector3& eye);

	void RenderTransparentPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const Vector3& eye);

	void RenderDebugPass(
		ID3D11DeviceContext* ctx,
		ConstantBuffer& baseCB,
		const Vector3& lightDir);

	// =========================================================================
	// Render Helpers (Static / Skinned)
	// =========================================================================

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

	// =========================================================================
	// Tone Mapping / SceneHDR
	// =========================================================================

	bool CreateSceneHDRResources(ID3D11Device* dev);
	void RenderToneMapPass(ID3D11DeviceContext* ctx);

	// =========================================================================
	// D3D Core Objects
	// =========================================================================

	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;
	IDXGISwapChain* m_pSwapChain = nullptr;
	ID3D11RenderTargetView* m_pRenderTargetView = nullptr;

	ID3D11Texture2D* m_pDepthStencil = nullptr;
	ID3D11DepthStencilView* m_pDepthStencilView = nullptr;
	ID3D11DepthStencilState* m_pDepthStencilState = nullptr;

	ID3D11SamplerState* m_pSamplerLinear = nullptr;
	ID3D11Buffer* m_pConstantBuffer = nullptr; // b0
	ID3D11Buffer* m_pBlinnCB = nullptr;        // b1

	Matrix                   m_Projection = Matrix::Identity;
	DirectX::XMMATRIX        m_World = DirectX::XMMatrixIdentity();

	// =========================================================================
	// Render States (Rasterizer / DepthStencil / Blend)
	// =========================================================================

	ID3D11RasterizerState* m_pCullBackRS = nullptr;
	ID3D11DepthStencilState* m_pDSS_Opaque = nullptr;
	ID3D11DepthStencilState* m_pDSS_Trans = nullptr;
	ID3D11BlendState* m_pBS_Alpha = nullptr;

	ID3D11RasterizerState* m_pNoCullRS = nullptr;
	ID3D11RasterizerState* m_pWireRS = nullptr;
	ID3D11DepthStencilState* m_pDSS_Disabled = nullptr;

	// =========================================================================
	// Skybox
	// =========================================================================

	ID3D11VertexShader* m_pSkyVS = nullptr;
	ID3D11PixelShader* m_pSkyPS = nullptr;
	ID3D11InputLayout* m_pSkyIL = nullptr;
	ID3D11Buffer* m_pSkyVB = nullptr;
	ID3D11Buffer* m_pSkyIB = nullptr;

	ID3D11DepthStencilState* m_pSkyDSS = nullptr;
	ID3D11RasterizerState* m_pSkyRS = nullptr;

	// IBL / Environment SRVs
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mSkyEnvMDRSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mSkyEnvHDRSRV;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mIBLIrrMDRSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mIBLIrrHDRSRV;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mIBLPrefMDRSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mIBLPrefHDRSRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mIBLBrdfSRV;

	Microsoft::WRL::ComPtr<ID3D11SamplerState>       mSamIBLClamp; // PS s3

	// =========================================================================
	// Static Mesh Pipeline
	// =========================================================================

	ID3D11VertexShader* m_pMeshVS = nullptr;
	ID3D11PixelShader* m_pMeshPS = nullptr;
	ID3D11InputLayout* m_pMeshIL = nullptr;
	ID3D11Buffer* m_pUseCB = nullptr; // b2

	StaticMesh               gTree;
	StaticMesh               gChar;
	StaticMesh               gZelda;
	StaticMesh               gFemale;

	std::vector<MaterialGPU> gTreeMtls;
	std::vector<MaterialGPU> gCharMtls;
	std::vector<MaterialGPU> gZeldaMtls;
	std::vector<MaterialGPU> gFemaleMtls;

	StaticMesh               gBoxHuman;
	std::vector<MaterialGPU> gBoxMtls;

	// =========================================================================
	// Skinned Mesh Pipeline
	// =========================================================================

	ID3D11VertexShader* m_pSkinnedVS = nullptr;
	ID3D11InputLayout* m_pSkinnedIL = nullptr;
	ID3D11Buffer* m_pBoneCB = nullptr; // VS b4
	std::unique_ptr<SkinnedSkeletal> mSkinRig;             // SkinningTest.fbx

	// =========================================================================
	// Shadow Resources (Directional)
	// =========================================================================

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mShadowTex;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   mShadowDSV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mShadowSRV;
	Microsoft::WRL::ComPtr<ID3D11SamplerState>       mSamShadowCmp;  // s1

	Microsoft::WRL::ComPtr<ID3D11RasterizerState>    mRS_ShadowBias;
	D3D11_VIEWPORT                                   mShadowVP{};

	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_Depth;
	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_DepthSkinned;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_Depth;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_PointShadow;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>        mIL_PNTT;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>        mIL_PNTT_BW;

	// =========================================================================
	// Point Shadow (Cube) : t10 / b13
	// =========================================================================

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mPointShadowTex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mPointShadowSRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   mPointShadowRTV[6];

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mPointShadowDepth;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   mPointShadowDSV[6];

	D3D11_VIEWPORT                                   mPointShadowVP{};
	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_PointShadow; // b13

	// Shadow CB (b6) + Light Camera Matrices
	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_Shadow;      // LVP, Params
	Matrix                                           mLightView = Matrix::Identity;
	Matrix                                           mLightProj = Matrix::Identity;

	// Shadow settings
	UINT  mShadowW = 4096;
	UINT  mShadowH = 4096;
	float mShadowCmpBias = 0.0015f;
	float mShadowFovY = DirectX::XMConvertToRadians(60.0f);
	float mShadowNear = 0.01f;
	float mShadowFar = 1000.0f;
	int   mShadowDepthBias = 1000;
	float mShadowSlopeBias = 1.5f;
	float mShadowAlphaCut = 0.4f;

	struct ShadowUI
	{
		bool  showSRV = true;
		bool  followCamera = true;
		bool  useManualPos = false;
		bool  autoCover = true;
		bool  useOrtho = false;

		float focusDist = 500.0f;
		float lightDist = 5000.0f;
		float coverMargin = 1.3f;

		DirectX::XMFLOAT3 manualPos = { 0, 30, -30 };
		DirectX::XMFLOAT3 manualTarget = { 0,  0,   0 };
	} mShUI;

	// =========================================================================
	// Animation Controls (Debug)
	// =========================================================================

	struct AnimCtrl
	{
		bool   play = true;
		bool   loop = true;
		float  speed = 1.0f;
		double t = 0.0;
	};

	AnimCtrl mBoxAC;
	AnimCtrl mSkinAC;

	// =========================================================================
	// Debug Arrow / Markers
	// =========================================================================

	ID3D11VertexShader* m_pDbgVS = nullptr;
	ID3D11PixelShader* m_pDbgPS = nullptr;
	ID3D11InputLayout* m_pDbgIL = nullptr;

	ID3D11Buffer* m_pArrowVB = nullptr;
	ID3D11Buffer* m_pArrowIB = nullptr;

	ID3D11Buffer* m_pPointMarkerVB = nullptr;
	ID3D11Buffer* m_pPointMarkerIB = nullptr;

	ID3D11RasterizerState* m_pDbgRS = nullptr;
	ID3D11Buffer* m_pDbgCB = nullptr; // PS b3

	// =========================================================================
	// Debug Grid
	// =========================================================================

	Microsoft::WRL::ComPtr<ID3D11Buffer>       mGridVB;
	Microsoft::WRL::ComPtr<ID3D11Buffer>       mGridIB;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>  mGridIL;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> mGridVS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  mGridPS;

	UINT  mGridIndexCount = 0;
	float mGridHalfSize = 1500.0f;
	float mGridY = -200.0f;

	// =========================================================================
	// Transform / Debug Toggles
	// =========================================================================

	struct XformUI
	{
		Vector3 pos{ 0, 0, 0 };
		Vector3 rotD{ 0, 0, 0 }; // degrees
		DirectX::SimpleMath::Quaternion rotQ = DirectX::SimpleMath::Quaternion::Identity;
		bool useQuat = false; // true면 rotQ 사용, false면 rotD 사용

		Vector3 scl{ 1, 1, 1 };

		Vector3 initPos{ 0, 0, 0 };
		Vector3 initRotD{ 0, 0, 0 };
		Vector3 initScl{ 1, 1, 1 };

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

		bool  forceAlphaClip = true;
		bool  showGrid = true;

		float alphaCut = 0.4f;

		bool  useToon = false;
		bool  toonHalfLambert = false;
		float toonSpecStep = 0.55f;
		float toonSpecBoost = 1.0f;
		float toonShadowMin = 0.02f;

		bool useDeferred = true;

		bool showDeferredUI = true;
		bool showGBuffer = true;

		bool showGBufferFS = false;
		int   gbufferMode = 0;
		float gbufferPosRange = 200.0f;

		bool showShadowWindow = true;
		bool showLightWindow = true;

		bool dirLightEnable = true; // vLightColor.w

		bool sortTransparent = true;
	};

	static Matrix ComposeSRT(const XformUI& xf)
	{
		using namespace DirectX;
		using namespace DirectX::SimpleMath;

		const Matrix S = Matrix::CreateScale(xf.scl);

		Matrix R;
		if (xf.useQuat)
		{
			R = Matrix::CreateFromQuaternion(xf.rotQ);
		}
		else
		{
			R = Matrix::CreateFromYawPitchRoll(
				XMConvertToRadians(xf.rotD.y),
				XMConvertToRadians(xf.rotD.x),
				XMConvertToRadians(xf.rotD.z));
		}

		const Matrix T = Matrix::CreateTranslation(xf.pos);
		return S * R * T;
	}

	// =========================================================================
	// Scene / Camera / Light Params
	// =========================================================================

	Matrix view;

	float color[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
	float spinSpeed = 0.0f;

	float m_FovDegree = 60.0f;
	float m_Near = 0.1f;
	float m_Far = 5000.0f;

	float   m_LightYaw = DirectX::XMConvertToRadians(-90.0f);
	float   m_LightPitch = DirectX::XMConvertToRadians(60.0f);
	Vector3 m_LightColor{ 1, 1, 1 };
	float   m_LightIntensity = 1.0f;

	struct PointLightSettings
	{
		bool    enable = true;
		Vector3 pos{ -10.0f, 0.0f, 135.0f };
		Vector3 color{ 1.0f, 0.9f, 0.7f };
		float   intensity = 30.0f;
		float   range = 600.0f;
		int     falloffMode = 0; // 0:smooth, 1:inverse-square

		bool    showMarker = true;
		float   markerSize = 25.0f;

		bool    shadowEnable = true;
		float   shadowBias = 0.01f;
		UINT    shadowMapSize = 1024;
	} mPoint;

	Vector3 cubeScale{ 5.0f, 5.0f, 5.0f };
	Vector3 cubeTransformA{ 0.0f, 0.0f, -20.0f };
	Vector3 cubeTransformB{ 5.0f, 0.0f,   0.0f };
	Vector3 cubeTransformC{ 3.0f, 0.0f,   0.0f };

	Vector3 m_Ia{ 0.1f, 0.1f, 0.1f };
	Vector3 m_Ka{ 1.0f, 1.0f, 1.0f };
	float   m_Ks = 0.9f;
	float   m_Shininess = 64.0f;

	XformUI      mTreeX;
	XformUI      mCharX;
	XformUI      mZeldaX;
	XformUI      mFemaleX;

	XformUI      mBoxX;
	XformUI      mSkinX;

	DebugToggles mDbg;

	// =========================================================================
	// Physics (PhysX)
	// =========================================================================
	std::unique_ptr<PhysXContext> mPxCtx;
	std::unique_ptr<PhysXWorld>   mPxWorld;

	// Fixed timestep
	float mPhysFixedDt = 1.0f / 60.0f;
	float mPhysAccum = 0.0f;

	// Step() 이후 moved-body 결과/이벤트 버퍼
	std::vector<ActiveTransform> mPhysMoved;
	std::vector<PhysicsEvent>    mPhysEvents;

	// 테스트용 핸들(원하면 씬 init에서 생성해서 씀)
	std::unique_ptr<IPhysicsActor> mPhysGround;
	std::unique_ptr<IRigidBody>    mPhysTestBody;

	// === [ADD] Physics Drop Test =================================================
	static constexpr int kDropCount = 4;

	StaticMesh                 mDropMesh[kDropCount];
	std::vector<MaterialGPU>   mDropMtls[kDropCount];
	Matrix                     mDropWorld[kDropCount];

	std::unique_ptr<IPhysicsActor>  mPxFloor;
	std::unique_ptr<IRigidBody>     mDropBody[kDropCount];

	int   mPhysMaxSubSteps = 8;
	bool  mPhysEnable = true;

	void TickPhysicsDrop(float dt);
	void SyncDropFromPhysics();

	// =========================================================================

	Vector4 vLightDir;
	Vector4 vLightColor;

	Vector3 m_ArrowPos{ 150.0f, 100.0f, 220.0f };
	Vector3 m_ArrowScale{ 1.0f,   1.0f,   1.0f };

	// =========================================================================
	// Rigid Skeletal (Box Human)
	// =========================================================================

	std::unique_ptr<RigidSkeletal> mBoxRig;

	double mAnimT = 0.0;
	double mAnimSpeed = 1.0;
	bool   mBox_Play = true;
	bool   mBox_Loop = true;
	float  mBox_Speed = 1.0f;

	// =========================================================================
	// Toon Shading
	// =========================================================================

	ID3D11ShaderResourceView* m_pRampSRV = nullptr; // PS t6
	ID3D11Buffer* m_pToonCB = nullptr;  // PS b7

	// =========================================================================
	// PBR
	// =========================================================================

	ID3D11PixelShader* m_pPBRPS = nullptr;
	ID3D11Buffer* m_pPBRParamsCB = nullptr;

	struct PBRUI
	{
		bool enable = true;

		bool useBaseColorTex = true;
		bool useNormalTex = true;
		bool useMetalTex = true;
		bool useRoughTex = true;

		bool  flipNormalY = false;
		float normalStrength = 1.0f;

		Vector3 baseColor{ 1, 1, 1 };
		float   metallic = 0.0f;
		float   roughness = 0.5f;

		Vector3 envDiffColor{ 1, 1, 1 };
		float   envDiffIntensity = 1.0f;

		Vector3 envSpecColor{ 1, 1, 1 };
		float   envSpecIntensity = 1.0f;
	} mPbr;

	Microsoft::WRL::ComPtr<ID3D11Buffer> mCB_Proc;
	float mTimeSec = 0.0f;

	int   mIBLSetIndex = 0;
	float mPrefilterMaxMip = 0.0f;

	bool LoadIBLSet(int idx);
	static UINT GetMipCountFromSRV(ID3D11ShaderResourceView* srv);

	// =========================================================================
	// Tone Mapping / SceneHDR
	// =========================================================================

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mSceneHDRTex;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   mSceneHDRRTV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mSceneHDRSRV;

	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_ToneMap;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_ToneMap;
	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_ToneMap;
	Microsoft::WRL::ComPtr<ID3D11SamplerState>       mSamToneMapClamp;

	struct CB_ToneMap
	{
		float    exposureEV;
		float    gamma;
		uint32_t operatorId;
		uint32_t flags;
	};

	struct ToneMapSettings
	{
		bool  useSceneHDR = true;
		bool  enable = true;
		int   operatorId = 2;
		float exposureEV = 0.0f;
		float gamma = 2.2f;
	};

	ToneMapSettings mTone;

	// =========================================================================
	// Deferred / GBuffer
	// =========================================================================

	static constexpr int GBUF_COUNT = 4;

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          mGBufferTex[GBUF_COUNT];
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   mGBufferRTV[GBUF_COUNT];
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mGBufferSRV[GBUF_COUNT];

	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_GBuffer;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_GBuffer;

	Microsoft::WRL::ComPtr<ID3D11VertexShader>       mVS_DeferredLight;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_DeferredLight;

	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_DeferredLights;

	Microsoft::WRL::ComPtr<ID3D11PixelShader>        mPS_GBufferDebug;
	Microsoft::WRL::ComPtr<ID3D11Buffer>             mCB_GBufferDebug;

	struct CB_GBufferDebug
	{
		UINT  mode;
		float posRange;
		float pad[2];
	};

	bool CreateGBufferResources(ID3D11Device* dev);
	void BindStaticMeshPipeline_GBuffer(ID3D11DeviceContext* ctx);
	void RenderGBufferPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB);
	void RenderDeferredLightPass(ID3D11DeviceContext* ctx);
	void RenderGBufferDebugPass(ID3D11DeviceContext* ctx);
};
