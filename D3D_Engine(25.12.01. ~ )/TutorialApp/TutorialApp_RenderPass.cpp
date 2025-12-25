// 렌더링 패스 세분화
//  - Shadow (Dir / PointCube)
//  - Deferred (GBuffer / Light / Debug)
//  - Post (ToneMap)
//  - Forward (Sky / Opaque / Cutout / Transparent)
//  - Debug (Arrow / Grid / PointMarker)
//  - Pipeline bind helpers
//  - Static draw helpers

#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"

////////////////////////////////////////////////////////////////////////////////
// 1) SHADOW PASS (Depth Only) - Directional
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderShadowPass_Main(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB)
{
	// --- 상태 백업 ---
	ID3D11DepthStencilState* dssBefore = nullptr; UINT dssRefBefore = 0;
	ID3D11BlendState* bsBefore = nullptr; float bfBefore[4] = { 0,0,0,0 }; UINT maskBefore = 0xFFFFFFFF;
	ID3D11RasterizerState* rsBefore = nullptr;

	ctx->OMGetDepthStencilState(&dssBefore, &dssRefBefore);
	ctx->OMGetBlendState(&bsBefore, bfBefore, &maskBefore);
	ctx->RSGetState(&rsBefore); // AddRef

	// --- Shadow 렌더링 기본 상태 ---
	float bf0[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf0, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0); // depth write ON
	if (mRS_ShadowBias) ctx->RSSetState(mRS_ShadowBias.Get());

	// --- RT/DS/VP 백업 + Shadow DSV로 전환 ---
	ID3D11RenderTargetView* rtBefore = nullptr;
	ID3D11DepthStencilView* dsBefore = nullptr;
	ctx->OMGetRenderTargets(1, &rtBefore, &dsBefore);

	D3D11_VIEWPORT vpBefore{}; UINT vpCount = 1;
	ctx->RSGetViewports(&vpCount, &vpBefore);

	// shadow map SRV(t5)로 잡혀있을 수 있으니 hazard 방지용 언바인드
	{
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(5, 1, nullSRV);
	}

	ctx->OMSetRenderTargets(0, nullptr, mShadowDSV.Get());
	ctx->ClearDepthStencilView(mShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	ctx->RSSetViewports(1, &mShadowVP);

	// --- StaticMesh 깊이 드로우 헬퍼 ---
	auto DrawDepth_Static = [&](StaticMesh& mesh,
		const std::vector<MaterialGPU>& mtls,
		const Matrix& world,
		bool alphaCut)
		{
			// b0: 라이트 카메라 기준(View/Proj)으로 교체
			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(world);
			cbd.mWorldInvTranspose = world.Invert();
			cbd.mView = XMMatrixTranspose(mLightView);
			cbd.mProjection = XMMatrixTranspose(mLightProj);

			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			// Depth-only 파이프라인
			ctx->IASetInputLayout(m_pMeshIL);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

			for (size_t i = 0; i < mesh.Ranges().size(); ++i)
			{
				const auto& r = mesh.Ranges()[i];
				const auto& mat = mtls[r.materialIndex];
				const bool isCut = mat.hasOpacity;

				if (alphaCut != isCut) continue;

				UseCB use{};
				use.useOpacity = isCut ? 1u : 0u;
				use.alphaCut = isCut ? mShadowAlphaCut : -1.0f; // 컷아웃이면 clip() 활성
				ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
				ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

				// opacity 텍스처를 PS에서 clip()에 사용
				mat.Bind(ctx);
				mesh.DrawSubmesh(ctx, (UINT)i);
				MaterialGPU::Unbind(ctx);
			}
		};

	auto DrawShadowStatic = [&](auto& X, auto& mesh, auto& mtls)
		{
			if (!X.enabled) return;
			Matrix W = ComposeSRT(X);

			if (mDbg.showOpaque)      DrawDepth_Static(mesh, mtls, W, false);
			if (mDbg.showTransparent) DrawDepth_Static(mesh, mtls, W, true);  // alpha-cut만
		};

	// --- Static ---
	DrawShadowStatic(mTreeX, gTree, gTreeMtls);
	DrawShadowStatic(mCharX, gChar, gCharMtls);
	DrawShadowStatic(mZeldaX, gZelda, gZeldaMtls);
	DrawShadowStatic(mFemaleX, gFemale, gFemaleMtls);

	// --- Rigid Skeletal ---
	if (mBoxRig && mBoxX.enabled)
	{
		const Matrix W = ComposeSRT(mBoxX);

		ConstantBuffer cbd = baseCB;
		cbd.mWorld = XMMatrixTranspose(W);
		cbd.mWorldInvTranspose = Matrix::Identity;
		cbd.mView = XMMatrixTranspose(mLightView);
		cbd.mProjection = XMMatrixTranspose(mLightProj);

		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		ctx->IASetInputLayout(mIL_PNTT.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
		ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

		UseCB use{};
		use.useOpacity = 1u;
		use.alphaCut = mShadowAlphaCut;
		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mBoxRig->DrawDepthOnly(
			ctx, W,
			mLightView, mLightProj,
			m_pConstantBuffer,   // b0
			m_pUseCB,            // b2
			mVS_Depth.Get(),
			mPS_Depth.Get(),
			mIL_PNTT.Get(),
			mShadowAlphaCut
		);
	}

	// --- Skinned ---
	if (mSkinRig && mSkinX.enabled)
	{
		const Matrix W = ComposeSRT(mSkinX);

		ctx->IASetInputLayout(mIL_PNTT_BW.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(mVS_DepthSkinned.Get(), nullptr, 0);
		ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

		ConstantBuffer cbd = baseCB;
		cbd.mWorld = XMMatrixTranspose(W);
		cbd.mWorldInvTranspose = Matrix::Identity;
		cbd.mView = XMMatrixTranspose(mLightView);
		cbd.mProjection = XMMatrixTranspose(mLightProj);

		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		mSkinRig->DrawDepthOnly(
			ctx, W,
			mLightView, mLightProj,
			m_pConstantBuffer,   // b0
			m_pUseCB,            // b2 (alphaCut)
			m_pBoneCB,           // b4
			mVS_DepthSkinned.Get(),
			mPS_Depth.Get(),
			mIL_PNTT_BW.Get(),
			mShadowAlphaCut
		);
	}

	// --- RT/DS/VP 복구 ---
	ctx->OMSetRenderTargets(1, &rtBefore, dsBefore);
	ctx->RSSetViewports(1, &vpBefore);

	SAFE_RELEASE(rtBefore);
	SAFE_RELEASE(dsBefore);

	// --- 상태 복구 ---
	ctx->RSSetState(rsBefore);
	ctx->OMSetBlendState(bsBefore, bfBefore, maskBefore);
	ctx->OMSetDepthStencilState(dssBefore, dssRefBefore);

	SAFE_RELEASE(rsBefore);
	SAFE_RELEASE(bsBefore);
	SAFE_RELEASE(dssBefore);
}

////////////////////////////////////////////////////////////////////////////////
// 2) SHADOW PASS (Point Cube)
//  - Color cube: distNorm(dist/range) 저장
//  - Depth cube: nearest surface 확보
//  - 현재는 첫 번째 point light(mPoint)만 지원
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderPointShadowPass_Cube(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB)
{
	if (!mPoint.enable || !mPoint.shadowEnable) return;
	if (!mPointShadowTex || !mPointShadowSRV || !mCB_PointShadow) return;

	// 렌더 전에 SRV(t10) 언바인드(hazard 방지)
	{
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(10, 1, nullSRV);
	}

	// --- 상태 백업 ---
	ID3D11DepthStencilState* dssBefore = nullptr; UINT dssRefBefore = 0;
	ID3D11BlendState* bsBefore = nullptr; float bfBefore[4] = { 0,0,0,0 }; UINT maskBefore = 0xFFFFFFFF;
	ID3D11RasterizerState* rsBefore = nullptr;

	ID3D11RenderTargetView* rtBefore = nullptr;
	ID3D11DepthStencilView* dsBefore = nullptr;

	ctx->OMGetDepthStencilState(&dssBefore, &dssRefBefore);
	ctx->OMGetBlendState(&bsBefore, bfBefore, &maskBefore);
	ctx->RSGetState(&rsBefore);
	ctx->OMGetRenderTargets(1, &rtBefore, &dsBefore);

	D3D11_VIEWPORT vpBefore{}; UINT vpCount = 1;
	ctx->RSGetViewports(&vpCount, &vpBefore);

	// --- 기본 상태 ---
	float bf0[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf0, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
	if (mRS_ShadowBias) ctx->RSSetState(mRS_ShadowBias.Get());
	ctx->RSSetViewports(1, &mPointShadowVP);

	// --- b13 업로드 (pos/range + bias/enable) ---
	{
		CB_PointShadow pcb{};
		pcb.posRange = DirectX::XMFLOAT4(mPoint.pos.x, mPoint.pos.y, mPoint.pos.z, mPoint.range);
		pcb.params = DirectX::XMFLOAT4(mPoint.shadowBias, mPoint.shadowEnable ? 1.0f : 0.0f, 0.0f, 0.0f);

		ctx->UpdateSubresource(mCB_PointShadow.Get(), 0, nullptr, &pcb, 0, 0);
		ID3D11Buffer* b13 = mCB_PointShadow.Get();
		ctx->PSSetConstantBuffers(13, 1, &b13);
	}

	// --- Face camera setup (LH) ---
	const Vector3 pos = mPoint.pos;
	const Vector3 dirs[6] = {
		Vector3(1,0,0), Vector3(-1,0,0),
		Vector3(0,1,0), Vector3(0,-1,0),
		Vector3(0,0,1), Vector3(0,0,-1)
	};
	const Vector3 ups[6] = {
		Vector3::UnitY, Vector3::UnitY,
		Vector3(0,0,-1), Vector3(0,0, 1),
		Vector3::UnitY, Vector3::UnitY
	};

	// NOTE: SimpleMath CreatePerspectiveFieldOfView/CreateLookAt는 RH 기본.
	// 프로젝트가 LH이므로 DirectXMath LH를 사용.
	const Matrix P = XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, mPoint.range);

	// --- StaticMesh 깊이 드로우 헬퍼 ---
	auto DrawPointDepth_Static = [&](StaticMesh& mesh,
		const std::vector<MaterialGPU>& mtls,
		const Matrix& world,
		const Matrix& V,
		bool alphaCut)
		{
			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(world);
			cbd.mWorldInvTranspose = world.Invert();
			cbd.mView = XMMatrixTranspose(V);
			cbd.mProjection = XMMatrixTranspose(P);

			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			ctx->IASetInputLayout(m_pMeshIL);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_PointShadow.Get(), nullptr, 0);

			for (size_t i = 0; i < mesh.Ranges().size(); ++i)
			{
				const auto& r = mesh.Ranges()[i];
				const auto& mat = mtls[r.materialIndex];
				const bool isCut = mat.hasOpacity;
				if (alphaCut != isCut) continue;

				UseCB use{};
				use.useOpacity = isCut ? 1u : 0u;
				use.alphaCut = isCut ? mShadowAlphaCut : -1.0f;

				ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
				ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

				mat.Bind(ctx);
				mesh.DrawSubmesh(ctx, (UINT)i);
				MaterialGPU::Unbind(ctx);
			}
		};

	auto DrawPointShadowStatic = [&](auto& X, auto& mesh, auto& mtls, const Matrix& V)
		{
			if (!X.enabled) return;
			Matrix W = ComposeSRT(X);

			if (mDbg.showOpaque)      DrawPointDepth_Static(mesh, mtls, W, V, false);
			if (mDbg.showTransparent) DrawPointDepth_Static(mesh, mtls, W, V, true);
		};

	// --- per-face render ---
	for (UINT face = 0; face < 6; ++face)
	{
		ID3D11RenderTargetView* rtv = mPointShadowRTV[face].Get();
		ID3D11DepthStencilView* dsv = mPointShadowDSV[face].Get();
		ctx->OMSetRenderTargets(1, &rtv, dsv);

		const float clear[4] = { 1,1,1,1 };
		ctx->ClearRenderTargetView(rtv, clear);
		ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

		const Matrix V = XMMatrixLookAtLH(pos, pos + dirs[face], ups[face]);

		// Static
		DrawPointShadowStatic(mTreeX, gTree, gTreeMtls, V);
		DrawPointShadowStatic(mCharX, gChar, gCharMtls, V);
		DrawPointShadowStatic(mZeldaX, gZelda, gZeldaMtls, V);
		DrawPointShadowStatic(mFemaleX, gFemale, gFemaleMtls, V);

		// Rigid
		if (mBoxRig && mBoxX.enabled)
		{
			const Matrix W = ComposeSRT(mBoxX);

			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(W);
			cbd.mWorldInvTranspose = Matrix::Identity;
			cbd.mView = XMMatrixTranspose(V);
			cbd.mProjection = XMMatrixTranspose(P);

			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			ctx->IASetInputLayout(mIL_PNTT.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_PointShadow.Get(), nullptr, 0);

			UseCB use{};
			use.useOpacity = 1u;
			use.alphaCut = mShadowAlphaCut;
			ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
			ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

			mBoxRig->DrawDepthOnly(
				ctx, W,
				V, P,
				m_pConstantBuffer,
				m_pUseCB,
				mVS_Depth.Get(),
				mPS_PointShadow.Get(),
				mIL_PNTT.Get(),
				mShadowAlphaCut
			);
		}

		// Skinned
		if (mSkinRig && mSkinX.enabled)
		{
			const Matrix W = ComposeSRT(mSkinX);

			ctx->IASetInputLayout(mIL_PNTT_BW.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_DepthSkinned.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_PointShadow.Get(), nullptr, 0);

			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(W);
			cbd.mWorldInvTranspose = Matrix::Identity;
			cbd.mView = XMMatrixTranspose(V);
			cbd.mProjection = XMMatrixTranspose(P);

			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			mSkinRig->DrawDepthOnly(
				ctx, W,
				V, P,
				m_pConstantBuffer,
				m_pUseCB,
				m_pBoneCB,
				mVS_DepthSkinned.Get(),
				mPS_PointShadow.Get(),
				mIL_PNTT_BW.Get(),
				mShadowAlphaCut
			);
		}
	}

	// --- 상태 복구 ---
	ctx->OMSetRenderTargets(1, &rtBefore, dsBefore);
	ctx->RSSetViewports(1, &vpBefore);
	ctx->RSSetState(rsBefore);
	ctx->OMSetBlendState(bsBefore, bfBefore, maskBefore);
	ctx->OMSetDepthStencilState(dssBefore, dssRefBefore);

	SAFE_RELEASE(rtBefore);
	SAFE_RELEASE(dsBefore);
	SAFE_RELEASE(rsBefore);
	SAFE_RELEASE(bsBefore);
	SAFE_RELEASE(dssBefore);
}

////////////////////////////////////////////////////////////////////////////////
// 3) DEFERRED: GBuffer Pass
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::BindStaticMeshPipeline_GBuffer(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(mVS_GBuffer.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_GBuffer.Get(), nullptr, 0);
}

void TutorialApp::RenderGBufferPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB)
{
	// RS 백업/설정
	ID3D11RasterizerState* oldRS = nullptr;
	ctx->RSGetState(&oldRS);

	if (mDbg.cullNone && m_pDbgRS) ctx->RSSetState(m_pDbgRS);
	else                          ctx->RSSetState(m_pCullBackRS);

	// 기본 상태
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);

	BindStaticMeshPipeline_GBuffer(ctx);

	// NOTE: DrawStatic...이 내부에서 PBR/Blinn 분기하는 구조라
	// deferred에서는 강제로 PBR enable=true로 고정하는 게 안전.
	const bool oldPBR = mPbr.enable;
	mPbr.enable = true;

	if (mDbg.showOpaque)
	{
		if (mTreeX.enabled)   DrawStaticOpaqueOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)   DrawStaticOpaqueOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled)  DrawStaticOpaqueOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);
		if (mFemaleX.enabled) DrawStaticOpaqueOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
	}

	if (mDbg.forceAlphaClip && mDbg.showTransparent)
	{
		if (mTreeX.enabled)   DrawStaticAlphaCutOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)   DrawStaticAlphaCutOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled)  DrawStaticAlphaCutOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);
		if (mFemaleX.enabled) DrawStaticAlphaCutOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
	}

	mPbr.enable = oldPBR;

	// RS 복구
	ctx->RSSetState(oldRS);
	SAFE_RELEASE(oldRS);
}

