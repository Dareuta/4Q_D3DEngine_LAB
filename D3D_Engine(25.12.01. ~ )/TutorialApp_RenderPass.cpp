// 렌더링 패스 세분화 (섀도우/스카이/불투명/투명/디버그)

#include "../D3D_Core/pch.h"
#include "TutorialApp.h"

//SHADOW PASS (DepthOnly)  
void TutorialApp::RenderShadowPass_Main(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB) {
	//=============================================

	ID3D11RasterizerState* rsBeforeShadow = nullptr;
	ctx->RSGetState(&rsBeforeShadow); // AddRef

	{
		// t5 언바인드, DSV only
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(5, 1, nullSRV);		
		ctx->OMSetRenderTargets(0, nullptr, mShadowDSV.Get());
		ctx->ClearDepthStencilView(mShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

		// 라이트용 VP/RS
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

		if (mTreeX.enabled) { Matrix W = ComposeSRT(mTreeX);  DrawDepth_Static(gTree, gTreeMtls, W, false); DrawDepth_Static(gTree, gTreeMtls, W, true); }
		if (mCharX.enabled) { Matrix W = ComposeSRT(mCharX);  DrawDepth_Static(gChar, gCharMtls, W, false); DrawDepth_Static(gChar, gCharMtls, W, true); }
		if (mZeldaX.enabled) { Matrix W = ComposeSRT(mZeldaX); DrawDepth_Static(gZelda, gZeldaMtls, W, false); DrawDepth_Static(gZelda, gZeldaMtls, W, true); }
		
		//if (mFemaleX.enabled) { Matrix W = ComposeSRT(mFemaleX); DrawDepth_Static(gFemale, gFemaleMtls, W, false); DrawDepth_Static(gFemale, gFemaleMtls, W, true); }

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
		ID3D11RenderTargetView* rtv = m_pRenderTargetView;
		ctx->OMSetRenderTargets(1, &rtv, m_pDepthStencilView);

		D3D11_VIEWPORT vp{};
		vp.TopLeftX = 0; vp.TopLeftY = 0;
		vp.Width = (float)m_ClientWidth; vp.Height = (float)m_ClientHeight;
		vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);
	}

	ctx->RSSetState(rsBeforeShadow);
	SAFE_RELEASE(rsBeforeShadow);

	//=============================================
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

		ctx->PSSetShaderResources(0, 1, &m_pSkySRV);
		ctx->PSSetSamplers(0, 1, &m_pSkySampler);

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
			if (mPbr.enable && mPbr.charOnly) BindStaticMeshPipeline_PBR(ctx);
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
	const DirectX::SimpleMath::Vector3& eye) {
	//=============================================

	ID3D11BlendState* oldBS = nullptr; float oldBF[4]; UINT oldSM = 0xFFFFFFFF;
	ctx->OMGetBlendState(&oldBS, oldBF, &oldSM);
	ID3D11DepthStencilState* oldDSS = nullptr; UINT oldSR = 0;
	ctx->OMGetDepthStencilState(&oldDSS, &oldSR);

	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(m_pBS_Alpha, bf, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Trans, 0);

	if (mDbg.showTransparent) {
		BindStaticMeshPipeline(ctx);
		if (mTreeX.enabled)  DrawStaticTransparentOnly(ctx, gTree, gTreeMtls, ComposeSRT(mTreeX), baseCB);
		if (mCharX.enabled)  DrawStaticTransparentOnly(ctx, gChar, gCharMtls, ComposeSRT(mCharX), baseCB);
		if (mZeldaX.enabled) DrawStaticTransparentOnly(ctx, gZelda, gZeldaMtls, ComposeSRT(mZeldaX), baseCB);

		if (mFemaleX.enabled) DrawStaticTransparentOnly(ctx, gFemale, gFemaleMtls, ComposeSRT(mFemaleX), baseCB);

		if (mBoxRig && mBoxX.enabled) {
			mBoxRig->DrawTransparentOnly(ctx, ComposeSRT(mBoxX),
				view, m_Projection, m_pConstantBuffer, m_pUseCB,
				baseCB.vLightDir, baseCB.vLightColor, eye,
				m_Ka, m_Ks, m_Shininess, m_Ia,
				mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
		}
		if (mSkinRig && mSkinX.enabled) {
			BindSkinnedMeshPipeline(ctx);
			mSkinRig->DrawTransparentOnly(ctx, ComposeSRT(mSkinX),
				view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
				baseCB.vLightDir, baseCB.vLightColor, eye,
				m_Ka, m_Ks, m_Shininess, m_Ia,
				mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
			BindStaticMeshPipeline(ctx);
		}
	}

	ctx->OMSetBlendState(oldBS, oldBF, oldSM);
	ctx->OMSetDepthStencilState(oldDSS, oldSR);
	SAFE_RELEASE(oldBS); SAFE_RELEASE(oldDSS);



	//=============================================
}

//디버그(광원 화살표, 그리드)
void TutorialApp::RenderDebugPass(
	ID3D11DeviceContext* ctx,
	ConstantBuffer& baseCB,
	const DirectX::SimpleMath::Vector3& lightDir) {
	//=============================================

	if (mDbg.showLightArrow) {
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

	if (mDbg.showGrid) {
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
		ctx->RSSetState(m_pCullBackRS); // 윗면 보이게 만든 그 상태

		ConstantBuffer local = {};
		local.mWorld = XMMatrixTranspose(Matrix::Identity);
		local.mWorldInvTranspose = Matrix::Identity;
		local.mView = XMMatrixTranspose(view);
		local.mProjection = XMMatrixTranspose(m_Projection);
		local.vLightDir = baseCB.vLightDir;     // ← 조명 동일
		local.vLightColor = baseCB.vLightColor;
		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
		ctx->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		UINT stride = sizeof(DirectX::XMFLOAT3), offset = 0;
		ctx->IASetInputLayout(mGridIL.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetVertexBuffers(0, 1, mGridVB.GetAddressOf(), &stride, &offset);
		ctx->IASetIndexBuffer(mGridIB.Get(), DXGI_FORMAT_R16_UINT, 0);
		ctx->VSSetShader(mGridVS.Get(), nullptr, 0);
		ctx->PSSetShader(mGridPS.Get(), nullptr, 0);
		ctx->DrawIndexed(mGridIndexCount, 0, 0);
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
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);        // VS는 기존 그대로 사용
	ctx->PSSetShader(m_pPBRPS, nullptr, 0);         // PS만 PBR로 교체
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

		ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
		ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

		mesh.DrawSubmesh(ctx, (UINT)i);
		MaterialGPU::Unbind(ctx);
	}
	//=============================================
}

