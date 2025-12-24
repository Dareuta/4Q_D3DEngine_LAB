// 렌더링 패스 세분화 (섀도우/스카이/불투명/투명/디버그)

#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"

//SHADOW PASS (DepthOnly)  
void TutorialApp::RenderShadowPass_Main(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB) {

	//=============================================

	ID3D11DepthStencilState* dssBefore = nullptr;
	UINT dssRefBefore = 0;
	ctx->OMGetDepthStencilState(&dssBefore, &dssRefBefore);

	ID3D11BlendState* bsBefore = nullptr;
	float bfBefore[4] = { 0,0,0,0 };
	UINT maskBefore = 0xFFFFFFFF;
	ctx->OMGetBlendState(&bsBefore, bfBefore, &maskBefore);

	float bf0[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf0, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0); // ✅ depth write ON

	ID3D11RasterizerState* rsBeforeShadow = nullptr;
	ctx->RSGetState(&rsBeforeShadow); // AddRef

	{
		ID3D11RenderTargetView* rtBefore = nullptr;
		ID3D11DepthStencilView* dsBefore = nullptr;
		ctx->OMGetRenderTargets(1, &rtBefore, &dsBefore);

		D3D11_VIEWPORT vpBefore{};
		UINT vpCount = 1;
		ctx->RSGetViewports(&vpCount, &vpBefore);

		// t5 언바인드, DSV only
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(5, 1, nullSRV);

		ctx->OMSetRenderTargets(0, nullptr, mShadowDSV.Get());
		ctx->ClearDepthStencilView(mShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

		ctx->RSSetViewports(1, &mShadowVP);
		if (mRS_ShadowBias) ctx->RSSetState(mRS_ShadowBias.Get());

		// Depth 전용 셰이더 바인드
		// 정적: m_pMeshVS + mPS_Depth / 스키닝: mVS_DepthSkinned + mPS_Depth
		// (정적 먼저 쓰도록 정리)
		auto DrawDepth_Static = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world, bool alphaCut)
			{



				// b0: 라이트 View/Proj 로 교체
				ConstantBuffer cbd = baseCB;
				cbd.mWorld = XMMatrixTranspose(world);
				cbd.mWorldInvTranspose = world.Invert();
				cbd.mView = XMMatrixTranspose(mLightView);
				cbd.mProjection = XMMatrixTranspose(mLightProj);
				ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
				ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

				ctx->IASetInputLayout(m_pMeshIL);
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);    // 깊이 전용 VS를 따로 두지 않는 경우
				ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

				for (size_t i = 0; i < mesh.Ranges().size(); ++i) {
					const auto& r = mesh.Ranges()[i];
					const auto& mat = mtls[r.materialIndex];
					const bool isCut = mat.hasOpacity;

					if (alphaCut != isCut) continue;

					UseCB use{};
					use.useOpacity = isCut ? 1u : 0u;
					use.alphaCut = isCut ? mShadowAlphaCut : -1.0f; // 컷아웃이면 clip() 활성
					ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
					ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

					mat.Bind(ctx);            // opacity 텍스처를 PS에서 clip()에 이용
					mesh.DrawSubmesh(ctx, (UINT)i);
					MaterialGPU::Unbind(ctx);
				}
			};

		auto DrawShadowStatic = [&](auto& X, auto& mesh, auto& mtls)
			{
				if (!X.enabled) return;

				Matrix W = ComposeSRT(X);

				if (mDbg.showOpaque)
					DrawDepth_Static(mesh, mtls, W, false); // opaque only

				if (mDbg.showTransparent)
					DrawDepth_Static(mesh, mtls, W, true);  // alpha-cut only
			};

		DrawShadowStatic(mTreeX, gTree, gTreeMtls);
		DrawShadowStatic(mCharX, gChar, gCharMtls);
		DrawShadowStatic(mZeldaX, gZelda, gZeldaMtls);
		DrawShadowStatic(mFemaleX, gFemale, gFemaleMtls);

		if (mBoxRig && mBoxX.enabled)
		{
			// b0: 라이트 뷰/프로젝션으로 업데이트
			ConstantBuffer cbd = baseCB;
			const Matrix W = ComposeSRT(mBoxX);
			cbd.mWorld = XMMatrixTranspose(W);
			cbd.mWorldInvTranspose = Matrix::Identity;       // Rigid는 VS에서 필요 없으면 Identity로
			cbd.mView = XMMatrixTranspose(mLightView);
			cbd.mProjection = XMMatrixTranspose(mLightProj);
			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			// IL/VS/PS를 depth 전용으로
			ctx->IASetInputLayout(mIL_PNTT.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

			// 알파 컷아웃 재질 대응(있다면 clip)
			UseCB use{};
			use.useOpacity = 1u;                  // Rigid 내부에서 재질 분기한다면 그대로 둬도 OK
			use.alphaCut = mShadowAlphaCut;     // ImGui에서 쓰는 값
			ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
			ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

			// RigidSkeletal 깊이 드로우 (시그니처는 네 프로젝트에 맞춰)
			mBoxRig->DrawDepthOnly(
				ctx, W,
				mLightView, mLightProj,
				m_pConstantBuffer,    // b0
				m_pUseCB,             // b2
				mVS_Depth.Get(),
				mPS_Depth.Get(),
				mIL_PNTT.Get(),
				mShadowAlphaCut
			);
		}

		// 스키닝 깊이
		if (mSkinRig && mSkinX.enabled)
		{
			ctx->IASetInputLayout(mIL_PNTT_BW.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_DepthSkinned.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

			// b0를 라이트 VP로 세팅
			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(ComposeSRT(mSkinX));
			cbd.mWorldInvTranspose = Matrix::Identity; // 스키닝에서는 VS에서 처리할 수 있음
			cbd.mView = XMMatrixTranspose(mLightView);
			cbd.mProjection = XMMatrixTranspose(mLightProj);
			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			mSkinRig->DrawDepthOnly(
				ctx, ComposeSRT(mSkinX),
				mLightView, mLightProj,
				m_pConstantBuffer,  // b0
				m_pUseCB,           // b2 (alphaCut 제어)
				m_pBoneCB,          // b4
				mVS_DepthSkinned.Get(),
				mPS_Depth.Get(),
				mIL_PNTT_BW.Get(),
				mShadowAlphaCut
			);
		}

		// 메인 RT 복구
		ctx->OMSetRenderTargets(1, &rtBefore, dsBefore);
		ctx->RSSetViewports(1, &vpBefore);

		SAFE_RELEASE(rtBefore);
		SAFE_RELEASE(dsBefore);
	}

	ctx->RSSetState(rsBeforeShadow);
	SAFE_RELEASE(rsBeforeShadow);

	ctx->OMSetBlendState(bsBefore, bfBefore, maskBefore);
	ctx->OMSetDepthStencilState(dssBefore, dssRefBefore);
	SAFE_RELEASE(bsBefore);
	SAFE_RELEASE(dssBefore);

	//=============================================
}


// -----------------------------------------------------------------------------
// Point Shadow Pass (Cube)
// - Color cube에 distNorm(dist/range) 저장
// - Depth cube로 nearest surface 확보
// - 첫 번째 point light(=mPoint)만 지원
// -----------------------------------------------------------------------------
void TutorialApp::RenderPointShadowPass_Cube(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB)
{
	if (!mPoint.enable || !mPoint.shadowEnable)
		return;

	if (!mPointShadowTex || !mPointShadowSRV || !mCB_PointShadow)
		return; // 리소스 없음

	// SRV(t10)로 바인딩되어 있을 수도 있으니 렌더 전에 언바인드
	{
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(10, 1, nullSRV);
	}

	// 상태 백업
	ID3D11DepthStencilState* dssBefore = nullptr;
	UINT dssRefBefore = 0;
	ctx->OMGetDepthStencilState(&dssBefore, &dssRefBefore);

	ID3D11BlendState* bsBefore = nullptr;
	float bfBefore[4] = { 0,0,0,0 };
	UINT maskBefore = 0xFFFFFFFF;
	ctx->OMGetBlendState(&bsBefore, bfBefore, &maskBefore);

	ID3D11RasterizerState* rsBefore = nullptr;
	ctx->RSGetState(&rsBefore);

	ID3D11RenderTargetView* rtBefore = nullptr;
	ID3D11DepthStencilView* dsBefore = nullptr;
	ctx->OMGetRenderTargets(1, &rtBefore, &dsBefore);

	D3D11_VIEWPORT vpBefore{};
	UINT vpCount = 1;
	ctx->RSGetViewports(&vpCount, &vpBefore);

	// 기본 상태
	float bf0[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf0, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
	if (mRS_ShadowBias) ctx->RSSetState(mRS_ShadowBias.Get());
	ctx->RSSetViewports(1, &mPointShadowVP);

	// b13 업로드 (pos/range + bias/enable)
	CB_PointShadow pcb{};
	pcb.posRange = DirectX::XMFLOAT4(mPoint.pos.x, mPoint.pos.y, mPoint.pos.z, mPoint.range);
	pcb.params = DirectX::XMFLOAT4(mPoint.shadowBias, mPoint.shadowEnable ? 1.0f : 0.0f, 0.0f, 0.0f);
	ctx->UpdateSubresource(mCB_PointShadow.Get(), 0, nullptr, &pcb, 0, 0);
	ID3D11Buffer* b13 = mCB_PointShadow.Get();
	ctx->PSSetConstantBuffers(13, 1, &b13);

	// Face camera setup
	const Vector3 pos = mPoint.pos;
	const Vector3 dirs[6] = {
		Vector3(1,0,0), Vector3(-1,0,0),
		Vector3(0,1,0), Vector3(0,-1,0),
		Vector3(0,0,1), Vector3(0,0,-1)
	};
	const Vector3 ups[6] = {
		Vector3::UnitY, Vector3::UnitY,
		Vector3(0,0,-1), Vector3(0,0,1),
		Vector3::UnitY, Vector3::UnitY
	};
	const Matrix P = Matrix::CreatePerspectiveFieldOfView(DirectX::XM_PIDIV2, 1.0f, 0.1f, mPoint.range);

	// Draw helper (StaticMesh)
	auto DrawPointDepth_Static = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world, const Matrix& V, bool alphaCut)
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

	// per-face render
	for (UINT face = 0; face < 6; ++face)
	{
		ID3D11RenderTargetView* rtv = mPointShadowRTV[face].Get();
		ID3D11DepthStencilView* dsv = mPointShadowDSV[face].Get();
		ctx->OMSetRenderTargets(1, &rtv, dsv);

		const float clear[4] = { 1,1,1,1 };
		ctx->ClearRenderTargetView(rtv, clear);
		ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

		const Matrix V = Matrix::CreateLookAt(pos, pos + dirs[face], ups[face]);

		DrawPointShadowStatic(mTreeX, gTree, gTreeMtls, V);
		DrawPointShadowStatic(mCharX, gChar, gCharMtls, V);
		DrawPointShadowStatic(mZeldaX, gZelda, gZeldaMtls, V);
		DrawPointShadowStatic(mFemaleX, gFemale, gFemaleMtls, V);

		// Rigid
		if (mBoxRig && mBoxX.enabled)
		{
			ConstantBuffer cbd = baseCB;
			const Matrix W = ComposeSRT(mBoxX);
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
			ctx->IASetInputLayout(mIL_PNTT_BW.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_DepthSkinned.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_PointShadow.Get(), nullptr, 0);

			ConstantBuffer cbd = baseCB;
			cbd.mWorld = XMMatrixTranspose(ComposeSRT(mSkinX));
			cbd.mWorldInvTranspose = Matrix::Identity;
			cbd.mView = XMMatrixTranspose(V);
			cbd.mProjection = XMMatrixTranspose(P);
			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			mSkinRig->DrawDepthOnly(
				ctx, ComposeSRT(mSkinX),
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

	// 상태 복구
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


void TutorialApp::BindStaticMeshPipeline_GBuffer(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(mVS_GBuffer.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_GBuffer.Get(), nullptr, 0);
}

void TutorialApp::RenderGBufferPass(ID3D11DeviceContext* ctx, ConstantBuffer& baseCB)
{
	ID3D11RasterizerState* oldRS = nullptr;
	ctx->RSGetState(&oldRS);

	if (mDbg.cullNone && m_pDbgRS) ctx->RSSetState(m_pDbgRS);
	else                           ctx->RSSetState(m_pCullBackRS);

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);

	BindStaticMeshPipeline_GBuffer(ctx);

	// 중요: 기존 DrawStatic...이 "mPbr.enable=false면 BlinnPhong 취급" 같은 분기 갖고 있어서
	// deferred에서는 강제로 true로 두는 게 안전함(메탈/러프 텍스처 플래그가 제대로 감)
	bool oldPBR = mPbr.enable;
	mPbr.enable = true;

	if (mDbg.showOpaque)
	{
		if (mTreeX.enabled)  DrawStaticOpaqueOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)  DrawStaticOpaqueOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled) DrawStaticOpaqueOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);
		if (mFemaleX.enabled)DrawStaticOpaqueOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
	}


	if (mDbg.forceAlphaClip && mDbg.showTransparent)

	{
		if (mTreeX.enabled)   DrawStaticAlphaCutOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)   DrawStaticAlphaCutOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled)  DrawStaticAlphaCutOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);
		if (mFemaleX.enabled) DrawStaticAlphaCutOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
	}


	mPbr.enable = oldPBR;

	ctx->RSSetState(oldRS);
	SAFE_RELEASE(oldRS);
}

