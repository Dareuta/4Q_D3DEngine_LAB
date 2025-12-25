// ============================================================================
// SkinnedSkeletal.cpp
// - SkinnedSkeletal 구현: 본 팔레트 업데이트/스키닝 애니/드로우
// ============================================================================

// ---- includes ----

#include "../D3D_Core/pch.h"
#include "../D3D_Core/Helper.h"

#include "SkinnedSkeletal.h"
#include "AssimpImporterEX.h"
#include "RenderSharedCB.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


static inline double fmod_pos(double x, double m) {
	if (m <= 0.0) return 0.0;
	double r = fmod(x, m);
	return (r < 0.0) ? r + m : r;
}

static Matrix ToM(const aiMatrix4x4& A)
{
	return Matrix(
		A.a1, A.b1, A.c1, A.d1,
		A.a2, A.b2, A.c2, A.d2,
		A.a3, A.b3, A.c3, A.d3,
		A.a4, A.b4, A.c4, A.d4
	);
}
static Quaternion ToQ(const aiQuaternion& q) { return Quaternion(q.x, q.y, q.z, q.w); }
static Vector3    ToV3(const aiVector3D& v) { return { v.x, v.y, v.z }; }

static unsigned MakeFlags(bool flipUV, bool leftHanded)
{
	unsigned f = aiProcess_Triangulate
		| aiProcess_JoinIdenticalVertices
		| aiProcess_ImproveCacheLocality
		| aiProcess_SortByPType
		| aiProcess_CalcTangentSpace
		| aiProcess_GenNormals;
	if (leftHanded) f |= aiProcess_ConvertToLeftHanded;
	if (flipUV)     f |= aiProcess_FlipUVs;
	return f;
}

// ============================================================================
// 키프레임 upper bound
// ============================================================================
int SkinnedSkeletal::UB_T(double t, const std::vector<SK_KeyT>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}
int SkinnedSkeletal::UB_R(double t, const std::vector<SK_KeyR>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}
int SkinnedSkeletal::UB_S(double t, const std::vector<SK_KeyS>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}

// ============================================================================
// 로컬 행렬 샘플링
// ============================================================================
Matrix SkinnedSkeletal::SampleLocalOf(int nodeIdx, double tTick) const
{
	const SK_Node& nd = mNodes[nodeIdx];

	Matrix S = Matrix::Identity, R = Matrix::Identity, T = Matrix::Identity;

	auto it = mClip.map.find(nd.name);
	if (it == mClip.map.end()) {
		return nd.bindLocal; // 채널 없으면 바인드 로컬 유지
	}
	const SK_Channel& ch = mClip.channels[it->second];

	// T
	if (!ch.T.empty()) {
		int ub = UB_T(tTick, ch.T);
		if (ub <= 0) T = Matrix::CreateTranslation(ch.T.front().v);
		else if (ub >= (int)ch.T.size()) T = Matrix::CreateTranslation(ch.T.back().v);
		else {
			const auto& a = ch.T[ub - 1]; const auto& b = ch.T[ub];
			double dt = (b.t - a.t); float u = (dt > 0.0) ? float((tTick - a.t) / dt) : 0.0f;
			T = Matrix::CreateTranslation(a.v + (b.v - a.v) * u);
		}
	}
	else T = Matrix::CreateTranslation(nd.bindLocal.Translation());

	// R
	if (!ch.R.empty()) {
		int ub = UB_R(tTick, ch.R);
		if (ub <= 0) R = Matrix::CreateFromQuaternion(ch.R.front().q);
		else if (ub >= (int)ch.R.size()) R = Matrix::CreateFromQuaternion(ch.R.back().q);
		else {
			const auto& a = ch.R[ub - 1]; const auto& b = ch.R[ub];
			double dt = (b.t - a.t); float u = (dt > 0.0) ? float((tTick - a.t) / dt) : 0.0f;
			R = Matrix::CreateFromQuaternion(Quaternion::Slerp(a.q, b.q, u));
		}
	}
	else R = Matrix::Identity;

	// S
	if (!ch.S.empty()) {
		int ub = UB_S(tTick, ch.S);
		if (ub <= 0) S = Matrix::CreateScale(ch.S.front().v);
		else if (ub >= (int)ch.S.size()) S = Matrix::CreateScale(ch.S.back().v);
		else {
			const auto& a = ch.S[ub - 1]; const auto& b = ch.S[ub];
			double dt = (b.t - a.t); float u = (dt > 0.0) ? float((tTick - a.t) / dt) : 0.0f;
			S = Matrix::CreateScale(a.v + (b.v - a.v) * u);
		}
	}
	else S = Matrix::Identity;

	return S * R * T;
}

