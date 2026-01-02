// OnInitialize/OnUninitialize/OnUpdate/OnRender/WndProc

#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"
#ifdef _DEBUG
#include "imgui.h"
#endif

bool TutorialApp::OnInitialize()
{
	// =========================================================================
	// 0) D3D Core
	// =========================================================================
	if (!InitD3D())
		return false;

	// =========================================================================
	// 1) Render Targets (HDR Scene / GBuffer)
	// =========================================================================
	if (!CreateSceneHDRResources(m_pDevice))
		return false;

	if (!CreateGBufferResources(m_pDevice))
		return false;

#ifdef _DEBUG
	// =========================================================================
	// 2) ImGui
	// =========================================================================
	if (!InitImGUI())
		return false;
#endif

	// =========================================================================
	// 2.5) Physics (PhysX)
	// =========================================================================
	{
		PhysXContextDesc cdesc{};
		cdesc.enablePvd = false;          // 필요하면 true로 (링크/배포 셋업 완료 후)
		cdesc.dispatcherThreads = 2;      // 일단 고정 (원하면 하드웨어 코어 기반으로 바꿔도 됨)
		cdesc.enableCooking = true;

		mPxCtx = std::make_unique<PhysXContext>(cdesc);

		PhysXWorld::Desc wdesc{};
		wdesc.gravity = { 0.0f, -981.00f, 0.0f }; // 디버깅 용도로 임의 조정함

		wdesc.enableSceneLocks = true;
		wdesc.enableActiveTransforms = true;
		wdesc.enableContactEvents = true;
		wdesc.enableContactPoints = false;
		wdesc.enableContactModify = false;
		wdesc.enableCCD = false;

		mPxWorld = std::make_unique<PhysXWorld>(*mPxCtx, wdesc);
	}

	// =========================================================================
	// 3) Scene / Assets
	// =========================================================================
	if (!InitScene())
		return false;

	mPhysAccum = 0; // 혹시모르니 초기화 한번

	return true;
}

void TutorialApp::OnUninitialize()
{
	// =========================================================================
	// 0) Scene
	// =========================================================================
	UninitScene();

	// =========================================================================
	// 0.5) Physics shutdown (Scene 이후에 정리)
	// =========================================================================
	mPhysTestBody.reset();
	mPhysGround.reset();
	mPxWorld.reset();
	mPxCtx.reset();

#ifdef _DEBUG
	// =========================================================================
	// 1) ImGui
	// =========================================================================
	UninitImGUI();
#endif

	// =========================================================================
	// 2) D3D Core
	// =========================================================================
	UninitD3D();
}