void TutorialApp::RenderDeferredLightPass(ID3D11DeviceContext* ctx)
{
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);

	// 풀스크린 패스는 depth 건드리면 또 터짐 (너가 이미 겪었지)
	ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : nullptr, 0);

	// fullscreen tri
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_DeferredLight.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_DeferredLight.Get(), nullptr, 0);

	ID3D11ShaderResourceView* srvs[6] =
	{
		mGBufferSRV[0].Get(), // t0 worldpos
		mGBufferSRV[1].Get(), // t1 normal
		mGBufferSRV[2].Get(), // t2 albedo
		mGBufferSRV[3].Get(), // t3 mr
		nullptr,              // t4 (비워둠)
		mShadowSRV.Get()      // t5 shadow map
	};
	ctx->PSSetShaderResources(0, 6, srvs);

	// IBL SRV (t7~t9)
	ID3D11ShaderResourceView* ibl[3] =
	{
		mIBLIrrMDRSRV.Get(),
		mIBLPrefMDRSRV.Get(),
		mIBLBrdfSRV.Get()
	};
	ctx->PSSetShaderResources(7, 3, ibl);

	// IBL sampler (s3)
	ID3D11SamplerState* sIBL = mSamIBLClamp.Get();
	ctx->PSSetSamplers(3, 1, &sIBL);

	ID3D11Buffer* b6 = mCB_Shadow.Get();
	ctx->PSSetConstantBuffers(6, 1, &b6);

	ID3D11SamplerState* cmp = mSamShadowCmp.Get();
	ctx->PSSetSamplers(1, 1, &cmp);


	// b12: point lights
	if (mCB_DeferredLights) { ID3D11Buffer* b12 = mCB_DeferredLights.Get(); ctx->PSSetConstantBuffers(12, 1, &b12); }


	ctx->Draw(3, 0);

	// 정리 (다음 프레임/패스 hazard 방지)
	ID3D11ShaderResourceView* nullSRV[6] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(0, 6, nullSRV);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0); // 또는 nullptr로 기본 복구

	ID3D11ShaderResourceView* nullIBL[3] = { nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(7, 3, nullIBL);
}