// ============================================================================
// 로드
// ============================================================================
std::unique_ptr<SkinnedSkeletal> SkinnedSkeletal::LoadFromFBX(
	ID3D11Device* dev,
	const std::wstring& fbxPath,
	const std::wstring& texDir)
{
	auto up = std::unique_ptr<SkinnedSkeletal>(new SkinnedSkeletal());

	Assimp::Importer imp;
	unsigned flags = MakeFlags(/*flipUV*/true, /*leftHanded*/true);
	const aiScene* sc = imp.ReadFile(std::string(fbxPath.begin(), fbxPath.end()), flags);
	if (!sc || !sc->mRootNode) throw std::runtime_error("Assimp load failed");

	up->mGlobalInv = ToM(sc->mRootNode->mTransformation).Invert();

	// ----------------------------------------------------------------------------
	// 1) 노드 트리
	// ----------------------------------------------------------------------------
	std::vector<SK_Node> nodes;
	std::unordered_map<std::string, int> nameToIdx;

	std::function<int(const aiNode*, int)> buildNode = [&](const aiNode* an, int parent)->int {
		SK_Node nd;
		nd.name = an->mName.C_Str();
		nd.parent = parent;
		nd.bindLocal = ToM(an->mTransformation);
		int my = (int)nodes.size();
		nodes.push_back(nd);
		nameToIdx[nd.name] = my;

		for (unsigned c = 0; c < an->mNumChildren; ++c) {
			int cid = buildNode(an->mChildren[c], my);
			nodes[my].children.push_back(cid);
		}
		return my;
		};
	int root = buildNode(sc->mRootNode, -1);

	// ----------------------------------------------------------------------------
	// 2) 재질
	// ----------------------------------------------------------------------------
	std::vector<MaterialCPU> sceneMaterials;
	AssimpImporterEx::ExtractMaterials(sc, sceneMaterials);

	// ----------------------------------------------------------------------------
	// 3) 파트 & 본/가중치 빌드
	// ----------------------------------------------------------------------------
	std::vector<SK_Part> parts;
	parts.reserve(sc->mNumMeshes);

	// 본 이름 -> bone index
	std::unordered_map<std::string, int> boneNameToIndex;
	std::vector<SK_Bone> bones;

	struct Influences {
		std::vector<std::pair<int, float>> inf;
		void add(int b, float w) { if (w > 0) inf.emplace_back(b, w); }
		void finalize(uint8_t bi[4], float bw[4]) {
			std::sort(inf.begin(), inf.end(), [](auto& a, auto& b) {return a.second > b.second;});
			float sum = 0;
			for (int i = 0;i < 4;++i) {
				if (i < (int)inf.size()) { bi[i] = (uint8_t)inf[i].first; bw[i] = inf[i].second; sum += bw[i]; }
				else { bi[i] = 0; bw[i] = 0; }
			}
			if (sum > 0) { for (int i = 0;i < 4;++i) bw[i] /= sum; }
			else { bi[0] = 0; bw[0] = 1.0f; for (int i = 1;i < 4;++i) { bi[i] = 0;bw[i] = 0; } }
		}
	};

	auto buildPartFromAiMesh = [&](unsigned meshIndex, int ownerNode) {
		const aiMesh* am = sc->mMeshes[meshIndex];

		std::vector<VertexCPU_PNTT_BW> vtx(am->mNumVertices);
		std::vector<uint32_t> idx; idx.reserve(am->mNumFaces * 3);
		std::vector<SubMeshCPU> submeshes;
		submeshes.push_back({ 0,0,(uint32_t)am->mNumFaces * 3, am->mMaterialIndex });

		// prim data
		for (unsigned v = 0; v < am->mNumVertices; ++v) {
			auto& vv = vtx[v];
			vv.px = am->mVertices[v].x;
			vv.py = am->mVertices[v].y;
			vv.pz = am->mVertices[v].z;

			if (am->mNormals) { vv.nx = am->mNormals[v].x; vv.ny = am->mNormals[v].y; vv.nz = am->mNormals[v].z; }
			else { vv.nx = 0; vv.ny = 1; vv.nz = 0; }

			if (am->mTextureCoords[0]) { vv.u = am->mTextureCoords[0][v].x; vv.v = am->mTextureCoords[0][v].y; }
			else { vv.u = vv.v = 0.0f; }

			if (am->mTangents && am->mBitangents) {
				Vector3 T(am->mTangents[v].x, am->mTangents[v].y, am->mTangents[v].z);
				Vector3 B(am->mBitangents[v].x, am->mBitangents[v].y, am->mBitangents[v].z);
				Vector3 N(vv.nx, vv.ny, vv.nz);

				float sign = (N.Cross(T).Dot(B) < 0.0f) ? -1.0f : 1.0f;
				vv.tx = T.x; vv.ty = T.y; vv.tz = T.z; vv.tw = sign;
			}
			else { vv.tx = 1; vv.ty = 0; vv.tz = 0; vv.tw = 1; }

			// init skin fields
			vv.bi[0] = vv.bi[1] = vv.bi[2] = vv.bi[3] = 0;
			vv.bw[0] = 1.0f; vv.bw[1] = vv.bw[2] = vv.bw[3] = 0.0f;
		}
		for (unsigned f = 0; f < am->mNumFaces; ++f) {
			const aiFace& face = am->mFaces[f];
			if (face.mNumIndices == 3) {
				idx.push_back(face.mIndices[0]);
				idx.push_back(face.mIndices[1]);
				idx.push_back(face.mIndices[2]);
			}
		}

		// collect influences
		std::vector<Influences> infl(am->mNumVertices);
		for (unsigned b = 0; b < am->mNumBones; ++b) {
			const aiBone* ab = am->mBones[b];
			std::string bname = ab->mName.C_Str();

			int boneIdx;
			auto itB = boneNameToIndex.find(bname);
			if (itB == boneNameToIndex.end()) {
				// map to node
				auto itNode = nameToIdx.find(bname);
				if (itNode == nameToIdx.end()) {
					throw std::runtime_error(("Bone node not found: " + bname).c_str());
				}
				SK_Bone bone;
				bone.name = bname;
				bone.node = itNode->second;
				bone.offset = ToM(ab->mOffsetMatrix);
				boneIdx = (int)bones.size();
				bones.push_back(bone);
				boneNameToIndex[bname] = boneIdx;
			}
			else {
				boneIdx = itB->second;
			}

			for (unsigned w = 0; w < ab->mNumWeights; ++w) {
				const aiVertexWeight& vw = ab->mWeights[w];
				if (vw.mVertexId < am->mNumVertices) {
					infl[vw.mVertexId].add(boneIdx, vw.mWeight);
				}
			}
		}
		// finalize influences per vertex
		for (unsigned v = 0; v < am->mNumVertices; ++v) {
			infl[v].finalize(vtx[v].bi, vtx[v].bw);
		}

		// build gpu mesh
		SK_Part part;
		if (!part.mesh.Build(dev, vtx, idx, submeshes))
			throw std::runtime_error("SkinnedMesh build failed");

		// materials
		part.materials.clear(); part.materials.resize(sceneMaterials.size());
		for (size_t i = 0; i < sceneMaterials.size(); ++i)
			part.materials[i].Build(dev, sceneMaterials[i], texDir);

		part.ownerNode = ownerNode;
		nodes[ownerNode].partIndices.push_back((int)parts.size());
		parts.push_back(std::move(part));
		};

	// traverse and build parts
	std::function<void(const aiNode*)> collectMeshes = [&](const aiNode* an) {
		int owner = nameToIdx[an->mName.C_Str()];
		for (unsigned m = 0; m < an->mNumMeshes; ++m) buildPartFromAiMesh(an->mMeshes[m], owner);
		for (unsigned c = 0; c < an->mNumChildren; ++c) collectMeshes(an->mChildren[c]);
		};
	collectMeshes(sc->mRootNode);

	// ----------------------------------------------------------------------------
	// 4) 애니메이션(첫 개)
	// ----------------------------------------------------------------------------
	SK_Clip clip;
	if (sc->mNumAnimations > 0) {
		const aiAnimation* a = sc->mAnimations[0];
		clip.name = a->mName.C_Str();
		clip.duration = a->mDuration;
		clip.tps = (a->mTicksPerSecond > 0.0) ? a->mTicksPerSecond : 25.0;

		clip.channels.reserve(a->mNumChannels);
		for (unsigned c = 0; c < a->mNumChannels; ++c) {
			const aiNodeAnim* na = a->mChannels[c];
			SK_Channel ch; ch.target = na->mNodeName.C_Str();
			ch.T.reserve(na->mNumPositionKeys);
			ch.R.reserve(na->mNumRotationKeys);
			ch.S.reserve(na->mNumScalingKeys);
			for (unsigned i = 0; i < na->mNumPositionKeys; ++i)
				ch.T.push_back({ na->mPositionKeys[i].mTime, ToV3(na->mPositionKeys[i].mValue) });
			for (unsigned i = 0; i < na->mNumRotationKeys; ++i)
				ch.R.push_back({ na->mRotationKeys[i].mTime, ToQ(na->mRotationKeys[i].mValue) });
			for (unsigned i = 0; i < na->mNumScalingKeys; ++i)
				ch.S.push_back({ na->mScalingKeys[i].mTime,  ToV3(na->mScalingKeys[i].mValue) });
			clip.map[ch.target] = (int)clip.channels.size();
			clip.channels.push_back(std::move(ch));
		}
	}

	up->mNodes = std::move(nodes);
	up->mParts = std::move(parts);
	up->mBones = std::move(bones);
	up->mNameToNode = std::move(nameToIdx);
	up->mClip = std::move(clip);
	up->mRoot = root;

	return up;
}