////////////////////////////////////////////////////////////////////////////////
// 4) DEFERRED: Light Pass (FullScreen Tri)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderDeferredLightPass(ID3D11DeviceContext* ctx)
{
	// blending off
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);

	// fullscreen pass는 depth state 잘못 건드리면 바로 망가짐
	ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : nullptr, 0);

	// fullscreen tri (SV_VertexID)
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_DeferredLight.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_DeferredLight.Get(), nullptr, 0);

	if (m_pConstantBuffer) ctx->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

	// GBuffer + Shadow (t0~t5)
	ID3D11ShaderResourceView* srvs[6] =
	{
		mGBufferSRV[0].Get(), // t0 worldpos
		mGBufferSRV[1].Get(), // t1 normal
		mGBufferSRV[2].Get(), // t2 albedo
		mGBufferSRV[3].Get(), // t3 mr
		nullptr,              // t4 (reserved)
		mShadowSRV.Get()      // t5 shadow map
	};
	ctx->PSSetShaderResources(0, 6, srvs);

	// IBL (t7~t9) + sampler(s3)
	ID3D11ShaderResourceView* ibl[3] =
	{
		mIBLIrrMDRSRV.Get(),
		mIBLPrefMDRSRV.Get(),
		mIBLBrdfSRV.Get()
	};
	ctx->PSSetShaderResources(7, 3, ibl);

	ID3D11SamplerState* sIBL = mSamIBLClamp ? mSamIBLClamp.Get() : m_pSamplerLinear;
	ctx->PSSetSamplers(3, 1, &sIBL);

	// Shadow CB(b6) + compare sampler(s1)
	if (mCB_Shadow)
	{
		ID3D11Buffer* b6 = mCB_Shadow.Get();
		ctx->PSSetConstantBuffers(6, 1, &b6);
	}
	if (mSamShadowCmp)
	{
		ID3D11SamplerState* cmp = mSamShadowCmp.Get();
		ctx->PSSetSamplers(1, 1, &cmp);
	}

	// Point lights (b12)
	if (mCB_DeferredLights)
	{
		ID3D11Buffer* b12 = mCB_DeferredLights.Get();
		ctx->PSSetConstantBuffers(12, 1, &b12);
	}

	ctx->Draw(3, 0);

	// hazard 정리
	ID3D11ShaderResourceView* nullSRV6[6] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(0, 6, nullSRV6);

	ID3D11ShaderResourceView* nullIBL[3] = { nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(7, 3, nullIBL);

	// depth state 기본으로 복구
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
}

