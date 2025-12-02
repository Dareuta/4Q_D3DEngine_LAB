// OnInitialize/OnUninitialize/OnUpdate/OnRender/WndProc

#include "TutorialApp.h"
#include "../D3D_Core/pch.h"

bool TutorialApp::OnInitialize()
{
	if (!InitD3D())
		return false;

#ifdef _DEBUG
	if (!InitImGUI())
		return false;
#endif

	if (!InitScene())
		return false;

	return true;
}

void TutorialApp::OnUninitialize()
{
	UninitScene();

#ifdef _DEBUG
	UninitImGUI();
#endif
	UninitD3D();
}

void TutorialApp::OnUpdate()
{
	static float tHold = 0.0f;
	if (!mDbg.freezeTime) tHold = GameTimer::m_Instance->TotalTime();
	float t = tHold;

	XMMATRIX mSpin = XMMatrixRotationY(t * spinSpeed);

	XMMATRIX mScaleA = XMMatrixScaling(cubeScale.x, cubeScale.y, cubeScale.z);
	XMMATRIX mTranslateA = XMMatrixTranslation(cubeTransformA.x, cubeTransformA.y, cubeTransformA.z);
	m_World = mScaleA * mSpin * mTranslateA;

	// TutorialApp.cpp::OnUpdate()

	const double dt = (double)GameTimer::m_Instance->DeltaTime();

	// --- BoxHuman ---
	if (mBoxRig) {
		if (!mDbg.freezeTime && mBoxAC.play) mBoxAC.t += dt * mBoxAC.speed;
		const double durSec = mBoxRig->GetClipDurationSec();
		if (durSec > 0.0) {
			if (mBoxAC.loop) {
				mBoxAC.t = fmod(mBoxAC.t, durSec); if (mBoxAC.t < 0.0) mBoxAC.t += durSec;
			}
			else {
				if (mBoxAC.t >= durSec) { mBoxAC.t = durSec; mBoxAC.play = false; } // 部縑憮 薑雖
				if (mBoxAC.t < 0.0) { mBoxAC.t = 0.0;   mBoxAC.play = false; } // 擅縑憮 薑雖
			}
		}
		mBoxRig->EvaluatePose(mBoxAC.t, mBoxAC.loop);  // ∠ loop 瞪殖
	}

	// --- Skinned ---
	if (mSkinRig) {
		if (!mDbg.freezeTime && mSkinAC.play) mSkinAC.t += dt * mSkinAC.speed;
		const double durSec = mSkinRig->DurationSec();
		if (durSec > 0.0) {
			if (mSkinAC.loop) {
				mSkinAC.t = fmod(mSkinAC.t, durSec); if (mSkinAC.t < 0.0) mSkinAC.t += durSec;
			}
			else {
				if (mSkinAC.t >= durSec) { mSkinAC.t = durSec; mSkinAC.play = false; }
				if (mSkinAC.t < 0.0) { mSkinAC.t = 0.0;   mSkinAC.play = false; }
			}
		}
		mSkinRig->EvaluatePose(mSkinAC.t, mSkinAC.loop);
	}


}