// ============================================================================
// 포즈 평가
// ============================================================================
void SkinnedSkeletal::EvaluatePose(double tSec) {
	EvaluatePose(tSec, /*loop*/true);
}

void SkinnedSkeletal::EvaluatePose(double tSec, bool loop)
{
	if (mClip.duration <= 0.0) {
		for (auto& n : mNodes) n.poseLocal = n.bindLocal;
	}
	else {
		const double tps = (mClip.tps > 0.0) ? mClip.tps : 25.0;
		const double T = tSec * tps;               // ticks
		const double t = loop ? fmod_pos(T, mClip.duration)
			: std::clamp(T, 0.0, mClip.duration);
		for (size_t i = 0; i < mNodes.size(); ++i)
			mNodes[i].poseLocal = SampleLocalOf((int)i, t);
	}

	if (mRoot >= 0) {
		mNodes[mRoot].poseGlobal = mNodes[mRoot].poseLocal;
		std::function<void(int)> dfs = [&](int u) {
			for (int v : mNodes[u].children) {
				mNodes[v].poseGlobal = mNodes[v].poseLocal * mNodes[u].poseGlobal;
				dfs(v);
			}
			};
		dfs(mRoot);
	}
}

// ============================================================================
// 본 팔레트 업데이트
// ============================================================================
void SkinnedSkeletal::UpdateBonePalette(
	ID3D11DeviceContext* ctx,
	ID3D11Buffer* boneCB,
	const Matrix& /*worldModel*/)
{
	// HLSL 쪽 kMaxBones = 256과 반드시 일치시키자
	static constexpr size_t kMaxBones = 256;

	// 항상 kMaxBones 개 만큼 업로드할 임시 버퍼 (트랜스포즈 반영)
	DirectX::XMFLOAT4X4 temp[kMaxBones];

	const size_t n = std::min(mBones.size(), kMaxBones);

	// 1) 실제 본 개수만큼 계산해서 채우기
	for (size_t i = 0; i < n; ++i) {
		const auto& b = mBones[i];
		const Matrix G = mNodes[b.node].poseGlobal;   // model space
		const Matrix M = b.offset * G;                // skinning matrix
		XMStoreFloat4x4(&temp[i], XMMatrixTranspose(M));
	}

	// 2) 남는 슬롯은 Identity로 패딩
	for (size_t i = n; i < kMaxBones; ++i) {
		XMStoreFloat4x4(&temp[i], XMMatrixIdentity());
	}

	// 3) CB 전체 크기만큼 항상 업로드 (크래시 방지 핵심)
	ctx->UpdateSubresource(boneCB, 0, nullptr, temp, 0, 0);
	ctx->VSSetConstantBuffers(4, 1, &boneCB); // b4
}