void TutorialApp::RenderGBufferDebugPass(ID3D11DeviceContext* ctx)
{
	if (!mPS_GBufferDebug || !mCB_GBufferDebug) return;

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);

	ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : nullptr, 0);

	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_DeferredLight.Get(), nullptr, 0); // 같은 VS 사용
	ctx->PSSetShader(mPS_GBufferDebug.Get(), nullptr, 0);

	CB_GBufferDebug cb{};
	cb.mode = (UINT)mDbg.gbufferMode;
	cb.posRange = mDbg.gbufferPosRange;
	ctx->UpdateSubresource(mCB_GBufferDebug.Get(), 0, nullptr, &cb, 0, 0);
	ID3D11Buffer* b11 = mCB_GBufferDebug.Get();
	ctx->PSSetConstantBuffers(11, 1, &b11);

	ID3D11ShaderResourceView* srvs[4] =
	{
		mGBufferSRV[0].Get(),
		mGBufferSRV[1].Get(),
		mGBufferSRV[2].Get(),
		mGBufferSRV[3].Get(),
	};
	ctx->PSSetShaderResources(0, 4, srvs);

	ctx->Draw(3, 0);

	ID3D11ShaderResourceView* nullSRV[4] = { nullptr,nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(0, 4, nullSRV);
	ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0); // 또는 nullptr로 기본 복구
}