////////////////////////////////////////////////////////////////////////////////
// 5) DEFERRED: GBuffer Debug View (FullScreen Tri)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderGBufferDebugPass(ID3D11DeviceContext* ctx)
{
	if (!mPS_GBufferDebug || !mCB_GBufferDebug) return;

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : nullptr, 0);

	// fullscreen tri (SV_VertexID)
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_DeferredLight.Get(), nullptr, 0); // 동일 VS 재사용
	ctx->PSSetShader(mPS_GBufferDebug.Get(), nullptr, 0);

	// b11 업데이트
	CB_GBufferDebug cb{};
	cb.mode = (UINT)mDbg.gbufferMode;
	cb.posRange = mDbg.gbufferPosRange;

	ctx->UpdateSubresource(mCB_GBufferDebug.Get(), 0, nullptr, &cb, 0, 0);
	ID3D11Buffer* b11 = mCB_GBufferDebug.Get();
	ctx->PSSetConstantBuffers(11, 1, &b11);

	// t0~t3
	ID3D11ShaderResourceView* srvs[4] =
	{
		mGBufferSRV[0].Get(),
		mGBufferSRV[1].Get(),
		mGBufferSRV[2].Get(),
		mGBufferSRV[3].Get(),
	};
	ctx->PSSetShaderResources(0, 4, srvs);

	ctx->Draw(3, 0);

	// hazard 정리
	ID3D11ShaderResourceView* nullSRV4[4] = { nullptr,nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(0, 4, nullSRV4);

	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
}

