// InitScene / UninitScene + 섀도우 리소스/상태 생성

#include "TutorialApp.h"
#include "../D3D_Core/pch.h"



bool TutorialApp::InitScene()
{
	CreateShadowResources(m_pDevice);
	CreateDepthOnlyShaders(m_pDevice);

	using Microsoft::WRL::ComPtr;

	// ---------- helpers ----------
	auto Compile = [&](const wchar_t* path, const char* entry, const char* profile, ComPtr<ID3DBlob>& blob) {
		HR_T(CompileShaderFromFile(path, entry, profile, &blob));
		};
	auto CreateVS = [&](ComPtr<ID3DBlob>& blob, ID3D11VertexShader** outVS) {
		HR_T(m_pDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outVS));
		};
	auto CreatePS = [&](ComPtr<ID3DBlob>& blob, ID3D11PixelShader** outPS) {
		HR_T(m_pDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outPS));
		};
	auto CreateIL = [&](const D3D11_INPUT_ELEMENT_DESC* il, UINT cnt, ComPtr<ID3DBlob>& vsBlob, ID3D11InputLayout** outIL) {
		HR_T(m_pDevice->CreateInputLayout(il, cnt, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), outIL));
		};

	// =========================================================
	// 1) Mesh(PNTT) shaders & IL
	// =========================================================
	{
		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"Shader/VertexShader.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pMeshVS);

		const D3D11_INPUT_ELEMENT_DESC IL_PNTT[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_PNTT, _countof(IL_PNTT), vsb, &m_pMeshIL);

		Compile(L"Shader/PixelShader.hlsl", "main", "ps_5_0", psb);
		CreatePS(psb, &m_pMeshPS);
	}

	// =========================================================
	// 2) DebugColor shaders & IL
	// =========================================================
	{
		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"../Resource/DebugColor_VS.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pDbgVS);

		const D3D11_INPUT_ELEMENT_DESC IL_DBG[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_DBG, _countof(IL_DBG), vsb, &m_pDbgIL);

		Compile(L"../Resource/DebugColor_PS.hlsl", "main", "ps_5_0", psb);
		CreatePS(psb, &m_pDbgPS);
	}

	// =========================================================
	// 3) Skinned VS(+IL)
	// =========================================================
	{
		D3D_SHADER_MACRO defs[] = { {"SKINNED","1"}, {nullptr,nullptr} };
		ComPtr<ID3DBlob> vsb;
		HR_T(D3DCompileFromFile(L"Shader/VertexShaderSkinning.hlsl",
			defs, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "vs_5_0", 0, 0, &vsb, nullptr));
		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_pSkinnedVS));

		const D3D11_INPUT_ELEMENT_DESC IL_SKIN[] = {
			{"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		CreateIL(IL_SKIN, _countof(IL_SKIN), vsb, &m_pSkinnedIL);
	}

	// =========================================================
	// 4) Constant Buffers & Samplers
	// =========================================================
	{
		auto MakeCB = [&](UINT bytes, ID3D11Buffer** out) {
			D3D11_BUFFER_DESC bd{};
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.ByteWidth = bytes;
			HR_T(m_pDevice->CreateBuffer(&bd, nullptr, out));
			};

		if (!m_pConstantBuffer) MakeCB(sizeof(ConstantBuffer), &m_pConstantBuffer);
		if (!m_pBlinnCB)        MakeCB(sizeof(BlinnPhongCB), &m_pBlinnCB);
		if (!m_pUseCB)          MakeCB(sizeof(UseCB), &m_pUseCB);
		if (!m_pToonCB)			MakeCB(sizeof(ToonCB_), &m_pToonCB);

		// Bone palette (VS b4)
		if (!m_pBoneCB) {
			D3D11_BUFFER_DESC cbd{};
			cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbd.Usage = D3D11_USAGE_DEFAULT;
			cbd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 256; // 256 bones
			HR_T(m_pDevice->CreateBuffer(&cbd, nullptr, &m_pBoneCB));
		}

		// PS sampler (linear wrap)
		if (!m_pSamplerLinear) {
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			HR_T(m_pDevice->CreateSamplerState(&sd, &m_pSamplerLinear));
		}

		// Debug color CB (PS b3)
		if (!m_pDbgCB) {
			D3D11_BUFFER_DESC bd{};
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = 16; // float4
			HR_T(m_pDevice->CreateBuffer(&bd, nullptr, &m_pDbgCB));
		}
	}

	// =========================================================
	// 5) Debug Arrow (geometry)
	// =========================================================
	{
		struct V { DirectX::XMFLOAT3 p; DirectX::XMFLOAT4 c; };
		const float halfT = 6.0f, shaftLen = 120.0f, headLen = 30.0f, headHalf = 10.0f;
		const DirectX::XMFLOAT4 Y = { 1.0f, 0.9f, 0.1f, 1.0f };

		enum { s0, s1, s2, s3, s4, s5, s6, s7, h0, h1, h2, h3, tip, COUNT };
		V v[COUNT] = {
			{{-halfT,-halfT,0},Y}, {{+halfT,-halfT,0},Y}, {{+halfT,+halfT,0},Y}, {{-halfT,+halfT,0},Y},
			{{-halfT,-halfT,shaftLen},Y}, {{+halfT,-halfT,shaftLen},Y}, {{+halfT,+halfT,shaftLen},Y}, {{-halfT,+halfT,shaftLen},Y},
			{{-headHalf,-headHalf,shaftLen},Y}, {{+headHalf,-headHalf,shaftLen},Y},
			{{+headHalf,+headHalf,shaftLen},Y}, {{-headHalf,+headHalf,shaftLen},Y},
			{{0,0,shaftLen + headLen},Y},
		};
		const uint16_t idx[] = {
			s0,s2,s1, s0,s3,s2, s0,s1,s5, s0,s5,s4, s1,s2,s6, s1,s6,s5, s3,s7,s6, s3,s6,s2, s0,s4,s7, s0,s7,s3,
			h2,h1,h0, h3,h2,h0, h0,h1,tip, h1,h2,tip, h2,h3,tip, h3,h0,tip,
		};

		// VB
		D3D11_BUFFER_DESC vbd{ sizeof(v), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
		D3D11_SUBRESOURCE_DATA vinit{ v };
		HR_T(m_pDevice->CreateBuffer(&vbd, &vinit, &m_pArrowVB));
		// IB
		D3D11_BUFFER_DESC ibd{ sizeof(idx), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER };
		D3D11_SUBRESOURCE_DATA iinit{ idx };
		HR_T(m_pDevice->CreateBuffer(&ibd, &iinit, &m_pArrowIB));
	}

	// =========================================================
	// 6) Initial transforms
	// =========================================================
	mTreeX.pos = { -100, -150, 100 };  mTreeX.initPos = mTreeX.pos;  mTreeX.scl = { 100,100,100 };
	mCharX.pos = { 100, -150, 100 };  mCharX.initPos = mCharX.pos;
	mZeldaX.pos = { 0, -150, 250 };  mZeldaX.initPos = mZeldaX.pos;
	mBoxX.pos = { -200, -150, 400 }; mBoxX.scl = { 0.2f,0.2f,0.2f };
	mSkinX.pos = { 200, -150, 400 };

	//mTreeX.enabled = mCharX.enabled = mZeldaX.enabled = mBoxX.enabled = false;

	mTreeX.initScl = mTreeX.scl; mCharX.initScl = mCharX.scl; mZeldaX.initScl = mZeldaX.scl;
	mTreeX.initRotD = mTreeX.rotD; mCharX.initRotD = mCharX.rotD; mZeldaX.initRotD = mZeldaX.rotD;
	mBoxX.initScl = mBoxX.scl; mBoxX.initRotD = mBoxX.rotD; mBoxX.initPos = mBoxX.pos;
	mSkinX.initScl = mSkinX.scl; mSkinX.initRotD = mSkinX.rotD; mSkinX.initPos = mSkinX.pos;

	// =========================================================
	// 7) Load FBX + build GPU
	// =========================================================
	{
		auto BuildAll = [&](const std::wstring& fbx, const std::wstring& texDir,
			StaticMesh& mesh, std::vector<MaterialGPU>& mtls)
			{
				MeshData_PNTT cpu;
				if (!AssimpImporterEx::LoadFBX_PNTT_AndMaterials(fbx, cpu, /*flipUV*/true, /*leftHanded*/true))
					throw std::runtime_error("FBX load failed");
				if (!mesh.Build(m_pDevice, cpu))
					throw std::runtime_error("Mesh build failed");

				mtls.resize(cpu.materials.size());
				for (size_t i = 0; i < cpu.materials.size(); ++i)
					mtls[i].Build(m_pDevice, cpu.materials[i], texDir);
			};

		BuildAll(L"../Resource/Tree/Tree.fbx", L"../Resource/Tree/", gTree, gTreeMtls);
		BuildAll(L"../Resource/Character/Character.fbx", L"../Resource/Character/", gChar, gCharMtls);
		BuildAll(L"../Resource/Zelda/zeldaPosed001.fbx", L"../Resource/Zelda/", gZelda, gZeldaMtls);
		BuildAll(L"../Resource/BoxHuman/BoxHuman.fbx", L"../Resource/BoxHuman/", gBoxHuman, gBoxMtls);

		mBoxRig = RigidSkeletal::LoadFromFBX(m_pDevice,
			L"../Resource/BoxHuman/BoxHuman.fbx",
			L"../Resource/BoxHuman/");

		mSkinRig = SkinnedSkeletal::LoadFromFBX(m_pDevice,
			L"../Resource/Skinning/SkinningTest.fbx",
			L"../Resource/Skinning/");

		if (mSkinRig && m_pBoneCB) mSkinRig->WarmupBoneCB(m_pDeviceContext, m_pBoneCB);
	}

	// =========================================================
	// 8) Rasterizer / Depth / Blend states
	// =========================================================
	{
		// Back-cull (default)
		D3D11_RASTERIZER_DESC rsBack{}; rsBack.FillMode = D3D11_FILL_SOLID; rsBack.CullMode = D3D11_CULL_BACK;
		rsBack.DepthClipEnable = TRUE; HR_T(m_pDevice->CreateRasterizerState(&rsBack, &m_pCullBackRS));

		// Solid + Cull None
		D3D11_RASTERIZER_DESC rsNone{}; rsNone.FillMode = D3D11_FILL_SOLID; rsNone.CullMode = D3D11_CULL_NONE;
		rsNone.DepthClipEnable = TRUE; HR_T(m_pDevice->CreateRasterizerState(&rsNone, &m_pDbgRS));

		// Wireframe + Cull None
		D3D11_RASTERIZER_DESC rw{}; rw.FillMode = D3D11_FILL_WIREFRAME; rw.CullMode = D3D11_CULL_NONE;
		rw.DepthClipEnable = TRUE; HR_T(m_pDevice->CreateRasterizerState(&rw, &m_pWireRS));

		// Depth OFF (debug)
		D3D11_DEPTH_STENCIL_DESC dsOff{}; dsOff.DepthEnable = FALSE; dsOff.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		HR_T(m_pDevice->CreateDepthStencilState(&dsOff, &m_pDSS_Disabled));

		// Opaque depth (write ON)
		D3D11_DEPTH_STENCIL_DESC dsO{}; dsO.DepthEnable = TRUE; dsO.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsO.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR_T(m_pDevice->CreateDepthStencilState(&dsO, &m_pDSS_Opaque));

		// Transparent depth (write OFF)
		D3D11_DEPTH_STENCIL_DESC dsT{}; dsT.DepthEnable = TRUE; dsT.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsT.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR_T(m_pDevice->CreateDepthStencilState(&dsT, &m_pDSS_Trans));

		// Straight alpha blend
		D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;  rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		HR_T(m_pDevice->CreateBlendState(&bd, &m_pBS_Alpha));
	}

	// =========================================================
	// 9) Skybox: shaders/IL, geometry, texture/sampler, depth/RS
	// =========================================================
	{
		// shaders & IL (position-only)
		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"../Resource/Sky_VS.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pSkyVS);
		const D3D11_INPUT_ELEMENT_DESC IL_SKY[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_SKY, _countof(IL_SKY), vsb, &m_pSkyIL);

		Compile(L"../Resource/Sky_PS.hlsl", "main", "ps_5_0", psb);
		CreatePS(psb, &m_pSkyPS);

		// geometry (unit cube)
		struct SkyV { DirectX::XMFLOAT3 pos; };
		const SkyV v[] = {
			{{-1,-1,-1}}, {{-1,+1,-1}}, {{+1,+1,-1}}, {{+1,-1,-1}},
			{{-1,-1,+1}}, {{-1,+1,+1}}, {{+1,+1,+1}}, {{+1,-1,+1}},
		};
		const uint16_t idx[] = {
			0,1,2, 0,2,3,  4,6,5, 4,7,6,  4,5,1, 4,1,0,
			3,2,6, 3,6,7,  1,5,6, 1,6,2,  4,0,3, 4,3,7
		};
		D3D11_BUFFER_DESC vb{ sizeof(v),   D3D11_USAGE_DEFAULT,   D3D11_BIND_VERTEX_BUFFER };
		D3D11_BUFFER_DESC ib{ sizeof(idx), D3D11_USAGE_DEFAULT,   D3D11_BIND_INDEX_BUFFER };
		D3D11_SUBRESOURCE_DATA vsd{ v }, isd{ idx };
		HR_T(m_pDevice->CreateBuffer(&vb, &vsd, &m_pSkyVB));
		HR_T(m_pDevice->CreateBuffer(&ib, &isd, &m_pSkyIB));

		// texture + sampler
		HR_T(CreateDDSTextureFromFile(m_pDevice, L"../Resource/Cubemap.dds", nullptr, &m_pSkySRV));
		D3D11_SAMPLER_DESC ssd{}; ssd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		ssd.AddressU = ssd.AddressV = ssd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		ssd.MaxLOD = D3D11_FLOAT32_MAX;
		HR_T(m_pDevice->CreateSamplerState(&ssd, &m_pSkySampler));

		// depth/raster states
		D3D11_DEPTH_STENCIL_DESC sd{}; sd.DepthEnable = TRUE; sd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		sd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR_T(m_pDevice->CreateDepthStencilState(&sd, &m_pSkyDSS));
		D3D11_RASTERIZER_DESC rs{}; rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_FRONT;
		HR_T(m_pDevice->CreateRasterizerState(&rs, &m_pSkyRS));
	}

	// =========================================================
	// 10) Debug Grid: geometry + shaders/IL
	// =========================================================
	{
		// geometry (XZ plane, CCW, up-facing)
		struct V { DirectX::XMFLOAT3 pos; };
		const float S = mGridHalfSize, Y = mGridY;
		V verts[4] = { {{-S,Y,-S}}, {{ S,Y,-S}}, {{ S,Y, S}}, {{-S,Y, S}} };
		const uint16_t idx[] = { 0,2,1, 0,3,2 };
		mGridIndexCount = 6;

		D3D11_BUFFER_DESC vb{ sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
		D3D11_SUBRESOURCE_DATA vsd{ verts };
		HR_T(m_pDevice->CreateBuffer(&vb, &vsd, &mGridVB));

		D3D11_BUFFER_DESC ib{ sizeof(idx),   D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER };
		D3D11_SUBRESOURCE_DATA isd{ idx };
		HR_T(m_pDevice->CreateBuffer(&ib, &isd, &mGridIB));

		// shaders & IL
		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"Shader/DbgGrid.hlsl", "VS_Main", "vs_5_0", vsb);
		Compile(L"Shader/DbgGrid.hlsl", "PS_Main", "ps_5_0", psb);
		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &mGridVS));
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &mGridPS));

		const D3D11_INPUT_ELEMENT_DESC IL_GRID[] = {
			{ "POSITION",0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		CreateIL(IL_GRID, 1, vsb, &mGridIL);
	}

	{
		HR_T(DirectX::CreateWICTextureFromFile(
			m_pDevice, L"../Resource/RampTexture.png", nullptr, &m_pRampSRV));
	}

	return true;
}


