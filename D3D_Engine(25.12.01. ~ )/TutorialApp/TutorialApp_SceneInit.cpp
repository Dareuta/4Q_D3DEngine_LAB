// InitScene / UninitScene + 섀도우 리소스/상태 생성

#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"

#include <d3dcompiler.h>


UINT TutorialApp::GetMipCountFromSRV(ID3D11ShaderResourceView* srv)
{
	if (!srv) return 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
	srv->GetDesc(&sd);

	UINT mips = 0;
	if (sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE) mips = sd.TextureCube.MipLevels;
	if (sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)   mips = sd.Texture2D.MipLevels;

	if (mips == 0 || mips == UINT(-1))
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> res;
		srv->GetResource(&res);

		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if (SUCCEEDED(res.As(&tex)))
		{
			D3D11_TEXTURE2D_DESC td{};
			tex->GetDesc(&td);
			mips = td.MipLevels;
		}
	}
	return mips;
}

bool TutorialApp::LoadIBLSet(int idx)
{
	struct Set {
		const char* name;
		const wchar_t* env;
		const wchar_t* irr;
		const wchar_t* pref;
	};

	static const Set kSets[] = {
		{ "BakerSample",
		  L"../Resource/SkyBox/Sample/BakerSampleEnvHDR.dds",
		  L"../Resource/SkyBox/Sample/BakerSampleDiffuseHDR.dds",
		  L"../Resource/SkyBox/Sample/BakerSampleSpecularHDR.dds" },

		{ "Skybox_B",
		  L"../Resource/SkyBox/Indoor/indoorEnvHDR.dds",
		  L"../Resource/SkyBox/Indoor/indoorDiffuseHDR.dds",
		  L"../Resource/SkyBox/Indoor/indoorSpecularHDR.dds" },

		{ "Skybox_C",
		  L"../Resource/SkyBox/Bridge/bridgeEnvHDR.dds",
		  L"../Resource/SkyBox/Bridge/bridgeDiffuseHDR.dds",
		  L"../Resource/SkyBox/Bridge/bridgeSpecularHDR.dds" },
	};

	if (idx < 0 || idx >= (int)_countof(kSets)) return false;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> env, irr, pref;

	auto TryLoad = [&](const wchar_t* path, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out) -> bool {
		HRESULT hr = CreateDDSTextureFromFile(m_pDevice, path, nullptr, out.GetAddressOf());
		if (FAILED(hr)) {
			wprintf(L"[IBL] load failed: %s (hr=0x%08X)\n", path, (unsigned)hr);
			return false;
		}
		return true;
		};

	if (!TryLoad(kSets[idx].env, env))  return false;
	if (!TryLoad(kSets[idx].irr, irr))  return false;
	if (!TryLoad(kSets[idx].pref, pref)) return false;

	// 실제 렌더 경로가 MDR 멤버를 쓰고 있으니 거기에 박아줌
	mSkyEnvMDRSRV = env;
	mIBLIrrMDRSRV = irr;
	mIBLPrefMDRSRV = pref;

	// (HDR 멤버도 같이 맞춰두면 헷갈림 덜함)
	mSkyEnvHDRSRV = env;
	mIBLIrrHDRSRV = irr;
	mIBLPrefHDRSRV = pref;

	UINT mipCount = GetMipCountFromSRV(pref.Get());
	mPrefilterMaxMip = (mipCount > 0) ? float(mipCount - 1) : 0.0f;

	mIBLSetIndex = idx;

	printf("[IBL] switched set=%d, prefilter mips=%u (maxMip=%.0f)\n", idx, mipCount, mPrefilterMaxMip);
	return true;
}