////////////////////////////////////////////////////////////////////////////////
// 6) POST: ToneMap (SceneHDR -> BackBuffer)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderToneMapPass(ID3D11DeviceContext* ctx)
{
	if (!mSceneHDRSRV || !mVS_ToneMap || !mPS_ToneMap || !mCB_ToneMap) return;

	// RS 백업/설정
	ID3D11RasterizerState* oldRS = nullptr;
	ctx->RSGetState(&oldRS);
	ctx->RSSetState(m_pDbgRS); // Solid + CullNone

	// BackBuffer로 출력
	ID3D11RenderTargetView* bb = m_pRenderTargetView;
	ctx->OMSetRenderTargets(1, &bb, nullptr);

	// viewport = 화면 전체
	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0; vp.TopLeftY = 0;
	vp.Width = (float)m_ClientWidth;
	vp.Height = (float)m_ClientHeight;
	vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	// 상태
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : nullptr, 0);

	// fullscreen tri
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_ToneMap.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_ToneMap.Get(), nullptr, 0);

	// b10 업데이트
	CB_ToneMap cb{};
	cb.exposureEV = mTone.exposureEV;
	cb.gamma = mTone.gamma;
	cb.operatorId = (mTone.enable ? (UINT)mTone.operatorId : 0u);
	cb.flags = 1u; // gamma 적용

	ctx->UpdateSubresource(mCB_ToneMap.Get(), 0, nullptr, &cb, 0, 0);
	ID3D11Buffer* b10 = mCB_ToneMap.Get();
	ctx->PSSetConstantBuffers(10, 1, &b10);

	// t0: SceneHDR
	ID3D11ShaderResourceView* hdr = mSceneHDRSRV.Get();
	ctx->PSSetShaderResources(0, 1, &hdr);

	// s0: clamp (없으면 linear)
	ID3D11SamplerState* samp = mSamToneMapClamp ? mSamToneMapClamp.Get() : m_pSamplerLinear;
	ctx->PSSetSamplers(0, 1, &samp);

	ctx->Draw(3, 0);

	// hazard 정리
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ctx->PSSetShaderResources(0, 1, nullSRV);

	// RS 복구
	ctx->RSSetState(oldRS);
	SAFE_RELEASE(oldRS);
}

////////////////////////////////////////////////////////////////////////////////
// 7) FORWARD: Sky
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderSkyPass(ID3D11DeviceContext* ctx, const Matrix& viewNoTrans)
{
	if (!mDbg.showSky) return;

	// 상태 백업
	ID3D11RasterizerState* oldRS = nullptr;
	ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef = 0;
	ctx->RSGetState(&oldRS);
	ctx->OMGetDepthStencilState(&oldDSS, &oldRef);

	// Sky 상태
	ctx->RSSetState(m_pSkyRS);
	ctx->OMSetDepthStencilState(m_pSkyDSS, 0);

	// Sky 파이프라인
	ctx->IASetInputLayout(m_pSkyIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pSkyVS, nullptr, 0);
	ctx->PSSetShader(m_pSkyPS, nullptr, 0);

	// b0 업데이트
	ConstantBuffer skyCB{};
	skyCB.mWorld = XMMatrixTranspose(Matrix::Identity);
	skyCB.mView = XMMatrixTranspose(viewNoTrans);
	skyCB.mProjection = XMMatrixTranspose(m_Projection);
	skyCB.mWorldInvTranspose = Matrix::Identity;

	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &skyCB, 0, 0);
	ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

	// t0: sky env
	ID3D11ShaderResourceView* sky = mSkyEnvMDRSRV.Get();
	ctx->PSSetShaderResources(0, 1, &sky);

	// s0: IBL clamp
	ID3D11SamplerState* s0 = mSamIBLClamp.Get();
	ctx->PSSetSamplers(0, 1, &s0);

	// draw cube
	UINT stride = sizeof(DirectX::XMFLOAT3), offset = 0;
	ctx->IASetVertexBuffers(0, 1, &m_pSkyVB, &stride, &offset);
	ctx->IASetIndexBuffer(m_pSkyIB, DXGI_FORMAT_R16_UINT, 0);
	ctx->DrawIndexed(36, 0, 0);

	// sky SRV 정리
	ID3D11ShaderResourceView* null0[1] = { nullptr };
	ctx->PSSetShaderResources(0, 1, null0);

	// 상태 복구
	ctx->RSSetState(oldRS);
	ctx->OMSetDepthStencilState(oldDSS, oldRef);
	SAFE_RELEASE(oldRS);
	SAFE_RELEASE(oldDSS);

	// 메쉬 파이프라인 기본 복구
	BindStaticMeshPipeline(ctx);
	if (m_pSamplerLinear) ctx->PSSetSamplers(0, 1, &m_pSamplerLinear);
}