void TutorialApp::RenderToneMapPass(ID3D11DeviceContext* ctx)
{
	if (!mSceneHDRSRV || !mVS_ToneMap || !mPS_ToneMap || !mCB_ToneMap)
		return;

	ID3D11RasterizerState* oldRS = nullptr;
	ctx->RSGetState(&oldRS);
	ctx->RSSetState(m_pDbgRS); // Solid + CullNone (InitScene에서 만든 거)

	// BackBuffer로 출력
	ID3D11RenderTargetView* bb = m_pRenderTargetView;
	ctx->OMSetRenderTargets(1, &bb, nullptr);

	// viewport
	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0; vp.TopLeftY = 0;
	vp.Width = (float)m_ClientWidth;
	vp.Height = (float)m_ClientHeight;
	vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	// 상태(안전빵)
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	if (m_pDSS_Disabled) ctx->OMSetDepthStencilState(m_pDSS_Disabled, 0);
	else                ctx->OMSetDepthStencilState(nullptr, 0);

	// 파이프라인: 풀스크린 tri (SV_VertexID)
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11Buffer* nullVB[1] = { nullptr };
	UINT zero = 0;
	ctx->IASetVertexBuffers(0, 1, nullVB, &zero, &zero);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	ctx->VSSetShader(mVS_ToneMap.Get(), nullptr, 0);
	ctx->PSSetShader(mPS_ToneMap.Get(), nullptr, 0);

	// 상수 업데이트 (b10)
	CB_ToneMap cb{};
	cb.exposureEV = mTone.exposureEV;
	cb.gamma = mTone.gamma;
	cb.operatorId = (mTone.enable ? (UINT)mTone.operatorId : 0u);
	cb.flags = 1u; // gamma 적용
	ctx->UpdateSubresource(mCB_ToneMap.Get(), 0, nullptr, &cb, 0, 0);

	ID3D11Buffer* b10 = mCB_ToneMap.Get();
	ctx->PSSetConstantBuffers(10, 1, &b10);

	// SRV(t0) + Samp(s0)
	ID3D11ShaderResourceView* hdr = mSceneHDRSRV.Get();
	ctx->PSSetShaderResources(0, 1, &hdr);

	ID3D11SamplerState* samp = mSamToneMapClamp ? mSamToneMapClamp.Get() : m_pSamplerLinear;
	ctx->PSSetSamplers(0, 1, &samp);

	ctx->Draw(3, 0);

	// hazard 방지: SRV 언바인드
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ctx->PSSetShaderResources(0, 1, nullSRV);

	ctx->RSSetState(oldRS);
	SAFE_RELEASE(oldRS);

}


//SKYBOX
void TutorialApp::RenderSkyPass(
	ID3D11DeviceContext* ctx,
	const Matrix& viewNoTrans) {
	//=============================================

	if (mDbg.showSky)
	{
		ID3D11RasterizerState* oldRS = nullptr; ctx->RSGetState(&oldRS);
		ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef = 0; ctx->OMGetDepthStencilState(&oldDSS, &oldRef);

		ctx->RSSetState(m_pSkyRS);
		ctx->OMSetDepthStencilState(m_pSkyDSS, 0);

		ctx->IASetInputLayout(m_pSkyIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_pSkyVS, nullptr, 0);
		ctx->PSSetShader(m_pSkyPS, nullptr, 0);

		ConstantBuffer skyCB{};
		skyCB.mWorld = XMMatrixTranspose(Matrix::Identity);
		skyCB.mView = XMMatrixTranspose(viewNoTrans);
		skyCB.mProjection = XMMatrixTranspose(m_Projection);
		skyCB.mWorldInvTranspose = Matrix::Identity;
		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &skyCB, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		ID3D11ShaderResourceView* sky = mSkyEnvMDRSRV.Get();
		ctx->PSSetShaderResources(0, 1, &sky);

		ID3D11SamplerState* s0 = mSamIBLClamp.Get();
		ctx->PSSetSamplers(0, 1, &s0);

		UINT stride = sizeof(DirectX::XMFLOAT3), offset = 0;
		ctx->IASetVertexBuffers(0, 1, &m_pSkyVB, &stride, &offset);
		ctx->IASetIndexBuffer(m_pSkyIB, DXGI_FORMAT_R16_UINT, 0);
		ctx->DrawIndexed(36, 0, 0);

		ID3D11ShaderResourceView* null0[1] = { nullptr };
		ctx->PSSetShaderResources(0, 1, null0);
		ctx->RSSetState(oldRS);
		ctx->OMSetDepthStencilState(oldDSS, oldRef);
		SAFE_RELEASE(oldRS); SAFE_RELEASE(oldDSS);

		// 메쉬 셋업 복구
		ctx->IASetInputLayout(m_pMeshIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_pMeshVS, nullptr, 0);
		ctx->PSSetShader(m_pMeshPS, nullptr, 0);
		if (m_pSamplerLinear) ctx->PSSetSamplers(0, 1, &m_pSamplerLinear);
	}

	//=============================================
}

