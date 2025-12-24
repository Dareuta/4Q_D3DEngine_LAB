// InitImGUI / UninitImGUI / UpdateImGUI / AnimUI

#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"


bool TutorialApp::InitImGUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	//폰트 등록
	ImGuiIO& io = ImGui::GetIO();
	const ImWchar* kr = io.Fonts->GetGlyphRangesKorean();
	io.Fonts->Clear();
	io.Fonts->AddFontFromFileTTF("../Resource/fonts/Regular.ttf", 15.0f, nullptr, kr);

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
	if (ImGui::TreeNode(label))
	{
		ImGui::Checkbox(u8"재생(Play)", &play); ImGui::SameLine();
		ImGui::Checkbox(u8"반복(Loop)", &loop);

		ImGui::DragFloat(u8"속도 배율(Speed x)", &speed, 0.01f, -4.0f, 4.0f, "%.2f");

		const float maxT = (float)((durationSec > 0.0) ? durationSec : 1.0);
		float tUI = (float)t;
		if (ImGui::SliderFloat(u8"시간(Time, sec)", &tUI, 0.0f, maxT, "%.3f"))
		{
			t = (double)tUI;
			if (evalPose) evalPose(t);
		}

		if (ImGui::Button(u8"처음으로(Rewind)")) { t = 0.0; if (evalPose) evalPose(t); }
		ImGui::SameLine();
		if (ImGui::Button(u8"끝으로(End)")) { t = durationSec; if (evalPose) evalPose(t); }

		ImGui::TreePop();
	}
}

//================================================================================================