////////////////////////////////////////////////////////////////////////////////
// 8) FORWARD: Opaque
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderOpaquePass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB, const DirectX::SimpleMath::Vector3& eye)
{
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Opaque, 0);

	if (!mDbg.showOpaque) return;

	BindStaticMeshPipeline(ctx);

	if (mTreeX.enabled)  DrawStaticOpaqueOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
	if (mCharX.enabled)  DrawStaticOpaqueOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
	if (mZeldaX.enabled) DrawStaticOpaqueOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);

	// 여자 모델: PBR 토글에 따라 PS 교체
	if (mFemaleX.enabled)
	{
		if (mPbr.enable) BindStaticMeshPipeline_PBR(ctx);
		else             BindStaticMeshPipeline(ctx);

		DrawStaticOpaqueOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);

		BindStaticMeshPipeline(ctx); // 다음 드로우 대비 원복
	}

	// Rigid
	if (mBoxRig && mBoxX.enabled)
	{
		mBoxRig->DrawOpaqueOnly(
			ctx, ComposeSRT(mBoxX),
			view, m_Projection, m_pConstantBuffer, m_pUseCB,
			baseCB.vLightDir, baseCB.vLightColor, eye,
			m_Ka, m_Ks, m_Shininess, m_Ia,
			mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
		);
	}

	// Skinned
	if (mSkinRig && mSkinX.enabled)
	{
		BindSkinnedMeshPipeline(ctx);
		mSkinRig->DrawOpaqueOnly(
			ctx, ComposeSRT(mSkinX),
			view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
			baseCB.vLightDir, baseCB.vLightColor, eye,
			m_Ka, m_Ks, m_Shininess, m_Ia,
			mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
		);
		BindStaticMeshPipeline(ctx);
	}
}

////////////////////////////////////////////////////////////////////////////////
// 9) FORWARD: Cutout (Alpha-Test 강제)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderCutoutPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB, const DirectX::SimpleMath::Vector3& eye)
{
	if (!mDbg.forceAlphaClip) return;

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);

	// RS 유지(원하는 경우)
	if (mDbg.cullNone && m_pDbgRS) ctx->RSSetState(m_pDbgRS);

	if (!mDbg.showTransparent) return;

	BindStaticMeshPipeline(ctx);

	if (mTreeX.enabled)  DrawStaticAlphaCutOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
	if (mCharX.enabled)  DrawStaticAlphaCutOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
	if (mZeldaX.enabled) DrawStaticAlphaCutOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);

	if (mFemaleX.enabled)
	{
		if (mPbr.enable) BindStaticMeshPipeline_PBR(ctx);
		else             BindStaticMeshPipeline(ctx);

		DrawStaticAlphaCutOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
		BindStaticMeshPipeline(ctx);
	}

	// Rigid
	if (mBoxRig && mBoxX.enabled)
	{
		mBoxRig->DrawAlphaCutOnly(
			ctx,
			ComposeSRT(mBoxX),
			view, m_Projection,
			m_pConstantBuffer,
			m_pUseCB,
			mDbg.alphaCut,
			baseCB.vLightDir, baseCB.vLightColor,
			eye,
			m_Ka, m_Ks, m_Shininess, m_Ia,
			mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
		);
	}

	// Skinned
	if (mSkinRig && mSkinX.enabled)
	{
		BindSkinnedMeshPipeline(ctx);
		mSkinRig->DrawAlphaCutOnly(
			ctx, ComposeSRT(mSkinX),
			view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
			baseCB.vLightDir, baseCB.vLightColor, eye,
			m_Ka, m_Ks, m_Shininess, m_Ia,
			mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
		);
		BindStaticMeshPipeline(ctx);
	}
}

////////////////////////////////////////////////////////////////////////////////
// 10) FORWARD: Transparent (Alpha Blend, 정렬 옵션)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderTransparentPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB, const DirectX::SimpleMath::Vector3& eye)
{
	// 투명 끄기 / 알파컷 강제면 이 패스는 스킵
	if (!mDbg.showTransparent) return;
	if (mDbg.forceAlphaClip)   return;

	// 상태 백업
	ID3D11BlendState* oldBS = nullptr; float oldBF[4]; UINT oldSM = 0xFFFFFFFF;
	ID3D11DepthStencilState* oldDSS = nullptr; UINT oldSR = 0;
	ctx->OMGetBlendState(&oldBS, oldBF, &oldSM);
	ctx->OMGetDepthStencilState(&oldDSS, &oldSR);

	// 투명 상태
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(m_pBS_Alpha, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Trans, 0); // DepthRead(WriteOff)인지 확인

	// --- 투명 큐 ---
	struct TItem { float keyZ; std::function<void()> draw; };
	std::vector<TItem> q; q.reserve(16);

	auto ViewZ = [&](const DirectX::SimpleMath::Matrix& W)->float
		{
			using namespace DirectX::SimpleMath;
			Vector3 p = W.Translation();
			Vector3 vp = Vector3::Transform(p, view);
			return vp.z;
		};

	auto PushStatic = [&](const XformUI& X, StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, bool usePBR)
		{
			if (!X.enabled) return;

			DirectX::SimpleMath::Matrix W = ComposeSRT(X);
			const float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[this, ctx, &mesh, &mtls, W, &baseCB, usePBR]()
				{
					if (usePBR) BindStaticMeshPipeline_PBR(ctx);
					else        BindStaticMeshPipeline(ctx);

					DrawStaticTransparentOnly(ctx, mesh, mtls, W, baseCB);

					if (usePBR) BindStaticMeshPipeline(ctx);
				}
				});
		};

	auto PushBoxRig = [&]()
		{
			if (!mBoxRig || !mBoxX.enabled) return;

			DirectX::SimpleMath::Matrix W = ComposeSRT(mBoxX);
			const float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[=, &baseCB, &eye]()
				{
					mBoxRig->DrawTransparentOnly(
						ctx, W,
						view, m_Projection, m_pConstantBuffer, m_pUseCB,
						baseCB.vLightDir, baseCB.vLightColor, eye,
						m_Ka, m_Ks, m_Shininess, m_Ia,
						mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
					);
					BindStaticMeshPipeline(ctx);
				}
				});
		};

	auto PushSkinRig = [&]()
		{
			if (!mSkinRig || !mSkinX.enabled) return;

			DirectX::SimpleMath::Matrix W = ComposeSRT(mSkinX);
			const float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[=, &baseCB, &eye]()
				{
					BindSkinnedMeshPipeline(ctx);
					mSkinRig->DrawTransparentOnly(
						ctx, W,
						view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
						baseCB.vLightDir, baseCB.vLightColor, eye,
						m_Ka, m_Ks, m_Shininess, m_Ia,
						mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
					);
					BindStaticMeshPipeline(ctx);
				}
				});
		};

	// 등록(현재: hasOpacity 서브메시만 DrawStaticTransparentOnly가 그린다)
	PushStatic(mTreeX, gTree, gTreeMtls, false);
	PushStatic(mCharX, gChar, gCharMtls, false);
	PushStatic(mZeldaX, gZelda, gZeldaMtls, false);
	PushStatic(mFemaleX, gFemale, gFemaleMtls, mPbr.enable);

	PushBoxRig();
	PushSkinRig();

	// 정렬(옵션)
	if (mDbg.sortTransparent)
	{
		std::stable_sort(q.begin(), q.end(),
			[](const TItem& a, const TItem& b) { return a.keyZ > b.keyZ; } // far -> near
		);
	}

	// 드로우
	for (auto& it : q) it.draw();

	// (선택) PBR SRV(t7~t9) 깔끔하게 정리
	ID3D11ShaderResourceView* nullIBL[3] = { nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(7, 3, nullIBL);

	// 상태 복구
	ctx->OMSetBlendState(oldBS, oldBF, oldSM);
	ctx->OMSetDepthStencilState(oldDSS, oldSR);
	SAFE_RELEASE(oldBS);
	SAFE_RELEASE(oldDSS);
}