//OPAQUE
void TutorialApp::RenderOpaquePass(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB,
	const DirectX::SimpleMath::Vector3& eye) {
	//=============================================

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Opaque, 0);

	if (mDbg.showOpaque) {
		BindStaticMeshPipeline(ctx);
		if (mTreeX.enabled)  DrawStaticOpaqueOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)  DrawStaticOpaqueOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled) DrawStaticOpaqueOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);

		//if (mFemaleX.enabled) DrawStaticOpaqueOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);

		if (mFemaleX.enabled) {
			if (mPbr.enable) BindStaticMeshPipeline_PBR(ctx);
			else                             BindStaticMeshPipeline(ctx);

			DrawStaticOpaqueOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);

			// 다음 드로우가 기존 PS 쓰도록 원복
			BindStaticMeshPipeline(ctx);
		}

		if (mBoxRig && mBoxX.enabled) {
			mBoxRig->DrawOpaqueOnly(ctx, ComposeSRT(mBoxX),
				view, m_Projection, m_pConstantBuffer, m_pUseCB,
				baseCB.vLightDir, baseCB.vLightColor, eye,
				m_Ka, m_Ks, m_Shininess, m_Ia,
				mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
		}
		if (mSkinRig && mSkinX.enabled) {
			BindSkinnedMeshPipeline(ctx);
			mSkinRig->DrawOpaqueOnly(ctx, ComposeSRT(mSkinX),
				view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
				baseCB.vLightDir, baseCB.vLightColor, eye,
				m_Ka, m_Ks, m_Shininess, m_Ia,
				mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
			BindStaticMeshPipeline(ctx);
		}

	}

	//=============================================
}

//CUTOUT (alpha-test 강제 모드)
void TutorialApp::RenderCutoutPass(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB,
	const DirectX::SimpleMath::Vector3& eye) {
	//=============================================
	if (mDbg.forceAlphaClip) {
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);

		// RS (wire/cullNone 유지)
		if (mDbg.cullNone && m_pDbgRS) ctx->RSSetState(m_pDbgRS);

		if (mDbg.showTransparent) {
			BindStaticMeshPipeline(ctx);
			if (mTreeX.enabled)  DrawStaticAlphaCutOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
			if (mCharX.enabled)  DrawStaticAlphaCutOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
			if (mZeldaX.enabled) DrawStaticAlphaCutOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);

			if (mFemaleX.enabled)
			{
				if (mPbr.enable) BindStaticMeshPipeline_PBR(ctx);
				else                             BindStaticMeshPipeline(ctx);

				DrawStaticAlphaCutOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);
				BindStaticMeshPipeline(ctx);
			}

			//if (mFemaleX.enabled) DrawStaticAlphaCutOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);

			if (mBoxRig && mBoxX.enabled) {
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

			if (mSkinRig && mSkinX.enabled) {
				BindSkinnedMeshPipeline(ctx);
				mSkinRig->DrawAlphaCutOnly(ctx, ComposeSRT(mSkinX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
					baseCB.vLightDir, baseCB.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
				BindStaticMeshPipeline(ctx);
			}
		}
	}
	//=============================================
}