// SkinnedSkeletal.cpp
void SkinnedSkeletal::WarmupBoneCB(ID3D11DeviceContext* ctx, ID3D11Buffer* boneCB)
{
	// 1) 바인드 포즈로 평가 (클립이 없으면 bindLocal, 있으면 t=0)
	EvaluatePose(0.0, /*loop=*/true);

	// 2) 팔레트 업로드 (항상 256개 패딩)
	UpdateBonePalette(ctx, boneCB, Matrix::Identity);
}

static void FillCB(ConstantBuffer& cb,
	const Matrix& world, const Matrix& view, const Matrix& proj,
	const Vector4& vLightDir, const Vector4& vLightColor)
{
	cb.mWorld = XMMatrixTranspose(world);
	cb.mView = XMMatrixTranspose(view);
	cb.mProjection = XMMatrixTranspose(proj);
	cb.mWorldInvTranspose = world.Invert();
	cb.vLightDir = vLightDir;
	cb.vLightColor = vLightColor;
}

static void PushUseCB(ID3D11DeviceContext* ctx, ID3D11Buffer* useCB,
	const MaterialGPU& mat,
	bool useOpacity, float alphaCut,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	UseCB use{};
	use.useDiffuse = mat.hasDiffuse ? 1u : 0u;
	use.useNormal = (mat.hasNormal && !disableNormal) ? 1u : 0u;
	use.useSpecular = (!disableSpecular) ? (mat.hasSpecular ? 1u : 2u) : 0u; // 2: fake/spec const
	use.useEmissive = (mat.hasEmissive && !disableEmissive) ? 1u : 0u;
	use.useOpacity = useOpacity ? 1u : 0u;
	use.alphaCut = alphaCut;

	ctx->UpdateSubresource(useCB, 0, nullptr, &use, 0, 0);
	ctx->PSSetConstantBuffers(2, 1, &useCB);
}