void TutorialApp::OnUpdate()
{
	// =========================================================================
	// 0) Time (FreezeTime 대응)
	// =========================================================================
	static float tHold = 0.0f;
	if (!mDbg.freezeTime) tHold = GameTimer::m_Instance->TotalTime();
	const float t = tHold;



	// =========================================================================
// 0.25) Mouse pick + drag (kinematic target tool)
// =========================================================================
	if (mPxWorld && (mPhysMousePickEnable || mPhysDragging))
	{
		auto* input = InputSystem::Instance;
		if (input)
		{
#ifdef _DEBUG
			const bool uiWantsMouse = (ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().WantCaptureMouse;
#else
			const bool uiWantsMouse = false;
#endif
			using Tracker = DirectX::Mouse::ButtonStateTracker;

			const bool lPressed = (input->m_MouseStateTracker.leftButton == Tracker::PRESSED);
			const bool lReleased = (input->m_MouseStateTracker.leftButton == Tracker::RELEASED);
			const bool lDown = input->m_MouseState.leftButton;

			float dtFrame = GameTimer::m_Instance->DeltaTime();
			dtFrame = std::clamp(dtFrame, 0.0f, 0.05f);

			// 클릭 시작: 픽
			if (mPhysMousePickEnable && lPressed && !uiWantsMouse)
			{
				Vec3 ro, rd;
				if (GetMousePickRay(ro, rd))
				{
					RaycastHit hit{};
					if (mPxWorld->Raycast(ro, rd, mPhysPickMaxDist, hit, 0xFFFFFFFFu, false))
					{
						const int idx = FindDropByNativeActor(hit.nativeActor);

						// 드랍 바디가 맞으면 선택 + (옵션) 드래그 시작
						if (idx >= 0)
						{
							mPhysSelDrop = idx;

							if (mPhysMouseDragEnable)
								BeginMouseDrag(idx, hit);
						}
					}
				}
			}

			// 드래그 유지
			if (mPhysDragging && lDown && !uiWantsMouse)
			{
				UpdateMouseDrag(dtFrame);
			}

			// 드래그 종료
			if (mPhysDragging && lReleased)
			{
				EndMouseDrag(dtFrame);
			}
		}
	}




	// =========================================================================
	// 0.5) Physics step (fixed timestep) + apply moved transforms
	// =========================================================================
	if (mPxWorld)
	{
		// Step (physics only)
		if (mPhysEnable)
		{
			const bool timeStopped = mDbg.freezeTime;

			if (!timeStopped && !mPhysPaused)
			{
				float dtPhys = GameTimer::m_Instance->DeltaTime();

				// 로딩/첫 프레임 hitch 방어: dt 상한 (예: 50ms)
				dtPhys = std::clamp(dtPhys, 0.0f, 0.05f);

				// accumulator도 상한: 최대 maxSubSteps 만큼만 따라잡게
				const float maxAccum = mPhysFixedDt * (float)mPhysMaxSubSteps;
				mPhysAccum = min(mPhysAccum + dtPhys, maxAccum);

				int steps = 0;
				while (mPhysAccum >= mPhysFixedDt && steps < mPhysMaxSubSteps)
				{
					mPxWorld->Step(mPhysFixedDt);
					mPhysAccum -= mPhysFixedDt;
					++steps;
				}
			}
			else
			{
				// Pause 중 1틱만
				if (!timeStopped && mPhysStepOnce)
				{
					mPxWorld->Step(mPhysFixedDt);
					mPhysStepOnce = false;
				}

				// pause/freeze 동안 누적 방지
				mPhysAccum = 0.0f;
			}
		}

		// 결과 반영 (한 번만)
		mPxWorld->DrainActiveTransforms(mPhysMoved);
		for (const ActiveTransform& at : mPhysMoved)
		{
			XformUI* xf = reinterpret_cast<XformUI*>(at.userData);
			if (!xf) continue;

			xf->pos = { at.position.x, at.position.y, at.position.z };
			xf->rotQ = at.rotation;
			xf->useQuat = true;
		}

		mPxWorld->DrainEvents(mPhysEvents);

		// 드랍 메쉬 행렬 동기화 (한 번만)
		SyncDropFromPhysics();
	}

	// =========================================================================
	// 1) Simple world animation (기존 큐브)
	// =========================================================================
	const XMMATRIX mSpin = XMMatrixRotationY(t * spinSpeed);
	const XMMATRIX mScaleA = XMMatrixScaling(cubeScale.x, cubeScale.y, cubeScale.z);
	const XMMATRIX mTranslateA = XMMatrixTranslation(cubeTransformA.x, cubeTransformA.y, cubeTransformA.z);
	m_World = mScaleA * mSpin * mTranslateA;

	// =========================================================================
	// 2) Animation update (Rigid / Skinned)
	// =========================================================================
	const double dt = (double)GameTimer::m_Instance->DeltaTime();

	// ---- BoxHuman (Rigid) ----
	if (mBoxRig)
	{
		if (!mDbg.freezeTime && mBoxAC.play) mBoxAC.t += dt * mBoxAC.speed;

		const double durSec = mBoxRig->GetClipDurationSec();
		if (durSec > 0.0)
		{
			if (mBoxAC.loop)
			{
				mBoxAC.t = fmod(mBoxAC.t, durSec);
				if (mBoxAC.t < 0.0) mBoxAC.t += durSec;
			}
			else
			{
				if (mBoxAC.t >= durSec) { mBoxAC.t = durSec; mBoxAC.play = false; }
				if (mBoxAC.t < 0.0) { mBoxAC.t = 0.0;   mBoxAC.play = false; }
			}
		}

		mBoxRig->EvaluatePose(mBoxAC.t, mBoxAC.loop);
	}

	// ---- Skinned ----
	if (mSkinRig)
	{
		if (!mDbg.freezeTime && mSkinAC.play) mSkinAC.t += dt * mSkinAC.speed;

		const double durSec = mSkinRig->DurationSec();
		if (durSec > 0.0)
		{
			if (mSkinAC.loop)
			{
				mSkinAC.t = fmod(mSkinAC.t, durSec);
				if (mSkinAC.t < 0.0) mSkinAC.t += durSec;
			}
			else
			{
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

	// =========================================================================
	// 0) Common sampler binding (s0~s3)
	// =========================================================================
	ID3D11SamplerState* s0 = m_pSamplerLinear;                  // s0: 일반 텍스처
	ID3D11SamplerState* s1 = mSamShadowCmp.Get();               // s1: shadow compare
	ID3D11SamplerState* s2 = m_pSamplerLinear;                  // s2: ramp/기타 (임시)
	ID3D11SamplerState* s3 = mSamIBLClamp ? mSamIBLClamp.Get()  // s3: IBL clamp
		: m_pSamplerLinear;

	ID3D11SamplerState* samps[4] = { s0, s1, s2, s3 };
	ctx->PSSetSamplers(0, 4, samps);

	// =========================================================================
	// 1) Shadow camera + Shadow CB 업데이트 (라이트 뷰/프로젝션)
	// =========================================================================
	UpdateLightCameraAndShadowCB(ctx);

	// =========================================================================
	// 2) Camera params clamp + Projection 갱신
	// =========================================================================
	if (m_FovDegree < 10.0f)       m_FovDegree = 10.0f;
	else if (m_FovDegree > 120.0f) m_FovDegree = 120.0f;

	if (m_Near < 0.0001f)          m_Near = 0.0001f;

	const float minFar = m_Near + 0.001f;
	if (m_Far < minFar)            m_Far = minFar;

	const float aspect = m_ClientWidth / (float)m_ClientHeight;
	m_Projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_FovDegree), aspect, m_Near, m_Far);

	// =========================================================================
	// 3) Rasterizer 선택 (Wire / CullNone / Default)
	// =========================================================================
	if (mDbg.wireframe && m_pWireRS)         ctx->RSSetState(m_pWireRS);
	else if (mDbg.cullNone && m_pDbgRS)      ctx->RSSetState(m_pDbgRS);
	else                                     ctx->RSSetState(m_pCullBackRS);

	// =========================================================================
	// 4) Main RT 선택 (SceneHDR vs BackBuffer) + Clear
	//    - SRV/RTV 충돌 방지: (특히 ToneMap에서 t0 썼으면 해제)
	// =========================================================================
	ID3D11ShaderResourceView* nullSRV[16] = {};
	ctx->PSSetShaderResources(0, 16, nullSRV);

	ID3D11RenderTargetView* mainRTV = m_pRenderTargetView;
	if (mTone.useSceneHDR && mSceneHDRRTV.Get())
		mainRTV = mSceneHDRRTV.Get();

	ctx->OMSetRenderTargets(1, &mainRTV, m_pDepthStencilView);

	const float clearColor[4] = { color[0], color[1], color[2], color[3] };
	ctx->ClearRenderTargetView(mainRTV, clearColor);
	ctx->ClearDepthStencilView(m_pDepthStencilView,
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// =========================================================================
	// 5) Per-frame common CB 업로드 (b0/b1/b8/b12)
	// =========================================================================
	// ---- View ----
	m_Camera.GetViewMatrix(view);
	Matrix viewNoTrans = view;
	viewNoTrans._41 = viewNoTrans._42 = viewNoTrans._43 = 0.0f;

	// ---- CB0 (b0) ----
	ConstantBuffer cb{};
	cb.mWorld = XMMatrixTranspose(Matrix::Identity);
	cb.mWorldInvTranspose = XMMatrixInverse(nullptr, Matrix::Identity);
	cb.mView = XMMatrixTranspose(view);
	cb.mProjection = XMMatrixTranspose(m_Projection);

	// dir light from yaw/pitch
	XMMATRIX R = XMMatrixRotationRollPitchYaw(m_LightPitch, m_LightYaw, 0.0f);
	XMVECTOR base = XMVector3Normalize(XMVectorSet(0, 0, 1, 0));
	XMVECTOR L = XMVector3Normalize(XMVector3TransformNormal(base, R));
	Vector3 dirV = { XMVectorGetX(L), XMVectorGetY(L), XMVectorGetZ(L) };

	cb.vLightDir = Vector4(dirV.x, dirV.y, dirV.z, 0.0f);

	const float dirOn = mDbg.dirLightEnable ? 1.0f : 0.0f;
	cb.vLightColor = Vector4(
		m_LightColor.x * m_LightIntensity * dirOn,
		m_LightColor.y * m_LightIntensity * dirOn,
		m_LightColor.z * m_LightIntensity * dirOn,
		dirOn);

	ctx->UpdateSubresource(m_pConstantBuffer, 0, nullptr, &cb, 0, 0);
	ctx->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
	ctx->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

	// ---- BP (b1) ----
	const Vector3 eye = m_Camera.m_World.Translation();

	BlinnPhongCB bp{};
	bp.EyePosW = Vector4(eye.x, eye.y, eye.z, 1);
	bp.kA = Vector4(m_Ka.x, m_Ka.y, m_Ka.z, 0);
	bp.kSAlpha = Vector4(m_Ks, m_Shininess, 0, 0);
	bp.I_ambient = Vector4(m_Ia.x, m_Ia.y, m_Ia.z, 0);

	ctx->UpdateSubresource(m_pBlinnCB, 0, nullptr, &bp, 0, 0);
	ctx->PSSetConstantBuffers(1, 1, &m_pBlinnCB);

	// ---- Deferred point light CB (b12) ----
	if (mCB_DeferredLights)
	{
		CB_DeferredLights dl{};
		dl.eyePosW = DirectX::XMFLOAT4(eye.x, eye.y, eye.z, 1.0f);
		dl.meta[0] = 1u;
		dl.meta[1] = mPoint.enable ? 1u : 0u;
		dl.meta[2] = (uint32_t)max(0, min(1, mPoint.falloffMode));
		dl.meta[3] = 0u;

		dl.pointPosRange[0] = DirectX::XMFLOAT4(mPoint.pos.x, mPoint.pos.y, mPoint.pos.z, mPoint.range);
		dl.pointColorInt[0] = DirectX::XMFLOAT4(mPoint.color.x, mPoint.color.y, mPoint.color.z, mPoint.intensity);

		ctx->UpdateSubresource(mCB_DeferredLights.Get(), 0, nullptr, &dl, 0, 0);
		ID3D11Buffer* b12 = mCB_DeferredLights.Get();
		ctx->PSSetConstantBuffers(12, 1, &b12);
	}

	// ---- PBR params (b8) ----
	CB_PBRParams pbr{};
	pbr.useBaseColorTex = mPbr.useBaseColorTex ? 1u : 0u;
	pbr.useNormalTex = mPbr.useNormalTex ? 1u : 0u;
	pbr.useMetalTex = mPbr.useMetalTex ? 1u : 0u;
	pbr.useRoughTex = mPbr.useRoughTex ? 1u : 0u;

	pbr.baseColorOverride = { mPbr.baseColor.x, mPbr.baseColor.y, mPbr.baseColor.z, 1.0f };
	pbr.m_r_n_flags = { mPbr.metallic, mPbr.roughness, mPbr.normalStrength, mPbr.flipNormalY ? 1.0f : 0.0f };

	pbr.envDiff = XMFLOAT4(mPbr.envDiffColor.x, mPbr.envDiffColor.y, mPbr.envDiffColor.z, mPbr.envDiffIntensity);
	pbr.envSpec = XMFLOAT4(mPbr.envSpecColor.x, mPbr.envSpecColor.y, mPbr.envSpecColor.z, mPbr.envSpecIntensity);
	pbr.envInfo = XMFLOAT4(mPrefilterMaxMip, 0, 0, 0);

	ctx->UpdateSubresource(m_pPBRParamsCB, 0, nullptr, &pbr, 0, 0);
	ctx->PSSetConstantBuffers(8, 1, &m_pPBRParamsCB);

	// =========================================================================
	// 6) Static mesh pipeline 기본 바인드 (Forward 경로용 기본값)
	// =========================================================================
	ctx->IASetInputLayout(m_pMeshIL);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_pMeshVS, nullptr, 0);
	ctx->PSSetShader(m_pMeshPS, nullptr, 0);

	// =========================================================================
	// 7) Shadow passes (DepthOnly)
	// =========================================================================
	RenderShadowPass_Main(ctx, cb);
	RenderPointShadowPass_Cube(ctx, cb);

	// =========================================================================
	// 8) Shadow bind (t5/s1/b6) + PointShadow bind (t10/b13)
	// =========================================================================
	auto BindShadowForShading = [&]()
		{
			ID3D11Buffer* b6 = mCB_Shadow.Get();
			ID3D11SamplerState* cmp = mSamShadowCmp.Get();
			ID3D11ShaderResourceView* shSRV = mShadowSRV.Get();

			ctx->PSSetConstantBuffers(6, 1, &b6);
			ctx->PSSetSamplers(1, 1, &cmp);
			ctx->PSSetShaderResources(5, 1, &shSRV);
		};

	BindShadowForShading();

	// Point shadow cube (t10 / b13)
	if (mCB_PointShadow)
	{
		CB_PointShadow pcb{};
		pcb.posRange = DirectX::XMFLOAT4(mPoint.pos.x, mPoint.pos.y, mPoint.pos.z, mPoint.range);
		pcb.params = DirectX::XMFLOAT4(mPoint.shadowBias,
			(mPoint.enable && mPoint.shadowEnable) ? 1.0f : 0.0f, 0.0f, 0.0f);

		ctx->UpdateSubresource(mCB_PointShadow.Get(), 0, nullptr, &pcb, 0, 0);
		ID3D11Buffer* b13 = mCB_PointShadow.Get();
		ctx->PSSetConstantBuffers(13, 1, &b13);
	}

	{
		ID3D11ShaderResourceView* srv = (mPoint.enable && mPoint.shadowEnable)
			? mPointShadowSRV.Get()
			: nullptr;
		ctx->PSSetShaderResources(10, 1, &srv);
	}

	// =========================================================================
	// 9) Toon (t6/b7) bind (옵션)
	// =========================================================================
	{
		ToonCB_ t{};
		t.useToon = mDbg.useToon ? 1u : 0u;
		t.halfLambert = mDbg.toonHalfLambert ? 1u : 0u;
		t.specStep = mDbg.toonSpecStep;
		t.specBoost = mDbg.toonSpecBoost;
		t.shadowMin = mDbg.toonShadowMin;

		if (m_pToonCB)
		{
			ctx->UpdateSubresource(m_pToonCB, 0, nullptr, &t, 0, 0);
			ctx->PSSetConstantBuffers(7, 1, &m_pToonCB);
		}
		if (m_pRampSRV && mDbg.useToon)
		{
			ctx->PSSetShaderResources(6, 1, &m_pRampSRV);
		}
	}

	// =========================================================================
	// 10) Main render path (Deferred vs Forward)
	// =========================================================================
	if (mDbg.useDeferred)
	{
		// ---------------------------------------------------------------------
		// 10-A) GBuffer pass (MRT)
		// ---------------------------------------------------------------------
		{
			ID3D11ShaderResourceView* null4[4] = { nullptr,nullptr,nullptr,nullptr };
			ctx->PSSetShaderResources(0, 4, null4); // t0~t3 충돌 방지

			ID3D11RenderTargetView* mrt[4] =
			{
				mGBufferRTV[0].Get(),
				mGBufferRTV[1].Get(),
				mGBufferRTV[2].Get(),
				mGBufferRTV[3].Get(),
			};

			ctx->OMSetRenderTargets(4, mrt, m_pDepthStencilView);

			const float clear0[4] = { 0,0,0,0 };
			for (int i = 0; i < 4; ++i) ctx->ClearRenderTargetView(mGBufferRTV[i].Get(), clear0);
			ctx->ClearDepthStencilView(m_pDepthStencilView,
				D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			RenderGBufferPass(ctx, cb);
		}

		// ---------------------------------------------------------------------
		// 10-B) Lighting pass (Fullscreen) -> Main RTV
		// ---------------------------------------------------------------------
		{
			ID3D11RenderTargetView* outRTV =
				(mTone.useSceneHDR && mSceneHDRRTV.Get()) ? mSceneHDRRTV.Get() : m_pRenderTargetView;

			ctx->OMSetRenderTargets(1, &outRTV, m_pDepthStencilView);

			if (mDbg.showGBufferFS) RenderGBufferDebugPass(ctx);
			else                    RenderDeferredLightPass(ctx);
		}

		// ---------------------------------------------------------------------
		// 10-C) Shadow/Toon 리바인드 (Deferred pass가 상태를 건드릴 수 있음)
		// ---------------------------------------------------------------------
		BindShadowForShading();
		if (mDbg.useToon && m_pRampSRV)
		{
			ctx->PSSetShaderResources(6, 1, &m_pRampSRV);
		}

		// ---------------------------------------------------------------------
		// 10-D) Sky / Debug / Transparent overlay
		// ---------------------------------------------------------------------
		RenderSkyPass(ctx, viewNoTrans);
		RenderDebugPass(ctx, cb, dirV);
		RenderTransparentPass(ctx, cb, eye);
	}
	else
	{
		// ---------------------------------------------------------------------
		// 10-E) Forward path (기존)
		// ---------------------------------------------------------------------
		RenderSkyPass(ctx, viewNoTrans);
		RenderOpaquePass(ctx, cb, eye);
		RenderCutoutPass(ctx, cb, eye);
		RenderDebugPass(ctx, cb, dirV);
		RenderTransparentPass(ctx, cb, eye);
	}

	// =========================================================================
	// 11) ToneMap (SceneHDR -> BackBuffer)
	// =========================================================================
	if (mTone.useSceneHDR && mSceneHDRSRV.Get())
		RenderToneMapPass(ctx);

#ifdef _DEBUG
	// =========================================================================
	// 12) ImGui overlay (항상 백버퍼 위)
	// =========================================================================
	{
		ID3D11RenderTargetView* bb = m_pRenderTargetView;
		ctx->OMSetRenderTargets(1, &bb, nullptr);
		UpdateImGUI();
	}
#endif

	// =========================================================================
	// 13) Present
	// =========================================================================
	m_pSwapChain->Present(1, 0);
}

void TutorialApp::SyncDropFromPhysics()
{
	for (int i = 0; i < kDropCount; ++i)
	{
		if (!mDropBody[i]) continue;

		const Vec3 p = mDropBody[i]->GetPosition();
		const Quat q = mDropBody[i]->GetRotation();

		const Matrix R = Matrix::CreateFromQuaternion(q);
		const Matrix T = Matrix::CreateTranslation(p);
		mDropWorld[i] = R * T;
	}
}

IRigidBody* TutorialApp::GetSelectedDrop()
{
	const int idx = std::clamp(mPhysSelDrop, 0, kDropCount - 1);
	return mDropBody[idx].get();
}

void TutorialApp::TeleportDropBody(int idx, const Vec3& p, const Quat& q, bool resetVel, bool wake)
{
	if (idx < 0 || idx >= kDropCount) return;
	IRigidBody* b = mDropBody[idx].get();
	if (!b) return;

	// "Teleport"는 즉시 pose 변경이 목적이니까 kinematic이어도 SetTransform이 맞다.
	b->SetTransform(p, q);

	if (resetVel)
	{
		b->SetLinearVelocity(Vec3::Zero);
		b->SetAngularVelocity(Vec3::Zero);
	}

	if (wake)
		b->WakeUp();
}


void TutorialApp::ResetDropBody(int idx, bool resetVel)
{
	if (idx < 0 || idx >= kDropCount) return;
	if (!mDropBody[idx]) return;

	TeleportDropBody(idx, mDropInitPos[idx], mDropInitRot[idx], resetVel, /*wake*/true);
}

void TutorialApp::ResetDropBodies(bool resetVel)
{
	for (int i = 0; i < kDropCount; ++i)
		ResetDropBody(i, resetVel);

	// UI도 선택 바디 기준으로 맞춰줌
	const int idx = std::clamp(mPhysSelDrop, 0, kDropCount - 1);
	mPhysTeleportPos = mDropInitPos[idx];
	mPhysTeleportRotD = Vec3::Zero;

	// 누적 dt 제거(리셋 직후 폭주 방지)
	mPhysAccum = 0.0f;

	SyncDropFromPhysics();
}

void TutorialApp::NudgeSelectedDrop(const Vec3& delta)
{
	const int idx = std::clamp(mPhysSelDrop, 0, kDropCount - 1);
	IRigidBody* b = mDropBody[idx].get();
	if (!b) return;

	const Vec3 p = b->GetPosition() + delta;
	const Quat q = b->GetRotation();
	TeleportDropBody(idx, p, q, mPhysZeroVelOnMove, mPhysWakeOnMove);
	mPhysTeleportPos = p;
}

// ============================================================================
// ------------------------------------------------------------
// Helpers: quaternion rotate (DirectXMath로 안전하게 처리)
// ------------------------------------------------------------
static Vec3 RotateVec(const Quat& q, const Vec3& v)
{
	using namespace DirectX;
	const XMVECTOR vv = XMVectorSet(v.x, v.y, v.z, 0.0f);
	const XMVECTOR qq = XMVectorSet(q.x, q.y, q.z, q.w);
	const XMVECTOR r = XMVector3Rotate(vv, qq);
	return Vec3(XMVectorGetX(r), XMVectorGetY(r), XMVectorGetZ(r));
}

static Vec3 RotateVecInv(const Quat& q, const Vec3& v)
{
	using namespace DirectX;
	const XMVECTOR vv = XMVectorSet(v.x, v.y, v.z, 0.0f);
	const XMVECTOR qq = XMQuaternionInverse(XMVectorSet(q.x, q.y, q.z, q.w));
	const XMVECTOR r = XMVector3Rotate(vv, qq);
	return Vec3(XMVectorGetX(r), XMVectorGetY(r), XMVectorGetZ(r));
}

bool TutorialApp::GetMousePickRay(Vec3& outOrigin, Vec3& outDir) const
{
	auto* input = InputSystem::Instance;
	if (!input) return false;

	// 마우스 좌표 (클라이언트 기준이라고 가정: DirectXTK 기본 absolute 모드)
	const float mx = (float)input->m_MouseState.x;
	const float my = (float)input->m_MouseState.y;

	// 카메라 View	

	// Projection은 렌더에서 매 프레임 갱신하지만, 픽에도 최신값이 필요하면 여기서 재계산해도 됨.
	// (여기선 m_Projection 그대로 사용)
	const DirectX::XMMATRIX V = view;
	const DirectX::XMMATRIX P = m_Projection;
	const DirectX::XMMATRIX W = DirectX::XMMatrixIdentity();

	// Unproject: near(0) / far(1)
	const DirectX::XMVECTOR pNear = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mx, my, 0.0f, 1.0f),
		0.0f, 0.0f, (float)m_ClientWidth, (float)m_ClientHeight,
		0.0f, 1.0f, P, V, W);

	const DirectX::XMVECTOR pFar = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mx, my, 1.0f, 1.0f),
		0.0f, 0.0f, (float)m_ClientWidth, (float)m_ClientHeight,
		0.0f, 1.0f, P, V, W);

	const Vec3 o(DirectX::XMVectorGetX(pNear), DirectX::XMVectorGetY(pNear), DirectX::XMVectorGetZ(pNear));
	const Vec3 f(DirectX::XMVectorGetX(pFar), DirectX::XMVectorGetY(pFar), DirectX::XMVectorGetZ(pFar));

	Vec3 d = f - o;
	if (d.LengthSquared() < 1e-8f) return false;
	d.Normalize();

	outOrigin = o;
	outDir = d;
	return true;
}