void TutorialApp::UpdateImGUI()
{
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
	static decltype(mPbr) s_initPbr{};
	static decltype(mTone) s_initTone{};


	if (!s_inited) {
		s_inited = true;
		s_initAnimT = mAnimT;
		s_initAnimSpeed = mAnimSpeed;
		s_initCubePos = cubeTransformA;   s_initCubeScale = cubeScale;   s_initSpin = spinSpeed;
		s_initFov = m_FovDegree;          s_initNear = m_Near;           s_initFar = m_Far;
		s_initLightColor = m_LightColor;  s_initLightYaw = m_LightYaw;   s_initLightPitch = m_LightPitch; s_initLightIntensity = m_LightIntensity;
		s_initKa = m_Ka; s_initIa = m_Ia; s_initKs = m_Ks; s_initShin = m_Shininess;
		s_initArrowPos = m_ArrowPos;      s_initArrowScale = m_ArrowScale;
		s_initPbr = mPbr;
		s_initTone = mTone;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowSize(ImVec2(370, 1080), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(u8"임꾸꾸이"))
	{
		// 상단 상태
		ImGui::Text("FPS: %.1f (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Separator();

		// ─────────────────────────────────────────────────────────────
		// 카메라
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"카메라(Camera)"))
		{
			ImGui::SliderFloat(u8"FOV (deg)", &m_FovDegree, 10.0f, 120.0f, "%.1f");
			ImGui::DragFloat(u8"Near (근평면)", &m_Near, 0.001f, 0.0001f, 10.0f, "%.5f");
			ImGui::DragFloat(u8"Far (원평면)", &m_Far, 0.1f, 0.01f, 20000.0f);
			ImGui::Text(u8"카메라 속도: F1 ~ F3");
			if (ImGui::Button(u8"카메라 값 초기화")) {
				m_FovDegree = s_initFov; m_Near = s_initNear; m_Far = s_initFar;
			}
		}

		// ─────────────────────────────────────────────────────────────
		// 재질(Blinn-Phong)
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"재질(Material) - Blinn-Phong"))
		{
			ImGui::ColorEdit3("I_a (Ambient Light)", (float*)&m_Ia);
			ImGui::ColorEdit3("k_a (Ambient Refl.)", (float*)&m_Ka);
			ImGui::SliderFloat("k_s (Specular)", &m_Ks, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Shininess", &m_Shininess, 2.0f, 256.0f, "%.0f");
			if (ImGui::Button(u8"재질 값 초기화")) {
				m_Ka = s_initKa; m_Ia = s_initIa; m_Ks = s_initKs; m_Shininess = s_initShin;
			}
		}

		// ─────────────────────────────────────────────────────────────
		// 모델
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"모델(Models)"))
		{
			auto ModelUI = [&](const char* name, XformUI& xf) {
				if (ImGui::TreeNode(name)) {
					ImGui::Checkbox(u8"활성화(Enabled)", &xf.enabled);
					ImGui::DragFloat3(u8"위치(Position)", (float*)&xf.pos, 0.1f, -10000.0f, 10000.0f);
					ImGui::DragFloat3(u8"회전(Rotation, deg XYZ)", (float*)&xf.rotD, 0.5f, -720.0f, 720.0f);
					ImGui::DragFloat3(u8"스케일(Scale)", (float*)&xf.scl, 0.01f, 0.0001f, 1000.0f);
					if (ImGui::Button(u8"모델 값 초기화")) {
						xf.pos = xf.initPos; xf.rotD = xf.initRotD; xf.scl = xf.initScl; xf.enabled = true;
					}
					ImGui::TreePop();
				}
				};

			ModelUI("Tree", mTreeX);
			ModelUI("Character", mCharX);
			ModelUI("Zelda", mZeldaX);

			if (ImGui::TreeNode(u8"라이트 방향 표시(Arrow)")) {
				ImGui::Checkbox(u8"활성화(Enabled)", &mDbg.showLightArrow);
				ImGui::DragFloat3(u8"위치(Position)", (float*)&m_ArrowPos, 0.1f, -10000.0f, 10000.0f);
				ImGui::DragFloat3(u8"스케일(Scale)", (float*)&m_ArrowScale, 0.01f, 0.0001f, 1000.0f);
				if (ImGui::Button(u8"화살표 값 초기화")) {
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

		// ─────────────────────────────────────────────────────────────
		// Rigid Skeletal
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"BoxHuman (RigidSkeletal)"))
		{
			ImGui::Checkbox(u8"활성화(Enabled)##Box", &mBoxX.enabled);
			ImGui::DragFloat3(u8"위치(Position)##Box", (float*)&mBoxX.pos, 0.1f, -10000.0f, 10000.0f);
			ImGui::DragFloat3(u8"회전(Rotation, deg XYZ)##Box", (float*)&mBoxX.rotD, 0.5f, -720.0f, 720.0f);
			ImGui::DragFloat3(u8"스케일(Scale)##Box", (float*)&mBoxX.scl, 0.01f, 0.0001f, 1000.0f);

			if (ImGui::Button(u8"트랜스폼 초기화")) {
				mBoxX.pos = mBoxX.initPos; mBoxX.rotD = mBoxX.initRotD; mBoxX.scl = mBoxX.initScl; mBoxX.enabled = true;
			}

			ImGui::SeparatorText(u8"애니메이션(Animation)");
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

		// ─────────────────────────────────────────────────────────────
		// Skinned Skeletal
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"SkinningTest (SkinnedSkeletal)"))
		{
			ImGui::Checkbox(u8"활성화(Enabled)", &mSkinX.enabled);
			ImGui::DragFloat3(u8"위치(Position)", (float*)&mSkinX.pos, 0.1f, -10000.0f, 10000.0f);
			ImGui::DragFloat3(u8"회전(Rotation, deg XYZ)", (float*)&mSkinX.rotD, 0.5f, -720.0f, 720.0f);
			ImGui::DragFloat3(u8"스케일(Scale)", (float*)&mSkinX.scl, 0.01f, 0.0001f, 1000.0f);
			if (ImGui::Button(u8"트랜스폼 초기화##skin")) {
				mSkinX.pos = mSkinX.initPos; mSkinX.rotD = mSkinX.initRotD; mSkinX.scl = mSkinX.initScl; mSkinX.enabled = true;
			}

			ImGui::SeparatorText(u8"애니메이션(Animation)");
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

		// ─────────────────────────────────────────────────────────────
		// Toon
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"툰 셰이딩(Toon Shading)"))
		{
			ImGui::Checkbox(u8"툰 사용(Enable)", &mDbg.useToon);
			ImGui::Checkbox(u8"Half-Lambert", &mDbg.toonHalfLambert);
			ImGui::DragFloat(u8"스펙 스텝(Spec Step)", &mDbg.toonSpecStep, 0.01f, 0.0f, 1.0f, "%.2f");
			ImGui::DragFloat(u8"스펙 부스트(Spec Boost)", &mDbg.toonSpecBoost, 0.01f, 0.0f, 3.0f, "%.2f");
			ImGui::DragFloat(u8"그림자 최소(Shadow Min)", &mDbg.toonShadowMin, 0.005f, 0.0f, 0.10f, "%.3f");
		}

		// ─────────────────────────────────────────────────────────────
		// PBR
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"PBR (Physically Based Rendering)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox(u8"PBR 사용(Enable)", &mPbr.enable);

			ImGui::SeparatorText(u8"텍스처 사용(Use Textures)");
			ImGui::Checkbox("BaseColor", &mPbr.useBaseColorTex);
			ImGui::Checkbox("Normal", &mPbr.useNormalTex);
			ImGui::Checkbox("Metallic", &mPbr.useMetalTex);
			ImGui::Checkbox("Roughness", &mPbr.useRoughTex);

			ImGui::SeparatorText(u8"대체값(텍스처 OFF 시, Overrides)");
			ImGui::ColorEdit3(u8"BaseColor (Override)", (float*)&mPbr.baseColor);
			ImGui::SliderFloat("Metallic (Override)", &mPbr.metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness (Override)", &mPbr.roughness, 0.02f, 1.0f);

			ImGui::SeparatorText(u8"노멀맵 옵션(Normal Map)");
			ImGui::Checkbox(u8"Y 반전(Flip Green)", &mPbr.flipNormalY);
			ImGui::SliderFloat(u8"강도(Strength)", &mPbr.normalStrength, 0.0f, 2.0f);

			if (ImGui::Button(u8"PBR 초기화(시작값)"))
			{
				mPbr = s_initPbr;
			}

			ImGui::SeparatorText(u8"IBL / Skybox");

			const char* iblItems[] = { "BakerSample", "Indoor", "Bridge" };
			int prev = mIBLSetIndex;

			if (ImGui::Combo(u8"IBL 세트", &mIBLSetIndex, iblItems, IM_ARRAYSIZE(iblItems)))
			{
				if (!LoadIBLSet(mIBLSetIndex))
					mIBLSetIndex = prev; // 로드 실패 시 롤백
			}

			ImGui::Text("Prefilter MaxMip: %.0f", mPrefilterMaxMip);

			ImGui::SeparatorText(u8"IBL 강도(Env)");

			ImGui::ColorEdit3(u8"Env Diff Color", (float*)&mPbr.envDiffColor);
			ImGui::SliderFloat(u8"Env Diff Intensity", &mPbr.envDiffIntensity, 0.0f, 3.0f, "%.3f");

			ImGui::ColorEdit3(u8"Env Spec Color", (float*)&mPbr.envSpecColor);
			ImGui::SliderFloat(u8"Env Spec Intensity", &mPbr.envSpecIntensity, 0.0f, 3.0f, "%.3f");

			if (ImGui::CollapsingHeader(u8"톤매핑(Tone Mapping) - HDR -> LDR", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox(u8"SceneHDR 사용(HDR RT로 렌더)", &mTone.useSceneHDR);
				ImGui::Checkbox(u8"ToneMap 적용(Enable)", &mTone.enable);

				ImGui::SliderFloat(u8"Exposure (EV)", &mTone.exposureEV, -8.0f, 8.0f, "%.2f");

				const char* ops[] = { "None", "Reinhard", "ACES(Fitted)" };
				ImGui::Combo(u8"Operator", &mTone.operatorId, ops, IM_ARRAYSIZE(ops));

				ImGui::SliderFloat(u8"Gamma", &mTone.gamma, 1.0f, 3.0f, "%.2f");

				if (ImGui::Button(u8"ToneMap 초기화(Reset)"))
				{
					mTone = s_initTone;
				}
			}

		}

		// ─────────────────────────────────────────────────────────────
		// 렌더/디버그 토글
		// ─────────────────────────────────────────────────────────────
		if (ImGui::CollapsingHeader(u8"렌더링/디버그(Render Toggles)"))
		{
			ImGui::Checkbox(u8"Light Window", &mDbg.showLightWindow);
			ImGui::Checkbox(u8"Shadow Window", &mDbg.showShadowWindow);
			ImGui::Checkbox(u8"GBuffer Window", &mDbg.showGBuffer);

			ImGui::Separator();

			ImGui::Checkbox("Skybox", &mDbg.showSky);
			ImGui::Checkbox("Opaque Pass", &mDbg.showOpaque);
			ImGui::Checkbox("Transparent Pass", &mDbg.showTransparent);
			ImGui::SameLine();
			ImGui::Checkbox("Sort", &mDbg.sortTransparent);

			ImGui::Separator();

			ImGui::Checkbox("Wireframe", &mDbg.wireframe); ImGui::SameLine();
			ImGui::Checkbox("Cull None", &mDbg.cullNone);
			ImGui::Checkbox(u8"Depth OFF (Mesh)", &mDbg.depthWriteOff);
			ImGui::Checkbox(u8"시간 정지(Freeze Time)", &mDbg.freezeTime);

			ImGui::Separator();

			ImGui::Checkbox(u8"노멀 무시(Disable Normal)", &mDbg.disableNormal);
			ImGui::Checkbox(u8"스페큘러 무시(Disable Specular)", &mDbg.disableSpecular);
			ImGui::Checkbox(u8"에미시브 무시(Disable Emissive)", &mDbg.disableEmissive);
			ImGui::Checkbox(u8"강제 알파컷(Force AlphaClip)", &mDbg.forceAlphaClip);
			ImGui::DragFloat("AlphaCut", &mDbg.alphaCut, 0.01f, 0.0f, 1.0f);

			if (ImGui::Button(u8"디버그 초기화(Reset)")) {
				mDbg = DebugToggles();
			}


		}
	}

	ImGui::End();


	if (mDbg.showLightWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(700, 200), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(610, 880), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(u8"조명(Light)", &mDbg.showLightWindow))
		{
			auto NormalizeSafe = [](Vector3 v, const Vector3& fallback)
				{
					float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
					if (len2 < 1e-8f) return fallback;
					float invLen = 1.0f / sqrtf(len2);
					return Vector3(v.x * invLen, v.y * invLen, v.z * invLen);
				};

			auto YawPitchToDir = [](float yaw, float pitch)
				{
					float cy = cosf(yaw), sy = sinf(yaw);
					float cp = cosf(pitch), sp = sinf(pitch);
					return Vector3(sy * cp, sp, cy * cp); // +Z forward 기준
				};

			auto DirToYawPitch = [&NormalizeSafe](const Vector3& d, float& yaw, float& pitch)
				{
					Vector3 n = NormalizeSafe(d, Vector3(0, -1, 0));
					pitch = asinf(std::clamp(n.y, -1.0f, 1.0f));
					if (fabsf(n.x) + fabsf(n.z) > 1e-5f)
						yaw = atan2f(n.x, n.z);
				};

			Vector3 dirUI = YawPitchToDir(m_LightYaw, m_LightPitch);
			ImGui::Checkbox("Directional Enable##dir", &mDbg.dirLightEnable);

			ImGui::TextDisabled(u8"방향 벡터 편집(자동 정규화).");
			if (ImGui::DragFloat3(u8"Light Dir (x,y,z)", (float*)&dirUI, 0.01f, -1.0f, 1.0f, "%.3f"))
			{
				dirUI = NormalizeSafe(dirUI, Vector3(0, -1, 0));
				DirToYawPitch(dirUI, m_LightYaw, m_LightPitch);
			}

			ImGui::SameLine();
			if (ImGui::Button(u8"Invert"))
			{
				dirUI = Vector3(-dirUI.x, -dirUI.y, -dirUI.z);
				DirToYawPitch(dirUI, m_LightYaw, m_LightPitch);
			}

			// yaw/pitch는 "보조"로 남기기 (원하면 접어두기)
			if (ImGui::CollapsingHeader(u8"Yaw/Pitch (보조 컨트롤)"))
			{
				ImGui::SliderAngle(u8"Yaw", &m_LightYaw, -180.0f, 180.0f);
				ImGui::SliderAngle(u8"Pitch", &m_LightPitch, -89.0f, 89.0f);
				ImGui::ColorEdit3(u8"색상(Color)", (float*)&m_LightColor);
				ImGui::DragFloat(u8"강도(Intensity)", &m_LightIntensity, 0.1f, 0.0f, 200.0f, "%.3f");
				if (ImGui::Button(u8"조명 값 초기화"))
				{
					m_LightColor = s_initLightColor;
					m_LightYaw = s_initLightYaw;
					m_LightPitch = s_initLightPitch;
					m_LightIntensity = s_initLightIntensity;
				}
			}

			ImGui::SeparatorText("Point Light");
			ImGui::Checkbox("Enable##pt", &mPoint.enable);
			ImGui::DragFloat3("Pos##pt", (float*)&mPoint.pos, 1.0f, -5000.0f, 5000.0f);
			ImGui::ColorEdit3("Color##pt", (float*)&mPoint.color);
			ImGui::DragFloat("Intensity##pt", &mPoint.intensity, 0.1f, 0.0f, 5000.0f);
			ImGui::DragFloat("Range##pt", &mPoint.range, 1.0f, 1.0f, 10000.0f);

			{
				const char* falloffs[] = { "Smooth (gamey)", "InverseSquare (phys-ish)" };
				ImGui::Combo("Falloff##pt", &mPoint.falloffMode, falloffs, IM_ARRAYSIZE(falloffs));
			}

			ImGui::Checkbox("Show Marker##pt", &mPoint.showMarker);
			ImGui::DragFloat("Marker Size##pt", &mPoint.markerSize, 0.5f, 1.0f, 500.0f, "%.1f");
			ImGui::SeparatorText("Point Shadow (Cube)");
			ImGui::Checkbox("Enable##ptshadow", &mPoint.shadowEnable);
			ImGui::DragFloat("Bias##ptshadow", &mPoint.shadowBias, 0.0005f, 0.0f, 0.05f, "%.5f");
			ImGui::TextDisabled(u8"MapSize=%u (변경하려면 재시작/리소스 재생성 필요)", (unsigned)mPoint.shadowMapSize);

		}
		ImGui::End();
	}

	// ─────────────────────────────────────────────────────────────
	// 그림자
	// ─────────────────────────────────────────────────────────────
	if (mDbg.showShadowWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(300, 440), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(370, 0), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(u8"그림자(Shadow)", &mDbg.showShadowWindow))
		{
			// ── ShadowMap Preview / Grid ────────────────────────────────
			if (ImGui::CollapsingHeader(u8"그림자(Shadow)", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox(u8"섀도우맵 미리보기(Show ShadowMap)", &mShUI.showSRV);
				ImGui::Checkbox(u8"그리드 표시(Show Grid)", &mDbg.showGrid);
				ImGui::Checkbox(u8"직교 투영(Ortho)", &mShUI.useOrtho);
				ImGui::Checkbox(u8"카메라 추적(Follow Camera)", &mShUI.followCamera);

				if (mShUI.showSRV) {
					ImTextureID id = (ImTextureID)mShadowSRV.Get();
					if (id) ImGui::Image(id, ImVec2(256, 256), ImVec2(0, 0), ImVec2(1, 1));
					else    ImGui::TextUnformatted("Shadow SRV is null");
				}

				if (ImGui::CollapsingHeader(u8"고급 옵션(Details)"))
				{
					ImGui::SeparatorText(u8"카메라 기준(locked)");

					ImGui::DragFloat(u8"Focus 거리(FocusDist)", &mShUI.focusDist, 0.1f, 0.1f, 5000.0f);
					ImGui::DragFloat(u8"Light 거리(LightDist)", &mShUI.lightDist, 0.1f, 0.1f, 10000.0f);
					ImGui::DragFloat(u8"커버 마진(Margin)", &mShUI.coverMargin, 0.01f, 1.0f, 2.0f);

					ImGui::SeparatorText("DepthOnly");
					ImGui::SliderFloat(u8"알파 컷(AlphaCut)", &mShadowAlphaCut, 0.0f, 1.0f, "%.3f");

					ImGui::SeparatorText("Bias");
					ImGui::DragFloat(u8"비교 Bias(CmpBias)", &mShadowCmpBias, 0.0001f, 0.0f, 0.02f, "%.5f");
					ImGui::DragInt(u8"DepthBias", (int*)&mShadowDepthBias, 1, 0, 200000);
					ImGui::DragFloat(u8"Slope Bias", &mShadowSlopeBias, 0.01f, 0.0f, 32.0f, "%.2f");

					ImGui::SeparatorText(u8"섀도우맵 해상도(ShadowMap Size)");
					static int resIdx =
						(mShadowW >= 4096) ? 3 :
						(mShadowW >= 2048) ? 2 :
						(mShadowW >= 1024) ? 1 : 0;
					const char* kResItems[] = { "512", "1024", "2048", "4096" };
					ImGui::Combo(u8"해상도(Resolution)", &resIdx, kResItems, IM_ARRAYSIZE(kResItems));

					ImGui::SeparatorText(u8"계산값(읽기 전용)");
					ImGui::Text("FovY: %.1f deg", DirectX::XMConvertToDegrees(mShadowFovY));
					ImGui::Text("Near/Far: %.3f / %.3f", mShadowNear, mShadowFar);

					if (ImGui::Button(u8"적용(ShadowMap 재생성)"))
					{
						int sz = 512;
						if (resIdx == 1) sz = 1024;
						else if (resIdx == 2) sz = 2048;
						else if (resIdx == 3) sz = 4096;
						mShadowW = mShadowH = sz;
						CreateShadowResources(m_pDevice);
					}
				}
			}
		}

		ImGui::End();
	}


	// =============================================================
	// G-Buffer Thumbnails Window (분리 창)
	// =============================================================
	if (mDbg.showGBuffer)
	{
		// ─────────────────────────────────────────────────────────────
		// Deferred / G-Buffer
		// ─────────────────────────────────────────────────────────────


		ImGui::SetNextWindowSize(ImVec2(500, 640), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(1420, 0), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(u8"G-Buffer", &mDbg.showGBuffer))
		{

			if (ImGui::CollapsingHeader(u8"지연 셰이딩(Deferred) / G-Buffer", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Deferred Shading (Opaque)##deferred", &mDbg.useDeferred);
				ImGui::TextDisabled(u8"(투명/머리카락은 마지막에 Forward Overlay)");

				// 출력 선택: Final vs G-Buffer Debug
				if (mDbg.useDeferred)
				{
					const char* outs[] = {
						"Final Lighting",
						"GBuffer: WorldPos",
						"GBuffer: WorldNormal",
						"GBuffer: BaseColor",
						"GBuffer: Metal/Rough",
					};

					int view = (mDbg.showGBufferFS ? mDbg.gbufferMode : 0); // 0..4
					if (ImGui::Combo("Output##gbuf_out", &view, outs, IM_ARRAYSIZE(outs)))
					{
						if (view == 0)
						{
							mDbg.showGBufferFS = false;
							mDbg.gbufferMode = 0;
						}
						else
						{
							mDbg.showGBufferFS = true;
							mDbg.gbufferMode = view; // 1..4
						}
					}

					if (mDbg.showGBufferFS && mDbg.gbufferMode == 1)
						ImGui::DragFloat("WorldPos Range##gbuf_pos", &mDbg.gbufferPosRange, 1.0f, 1.0f, 5000.0f);




				}
				else
				{
					ImGui::TextDisabled(u8"Deferred가 꺼져있습니다.");

				}
			}

			// SRV 없으면 걍 안내만
			if (!mGBufferSRV[0] || !mGBufferSRV[1] || !mGBufferSRV[2] || !mGBufferSRV[3])
			{
				ImGui::TextDisabled(u8"GBuffer SRV is null");
			}
			else
			{
				// 창 폭에 맞춰 썸네일 크기 자동 계산 (2열)
				float w = ImGui::GetContentRegionAvail().x;
				float thumbW = (w - 12.0f) * 0.5f;
				float thumbH = thumbW * (140.0f / 220.0f); // 기존 비율 대충 유지
				ImVec2 sz(thumbW, thumbH);

				auto Thumb = [&](const char* title, int idx, const char* hint)
					{
						ImGui::TextUnformatted(title);
						if (hint) { ImGui::SameLine(); ImGui::TextDisabled(hint); }
						ImGui::Image((ImTextureID)mGBufferSRV[idx].Get(), sz);
					};

				if (ImGui::BeginTable("gbuf_tbl", 2, ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					Thumb("G0 WorldPos", 0, "(raw)");
					ImGui::TableSetColumnIndex(1);
					Thumb("G1 WorldNormal", 1, "(raw -1..1)");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					Thumb("G2 BaseColor", 2, nullptr);
					ImGui::TableSetColumnIndex(1);
					Thumb("G3 Metal/Rough", 3, "(R/G)");

					ImGui::EndTable();
				}

				ImGui::Separator();
			}
		}
		ImGui::End();
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

}