//TRANSPARENT
void TutorialApp::RenderTransparentPass(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB,
	const DirectX::SimpleMath::Vector3& eye)
{
	// 투명 패스 자체를 끌 때 / 알파컷 강제일 때는 할 게 없음
	if (!mDbg.showTransparent) return;
	if (mDbg.forceAlphaClip)   return; // 알파블렌드 안 쓸 거면 패스 스킵

	// 상태 백업
	ID3D11BlendState* oldBS = nullptr; float oldBF[4]; UINT oldSM = 0xFFFFFFFF;
	ctx->OMGetBlendState(&oldBS, oldBF, &oldSM);
	ID3D11DepthStencilState* oldDSS = nullptr; UINT oldSR = 0;
	ctx->OMGetDepthStencilState(&oldDSS, &oldSR);

	// 투명 상태 세팅 (이미 너 프로젝트에 있음)
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(m_pBS_Alpha, bf, 0xFFFFFFFF);
	//ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Trans, 0);
	ctx->OMSetDepthStencilState(m_pDSS_Trans, 0); // m_pDSS_Trans가 DepthRead(WriteOff)인지 꼭 확인


	// ---- 투명 큐 수집 ----
	struct TItem
	{
		float keyZ;                  // view-space z (클수록 멀다)
		std::function<void()> draw;  // 드로우 실행
	};

	std::vector<TItem> q;
	q.reserve(16);

	auto ViewZ = [&](const DirectX::SimpleMath::Matrix& W)->float
		{
			const auto m_View = view;
			using namespace DirectX::SimpleMath;
			Vector3 p = W.Translation();                 // 오브젝트 중심(대충)
			Vector3 vp = Vector3::Transform(p, m_View);    // view-space
			return vp.z;
		};

	auto PushStatic = [&](const XformUI& X, StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, bool usePBR)
		{
			if (!X.enabled) return;
			DirectX::SimpleMath::Matrix W = ComposeSRT(X);
			float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[this, ctx, &mesh, &mtls, W, &baseCB, usePBR]()
				{
					if (usePBR) BindStaticMeshPipeline_PBR(ctx);
					else        BindStaticMeshPipeline(ctx);

					DrawStaticTransparentOnly(ctx, mesh, mtls, W, baseCB);

					if (usePBR) BindStaticMeshPipeline(ctx);
				} });

		};

	auto PushBoxRig = [&]()
		{
			if (!mBoxRig || !mBoxX.enabled) return;
			DirectX::SimpleMath::Matrix W = ComposeSRT(mBoxX);
			float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[=,  &baseCB, &eye]()
				{
					mBoxRig->DrawTransparentOnly(ctx, W,
						view, m_Projection, m_pConstantBuffer, m_pUseCB,
						baseCB.vLightDir, baseCB.vLightColor, eye,
						m_Ka, m_Ks, m_Shininess, m_Ia,
						mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);

					BindStaticMeshPipeline(ctx);
				}
				});
		};

	auto PushSkinRig = [&]()
		{
			if (!mSkinRig || !mSkinX.enabled) return;
			DirectX::SimpleMath::Matrix W = ComposeSRT(mSkinX);
			float z = ViewZ(W);

			q.push_back(TItem{
				z,
				[=,  &baseCB, &eye]()
				{
					BindSkinnedMeshPipeline(ctx);
					mSkinRig->DrawTransparentOnly(ctx, W,
						view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
						baseCB.vLightDir, baseCB.vLightColor, eye,
						m_Ka, m_Ks, m_Shininess, m_Ia,
						mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);

					BindStaticMeshPipeline(ctx);
				}
				});
		};

	//  여기서 “투명 취급할 애들” 큐에 넣기
	// (현재는 hasOpacity 서브메시들만 DrawStaticTransparentOnly가 그린다)
	PushStatic(mTreeX, gTree, gTreeMtls, false);
	PushStatic(mCharX, gChar, gCharMtls, false);
	PushStatic(mZeldaX, gZelda, gZeldaMtls, false);

	//  여자 모델(헤어 등) : PBR 켜져있으면 PBR 파이프라인로 투명 그리기
	PushStatic(mFemaleX, gFemale, gFemaleMtls, mPbr.enable);

	PushBoxRig();
	PushSkinRig();

	// ---- 소트 ----
	if (mDbg.sortTransparent)
	{
		std::stable_sort(q.begin(), q.end(), [](const TItem& a, const TItem& b)
			{
				return a.keyZ > b.keyZ; // far -> near
			});
	}

	// ---- 드로우 ----
	for (auto& it : q)
		it.draw();

	// (선택) PBR이 t7~t9에 SRV 걸어놨을 수 있으니 깔끔하게 언바인드
	ID3D11ShaderResourceView* nullIBL[3] = { nullptr,nullptr,nullptr };
	ctx->PSSetShaderResources(7, 3, nullIBL);

	// 상태 복구
	ctx->OMSetBlendState(oldBS, oldBF, oldSM);
	ctx->OMSetDepthStencilState(oldDSS, oldSR);
	SAFE_RELEASE(oldBS);
	SAFE_RELEASE(oldDSS);
}