// ============================================================================
// Draw Opaque / Cutout / Transparent
// ============================================================================
void SkinnedSkeletal::DrawOpaqueOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel, const Matrix& view, const Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
	const Vector4& vLightDir, const Vector4& vLightColor,
	const Vector3& /*eyePos*/,
	const Vector3& /*kA*/, float /*ks*/, float /*shininess*/, const Vector3& /*Ia*/,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	UpdateBonePalette(ctx, boneCB, worldModel);

	for (const auto& part : mParts) {
		const auto& ranges = part.mesh.Ranges();
		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];
			if (mat.hasOpacity) continue; // 불투명 패스: opacity X

			const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

			ConstantBuffer cb{};
			FillCB(cb, world, view, proj, vLightDir, vLightColor);
			ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb0);
			ctx->PSSetConstantBuffers(0, 1, &cb0);

			mat.Bind(ctx);
			PushUseCB(ctx, useCB, mat, /*useOpacity*/false, /*alphaCut*/-1.0f,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}

void SkinnedSkeletal::DrawAlphaCutOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel, const Matrix& view, const Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
	const Vector4& vLightDir, const Vector4& vLightColor,
	const Vector3& /*eyePos*/,
	const Vector3& /*kA*/, float /*ks*/, float /*shininess*/, const Vector3& /*Ia*/,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	UpdateBonePalette(ctx, boneCB, worldModel);

	for (const auto& part : mParts) {
		const auto& ranges = part.mesh.Ranges();
		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];
			if (!mat.hasOpacity) continue; // 컷아웃 패스: opacity 있는 애만

			const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

			ConstantBuffer cb{};
			FillCB(cb, world, view, proj, vLightDir, vLightColor);
			ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb0);
			ctx->PSSetConstantBuffers(0, 1, &cb0);

			mat.Bind(ctx);
			// 컷아웃: useOpacity=1, alphaCut>0
			PushUseCB(ctx, useCB, mat, /*useOpacity*/true, /*alphaCut*/0.5f,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}

