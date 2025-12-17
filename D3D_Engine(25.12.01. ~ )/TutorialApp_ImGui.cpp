// InitImGUI / UninitImGUI / UpdateImGUI / AnimUI

#include "TutorialApp.h"
#include "../D3D_Core/pch.h"


bool TutorialApp::InitImGUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	//폰트 등록
	ImGuiIO& io = ImGui::GetIO();
	const ImWchar* kr = io.Fonts->GetGlyphRangesKorean();
	io.Fonts->Clear();
	io.Fonts->AddFontFromFileTTF("../Resource/fonts/Regular.ttf", 16.0f, nullptr, kr);

	ImGui_ImplWin32_Init(m_hWnd);
	ImGui_ImplDX11_Init(this->m_pDevice, this->m_pDeviceContext);


	return true;
}

void TutorialApp::UninitImGUI()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

static void AnimUI(const char* label,
	bool& play, bool& loop, float& speed, double& t,
	double durationSec,
	const std::function<void(double)>& evalPose)
{
	if (ImGui::TreeNode(label)) {
		ImGui::Checkbox("Play", &play); ImGui::SameLine();
		ImGui::Checkbox("Loop", &loop);

		ImGui::DragFloat("Speed (x)", &speed, 0.01f, -4.0f, 4.0f, "%.2f");

		const float maxT = (float)((durationSec > 0.0) ? durationSec : 1.0);
		float tUI = (float)t;
		if (ImGui::SliderFloat("Time (sec)", &tUI, 0.0f, maxT, "%.3f")) {
			t = (double)tUI;
			if (evalPose) evalPose(t);
		}

		if (ImGui::Button("Rewind")) { t = 0.0; if (evalPose) evalPose(t); }
		ImGui::SameLine();
		if (ImGui::Button("Go End")) { t = durationSec; if (evalPose) evalPose(t); }

		ImGui::TreePop();
	}
}

//================================================================================================