static void LogSRV(const wchar_t* name, ID3D11ShaderResourceView* srv)
{
	if (!srv) { wprintf(L"%s: NULL\n", name); return; }

	D3D11_SHADER_RESOURCE_VIEW_DESC d{};
	srv->GetDesc(&d);

	UINT mips = 0;
	if (d.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE)  mips = d.TextureCube.MipLevels;
	if (d.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)    mips = d.Texture2D.MipLevels;

	wprintf(L"%s: dim=%d mips=%u\n", name, d.ViewDimension, mips);
} // 디버그용


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
		Compile(L"../Resource/Shader/VertexShader.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pMeshVS);

		const D3D11_INPUT_ELEMENT_DESC IL_PNTT[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_PNTT, _countof(IL_PNTT), vsb, &m_pMeshIL);

		Compile(L"../Resource/Shader/PixelShader.hlsl", "main", "ps_5_0", psb);
		CreatePS(psb, &m_pMeshPS);
	}

	// =========================================================
	// 2) DebugColor shaders & IL
	// =========================================================
	{
		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"../Resource/Shader/DebugColor_VS.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pDbgVS);

		const D3D11_INPUT_ELEMENT_DESC IL_DBG[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_DBG, _countof(IL_DBG), vsb, &m_pDbgIL);

		Compile(L"../Resource/Shader/DebugColor_PS.hlsl", "main", "ps_5_0", psb);
		CreatePS(psb, &m_pDbgPS);
	}

	//=====================================
	// PBR Pixel Shader
	ID3DBlob* pbrPS = nullptr;
	HR_T(CompileShaderFromFile(L"../Resource/Shader/PBR_PS.hlsl", "main", "ps_5_0", &pbrPS));
	HR_T(m_pDevice->CreatePixelShader(pbrPS->GetBufferPointer(), pbrPS->GetBufferSize(), nullptr, &m_pPBRPS));
	SAFE_RELEASE(pbrPS);

	// PBR Params CB (b8)
	D3D11_BUFFER_DESC bd{};
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CB_PBRParams);
	HR_T(m_pDevice->CreateBuffer(&bd, nullptr, &m_pPBRParamsCB));

	// =========================================================
	// ToneMap: shaders + CB + sampler
	// =========================================================

	{
		using Microsoft::WRL::ComPtr;

		ComPtr<ID3DBlob> vsb, psb;
		Compile(L"../Resource/Shader/ToneMap.hlsl", "VS_Main", "vs_5_0", vsb);
		Compile(L"../Resource/Shader/ToneMap.hlsl", "PS_Main", "ps_5_0", psb);

		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, mVS_ToneMap.GetAddressOf()));
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, mPS_ToneMap.GetAddressOf()));

		// b10
		D3D11_BUFFER_DESC bd{};
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CB_ToneMap);
		HR_T(m_pDevice->CreateBuffer(&bd, nullptr, mCB_ToneMap.GetAddressOf()));

		// s0 clamp
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		HR_T(m_pDevice->CreateSamplerState(&sd, mSamToneMapClamp.GetAddressOf()));
	}

	// =========================================================
	// Deferred: GBuffer + Light + Debug
	// =========================================================
	{
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3DBlob> vsb, psb;

		Compile(L"../Resource/Shader/Deferred_GBuffer.hlsl", "VS_Main", "vs_5_0", vsb);
		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, mVS_GBuffer.GetAddressOf()));

		Compile(L"../Resource/Shader/Deferred_GBuffer.hlsl", "PS_Main", "ps_5_0", psb);
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, mPS_GBuffer.GetAddressOf()));
	}

	{
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3DBlob> vsb, psb;

		Compile(L"../Resource/Shader/Deferred_Light.hlsl", "VS_Main", "vs_5_0", vsb);
		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, mVS_DeferredLight.GetAddressOf()));

		Compile(L"../Resource/Shader/Deferred_Light.hlsl", "PS_Main", "ps_5_0", psb);
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, mPS_DeferredLight.GetAddressOf()));
	}

	{
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3DBlob> psb;

		Compile(L"../Resource/Shader/GBufferDebug.hlsl", "PS_Main", "ps_5_0", psb);
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, mPS_GBufferDebug.GetAddressOf()));

		D3D11_BUFFER_DESC bd{};
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CB_GBufferDebug);
		HR_T(m_pDevice->CreateBuffer(&bd, nullptr, mCB_GBufferDebug.GetAddressOf()));
	}

	// =========================================================
	// 3) Skinned VS(+IL)
	// =========================================================
	{
		D3D_SHADER_MACRO defs[] = { {"SKINNED","1"}, {nullptr,nullptr} };
		ComPtr<ID3DBlob> vsb;
		HR_T(D3DCompileFromFile(L"../Resource/Shader/VertexShaderSkinning.hlsl",
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

		// Deferred Light CB (PS b12): point lights array
		if (!mCB_DeferredLights) MakeCB(sizeof(CB_DeferredLights), mCB_DeferredLights.GetAddressOf());

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
	mZeldaX.pos = { 0, -150, 350 };  mZeldaX.initPos = mZeldaX.pos;
	mBoxX.pos = { -200, -150, 400 }; mBoxX.scl = { 0.2f,0.2f,0.2f };
	mSkinX.pos = { 200, -150, 400 };
	mFemaleX.pos = { 0, -180, 200 };

	mTreeX.enabled = mCharX.enabled = mZeldaX.enabled = mBoxX.enabled = mSkinX.enabled = false;

	mTreeX.initScl = mTreeX.scl; mCharX.initScl = mCharX.scl; mZeldaX.initScl = mZeldaX.scl;
	mTreeX.initRotD = mTreeX.rotD; mCharX.initRotD = mCharX.rotD; mZeldaX.initRotD = mZeldaX.rotD;
	mBoxX.initScl = mBoxX.scl; mBoxX.initRotD = mBoxX.rotD; mBoxX.initPos = mBoxX.pos;
	mSkinX.initScl = mSkinX.scl; mSkinX.initRotD = mSkinX.rotD; mSkinX.initPos = mSkinX.pos;

	mFemaleX.initScl = mFemaleX.scl; mFemaleX.initRotD = mFemaleX.rotD; mFemaleX.initPos = mFemaleX.pos;

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

		BuildAll(L"../Resource/FBX/char.fbx", L"../Resource/FBX/", gFemale, gFemaleMtls);

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
		Compile(L"../Resource/Shader/Sky_VS.hlsl", "main", "vs_5_0", vsb);
		CreateVS(vsb, &m_pSkyVS);
		const D3D11_INPUT_ELEMENT_DESC IL_SKY[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		CreateIL(IL_SKY, _countof(IL_SKY), vsb, &m_pSkyIL);

		Compile(L"../Resource/Shader/Sky_PS.hlsl", "main", "ps_5_0", psb);
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
		//Hanako << 디버그용 큐브맵 이름
		//HR_T(CreateDDSTextureFromFile(m_pDevice, L"../Resource/SkyBox/TestCubemap/Cubemap.dds", nullptr, &m_pSkySRV));
		//HR_T(CreateDDSTextureFromFile(m_pDevice, L"../Resource/SkyBox/TestCubemap/Hanako.dds", nullptr, &m_pSkySRV));

		HR_T(CreateDDSTextureFromFile(m_pDevice, L"../Resource/SkyBox/baseBrdf.dds", nullptr, &mIBLBrdfSRV));

		// 1~3) Env/Irr/Pref 는 LoadIBLSet()에서 통일
		mIBLSetIndex = 0;

		if (!LoadIBLSet(mIBLSetIndex))
		{
			// 너 스타일에 맞게 처리해. 일단은 강제 중단이 낫다.
			std::cout << "먼가 잘못된거심" << std::endl;
			HR_T(E_FAIL);
		}

		// (선택) 한번만 찍고 싶으면 LoadIBLSet 안에서 찍는 게 더 깔끔함
		LogSRV(L"BRDF", mIBLBrdfSRV.Get());

		D3D11_SAMPLER_DESC iblSd{};
		iblSd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		iblSd.AddressU = iblSd.AddressV = iblSd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		iblSd.MaxLOD = D3D11_FLOAT32_MAX;
		HR_T(m_pDevice->CreateSamplerState(&iblSd, &mSamIBLClamp));

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
		Compile(L"../Resource/Shader/DbgGrid.hlsl", "VS_Main", "vs_5_0", vsb);
		Compile(L"../Resource/Shader/DbgGrid.hlsl", "PS_Main", "ps_5_0", psb);
		HR_T(m_pDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &mGridVS));
		HR_T(m_pDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &mGridPS));

		const D3D11_INPUT_ELEMENT_DESC IL_GRID[] = {
			{ "POSITION",0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		CreateIL(IL_GRID, 1, vsb, &mGridIL);
	}

	{
		HR_T(DirectX::CreateWICTextureFromFile(
			m_pDevice, L"../Resource/Toon/RampTexture.png", nullptr, &m_pRampSRV));
	}

	{

		D3D11_BUFFER_DESC bd{};
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CB_Proc);

		HR_T(m_pDevice->CreateBuffer(&bd, nullptr, mCB_Proc.GetAddressOf()));
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
	//scb.Params = Vector4(/*compareBias*/ 0.0f, 1.0f / mShadowW, 1.0f / mShadowH, 0.0f); // 비교바이어스 0으로 운용
	scb.Params = Vector4(mShadowCmpBias, 1.0f / mShadowW, 1.0f / mShadowH, 0.0f);
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

	HR_T(CompileShaderFromFile(L"../Resource/Shader/DepthOnly_VS.hlsl", "main", "vs_5_0", vsPntt.GetAddressOf()));
	HR_T(CompileShaderFromFile(L"../Resource/Shader/DepthOnly_SkinnedVS.hlsl", "main", "vs_5_0", vsSkin.GetAddressOf()));
	HR_T(CompileShaderFromFile(L"../Resource/Shader/DepthOnly_PS.hlsl", "main", "ps_5_0", psDepth.GetAddressOf()));

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
	//SAFE_RELEASE(m_pSkySRV);
	//SAFE_RELEASE(m_pSkySampler);
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

	SAFE_RELEASE(m_pPBRPS);
	SAFE_RELEASE(m_pPBRParamsCB);
}