//디버그(광원 화살표, 그리드)
void TutorialApp::RenderDebugPass(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB,
	const DirectX::SimpleMath::Vector3& lightDir)
{
	//=============================================

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

		// 상태 백업
		ID3D11RasterizerState* oRS = nullptr; ctx->RSGetState(&oRS);
		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; ctx->OMGetDepthStencilState(&oDSS, &oRef);
		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF; ctx->OMGetBlendState(&oBS, oBF, &oSM);
		ID3D11InputLayout* oIL = nullptr; ctx->IAGetInputLayout(&oIL);
		ID3D11VertexShader* oVS = nullptr; ctx->VSGetShader(&oVS, nullptr, 0);
		ID3D11PixelShader* oPS = nullptr; ctx->PSGetShader(&oPS, nullptr, 0);

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

		// 상태 복구
		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);
		ctx->IASetInputLayout(oIL);
		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);

		SAFE_RELEASE(oVS); SAFE_RELEASE(oPS); SAFE_RELEASE(oIL);
		SAFE_RELEASE(oBS); SAFE_RELEASE(oDSS); SAFE_RELEASE(oRS);
	}

	if (mDbg.showGrid)
	{
		using namespace DirectX::SimpleMath;

		// -----------------------------
		// ✅ Grid가 건드리는 상태/바인딩 전부 백업
		// -----------------------------
		ID3D11RasterizerState* oRS = nullptr; ctx->RSGetState(&oRS);

		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0;
		ctx->OMGetDepthStencilState(&oDSS, &oRef);

		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF;
		ctx->OMGetBlendState(&oBS, oBF, &oSM);

		ID3D11InputLayout* oIL = nullptr; ctx->IAGetInputLayout(&oIL);
		D3D11_PRIMITIVE_TOPOLOGY oTopo; ctx->IAGetPrimitiveTopology(&oTopo);

		ID3D11Buffer* oVB0 = nullptr; UINT oStride0 = 0, oOffset0 = 0;
		ctx->IAGetVertexBuffers(0, 1, &oVB0, &oStride0, &oOffset0);

		ID3D11Buffer* oIB = nullptr; DXGI_FORMAT oIBFmt = DXGI_FORMAT_UNKNOWN; UINT oIBOff = 0;
		ctx->IAGetIndexBuffer(&oIB, &oIBFmt, &oIBOff);

		ID3D11VertexShader* oVS = nullptr; ctx->VSGetShader(&oVS, nullptr, 0);
		ID3D11PixelShader* oPS = nullptr; ctx->PSGetShader(&oPS, nullptr, 0);

		// VS/PS CB(0)도 건드리니까 백업
		ID3D11Buffer* oVSb0 = nullptr; ctx->VSGetConstantBuffers(0, 1, &oVSb0);
		ID3D11Buffer* oPSb0 = nullptr; ctx->PSGetConstantBuffers(0, 1, &oPSb0);

		// Grid가 건드리는 PS 슬롯들
		ID3D11Buffer* oPSb6 = nullptr; ctx->PSGetConstantBuffers(6, 1, &oPSb6);
		ID3D11Buffer* oPSb9 = nullptr; ctx->PSGetConstantBuffers(9, 1, &oPSb9);

		ID3D11SamplerState* oSamp1 = nullptr; ctx->PSGetSamplers(1, 1, &oSamp1);
		ID3D11ShaderResourceView* oSRV5 = nullptr; ctx->PSGetShaderResources(5, 1, &oSRV5);

		// -----------------------------
		// (기존 Grid 코드) Shadow 바인딩
		// -----------------------------
		if (mCB_Shadow && mShadowSRV && mSamShadowCmp)
		{
			ID3D11Buffer* b6 = mCB_Shadow.Get();
			ctx->PSSetConstantBuffers(6, 1, &b6);

			ID3D11SamplerState* cmp = mSamShadowCmp.Get();
			ctx->PSSetSamplers(1, 1, &cmp);

			ID3D11ShaderResourceView* sh = mShadowSRV.Get();
			ctx->PSSetShaderResources(5, 1, &sh);
		}

		// -----------------------------
		// (기존 Grid 코드) 물결 / ProcCB(b9)
		// -----------------------------
		mTimeSec += GameTimer::m_Instance->DeltaTime();

		CB_Proc cb{};
		cb.uProc1 = { mTimeSec, 18.0f, 0.5f, 0.0f };
		cb.uProc2 = { 0.0f, 0.0f, 0.2f, 250.0f };
		cb.uProc2.w = 1000.0f;

		ctx->UpdateSubresource(mCB_Proc.Get(), 0, nullptr, &cb, 0, 0);

		ID3D11Buffer* b9 = mCB_Proc.Get();
		ctx->PSSetConstantBuffers(9, 1, &b9);

		// -----------------------------
		// (기존 Grid 코드) 상태/파이프라인 세팅 + 드로우
		// -----------------------------
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
		ctx->RSSetState(m_pCullBackRS);

		ConstantBuffer local = {};
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

		// -----------------------------
		// ✅ 상태/바인딩 복구 (이게 핵심)
		// -----------------------------
		ctx->PSSetShaderResources(5, 1, &oSRV5);
		ctx->PSSetSamplers(1, 1, &oSamp1);
		ctx->PSSetConstantBuffers(6, 1, &oPSb6);
		ctx->PSSetConstantBuffers(9, 1, &oPSb9);

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

		// Release
		SAFE_RELEASE(oSRV5);
		SAFE_RELEASE(oSamp1);
		SAFE_RELEASE(oPSb6);
		SAFE_RELEASE(oPSb9);
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


	// ------------------------------------------------------------
	// PointLight marker (billboard quad)
	// ------------------------------------------------------------
	if (mPoint.enable && mPoint.showMarker && m_pPointMarkerVB && m_pPointMarkerIB)
	{
		using namespace DirectX::SimpleMath;

		const Vector3 eye = m_Camera.m_World.Translation();
		Matrix worldMarker = Matrix::CreateScale(mPoint.markerSize) *
			Matrix::CreateBillboard(mPoint.pos, eye, Vector3::UnitY, nullptr);

		ConstantBuffer local = baseCB;
		local.mWorld = XMMatrixTranspose(worldMarker);
		local.mWorldInvTranspose = worldMarker.Invert();
		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		// 상태 백업
		ID3D11RasterizerState* oRS = nullptr; ctx->RSGetState(&oRS);
		ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; ctx->OMGetDepthStencilState(&oDSS, &oRef);
		ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0xFFFFFFFF; ctx->OMGetBlendState(&oBS, oBF, &oSM);
		ID3D11InputLayout* oIL = nullptr; ctx->IAGetInputLayout(&oIL);
		ID3D11VertexShader* oVS = nullptr; ctx->VSGetShader(&oVS, nullptr, 0);
		ID3D11PixelShader* oPS = nullptr; ctx->PSGetShader(&oPS, nullptr, 0);

		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Opaque, 0);
		if (m_pDbgRS) ctx->RSSetState(m_pDbgRS);

		UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT4), offset = 0;
		ctx->IASetInputLayout(m_pDbgIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 1, &m_pPointMarkerVB, &stride, &offset);
		ctx->IASetIndexBuffer(m_pPointMarkerIB, DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(m_pDbgVS, nullptr, 0);
		ctx->PSSetShader(m_pDbgPS, nullptr, 0);

		const DirectX::XMFLOAT4 kMagenta = { 1.0f, 0.0f, 1.0f, 1.0f };
		ctx->UpdateSubresource(m_pDbgCB, 0, nullptr, &kMagenta, 0, 0);
		ctx->PSSetConstantBuffers(3, 1, &m_pDbgCB);

		ctx->DrawIndexed(6, 0, 0);

		// 상태 복구
		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);
		ctx->IASetInputLayout(oIL);
		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);

		SAFE_RELEASE(oVS); SAFE_RELEASE(oPS); SAFE_RELEASE(oIL);
		SAFE_RELEASE(oBS); SAFE_RELEASE(oDSS); SAFE_RELEASE(oRS);
	}
	//=============================================
}