void TutorialApp::UpdateImGUI()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(u8"임꾸이(IMGUI)"))
	{
		// 상단 상태
		ImGui::Text("FPS: %.1f (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Separator();

		// 스냅샷 1회 저장
		static bool s_inited = false;
		static double s_initAnimT = 0.0;
		static float  s_initAnimSpeed = 1.0f;
		static Vector3 s_initCubePos{}, s_initCubeScale{};
		static float   s_initSpin = 0.0f, s_initFov = 60.0f, s_initNear = 0.1f, s_initFar = 1.0f;
		static Vector3 s_initLightColor{};
		static float   s_initLightYaw = 0.0f, s_initLightPitch = 0.0f, s_initLightIntensity = 1.0f;
		static Vector3 s_initKa{}, s_initIa{};
		static float   s_initKs = 0.5f, s_initShin = 32.0f;
		static Vector3 s_initArrowPos{}, s_initArrowScale{};
		if (!s_inited) {
			s_inited = true;
			s_initAnimT = mAnimT;
			s_initAnimSpeed = mAnimSpeed;
			s_initCubePos = cubeTransformA;   s_initCubeScale = cubeScale;   s_initSpin = spinSpeed;
			s_initFov = m_FovDegree;          s_initNear = m_Near;           s_initFar = m_Far;
			s_initLightColor = m_LightColor;  s_initLightYaw = m_LightYaw;   s_initLightPitch = m_LightPitch; s_initLightIntensity = m_LightIntensity;
			s_initKa = m_Ka; s_initIa = m_Ia; s_initKs = m_Ks; s_initShin = m_Shininess;
			s_initArrowPos = m_ArrowPos;      s_initArrowScale = m_ArrowScale;
		}

		// === Camera ===
		if (ImGui::CollapsingHeader(u8"Camera"))
		{
			ImGui::SliderFloat("FOV (deg)", &m_FovDegree, 10.0f, 120.0f, "%.1f");
			ImGui::DragFloat("Near", &m_Near, 0.001f, 0.0001f, 10.0f, "%.5f");
			ImGui::DragFloat("Far", &m_Far, 0.1f, 0.01f, 20000.0f);
			ImGui::Text(u8"카메라 속도 변경: F1 ~ F3");
			if (ImGui::Button(u8"카메라 초기화")) {
				m_FovDegree = s_initFov; m_Near = s_initNear; m_Far = s_initFar;
			}
		}

		//---------------------------------------------------


		if (ImGui::Begin("Toon Shading"), ImGuiCond_FirstUseEver) {
			ImGui::Checkbox("Use Toon", &mDbg.useToon);
			ImGui::Checkbox("Half-Lambert", &mDbg.toonHalfLambert);
			ImGui::DragFloat("Spec Step", &mDbg.toonSpecStep, 0.01f, 0.0f, 1.0f, "%.2f");
			ImGui::DragFloat("Spec Boost", &mDbg.toonSpecBoost, 0.01f, 0.0f, 3.0f, "%.2f");
			ImGui::DragFloat("Shadow Min", &mDbg.toonShadowMin, 0.005f, 0.0f, 0.10f, "%.3f");
		}
		ImGui::End();

		if (ImGui::Begin("Shadow / Light Camera"))
		{
			// ── Lighting ────────────────────────────────────────────────
			ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
			if (ImGui::CollapsingHeader(u8"Lighting"))
			{
				ImGui::SliderAngle("Yaw", &m_LightYaw, -180.0f, 180.0f);
				ImGui::SliderAngle("Pitch", &m_LightPitch, -89.0f, 89.0f);
				ImGui::ColorEdit3("Color", (float*)&m_LightColor);
				ImGui::SliderFloat("Intensity", &m_LightIntensity, 0.0f, 5.0f);
				if (ImGui::Button(u8"조명 초기화")) {
					m_LightColor = s_initLightColor;
					m_LightYaw = s_initLightYaw;
					m_LightPitch = s_initLightPitch;
					m_LightIntensity = s_initLightIntensity;
				}
			}

			// ── ShadowMap Preview / Grid ────────────────────────────────
			ImGui::Checkbox("Show ShadowMap", &mShUI.showSRV);
			ImGui::Checkbox("Show Grid", &mDbg.showGrid);
			ImGui::Checkbox("Use Orthographic", &mShUI.useOrtho);
			ImGui::Checkbox("Use followCamera", &mShUI.followCamera);

			if (mShUI.showSRV) {
				ImTextureID id = (ImTextureID)mShadowSRV.Get();
				if (id) ImGui::Image(id, ImVec2(256, 256), ImVec2(0, 0), ImVec2(1, 1));
				else    ImGui::TextUnformatted("Shadow SRV is null");
			}

			// ── Focus / Light (카메라 전방 락) ──────────────────────────
			ImGui::SeparatorText("Focus & Light (locked to camera)");
			ImGui::DragFloat("FocusDist", &mShUI.focusDist, 0.1f, 0.1f, 5000.0f);
			ImGui::DragFloat("LightDist", &mShUI.lightDist, 0.1f, 0.1f, 10000.0f);
			ImGui::DragFloat("Margin", &mShUI.coverMargin, 0.01f, 1.0f, 2.0f);

			// ── DepthOnly 옵션 ───────────────────────────────────────────
			ImGui::SeparatorText("DepthOnly");
			ImGui::SliderFloat("AlphaCut (DepthOnly)", &mShadowAlphaCut, 0.0f, 1.0f, "%.3f");

			// ── Bias(비교/래스터) ───────────────────────────────────────
			ImGui::SeparatorText("Bias");
			ImGui::DragFloat("CmpBias", &mShadowCmpBias, 0.0001f, 0.0f, 0.02f, "%.5f");
			ImGui::DragInt("DepthBias", (int*)&mShadowDepthBias, 1, 0, 200000);
			ImGui::DragFloat("SlopeScaledBias", &mShadowSlopeBias, 0.01f, 0.0f, 32.0f, "%.2f");

			// ── ShadowMap 해상도 + 재생성 ───────────────────────────────
			ImGui::SeparatorText("Shadowmap Size");
			static int resIdx =
				(mShadowW >= 4096) ? 3 :
				(mShadowW >= 2048) ? 2 :
				(mShadowW >= 1024) ? 1 : 0;
			const char* kResItems[] = { "512", "1024", "2048", "4096" };
			ImGui::Combo("Resolution", &resIdx, kResItems, IM_ARRAYSIZE(kResItems));

			// 읽기 전용(자동 커버 결과 확인)
			ImGui::SeparatorText("Derived (read-only)");
			ImGui::Text("FovY: %.1f deg", DirectX::XMConvertToDegrees(mShadowFovY));
			ImGui::Text("Near/Far: %.3f / %.3f", mShadowNear, mShadowFar);

			// 적용 버튼: RS(바이어스) + 텍스처(SRV/DSV) 재생성
			if (ImGui::Button("Apply (recreate shadow map)")) {
				int sz = 512;
				if (resIdx == 1) sz = 1024;
				else if (resIdx == 2) sz = 2048;
				else if (resIdx == 3) sz = 4096;
				mShadowW = mShadowH = sz;
				CreateShadowResources(m_pDevice); // RS(DepthBias/SlopeBias)와 DSV/SRV 갱신
			}
		}
		ImGui::End();

		//---------------------------------------------------

		// === Material (Blinn-Phong) ===
		if (ImGui::CollapsingHeader(u8"Material"))
		{
			ImGui::ColorEdit3("I_a (ambient light)", (float*)&m_Ia);
			ImGui::ColorEdit3("k_a (ambient refl.)", (float*)&m_Ka);
			ImGui::SliderFloat("k_s (specular)", &m_Ks, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("shininess", &m_Shininess, 2.0f, 256.0f, "%.0f");
			if (ImGui::Button(u8"재질 초기화")) {
				m_Ka = s_initKa; m_Ia = s_initIa; m_Ks = s_initKs; m_Shininess = s_initShin;
			}
		}

		// === Models ===
		if (ImGui::CollapsingHeader(u8"Models"))
		{
			auto ModelUI = [&](const char* name, XformUI& xf) {
				if (ImGui::TreeNode(name)) {
					ImGui::Checkbox("Enabled", &xf.enabled);
					ImGui::DragFloat3("Position", (float*)&xf.pos, 0.1f, -10000.0f, 10000.0f);
					ImGui::DragFloat3("Rotation (deg XYZ)", (float*)&xf.rotD, 0.5f, -720.0f, 720.0f);
					ImGui::DragFloat3("Scale", (float*)&xf.scl, 0.01f, 0.0001f, 1000.0f);
					if (ImGui::Button(u8"모델 초기화")) {
						xf.pos = xf.initPos; xf.rotD = xf.initRotD; xf.scl = xf.initScl; xf.enabled = true;
					}
					ImGui::TreePop();
				}
				};

			ModelUI("Tree", mTreeX);
			ModelUI("Character", mCharX);
			ModelUI("Zelda", mZeldaX);

			// Models 
			if (ImGui::TreeNode(u8"Debug Arrow (라이트 방향)")) {
				ImGui::Checkbox("Enabled", &mDbg.showLightArrow);
				ImGui::DragFloat3("Position", (float*)&m_ArrowPos, 0.1f, -10000.0f, 10000.0f);
				ImGui::DragFloat3("Scale", (float*)&m_ArrowScale, 0.01f, 0.0001f, 1000.0f);
				if (ImGui::Button(u8"화살표 초기화")) {
					m_ArrowPos = s_initArrowPos;
					m_ArrowScale = s_initArrowScale;
					mDbg.showLightArrow = true;
				}
				ImGui::TreePop();
			}

			if (ImGui::Button(u8"모든 모델 초기화")) {
				for (XformUI* p : { &mTreeX, &mCharX, &mZeldaX }) {
					p->pos = p->initPos; p->rotD = p->initRotD; p->scl = p->initScl; p->enabled = true;
				}
				m_ArrowPos = s_initArrowPos;
				m_ArrowScale = s_initArrowScale;
				mDbg.showLightArrow = true;
			}
		}

		if (ImGui::CollapsingHeader(u8"BoxHuman (RigidSkeletal)"))
		{
			// Transform
			ImGui::Checkbox("Enabled##Box", &mBoxX.enabled);
			ImGui::DragFloat3("Position##Box", (float*)&mBoxX.pos, 0.1f, -10000.0f, 10000.0f);
			ImGui::DragFloat3("Rotation (deg XYZ)##Box", (float*)&mBoxX.rotD, 0.5f, -720.0f, 720.0f);
			ImGui::DragFloat3("Scale##Box", (float*)&mBoxX.scl, 0.01f, 0.0001f, 1000.0f);
			if (ImGui::Button(u8"트랜스폼 초기화")) {
				mBoxX.pos = mBoxX.initPos; mBoxX.rotD = mBoxX.initRotD; mBoxX.scl = mBoxX.initScl; mBoxX.enabled = true;
			}

			ImGui::SeparatorText("Animation");
			if (mBoxRig)
			{
				const double tps = mBoxRig->GetTicksPerSecond();
				const double durS = mBoxRig->GetClipDurationSec();
				ImGui::Text("Ticks/sec: %.3f", tps);
				ImGui::Text("Duration : %.3f sec", durS);

				AnimUI("Controls",
					mBoxAC.play, mBoxAC.loop, mBoxAC.speed, mBoxAC.t,
					durS,
					[&](double tNow) { mBoxRig->EvaluatePose(tNow); });

				if (ImGui::Button(u8"애니메이션 초기화")) {
					mBoxAC.play = true; mBoxAC.loop = true; mBoxAC.speed = 1.0f; mBoxAC.t = 0.0;
					mBoxRig->EvaluatePose(mBoxAC.t);
				}
			}
			else {
				ImGui::TextDisabled("BoxHuman not loaded.");
			}
		}

		if (ImGui::CollapsingHeader(u8"SkinningTest (SkinnedSkeletal)"))
		{
			ImGui::Checkbox("Enabled", &mSkinX.enabled);
			ImGui::DragFloat3("Position", (float*)&mSkinX.pos, 0.1f, -10000.0f, 10000.0f);
			ImGui::DragFloat3("Rotation (deg XYZ)", (float*)&mSkinX.rotD, 0.5f, -720.0f, 720.0f);
			ImGui::DragFloat3("Scale", (float*)&mSkinX.scl, 0.01f, 0.0001f, 1000.0f);
			if (ImGui::Button(u8"트랜스폼 초기화##skin")) {
				mSkinX.pos = mSkinX.initPos; mSkinX.rotD = mSkinX.initRotD; mSkinX.scl = mSkinX.initScl; mSkinX.enabled = true;
			}

			ImGui::SeparatorText("Animation");
			if (mSkinRig)
			{
				const double durS = mSkinRig->DurationSec();
				ImGui::Text("Duration : %.3f sec", durS);

				AnimUI("Controls##skin",
					mSkinAC.play, mSkinAC.loop, mSkinAC.speed, mSkinAC.t,
					durS,
					[&](double tNow) { mSkinRig->EvaluatePose(tNow); });

				if (ImGui::Button(u8"애니메이션 초기화##skin")) {
					mSkinAC.play = true; mSkinAC.loop = true; mSkinAC.speed = 1.0f; mSkinAC.t = 0.0;
					mSkinRig->EvaluatePose(mSkinAC.t);
				}
			}
			else {
				ImGui::TextDisabled("Skinned rig not loaded.");
			}
		}


		// === Toggles / Render Debug ===
		if (ImGui::CollapsingHeader(u8"Toggles & Debug"))
		{
			ImGui::Checkbox("Show Skybox", &mDbg.showSky);
			ImGui::Checkbox("Show Opaque", &mDbg.showOpaque);
			ImGui::Checkbox("Show Transparent", &mDbg.showTransparent);

			ImGui::Separator();

			ImGui::Checkbox("Wireframe", &mDbg.wireframe); ImGui::SameLine();
			ImGui::Checkbox("Cull None", &mDbg.cullNone);
			ImGui::Checkbox("Depth Write/Test OFF (mesh)", &mDbg.depthWriteOff);
			ImGui::Checkbox("Freeze Time", &mDbg.freezeTime); // 이거 작동 안함

			ImGui::Separator();

			ImGui::Checkbox("Disable Normal", &mDbg.disableNormal);
			ImGui::Checkbox("Disable Specular", &mDbg.disableSpecular);
			ImGui::Checkbox("Disable Emissive", &mDbg.disableEmissive);
			ImGui::Checkbox("Force AlphaClip", &mDbg.forceAlphaClip);
			ImGui::DragFloat("alphaCut", &mDbg.alphaCut, 0.01f, 0.0f, 1.0f);

			if (ImGui::Button(u8"디버그 토글 초기화")) {
				mDbg = DebugToggles(); // 리셋
			}
		}
	}

	ImGui::End();
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}