int TutorialApp::FindDropByNativeActor(void* nativeActor) const
{
	if (!nativeActor) return -1;
	for (int i = 0; i < kDropCount; ++i)
	{
		if (!mDropBody[i]) continue;
		if (mDropBody[i]->GetNativeActor() == nativeActor)
			return i;
	}
	return -1;
}

void TutorialApp::BeginMouseDrag(int idx, const RaycastHit& hit)
{
	if (idx < 0 || idx >= kDropCount) return;
	IRigidBody* b = mDropBody[idx].get();
	if (!b) return;

	// 선택 UI도 같이 갱신
	mPhysSelDrop = idx;
	mPhysTeleportPos = b->GetPosition();
	mPhysTeleportRotD = Vec3::Zero;

	// 드래그 상태 저장
	mPhysDragging = true;
	mPhysDragIdx = idx;

	mPhysDragPrevKinematic = b->IsKinematic();
	mPhysDragStartRot = b->GetRotation();

	// “카메라를 바라보는 평면” 위에서 끌기 (깊이 유지)
	mPhysDragPlanePoint = hit.position;
	mPhysDragPlaneNormal = m_Camera.GetForward(); // normalized 라고 가정

	// 바디 원점 기준, 히트 지점의 로컬 오프셋을 저장해두면
	// '잡은 점'이 커서에 달라붙는 느낌이 됨(중심만 따라오면 좀 어색함).
	const Vec3 bodyPos = b->GetPosition();
	const Vec3 offsetWorld = hit.position - bodyPos;
	mPhysDragLocalOffset = RotateVecInv(mPhysDragStartRot, offsetWorld);

	// 드래그 방식: "body를 잠깐 kinematic으로 바꾸고 target을 계속 업데이트"
	if (!mPhysDragPrevKinematic)
		b->SetKinematic(true);

	// 초기 target 세팅
	const Vec3 keepGrabPoint = hit.position;
	const Vec3 offsetBackToWorld = RotateVec(mPhysDragStartRot, mPhysDragLocalOffset);
	const Vec3 targetPos = keepGrabPoint - offsetBackToWorld;

	b->SetKinematicTarget(targetPos, mPhysDragStartRot);

	mPhysDragPrevTargetPos = targetPos;
	mPhysDragCurrTargetPos = targetPos;
}