bool TutorialApp::CreateShadowResources(ID3D11Device* dev)
{
	// 1) Shadow map: R32 typeless + DSV + SRV
	D3D11_TEXTURE2D_DESC td{};
	td.Width = mShadowW; td.Height = mShadowH;
	td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R32_TYPELESS;
	td.SampleDesc.Count = 1;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	HR_T(dev->CreateTexture2D(&td, nullptr, mShadowTex.GetAddressOf()));

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
	dsvd.Format = DXGI_FORMAT_D32_FLOAT;
	dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	HR_T(dev->CreateDepthStencilView(mShadowTex.Get(), &dsvd, mShadowDSV.GetAddressOf()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.Format = DXGI_FORMAT_R32_FLOAT;
	srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvd.Texture2D.MipLevels = 1;
	HR_T(dev->CreateShaderResourceView(mShadowTex.Get(), &srvd, mShadowSRV.GetAddressOf()));

	// 2) Comparison sampler (PS s1)
	D3D11_SAMPLER_DESC sd{};
	sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
	HR_T(dev->CreateSamplerState(&sd, mSamShadowCmp.GetAddressOf()));

	// 3) Depth-bias Rasterizer (그림자 패스 전용)
	D3D11_RASTERIZER_DESC rs{};
	rs.FillMode = D3D11_FILL_SOLID;
	rs.CullMode = D3D11_CULL_BACK;
	rs.DepthClipEnable = TRUE;
	rs.DepthBias = (INT)mShadowDepthBias;      // 예: 100~2000 구간에서 튜닝
	rs.SlopeScaledDepthBias = mShadowSlopeBias; // 예: 1.0~2.0
	rs.DepthBiasClamp = 0.0f;
	HR_T(dev->CreateRasterizerState(&rs, mRS_ShadowBias.GetAddressOf()));

	// 4) Viewport
	mShadowVP = { 0, 0, (float)mShadowW, (float)mShadowH, 0.0f, 1.0f };

	// 5) ShadowCB (b6) : LVP + Params
	D3D11_BUFFER_DESC cbd{};
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.Usage = D3D11_USAGE_DEFAULT;
	cbd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) + sizeof(DirectX::XMFLOAT4);
	HR_T(dev->CreateBuffer(&cbd, nullptr, mCB_Shadow.GetAddressOf()));

	return true;
}

void TutorialApp::UpdateLightCameraAndShadowCB(ID3D11DeviceContext* ctx)
{
	using namespace DirectX::SimpleMath;

	// 1) 타겟 지점
	const Vector3 camPos = m_Camera.m_World.Translation();
	const Vector3 camDir = m_Camera.GetForward(); // normalized
	const Vector3 lookAt = mShUI.followCamera
		? (camPos + camDir * mShUI.focusDist)
		: mShUI.manualTarget;

	// 2) 라이트 방향 (광선 진행 방향 = 아래로 향함)
	Vector3 lightDir = Vector3::TransformNormal(
		Vector3::UnitZ,
		Matrix::CreateFromYawPitchRoll(m_LightYaw, m_LightPitch, 0.0f)
	);
	lightDir.Normalize();

	// 3) 라이트 "계산용 위치"
	const Vector3 lightPos = mShUI.useManualPos
		? mShUI.manualPos
		: (lookAt - lightDir * mShUI.lightDist);

	// 4) up 특이점 회피
	const Vector3 up = (fabsf(lightDir.y) > 0.97f) ? Vector3::UnitZ : Vector3::UnitY;

	// 5) 자동 커버(원근 프러스텀) 또는 직교(옵션)
	if (mShUI.autoCover) {
		const float fovY = XMConvertToRadians(m_FovDegree);
		const float aspect = float(m_ClientWidth) / float(m_ClientHeight);
		const float halfH = tanf(0.5f * fovY) * mShUI.focusDist;
		const float halfW = halfH * aspect;
		const float r = sqrtf(halfW * halfW + halfH * halfH) * mShUI.coverMargin;
		const float d = mShUI.lightDist;

		mShadowNear = max(0.01f, d - r);
		mShadowFar = d + r;
		//mShadowNear = 0.01f;
		//mShadowFar = 500.0f;
		mShadowFovY = 2.0f * atanf(r / max(1e-4f, d));
	}

	const float aspectSh = float(mShadowW) / float(mShadowH);

	const Matrix V = Matrix::CreateLookAt(lightPos, lookAt, up);

	Matrix P;
	if (mShUI.useOrtho) {
		// 직교 투영(안정적): autoCover 계산 r을 그대로 씀
		const float fovY = XMConvertToRadians(m_FovDegree);
		const float aspect = float(m_ClientWidth) / float(m_ClientHeight);
		const float halfH = tanf(0.5f * fovY) * mShUI.focusDist * mShUI.coverMargin;
		const float halfW = halfH * aspect;
		const float l = -halfW, r = +halfW, b = -halfH, t = +halfH;
		P = Matrix::CreateOrthographicOffCenter(l, r, b, t, mShadowNear, mShadowFar);
	}
	else {
		// 기존: 원근 투영
		P = Matrix::CreatePerspectiveFieldOfView(mShadowFovY, aspectSh, mShadowNear, mShadowFar);
	}

	mLightView = V;
	mLightProj = P;

	// 6) b6 업로드
	struct ShadowCB_ { Matrix LVP; Vector4 Params; } scb;
	scb.LVP = XMMatrixTranspose(V * P);
	scb.Params = Vector4(/*compareBias*/ 0.0f, 1.0f / mShadowW, 1.0f / mShadowH, 0.0f); // 비교바이어스 0으로 운용
	ctx->UpdateSubresource(mCB_Shadow.Get(), 0, nullptr, &scb, 0, 0);

	ID3D11Buffer* b6 = mCB_Shadow.Get();
	ctx->VSSetConstantBuffers(6, 1, &b6);
	ctx->PSSetConstantBuffers(6, 1, &b6);

	// 7) 메인 조명 CB(vLightDir)도 동일 정의로 유지
	//    셰이딩에서 NdotL = dot(N, -vLightDir)를 사용하도록 HLSL 확인
}

bool TutorialApp::CreateDepthOnlyShaders(ID3D11Device* dev)
{
	using Microsoft::WRL::ComPtr;
	ComPtr<ID3DBlob> vsPntt, vsSkin, psDepth;

	HR_T(CompileShaderFromFile(L"Shader/DepthOnly_VS.hlsl", "main", "vs_5_0", vsPntt.GetAddressOf()));
	HR_T(CompileShaderFromFile(L"Shader/DepthOnly_SkinnedVS.hlsl", "main", "vs_5_0", vsSkin.GetAddressOf()));
	HR_T(CompileShaderFromFile(L"Shader/DepthOnly_PS.hlsl", "main", "ps_5_0", psDepth.GetAddressOf()));

	HR_T(dev->CreateVertexShader(vsPntt->GetBufferPointer(), vsPntt->GetBufferSize(), nullptr, mVS_Depth.GetAddressOf()));
	HR_T(dev->CreateVertexShader(vsSkin->GetBufferPointer(), vsSkin->GetBufferSize(), nullptr, mVS_DepthSkinned.GetAddressOf()));
	HR_T(dev->CreatePixelShader(psDepth->GetBufferPointer(), psDepth->GetBufferSize(), nullptr, mPS_Depth.GetAddressOf()));

	// IL: PNTT
	static const D3D11_INPUT_ELEMENT_DESC IL_PNTT[] = {
		{"POSITION",0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD",0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	HR_T(dev->CreateInputLayout(IL_PNTT, _countof(IL_PNTT),
		vsPntt->GetBufferPointer(), vsPntt->GetBufferSize(), mIL_PNTT.GetAddressOf()));

	// IL: PNTT + Bone
	static const D3D11_INPUT_ELEMENT_DESC IL_SKIN[] = {
		{"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	HR_T(dev->CreateInputLayout(IL_SKIN, _countof(IL_SKIN),
		vsSkin->GetBufferPointer(), vsSkin->GetBufferSize(), mIL_PNTT_BW.GetAddressOf()));

	return true;
}

void TutorialApp::BuildLightCameraAndUpload(ID3D11DeviceContext* ctx,
	const DirectX::SimpleMath::Vector3& camPos,
	const DirectX::SimpleMath::Vector3& camForward,
	const DirectX::SimpleMath::Vector3& lightDir_unit,
	float focusDist, float lightDist)
{
	// === 라이트 카메라 구성 ===
	using namespace DirectX;
	const Vector3 eyePos = m_Camera.m_World.Translation();
	const Vector3 camFwd = m_Camera.GetForward();

	// 1) lookAt 결정
	Vector3 lookAt;
	if (mShUI.followCamera) {
		lookAt = eyePos + camFwd * mShUI.focusDist;
	}
	else {
		lookAt = XMLoadFloat3(&mShUI.manualTarget);
	}

	// 2) 라이트 방향 (기존 yaw/pitch → vLightDir)
	Vector3 Ldir(vLightDir.x, vLightDir.y, vLightDir.z);
	Ldir.Normalize();

	// 3) lightPos 결정 (directional을 '계산용 위치'로)
	Vector3 lightPos;
	if (mShUI.useManualPos) {
		lightPos = XMLoadFloat3(&mShUI.manualPos);
	}
	else {
		lightPos = lookAt - Ldir * mShUI.lightDist;
	}

	// 4) View 행렬
	const Matrix lightView = Matrix::CreateLookAt(lightPos, lookAt, Vector3::UnitY);

	// 5) Proj 행렬
	float aspectSh = float(mShadowW) / float(mShadowH);

	// “카메라 화면의 focusDist 평면 직사각형”을 라이트 프러스텀이 덮게(자동 커버)
	if (mShUI.autoCover) {
		// 카메라 파라미터에서 반높이/반너비
		const float camFovY = XMConvertToRadians(m_FovDegree);
		const float halfH = tanf(camFovY * 0.5f) * mShUI.focusDist;
		const float halfW = halfH * (m_ClientWidth / float(m_ClientHeight));
		const float r = sqrtf(halfW * halfW + halfH * halfH) * mShUI.coverMargin;

		// lightDist 기준으로 FOV / 클립 자동 산출
		const float d = mShUI.lightDist;
		const float nz = max(d - r, 0.01f);
		const float fz = d + r;
		mShadowNear = nz;
		mShadowFar = fz;
		mShadowFovY = 2.0f * atanf(r / d); // 라이트 FOVY 자동화
	}

	// 최종 투영
	const Matrix lightProj = Matrix::CreatePerspectiveFieldOfView(mShadowFovY, aspectSh, mShadowNear, mShadowFar);
	const Matrix lightVP = lightView * lightProj;

	// (디버그용 보관)
	mLightView = lightView;
	mLightProj = lightProj;

	// 4) CB(b6) 업로드 (Shared.hlsli: LightViewProj, ShadowParams)
	struct ShadowCB_ {
		DirectX::XMFLOAT4X4 LVP;
		DirectX::XMFLOAT4   Params; // x=bias(미사용), y=1/w, z=1/h, w=0
	} scb{};

	auto LVP = (mLightView * mLightProj).Transpose();
	XMStoreFloat4x4(&scb.LVP, LVP);
	scb.Params = { 0.0f, 1.0f / mShadowW, 1.0f / mShadowH, 0.0f };

	ctx->UpdateSubresource(mCB_Shadow.Get(), 0, nullptr, &scb, 0, 0);
	ID3D11Buffer* b6 = mCB_Shadow.Get();
	ctx->VSSetConstantBuffers(6, 1, &b6); // b6: ShadowCB
	ctx->PSSetConstantBuffers(6, 1, &b6);
}

void TutorialApp::RenderShadowPass(ID3D11DeviceContext* ctx,
	const DirectX::SimpleMath::Vector3& camPos,
	const DirectX::SimpleMath::Vector3& camForward,
	const DirectX::SimpleMath::Vector3& lightDir_unit,
	float focusDist, float lightDist)
{
	// 0) LightViewProj 계산 + CB(b6) 업로드
	BuildLightCameraAndUpload(ctx, camPos, camForward, lightDir_unit, focusDist, lightDist);

	// 1) 타깃 설정 (컬러 RTV 없음, DSV만)
	ctx->OMSetRenderTargets(0, nullptr, mShadowDSV.Get());
	ctx->ClearDepthStencilView(mShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	ctx->RSSetViewports(1, &mShadowVP);
	ctx->RSSetState(mRS_ShadowBias.Get());

	// 2) 공용 상태
	float blend[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, blend, 0xFFFFFFFF);
	ID3D11SamplerState* cmp = mSamShadowCmp.Get();
	ctx->PSSetSamplers(1, 1, &cmp); // s1

	ID3D11SamplerState* lin = m_pSamplerLinear;
	ctx->PSSetSamplers(0, 1, &lin); // s0 (samLinear) - opacity 샘플링용	
}



void TutorialApp::UninitScene()
{
	// FBX 전용 파이프라인 자원
	SAFE_RELEASE(m_pMeshIL);
	SAFE_RELEASE(m_pMeshVS);
	SAFE_RELEASE(m_pMeshPS);
	SAFE_RELEASE(m_pConstantBuffer);

	SAFE_RELEASE(m_pUseCB);
	SAFE_RELEASE(m_pNoCullRS);
	SAFE_RELEASE(m_pSamplerLinear);
	SAFE_RELEASE(m_pBlinnCB);

	SAFE_RELEASE(m_pSkyVS);
	SAFE_RELEASE(m_pSkyPS);
	SAFE_RELEASE(m_pSkyIL);
	SAFE_RELEASE(m_pSkyVB);
	SAFE_RELEASE(m_pSkyIB);
	SAFE_RELEASE(m_pSkySRV);
	SAFE_RELEASE(m_pSkySampler);
	SAFE_RELEASE(m_pSkyDSS);
	SAFE_RELEASE(m_pSkyRS);

	SAFE_RELEASE(m_pDbgRS);
	SAFE_RELEASE(m_pArrowIB);
	SAFE_RELEASE(m_pArrowVB);
	SAFE_RELEASE(m_pDbgIL);
	SAFE_RELEASE(m_pDbgVS);
	SAFE_RELEASE(m_pDbgPS);
	SAFE_RELEASE(m_pDbgCB);

	SAFE_RELEASE(m_pWireRS);
	SAFE_RELEASE(m_pCullBackRS);
	SAFE_RELEASE(m_pDSS_Disabled);

	SAFE_RELEASE(m_pBS_Alpha);
	SAFE_RELEASE(m_pDSS_Opaque);
	SAFE_RELEASE(m_pDSS_Trans);
	//머가 이리 많음
	SAFE_RELEASE(m_pSkinnedIL);
	SAFE_RELEASE(m_pSkinnedVS);
	SAFE_RELEASE(m_pBoneCB);
	//툰툰
	SAFE_RELEASE(m_pRampSRV);
	SAFE_RELEASE(m_pToonCB);
}