////////////////////////////////////////////////////////////////////////////////
// 11) DEBUG PASS (Light Arrow / Grid / Point Marker)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::RenderDebugPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB, const DirectX::SimpleMath::Vector3& lightDir)
{
	// -------------------------------------------------------------------------
	// A) Directional Light Arrow
	// -------------------------------------------------------------------------
	if (mDbg.showLightArrow && mDbg.dirLightEnable)
	{
		using namespace DirectX::SimpleMath;

		Vector3 D = -lightDir; D.Normalize();
		Matrix worldArrow = Matrix::CreateScale(m_ArrowScale) * Matrix::CreateWorld(m_ArrowPos, D, Vector3::UnitY);

		ConstantBuffer local = baseCB;
		local.mWorld = XMMatrixTranspose(worldArrow);
		local.mWorldInvTranspose = worldArrow.Invert();

		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		// 상태 백업(최소)
		ID3D11RasterizerState* oRS = nullptr;
		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0;
		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF;
		ID3D11InputLayout* oIL = nullptr;
		ID3D11VertexShader* oVS = nullptr;
		ID3D11PixelShader* oPS = nullptr;

		ctx->RSGetState(&oRS);
		ctx->OMGetDepthStencilState(&oDSS, &oRef);
		ctx->OMGetBlendState(&oBS, oBF, &oSM);
		ctx->IAGetInputLayout(&oIL);
		ctx->VSGetShader(&oVS, nullptr, 0);
		ctx->PSGetShader(&oPS, nullptr, 0);

		// draw state
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
		if (m_pDbgRS) ctx->RSSetState(m_pDbgRS);

		UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT4), offset = 0;
		ctx->IASetInputLayout(m_pDbgIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 1, &m_pArrowVB, &stride, &offset);
		ctx->IASetIndexBuffer(m_pArrowIB, DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(m_pDbgVS, nullptr, 0);
		ctx->PSSetShader(m_pDbgPS, nullptr, 0);

		const UINT indexCount = 6 + 24 + 6 + 12;
		const DirectX::XMFLOAT4 kBright = { 1.0f, 0.95f, 0.2f, 1.0f };
		ctx->UpdateSubresource(m_pDbgCB, 0, nullptr, &kBright, 0, 0);
		ctx->PSSetConstantBuffers(3, 1, &m_pDbgCB);

		ctx->DrawIndexed(indexCount, 0, 0);

		// 복구
		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);
		ctx->IASetInputLayout(oIL);
		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);

		SAFE_RELEASE(oVS); SAFE_RELEASE(oPS); SAFE_RELEASE(oIL);
		SAFE_RELEASE(oBS); SAFE_RELEASE(oDSS); SAFE_RELEASE(oRS);
	}

	// -------------------------------------------------------------------------
	// B) Grid (상태/바인딩을 많이 건드리므로 전체 백업/복구)
	// -------------------------------------------------------------------------
	if (mDbg.showGrid)
	{
		using namespace DirectX::SimpleMath;

		// --- 상태/바인딩 백업 ---
		ID3D11RasterizerState* oRS = nullptr; ctx->RSGetState(&oRS);
		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; ctx->OMGetDepthStencilState(&oDSS, &oRef);
		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF; ctx->OMGetBlendState(&oBS, oBF, &oSM);

		ID3D11InputLayout* oIL = nullptr; ctx->IAGetInputLayout(&oIL);
		D3D11_PRIMITIVE_TOPOLOGY oTopo; ctx->IAGetPrimitiveTopology(&oTopo);

		ID3D11Buffer* oVB0 = nullptr; UINT oStride0 = 0, oOffset0 = 0;
		ctx->IAGetVertexBuffers(0, 1, &oVB0, &oStride0, &oOffset0);

		ID3D11Buffer* oIB = nullptr; DXGI_FORMAT oIBFmt = DXGI_FORMAT_UNKNOWN; UINT oIBOff = 0;
		ctx->IAGetIndexBuffer(&oIB, &oIBFmt, &oIBOff);

		ID3D11VertexShader* oVS = nullptr; ctx->VSGetShader(&oVS, nullptr, 0);
		ID3D11PixelShader* oPS = nullptr; ctx->PSGetShader(&oPS, nullptr, 0);

		ID3D11Buffer* oVSb0 = nullptr; ctx->VSGetConstantBuffers(0, 1, &oVSb0);
		ID3D11Buffer* oPSb0 = nullptr; ctx->PSGetConstantBuffers(0, 1, &oPSb0);

		// Grid가 건드리는 슬롯들
		ID3D11Buffer* oPSb6 = nullptr; ctx->PSGetConstantBuffers(6, 1, &oPSb6);
		ID3D11Buffer* oPSb9 = nullptr; ctx->PSGetConstantBuffers(9, 1, &oPSb9);
		ID3D11Buffer* oPSb12 = nullptr; ctx->PSGetConstantBuffers(12, 1, &oPSb12);
		ID3D11Buffer* oPSb13 = nullptr; ctx->PSGetConstantBuffers(13, 1, &oPSb13);
		ID3D11ShaderResourceView* oSRV10 = nullptr; ctx->PSGetShaderResources(10, 1, &oSRV10);

		ID3D11SamplerState* oSamp1 = nullptr; ctx->PSGetSamplers(1, 1, &oSamp1);
		ID3D11ShaderResourceView* oSRV5 = nullptr; ctx->PSGetShaderResources(5, 1, &oSRV5);

		// --- Shadow bind(그리드에서 shadow sample) ---
		if (mCB_Shadow && mShadowSRV && mSamShadowCmp)
		{
			ID3D11Buffer* b6 = mCB_Shadow.Get();
			ctx->PSSetConstantBuffers(6, 1, &b6);

			ID3D11SamplerState* cmp = mSamShadowCmp.Get();
			ctx->PSSetSamplers(1, 1, &cmp);

			ID3D11ShaderResourceView* sh = mShadowSRV.Get();
			ctx->PSSetShaderResources(5, 1, &sh);

			// Point shadow cube + point cb
			if (mPointShadowSRV && mCB_PointShadow)
			{
				ID3D11ShaderResourceView* ps = mPointShadowSRV.Get();
				ctx->PSSetShaderResources(10, 1, &ps);

				ID3D11Buffer* b13 = mCB_PointShadow.Get();
				ctx->PSSetConstantBuffers(13, 1, &b13);
			}
			if (mCB_DeferredLights)
			{
				ID3D11Buffer* b12 = mCB_DeferredLights.Get();
				ctx->PSSetConstantBuffers(12, 1, &b12);
			}
		}

		// --- ProcCB(b9) 업데이트 (물결/노이즈 등) ---
		mTimeSec += GameTimer::m_Instance->DeltaTime();

		CB_Proc pcb{};
		pcb.uProc1 = { mTimeSec, 18.0f, 0.5f, 0.0f };
		pcb.uProc2 = { 0.0f, 0.0f, 0.2f, 250.0f };
		pcb.uProc2.w = 1000.0f;

		ctx->UpdateSubresource(mCB_Proc.Get(), 0, nullptr, &pcb, 0, 0);
		ID3D11Buffer* b9 = mCB_Proc.Get();
		ctx->PSSetConstantBuffers(9, 1, &b9);

		// --- Grid draw ---
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
		ctx->RSSetState(m_pCullBackRS);

		ConstantBuffer local{};
		local.mWorld = XMMatrixTranspose(Matrix::Identity);
		local.mWorldInvTranspose = Matrix::Identity;
		local.mView = XMMatrixTranspose(view);
		local.mProjection = XMMatrixTranspose(m_Projection);
		local.vLightDir = baseCB.vLightDir;
		local.vLightColor = baseCB.vLightColor;

		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
		ctx->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		UINT stride = sizeof(DirectX::XMFLOAT3), offset = 0;
		ID3D11Buffer* vb = mGridVB.Get();

		ctx->IASetInputLayout(mGridIL.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetIndexBuffer(mGridIB.Get(), DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(mGridVS.Get(), nullptr, 0);
		ctx->PSSetShader(mGridPS.Get(), nullptr, 0);

		ctx->DrawIndexed(mGridIndexCount, 0, 0);

		// --- 복구(핵심) ---
		ctx->PSSetShaderResources(5, 1, &oSRV5);
		ctx->PSSetSamplers(1, 1, &oSamp1);
		ctx->PSSetConstantBuffers(6, 1, &oPSb6);
		ctx->PSSetConstantBuffers(9, 1, &oPSb9);
		ctx->PSSetShaderResources(10, 1, &oSRV10);
		ctx->PSSetConstantBuffers(12, 1, &oPSb12);
		ctx->PSSetConstantBuffers(13, 1, &oPSb13);

		ctx->VSSetConstantBuffers(0, 1, &oVSb0);
		ctx->PSSetConstantBuffers(0, 1, &oPSb0);

		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);

		ctx->IASetInputLayout(oIL);
		ctx->IASetPrimitiveTopology(oTopo);
		ctx->IASetVertexBuffers(0, 1, &oVB0, &oStride0, &oOffset0);
		ctx->IASetIndexBuffer(oIB, oIBFmt, oIBOff);

		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);

		// release
		SAFE_RELEASE(oSRV5);
		SAFE_RELEASE(oSamp1);
		SAFE_RELEASE(oPSb6);
		SAFE_RELEASE(oPSb9);
		SAFE_RELEASE(oPSb12);
		SAFE_RELEASE(oPSb13);
		SAFE_RELEASE(oSRV10);

		SAFE_RELEASE(oVSb0);
		SAFE_RELEASE(oPSb0);

		SAFE_RELEASE(oVS);
		SAFE_RELEASE(oPS);
		SAFE_RELEASE(oIL);
		SAFE_RELEASE(oVB0);
		SAFE_RELEASE(oIB);
		SAFE_RELEASE(oBS);
		SAFE_RELEASE(oDSS);
		SAFE_RELEASE(oRS);
	}

	// -------------------------------------------------------------------------
	// C) PointLight marker cube
	// -------------------------------------------------------------------------
	if (mPoint.enable && mPoint.showMarker && m_pPointMarkerVB && m_pPointMarkerIB)
	{
		using namespace DirectX::SimpleMath;

		Matrix worldMarker = Matrix::CreateScale(mPoint.markerSize) * Matrix::CreateTranslation(mPoint.pos);

		ConstantBuffer local = baseCB;
		local.mWorld = XMMatrixTranspose(worldMarker);
		local.mWorldInvTranspose = worldMarker.Invert();

		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		// 상태 백업(최소)
		ID3D11RasterizerState* oRS = nullptr;
		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0;
		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF;
		ID3D11InputLayout* oIL = nullptr;
		ID3D11VertexShader* oVS = nullptr;
		ID3D11PixelShader* oPS = nullptr;

		ctx->RSGetState(&oRS);
		ctx->OMGetDepthStencilState(&oDSS, &oRef);
		ctx->OMGetBlendState(&oBS, oBF, &oSM);
		ctx->IAGetInputLayout(&oIL);
		ctx->VSGetShader(&oVS, nullptr, 0);
		ctx->PSGetShader(&oPS, nullptr, 0);

		// draw state
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Trans ? m_pDSS_Trans : m_pDSS_Opaque, 0);
		if (m_pDbgRS) ctx->RSSetState(m_pDbgRS);

		UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT4), offset = 0;
		ctx->IASetInputLayout(m_pDbgIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 1, &m_pPointMarkerVB, &stride, &offset);
		ctx->IASetIndexBuffer(m_pPointMarkerIB, DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(m_pDbgVS, nullptr, 0);
		ctx->PSSetShader(m_pDbgPS, nullptr, 0);

		const DirectX::XMFLOAT4 cubeColor = { 0.9131f, 0.3419f, 0.00335f, 1.0f }; // amber
		ctx->UpdateSubresource(m_pDbgCB, 0, nullptr, &cubeColor, 0, 0);
		ctx->PSSetConstantBuffers(3, 1, &m_pDbgCB);

		ctx->DrawIndexed(36, 0, 0);

		// 복구
		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);
		ctx->IASetInputLayout(oIL);
		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);

		SAFE_RELEASE(oVS); SAFE_RELEASE(oPS); SAFE_RELEASE(oIL);
		SAFE_RELEASE(oBS); SAFE_RELEASE(oDSS); SAFE_RELEASE(oRS);
	}
}