void TutorialApp::UpdateMouseDrag(float dt)
{
	if (!mPhysDragging) return;
	if (mPhysDragIdx < 0 || mPhysDragIdx >= kDropCount) return;

	IRigidBody* b = mDropBody[mPhysDragIdx].get();
	if (!b) return;

	Vec3 ro, rd;
	if (!GetMousePickRay(ro, rd))
		return;

	// Ray-plane intersection
	const Vec3 n = mPhysDragPlaneNormal;
	const float denom = rd.Dot(n);
	if (fabsf(denom) < 1e-5f)
		return;

	const float t = (mPhysDragPlanePoint - ro).Dot(n) / denom;
	const float tt = max(t, 0.0f);

	const Vec3 grabPoint = ro + rd * tt;

	const Vec3 offsetBackToWorld = RotateVec(mPhysDragStartRot, mPhysDragLocalOffset);
	const Vec3 targetPos = grabPoint - offsetBackToWorld;

	b->SetKinematicTarget(targetPos, mPhysDragStartRot);

	mPhysDragPrevTargetPos = mPhysDragCurrTargetPos;
	mPhysDragCurrTargetPos = targetPos;

	(void)dt;
}

void TutorialApp::EndMouseDrag(float dt)
{
	if (!mPhysDragging) return;

	IRigidBody* b = (mPhysDragIdx >= 0 && mPhysDragIdx < kDropCount) ? mDropBody[mPhysDragIdx].get() : nullptr;

	if (b)
	{
		// 드래그 시작 전에 dynamic이었던 바디는 원상 복구(kinematic -> dynamic)
		if (!mPhysDragPrevKinematic)
		{
			b->SetKinematic(false);

			if (mPhysThrowOnRelease && dt > 1e-6f)
			{
				// 놓을 때 손맛: 마지막 target 이동량으로 속도 부여
				const Vec3 v = (mPhysDragCurrTargetPos - mPhysDragPrevTargetPos) * (1.0f / dt);
				b->SetLinearVelocity(v);
				b->WakeUp();
			}
		}
	}

	mPhysDragging = false;
	mPhysDragIdx = -1;
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