void TutorialApp::OnRender()
{
	auto* ctx = m_pDeviceContext;

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 0) 塭檜お 蘋詭塭/憎紫辦 CB 機等檜お 
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	UpdateLightCameraAndShadowCB(ctx); // mLightView, mLightProj, mShadowVP, mCB_Shadow

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 1) 晦獄 だ塭嘐攪 贗極Щ + 詭檣 RT 贗葬橫
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	if (m_FovDegree < 10.0f)       m_FovDegree = 10.0f;
	else if (m_FovDegree > 120.0f) m_FovDegree = 120.0f;
	if (m_Near < 0.0001f)          m_Near = 0.0001f;
	float minFar = m_Near + 0.001f;
	if (m_Far < minFar)            m_Far = minFar;

	const float aspect = m_ClientWidth / (float)m_ClientHeight;
	m_Projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_FovDegree), aspect, m_Near, m_Far);

	// RS 摹鷗
	if (mDbg.wireframe && m_pWireRS)         ctx->RSSetState(m_pWireRS);
	else if (mDbg.cullNone && m_pDbgRS)      ctx->RSSetState(m_pDbgRS);
	else                                     ctx->RSSetState(m_pCullBackRS);

	const float clearColor[4] = { color[0], color[1], color[2], color[3] };
	ctx->ClearRenderTargetView(m_pRenderTargetView, clearColor);
	ctx->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 2) 奢鱔 CB0(b0) / Blinn(b1) 機煎萄 (詭檣 蘋詭塭 晦遽)
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	Matrix view; m_Camera.GetViewMatrix(view);
	Matrix viewNoTrans = view; viewNoTrans._41 = viewNoTrans._42 = viewNoTrans._43 = 0.0f;

	ConstantBuffer cb{};
	cb.mWorld = XMMatrixTranspose(Matrix::Identity);
	cb.mWorldInvTranspose = XMMatrixInverse(nullptr, Matrix::Identity);
	cb.mView = XMMatrixTranspose(view);
	cb.mProjection = XMMatrixTranspose(m_Projection);

	// 蛤滓敷割 塭檜お(dir from yaw/pitch)
	XMMATRIX R = XMMatrixRotationRollPitchYaw(m_LightPitch, m_LightYaw, 0.0f);
	XMVECTOR base = XMVector3Normalize(XMVectorSet(0, 0, 1, 0));
	XMVECTOR L = XMVector3Normalize(XMVector3TransformNormal(base, R));
	Vector3  dirV = { XMVectorGetX(L), XMVectorGetY(L), XMVectorGetZ(L) };
	cb.vLightDir = Vector4(dirV.x, dirV.y, dirV.z, 0.0f);
	cb.vLightColor = Vector4(m_LightColor.x * m_LightIntensity,
		m_LightColor.y * m_LightIntensity,
		m_LightColor.z * m_LightIntensity, 1.0f);

	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cb, 0, 0);
	ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
	ctx->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

	// b1
	BlinnPhongCB bp{};
	const Vector3 eye = m_Camera.m_World.Translation();
	bp.EyePosW = Vector4(eye.x, eye.y, eye.z, 1);
	bp.kA = Vector4(m_Ka.x, m_Ka.y, m_Ka.z, 0);
	bp.kSAlpha = Vector4(m_Ks, m_Shininess, 0, 0);
	bp.I_ambient = Vector4(m_Ia.x, m_Ia.y, m_Ia.z, 0);
	ctx->UpdateSubresource(m_pBlinnCB, 0, nullptr, &bp, 0, 0);
	ctx->PSSetConstantBuffers(1, 1, &m_pBlinnCB);

	// 奢鱔 樁檜渦(薑瞳 詭蓮) 晦獄 夥檣萄
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
	if (m_pSamplerLinear) ctx->PSSetSamplers(0, 1, &m_pSamplerLinear);

	// 憎紫辦辨 だ塭嘐攪(и 廓虜)
	struct ShadowCB_ { Matrix LVP; Vector4 Params; } scb;
	scb.LVP = XMMatrixTranspose(mLightView * mLightProj);
	scb.Params = Vector4(mShadowCmpBias, 1.0f / mShadowW, 1.0f / mShadowH, 0.0f);
	ctx->UpdateSubresource(mCB_Shadow.Get(), 0, nullptr, &scb, 0, 0);
	ID3D11Buffer* b6 = mCB_Shadow.Get();
	ctx->VSSetConstantBuffers(6, 1, &b6);
	ctx->PSSetConstantBuffers(6, 1, &b6);

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 3) SHADOW PASS (DepthOnly)  
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	ID3D11RasterizerState* rsBeforeShadow = nullptr;
	ctx->RSGetState(&rsBeforeShadow); // AddRef

	{
		// t5 樹夥檣萄, DSV only
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(5, 1, nullSRV);
		ctx->OMSetRenderTargets(0, nullptr, mShadowDSV.Get());
		ctx->ClearDepthStencilView(mShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

		// 塭檜お辨 VP/RS
		ctx->RSSetViewports(1, &mShadowVP);
		if (mRS_ShadowBias) ctx->RSSetState(mRS_ShadowBias.Get());

		// Depth 瞪辨 樁檜渦 夥檣萄
		// 薑瞳: m_pMeshVS + mPS_Depth / 蝶酈棚: mVS_DepthSkinned + mPS_Depth
		// (薑瞳 試盪 噙紫煙 薑葬)
		auto DrawDepth_Static = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world, bool alphaCut)
			{
				// b0: 塭檜お View/Proj 煎 掖羹
				ConstantBuffer cbd = cb;
				cbd.mWorld = XMMatrixTranspose(world);
				cbd.mWorldInvTranspose = world.Invert();
				cbd.mView = XMMatrixTranspose(mLightView);
				cbd.mProjection = XMMatrixTranspose(mLightProj);
				ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
				ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

				ctx->IASetInputLayout(m_pMeshIL);
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);    // 桶檜 瞪辨 VS蒂 評煎 舒雖 彊朝 唳辦
				ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

				for (size_t i = 0; i < mesh.Ranges().size(); ++i) {
					const auto& r = mesh.Ranges()[i];
					const auto& mat = mtls[r.materialIndex];
					const bool isCut = mat.hasOpacity;

					if (alphaCut != isCut) continue;

					UseCB use{};
					use.useOpacity = isCut ? 1u : 0u;
					use.alphaCut = isCut ? mShadowAlphaCut : -1.0f; // 飄嬴醒檜賊 clip() �側�
					ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
					ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

					mat.Bind(ctx);            // opacity 臢蝶籀蒂 PS縑憮 clip()縑 檜辨
					mesh.DrawSubmesh(ctx, (UINT)i);
					MaterialGPU::Unbind(ctx);
				}
			};

		if (mTreeX.enabled) { Matrix W = ComposeSRT(mTreeX);  DrawDepth_Static(gTree, gTreeMtls, W, false); DrawDepth_Static(gTree, gTreeMtls, W, true); }
		if (mCharX.enabled) { Matrix W = ComposeSRT(mCharX);  DrawDepth_Static(gChar, gCharMtls, W, false); DrawDepth_Static(gChar, gCharMtls, W, true); }
		if (mZeldaX.enabled) { Matrix W = ComposeSRT(mZeldaX); DrawDepth_Static(gZelda, gZeldaMtls, W, false); DrawDepth_Static(gZelda, gZeldaMtls, W, true); }

		if (mBoxRig && mBoxX.enabled)
		{
			// b0: 塭檜お 箔/Щ煎薛暮戲煎 機等檜お
			ConstantBuffer cbd = cb;
			const Matrix W = ComposeSRT(mBoxX);
			cbd.mWorld = XMMatrixTranspose(W);
			cbd.mWorldInvTranspose = Matrix::Identity;       // Rigid朝 VS縑憮 в蹂 橈戲賊 Identity煎
			cbd.mView = XMMatrixTranspose(mLightView);
			cbd.mProjection = XMMatrixTranspose(mLightProj);
			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			// IL/VS/PS蒂 depth 瞪辨戲煎
			ctx->IASetInputLayout(mIL_PNTT.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_Depth.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

			// 憲だ 飄嬴醒 營韓 渠擬(氈棻賊 clip)
			UseCB use{};
			use.useOpacity = 1u;                  // Rigid 頂睡縑憮 營韓 碟晦и棻賊 斜渠煎 萋紫 OK
			use.alphaCut = mShadowAlphaCut;     // ImGui縑憮 噙朝 高
			ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
			ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

			// RigidSkeletal 桶檜 萄煎辦 (衛斜棲籀朝 啻 Щ煎薛お縑 蜃醮)
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

		// 蝶酈棚 桶檜
		if (mSkinRig && mSkinX.enabled)
		{
			ctx->IASetInputLayout(mIL_PNTT_BW.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->VSSetShader(mVS_DepthSkinned.Get(), nullptr, 0);
			ctx->PSSetShader(mPS_Depth.Get(), nullptr, 0);

			// b0蒂 塭檜お VP煎 撮た
			ConstantBuffer cbd = cb;
			cbd.mWorld = XMMatrixTranspose(ComposeSRT(mSkinX));
			cbd.mWorldInvTranspose = Matrix::Identity; // 蝶酈棚縑憮朝 VS縑憮 籀葬й 熱 氈擠
			cbd.mView = XMMatrixTranspose(mLightView);
			cbd.mProjection = XMMatrixTranspose(mLightProj);
			ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cbd, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

			mSkinRig->DrawDepthOnly(
				ctx, ComposeSRT(mSkinX),
				mLightView, mLightProj,
				m_pConstantBuffer,  // b0
				m_pUseCB,           // b2 (alphaCut 薯橫)
				m_pBoneCB,          // b4
				mVS_DepthSkinned.Get(),
				mPS_Depth.Get(),
				mIL_PNTT_BW.Get(),
				mShadowAlphaCut
			);
		}

		// 詭檣 RT 犒掘
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

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 4) SKYBOX (摹鷗)
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
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

		// 詭蓮 撢機 犒掘
		ctx->IASetInputLayout(m_pMeshIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_pMeshVS, nullptr, 0);
		ctx->PSSetShader(m_pMeshPS, nullptr, 0);
		if (m_pSamplerLinear) ctx->PSSetSamplers(0, 1, &m_pSamplerLinear);
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 5) 獄 ぬ蝶縑憮 憎紫辦 價Ы 夥檣萄 (PS: t5/s1/b6)
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	{
		// 寰瞪ж啪 營夥檣萄
		ctx->UpdateSubresource(mCB_Shadow.Get(), 0, nullptr, &scb, 0, 0);
		ID3D11Buffer* b6r = mCB_Shadow.Get();
		ID3D11SamplerState* cmp = mSamShadowCmp.Get();
		ID3D11ShaderResourceView* shSRV = mShadowSRV.Get();
		ctx->PSSetConstantBuffers(6, 1, &b6r);
		ctx->PSSetSamplers(1, 1, &cmp);
		ctx->PSSetShaderResources(5, 1, &shSRV);
	}

	// === Toon ramp bind (PS: t6/b7) ===
	{
		//矗 樁檜註 夥檣萄
		ToonCB_ t{};
		t.useToon = mDbg.useToon ? 1u : 0u;
		t.halfLambert = mDbg.toonHalfLambert ? 1u : 0u;
		t.specStep = mDbg.toonSpecStep;
		t.specBoost = mDbg.toonSpecBoost;
		t.shadowMin = mDbg.toonShadowMin;

		if (m_pToonCB) {
			ctx->UpdateSubresource(m_pToonCB, 0, nullptr, &t, 0, 0);
			ctx->PSSetConstantBuffers(7, 1, &m_pToonCB);      // PS b7
		}
		if (m_pRampSRV && mDbg.useToon) {
			ctx->PSSetShaderResources(6, 1, &m_pRampSRV);     // PS t6
		}
	}


	// 夥檣渦
	auto BindStatic = [&]() {
		ctx->IASetInputLayout(m_pMeshIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_pMeshVS, nullptr, 0);
		ctx->PSSetShader(m_pMeshPS, nullptr, 0);
		};
	auto BindSkinned = [&]() {
		ctx->IASetInputLayout(m_pSkinnedIL);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_pSkinnedVS, nullptr, 0);
		ctx->PSSetShader(m_pMeshPS, nullptr, 0);
		};

	// 萄煎辦 ⑦ぷ
	auto DrawOpaqueOnly = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world)
		{
			ConstantBuffer local = cb;
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
		};
	auto DrawAlphaCutOnly = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world)
		{
			ConstantBuffer local = cb;
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
		};
	auto DrawTransparentOnly = [&](StaticMesh& mesh, const std::vector<MaterialGPU>& mtls, const Matrix& world)
		{
			if (mDbg.forceAlphaClip) return;

			ConstantBuffer local = cb;
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
				use.useOpacity = 1u;           // 癱貲 綰溶萄
				use.alphaCut = mDbg.forceAlphaClip ? mDbg.alphaCut : -1.0f;
				ctx->UpdateSubresource(m_pUseCB, 0, nullptr, &use, 0, 0);
				ctx->PSSetConstantBuffers(2, 1, &m_pUseCB);

				mesh.DrawSubmesh(ctx, (UINT)i);
				MaterialGPU::Unbind(ctx);
			}
		};

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 6) OPAQUE
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	{
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Opaque, 0);

		if (mDbg.showOpaque) {
			BindStatic();
			if (mTreeX.enabled)  DrawOpaqueOnly(gTree, gTreeMtls, ComposeSRT(mTreeX));
			if (mCharX.enabled)  DrawOpaqueOnly(gChar, gCharMtls, ComposeSRT(mCharX));
			if (mZeldaX.enabled) DrawOpaqueOnly(gZelda, gZeldaMtls, ComposeSRT(mZeldaX));

			if (mBoxRig && mBoxX.enabled) {
				mBoxRig->DrawOpaqueOnly(ctx, ComposeSRT(mBoxX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB,
					cb.vLightDir, cb.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
			}
			if (mSkinRig && mSkinX.enabled) {
				BindSkinned();
				mSkinRig->DrawOpaqueOnly(ctx, ComposeSRT(mSkinX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
					cb.vLightDir, cb.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
				BindStatic();
			}

			// B) OPAQUE 綰煙 裔 部薹縑 稱罹塭
			if (mDbg.showGrid) {
				float bf[4] = { 0,0,0,0 };
				ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
				ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);
				ctx->RSSetState(m_pCullBackRS); // 嶺賊 爾檜啪 虜萇 斜 鼻鷓

				ConstantBuffer local = {};
				local.mWorld = XMMatrixTranspose(Matrix::Identity);
				local.mWorldInvTranspose = Matrix::Identity;
				local.mView = XMMatrixTranspose(view);
				local.mProjection = XMMatrixTranspose(m_Projection);
				local.vLightDir = cb.vLightDir;     // ∠ 褻貲 翕橾
				local.vLightColor = cb.vLightColor;
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


		}
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 7) CUTOUT (alpha-test 鬼薯 賅萄)
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	if (mDbg.forceAlphaClip) {
		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(m_pDSS_Opaque, 0);

		// RS (wire/cullNone 嶸雖)
		if (mDbg.cullNone && m_pDbgRS) ctx->RSSetState(m_pDbgRS);

		if (mDbg.showTransparent) {
			BindStatic();
			if (mTreeX.enabled)  DrawAlphaCutOnly(gTree, gTreeMtls, ComposeSRT(mTreeX));
			if (mCharX.enabled)  DrawAlphaCutOnly(gChar, gCharMtls, ComposeSRT(mCharX));
			if (mZeldaX.enabled) DrawAlphaCutOnly(gZelda, gZeldaMtls, ComposeSRT(mZeldaX));

			if (mBoxRig && mBoxX.enabled) {
				mBoxRig->DrawAlphaCutOnly(
					ctx,
					ComposeSRT(mBoxX),
					view, m_Projection,
					m_pConstantBuffer,
					m_pUseCB,
					mDbg.alphaCut,
					cb.vLightDir, cb.vLightColor,
					eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive
				);
			}

			if (mSkinRig && mSkinX.enabled) {
				BindSkinned();
				mSkinRig->DrawAlphaCutOnly(ctx, ComposeSRT(mSkinX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
					cb.vLightDir, cb.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
				BindStatic();
			}
		}
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 8) TRANSPARENT
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	{
		ID3D11BlendState* oldBS = nullptr; float oldBF[4]; UINT oldSM = 0xFFFFFFFF;
		ctx->OMGetBlendState(&oldBS, oldBF, &oldSM);
		ID3D11DepthStencilState* oldDSS = nullptr; UINT oldSR = 0;
		ctx->OMGetDepthStencilState(&oldDSS, &oldSR);

		float bf[4] = { 0,0,0,0 };
		ctx->OMSetBlendState(m_pBS_Alpha, bf, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(mDbg.depthWriteOff && m_pDSS_Disabled ? m_pDSS_Disabled : m_pDSS_Trans, 0);

		if (mDbg.showTransparent) {
			BindStatic();
			if (mTreeX.enabled)  DrawTransparentOnly(gTree, gTreeMtls, ComposeSRT(mTreeX));
			if (mCharX.enabled)  DrawTransparentOnly(gChar, gCharMtls, ComposeSRT(mCharX));
			if (mZeldaX.enabled) DrawTransparentOnly(gZelda, gZeldaMtls, ComposeSRT(mZeldaX));

			if (mBoxRig && mBoxX.enabled) {
				mBoxRig->DrawTransparentOnly(ctx, ComposeSRT(mBoxX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB,
					cb.vLightDir, cb.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
			}
			if (mSkinRig && mSkinX.enabled) {
				BindSkinned();
				mSkinRig->DrawTransparentOnly(ctx, ComposeSRT(mSkinX),
					view, m_Projection, m_pConstantBuffer, m_pUseCB, m_pBoneCB,
					cb.vLightDir, cb.vLightColor, eye,
					m_Ka, m_Ks, m_Shininess, m_Ia,
					mDbg.disableNormal, mDbg.disableSpecular, mDbg.disableEmissive);
				BindStatic();
			}
		}

		ctx->OMSetBlendState(oldBS, oldBF, oldSM);
		ctx->OMSetDepthStencilState(oldDSS, oldSR);
		SAFE_RELEASE(oldBS); SAFE_RELEASE(oldDSS);
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	// 9) 蛤幗斜(惜錳 �香嚂�, 斜葬萄)
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	if (mDbg.showLightArrow) {
		Vector3 D = -dirV; D.Normalize();
		Matrix worldArrow = Matrix::CreateScale(m_ArrowScale) * Matrix::CreateWorld(m_ArrowPos, D, Vector3::UnitY);

		ConstantBuffer local = cb;
		local.mWorld = XMMatrixTranspose(worldArrow);
		local.mWorldInvTranspose = worldArrow.Invert();
		ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &local, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);

		// 鼻鷓 寥機
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

		ID3D11ShaderResourceView* nullAll[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
		ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullAll);

		ctx->DrawIndexed(indexCount, 0, 0);

		// 鼻鷓 犒掘
		ctx->VSSetShader(oVS, nullptr, 0);
		ctx->PSSetShader(oPS, nullptr, 0);
		ctx->IASetInputLayout(oIL);
		ctx->OMSetBlendState(oBS, oBF, oSM);
		ctx->OMSetDepthStencilState(oDSS, oRef);
		ctx->RSSetState(oRS);
		SAFE_RELEASE(oVS); SAFE_RELEASE(oPS); SAFE_RELEASE(oIL);
		SAFE_RELEASE(oBS); SAFE_RELEASE(oDSS); SAFE_RELEASE(oRS);
	}

#ifdef _DEBUG
	UpdateImGUI();
#endif
	m_pSwapChain->Present(1, 0);
}

#ifdef _DEBUG
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif

LRESULT CALLBACK TutorialApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef _DEBUG
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;
#endif
	return __super::WndProc(hWnd, message, wParam, lParam);
}