////////////////////////////////////////////////////////////////////////////////
// 12) PIPELINE BIND HELPERS
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::BindStaticMeshPipeline(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
}

void TutorialApp::BindStaticMeshPipeline_PBR(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pPBRPS, nullptr, 0);

	// IBL (t7~t9)
	ID3D11ShaderResourceView* ibl[3] =
	{
		mIBLIrrMDRSRV.Get(),   // t7 cube
		mIBLPrefMDRSRV.Get(),  // t8 cube (mips)
		mIBLBrdfSRV.Get()      // t9 2D
	};

	// NULL이면 여기서 바로 티나게(디버깅용)
	ctx->PSSetShaderResources(7, 3, ibl);

	// s3: clamp
	if (auto* s3 = mSamIBLClamp.Get())
		ctx->PSSetSamplers(3, 1, &s3);
}

void TutorialApp::BindSkinnedMeshPipeline(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pSkinnedIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pSkinnedVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////
// 13) STATIC DRAW HELPERS (Opaque / AlphaCut / Transparent)
////////////////////////////////////////////////////////////////////////////////
void TutorialApp::DrawStaticOpaqueOnly(ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB)
{
	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i)
	{
		const auto& r = mesh.Ranges()[i];
		const auto& mat = mtls[r.materialIndex];
		if (mat.hasOpacity) continue;

		mat.Bind(ctx);

		UseCB use{};
		use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
		use.useNormal = (mat.hasNormal && !mDbg.disableNormal) ? 1u : 0u;
		use.useSpecular = (!mDbg.disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u;
		use.useEmissive = (mat.hasEmissive && !mDbg.disableEmissive) ? 1u : 0u;
		use.useOpacity = 0u;
		use.alphaCut = mDbg.forceAlphaClip ? mDbg.alphaCut : -1.0f;

		// Blinn-Phong fallback에서 PBR-packed asset(여자 모델) 보호
		const bool blinnPhongMode = !mPbr.enable;
		const bool isPbrPackedAsset = (&mtls == &gFemaleMtls);

		if (blinnPhongMode && isPbrPackedAsset)
		{
			use.useEmissive = 0;
			use.useSpecular = mDbg.disableSpecular ? 0u : 2u; // 2 = const specMask(1.0)
		}

		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mesh.DrawSubmesh(ctx, (UINT)i);
		MaterialGPU::Unbind(ctx);
	}
}

void TutorialApp::DrawStaticAlphaCutOnly(ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB)
{
	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i)
	{
		const auto& r = mesh.Ranges()[i];
		const auto& mat = mtls[r.materialIndex];
		if (!mat.hasOpacity) continue;

		mat.Bind(ctx);

		UseCB use{};
		use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
		use.useNormal = (mat.hasNormal && !mDbg.disableNormal) ? 1u : 0u;
		use.useSpecular = (!mDbg.disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u;
		use.useEmissive = (mat.hasEmissive && !mDbg.disableEmissive) ? 1u : 0u;
		use.useOpacity = 1u;
		use.alphaCut = mDbg.alphaCut;

		const bool blinnPhongMode = !mPbr.enable;
		const bool isPbrPackedAsset = (&mtls == &gFemaleMtls);

		if (blinnPhongMode && isPbrPackedAsset)
		{
			use.useEmissive = 0;
			use.useSpecular = mDbg.disableSpecular ? 0u : 2u;
		}

		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mesh.DrawSubmesh(ctx, (UINT)i);
		MaterialGPU::Unbind(ctx);
	}
}

void TutorialApp::DrawStaticTransparentOnly(ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB)
{
	if (mDbg.forceAlphaClip) return;

	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i)
	{
		const auto& r = mesh.Ranges()[i];
		const auto& mat = mtls[r.materialIndex];
		if (!mat.hasOpacity) continue;

		mat.Bind(ctx);

		UseCB use{};
		use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
		use.useNormal = (mat.hasNormal && !mDbg.disableNormal) ? 1u : 0u;
		use.useSpecular = (!mDbg.disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u;
		use.useEmissive = (mat.hasEmissive && !mDbg.disableEmissive) ? 1u : 0u;
		use.useOpacity = 1u;  // alpha blend
		use.alphaCut = -1.0f;

		const bool blinnPhongMode = !mPbr.enable;
		const bool isPbrPackedAsset = (&mtls == &gFemaleMtls);

		if (blinnPhongMode && isPbrPackedAsset)
		{
			use.useEmissive = 0;
			use.useSpecular = mDbg.disableSpecular ? 0u : 2u;
		}

		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mesh.DrawSubmesh(ctx, (UINT)i);
		MaterialGPU::Unbind(ctx);
	}
}