void TutorialApp::BindStaticMeshPipeline(ID3D11DeviceContext* ctx) {
	//=============================================
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
	//=============================================
}

void TutorialApp::BindStaticMeshPipeline_PBR(ID3D11DeviceContext* ctx)
{
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pPBRPS, nullptr, 0);

	ID3D11ShaderResourceView* ibl[3] =
	{
		mIBLIrrMDRSRV.Get(),   // t7 (cube)
		mIBLPrefMDRSRV.Get(),  // t8 (cube, mips 많음)
		mIBLBrdfSRV.Get()   // t9 (2D)
	};


	if (ibl[0] && ibl[1] && ibl[2])
		ctx->PSSetShaderResources(7, 3, ibl);
	else
	{
		ctx->PSSetShaderResources(7, 3, ibl); // 디버깅 중이면 걍 걸어두면 어디가 NULL인지 바로 티남]
		std::cout << "뭔가 잘못대씀" << std::endl;
	}


	if (auto* s3 = mSamIBLClamp.Get())
		ctx->PSSetSamplers(3, 1, &s3);
}



void TutorialApp::BindSkinnedMeshPipeline(ID3D11DeviceContext* ctx) {
	//=============================================
	ctx->IASetInputLayout(m_pSkinnedIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pSkinnedVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
	//=============================================
}

void TutorialApp::DrawStaticOpaqueOnly(
	ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB) {
	//=============================================
	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i) {
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

		// [Blinn-Phong fallback] PBR 패킹 재질(metal/rough를 spec/emissive 슬롯에 넣어둔 케이스) 보호
		const bool blinnPhongMode = !mPbr.enable;

		const bool isPbrPackedAsset = (&mtls == &gFemaleMtls);

		if (blinnPhongMode && isPbrPackedAsset)
		{
			// roughness 텍스처가 emissive 슬롯에 들어있으면, Blinn-Phong에선 빨갛게 타니까 무시
			use.useEmissive = 0;

			// metallic 텍스처가 specular 슬롯에 들어있으면, Blinn-Phong에선 의미가 다르니 샘플링하지 말자
			// disableSpecular 토글 존중
			use.useSpecular = mDbg.disableSpecular ? 0u : 2u; // 2 = 상수 specMask(1.0) 사용
		}

		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mesh.DrawSubmesh(ctx, (UINT)i);
		MaterialGPU::Unbind(ctx);
	}
	//=============================================
}

void TutorialApp::DrawStaticAlphaCutOnly(
	ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB) {
	//=============================================
	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i) {
		const auto& r = mesh.Ranges()[i];
		const auto& mat = mtls[r.materialIndex];
		if (!mat.hasOpacity) continue;

		mat.Bind(ctx);

		UseCB use{};
		use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
		use.useNormal = (mat.hasNormal && !mDbg.disableNormal) ? 1u : 0u;
		use.useSpecular = (!mDbg.disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u;
		use.useEmissive = (mat.hasEmissive && !mDbg.disableEmissive) ? 1u : 0u;
		use.useOpacity = 1u;          // alpha-test
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
	//=============================================
}

void TutorialApp::DrawStaticTransparentOnly(
	ID3D11DeviceContext* ctx,
	StaticMesh& mesh,
	const std::vector<MaterialGPU>& mtls,
	const Matrix& world,
	const ConstantBuffer& baseCB) {
	//=============================================
	if (mDbg.forceAlphaClip) return;

	ConstantBuffer local = baseCB;
	local.mWorld = XMMatrixTranspose(world);
	local.mWorldInvTranspose = world.Invert();
	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);

	for (size_t i = 0; i < mesh.Ranges().size(); ++i) {
		const auto& r = mesh.Ranges()[i];
		const auto& mat = mtls[r.materialIndex];
		if (!mat.hasOpacity) continue;

		mat.Bind(ctx);

		UseCB use{};
		use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
		use.useNormal = (mat.hasNormal && !mDbg.disableNormal) ? 1u : 0u;
		use.useSpecular = (!mDbg.disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u;
		use.useEmissive = (mat.hasEmissive && !mDbg.disableEmissive) ? 1u : 0u;
		use.useOpacity = 1u;           // 투명 블렌드
		use.alphaCut = mDbg.forceAlphaClip ? mDbg.alphaCut : -1.0f;

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
	//=============================================
}

