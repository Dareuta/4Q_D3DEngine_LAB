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
				if (mBoxAC.t >= durSec) { mBoxAC.t = durSec; mBoxAC.play = false; } // 끝에서 정지
				if (mBoxAC.t < 0.0) { mBoxAC.t = 0.0;   mBoxAC.play = false; } // 앞에서 정지
			}
		}
		mBoxRig->EvaluatePose(mBoxAC.t, mBoxAC.loop);  // ← loop 전달
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

	ID3D11SamplerState* s0 = m_pSamplerLinear;
	ID3D11SamplerState* s1 = mSamShadowCmp.Get();
	ID3D11SamplerState* s2 = m_pSamplerLinear;

	ID3D11SamplerState* samps[3] = { s0, s1, s2 };
	ctx->PSSetSamplers(0, 3, samps); // 일단 전부 채워두고 생각하자


	// 0) 라이트 카메라/섀도우 CB 업데이트 

	UpdateLightCameraAndShadowCB(ctx); // mLightView, mLightProj, mShadowVP, mCB_Shadow

	// ───────────────────────────────────────────────────────────────
	// 1) 기본 파라미터 클램프 + 메인 RT 클리어
	// ───────────────────────────────────────────────────────────────

	if (m_FovDegree < 10.0f)       m_FovDegree = 10.0f;
	else if (m_FovDegree > 120.0f) m_FovDegree = 120.0f;
	if (m_Near < 0.0001f)          m_Near = 0.0001f;
	float minFar = m_Near + 0.001f;
	if (m_Far < minFar)            m_Far = minFar;

	const float aspect = m_ClientWidth / (float)m_ClientHeight;
	m_Projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_FovDegree), aspect, m_Near, m_Far);

	// RS 선택
	if (mDbg.wireframe && m_pWireRS)         ctx->RSSetState(m_pWireRS);
	else if (mDbg.cullNone && m_pDbgRS)      ctx->RSSetState(m_pDbgRS);
	else                                     ctx->RSSetState(m_pCullBackRS);

	const float clearColor[4] = { color[0], color[1], color[2], color[3] };
	ctx->ClearRenderTargetView(m_pRenderTargetView, clearColor);
	ctx->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// ───────────────────────────────────────────────────────────────
	// 2) 공통 CB0(b0) / Blinn(b1) 업로드 (메인 카메라 기준)
	// ───────────────────────────────────────────────────────────────

	m_Camera.GetViewMatrix(view);
	Matrix viewNoTrans = view; viewNoTrans._41 = viewNoTrans._42 = viewNoTrans._43 = 0.0f;

	ConstantBuffer cb{};
	cb.mWorld = XMMatrixTranspose(Matrix::Identity);
	cb.mWorldInvTranspose = XMMatrixInverse(nullptr, Matrix::Identity);
	cb.mView = XMMatrixTranspose(view);
	cb.mProjection = XMMatrixTranspose(m_Projection);

	// 디렉셔널 라이트(dir from yaw/pitch)
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

	// 공통 셰이더(정적 메쉬) 기본 바인드
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);
	//if (m_pSamplerLinear) ctx->PSSetSamplers(0, 1, &m_pSamplerLinear);

	//============================================================================================

	// 3) SHADOW PASS (DepthOnly)  
	RenderShadowPass_Main(ctx, cb);
	// 4) SKYBOX (선택)
	RenderSkyPass(ctx, viewNoTrans);

	// 5) 본 패스에서 섀도우 샘플 바인드 (PS: t5/s1/b6)
	{
		ID3D11Buffer* b6r = mCB_Shadow.Get();
		ID3D11SamplerState* cmp = mSamShadowCmp.Get();
		ID3D11ShaderResourceView* shSRV = mShadowSRV.Get();

		ctx->PSSetConstantBuffers(6, 1, &b6r);
		ctx->PSSetSamplers(1, 1, &cmp);
		ctx->PSSetShaderResources(5, 1, &shSRV);
	}

	// === Toon ramp bind (PS: t6/b7) ===
	{
		//툰 셰이딩 바인드
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

	// 6) OPAQUE
	RenderOpaquePass(ctx, cb, eye);
	// 7) CUTOUT (alpha-test 강제 모드)
	RenderCutoutPass(ctx, cb, eye);
	// 8) TRANSPARENT
	RenderTransparentPass(ctx, cb, eye);
	// 9) 디버그(광원 화살표, 그리드)
	RenderDebugPass(ctx, cb, dirV);

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