void SkinnedSkeletal::DrawTransparentOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel, const Matrix& view, const Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
	const Vector4& vLightDir, const Vector4& vLightColor,
	const Vector3& /*eyePos*/,
	const Vector3& /*kA*/, float /*ks*/, float /*shininess*/, const Vector3& /*Ia*/,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	UpdateBonePalette(ctx, boneCB, worldModel);

	for (const auto& part : mParts) {
		const auto& ranges = part.mesh.Ranges();
		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];
			if (!mat.hasOpacity) continue; // 투명 패스에서 쓰는 경우(직알파) — 상태는 앱에서 세팅

			const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

			ConstantBuffer cb{};
			FillCB(cb, world, view, proj, vLightDir, vLightColor);
			ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb0);
			ctx->PSSetConstantBuffers(0, 1, &cb0);

			mat.Bind(ctx);
			// 투명: useOpacity=1, alphaCut=-1 (discard 없음), 블렌드 ST(직알파)
			PushUseCB(ctx, useCB, mat, /*useOpacity*/true, /*alphaCut*/-1.0f,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}

void SkinnedSkeletal::DrawDepthOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel,
	const Matrix& lightView, const Matrix& lightProj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB, ID3D11Buffer* boneCB,
	ID3D11VertexShader* vsDepthSkinned,
	ID3D11PixelShader* psDepth,
	ID3D11InputLayout* ilPNTT_BW,
	float alphaCut)
{
	// 본 팔레트(b4) 업데이트
	UpdateBonePalette(ctx, boneCB, worldModel);

	ctx->IASetInputLayout(ilPNTT_BW);
	ctx->VSSetShader(vsDepthSkinned, nullptr, 0);
	ctx->PSSetShader(psDepth, nullptr, 0);

	for (const auto& part : mParts)
	{
		const auto& ranges = part.mesh.Ranges();
		const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

		ConstantBuffer cb{};
		cb.mWorld = XMMatrixTranspose(world);
		cb.mView = XMMatrixTranspose(lightView);
		cb.mProjection = XMMatrixTranspose(lightProj);
		cb.mWorldInvTranspose = world.Invert();
		ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
		ctx->VSSetConstantBuffers(0, 1, &cb0);

		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];

			UseCB use{};
			use.useOpacity = mat.hasOpacity ? 1u : 0u;
			use.alphaCut = alphaCut;

			ctx->UpdateSubresource(useCB, 0, nullptr, &use, 0, 0);
			ctx->PSSetConstantBuffers(2, 1, &useCB);

			mat.Bind(ctx);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
		}
		MaterialGPU::Unbind(ctx);
	}
}
