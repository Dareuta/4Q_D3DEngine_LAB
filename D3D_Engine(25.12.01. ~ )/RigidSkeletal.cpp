// ============================================================================
// RigidSkeletal.cpp
// - RigidSkeletal 구현: 노드 트리/애니 샘플링/파트 렌더링
// ============================================================================

// ---- includes ----

#include "../D3D_Core/pch.h"
#include "../D3D_Core/Helper.h"

#include "RigidSkeletal.h"
#include "AssimpImporterEX.h"
#include "RenderSharedCB.h"
#include <assimp/Importer.hpp>


#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;


static inline double fmod_pos(double x, double period) {
	if (period <= 0.0) return 0.0;
	double r = fmod(x, period);
	return (r < 0.0) ? r + period : r;
}

// ============================================================================
// ai -> DX 변환
// ============================================================================
static Matrix ToM(const aiMatrix4x4& A)
{
	// Assimp의 aiMatrix4x4는 행우선. SimpleMath::Matrix도 행우선.
	return Matrix(
		A.a1, A.b1, A.c1, A.d1,
		A.a2, A.b2, A.c2, A.d2,
		A.a3, A.b3, A.c3, A.d3,
		A.a4, A.b4, A.c4, A.d4
	);
}
static Quaternion ToQ(const aiQuaternion& q)
{
	return Quaternion(q.x, q.y, q.z, q.w);
}
static Vector3 ToV3(const aiVector3D& v)
{
	return { v.x, v.y, v.z };
}

// ============================================================================
// 키프레임 upper bound
// ============================================================================
int RigidSkeletal::UpperBoundT(double t, const std::vector<RS_KeyT>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}
int RigidSkeletal::UpperBoundR(double t, const std::vector<RS_KeyR>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}
int RigidSkeletal::UpperBoundS(double t, const std::vector<RS_KeyS>& v)
{
	int n = (int)v.size();
	int i = 0; while (i < n && v[i].t <= t) ++i; return i;
}

// ============================================================================
// 로컬 행렬 샘플링
// ============================================================================
Matrix RigidSkeletal::SampleLocalOf(int nodeIdx, double tTick) const
{
	const RS_Node& nd = mNodes[nodeIdx];

	// 채널 찾기
	Matrix S = Matrix::Identity, R = Matrix::Identity, T = Matrix::Identity;

	auto it = mClip.map.find(nd.name);
	if (it == mClip.map.end()) {
		// 애니 없으면 바인드 로컬 유지
		return nd.bindLocal;
	}
	const RS_Channel& ch = mClip.channels[it->second];

	// T
	if (!ch.T.empty()) {
		int ub = UpperBoundT(tTick, ch.T);
		if (ub <= 0) T = Matrix::CreateTranslation(ch.T.front().v);
		else if (ub >= (int)ch.T.size()) T = Matrix::CreateTranslation(ch.T.back().v);
		else {
			const auto& a = ch.T[ub - 1]; const auto& b = ch.T[ub];
			double len = b.t - a.t; double u = (len > 0.0) ? (tTick - a.t) / len : 0.0;
			Vector3 v = a.v + (b.v - a.v) * float(u);
			T = Matrix::CreateTranslation(v);
		}
	}
	else T = Matrix::Identity;

	// R
	if (!ch.R.empty()) {
		int ub = UpperBoundR(tTick, ch.R);
		if (ub <= 0) R = Matrix::CreateFromQuaternion(ch.R.front().q);
		else if (ub >= (int)ch.R.size()) R = Matrix::CreateFromQuaternion(ch.R.back().q);
		else {
			const auto& a = ch.R[ub - 1]; const auto& b = ch.R[ub];
			double len = b.t - a.t; double u = (len > 0.0) ? (tTick - a.t) / len : 0.0;
			Quaternion q = Quaternion::Slerp(a.q, b.q, float(u));
			R = Matrix::CreateFromQuaternion(q);
		}
	}
	else R = Matrix::Identity;

	// S
	if (!ch.S.empty()) {
		int ub = UpperBoundS(tTick, ch.S);
		if (ub <= 0) S = Matrix::CreateScale(ch.S.front().v);
		else if (ub >= (int)ch.S.size()) S = Matrix::CreateScale(ch.S.back().v);
		else {
			const auto& a = ch.S[ub - 1]; const auto& b = ch.S[ub];
			double len = b.t - a.t; double u = (len > 0.0) ? (tTick - a.t) / len : 0.0;
			Vector3 v = a.v + (b.v - a.v) * float(u);
			S = Matrix::CreateScale(v);
		}
	}
	else S = Matrix::Identity;

	return S * R * T;
}

// ============================================================================
// 로딩
// ============================================================================
std::unique_ptr<RigidSkeletal> RigidSkeletal::LoadFromFBX(
	ID3D11Device* dev,
	const std::wstring& fbxPath,
	const std::wstring& texDir)
{
	auto up = std::unique_ptr<RigidSkeletal>(new RigidSkeletal);

	Assimp::Importer imp;
	unsigned flags =
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType |
		aiProcess_CalcTangentSpace |
		aiProcess_GenNormals |
		aiProcess_ConvertToLeftHanded |
		aiProcess_FlipUVs;

	const aiScene* sc = imp.ReadFile(std::string(fbxPath.begin(), fbxPath.end()), flags);
	if (!sc || !sc->mRootNode) throw std::runtime_error("Assimp load failed");

	// ----------------------------------------------------------------------------
	// 1) 노드 트리 구축
	// ----------------------------------------------------------------------------
	std::vector<RS_Node> nodes;
	std::unordered_map<std::string, int> nameToIdx;

	std::function<int(const aiNode*, int)> buildNode = [&](const aiNode* an, int parent) -> int {
		RS_Node nd;
		nd.name = an->mName.C_Str();
		nd.parent = parent;
		nd.bindLocal = ToM(an->mTransformation);
		int my = (int)nodes.size();
		nodes.push_back(nd);
		nameToIdx[nd.name] = my;

		// children
		for (unsigned c = 0; c < an->mNumChildren; ++c) {
			int cid = buildNode(an->mChildren[c], my);
			nodes[my].children.push_back(cid);
		}
		return my;
		};
	int root = buildNode(sc->mRootNode, -1);

	// ----------------------------------------------------------------------------
	// 2) 파트(StaticMesh) 구성: 노드에 붙은 aiMesh를 각자 하나의 파트로 만든다
	// ----------------------------------------------------------------------------
	std::vector<RS_Part> parts;

	std::vector<MaterialCPU> sceneMaterials;
	AssimpImporterEx::ExtractMaterials(sc, sceneMaterials);

	auto buildPartFromAiMesh = [&](unsigned meshIndex, int ownerNode) {
		const aiMesh* am = sc->mMeshes[meshIndex];

		MeshData_PNTT cpu; // 네 새 구조: vertices / indices / submeshes / materials
		cpu.vertices.resize(am->mNumVertices);

		for (unsigned v = 0; v < am->mNumVertices; ++v) {
			VertexCPU_PNTT vv{};

			// position
			vv.px = am->mVertices[v].x;
			vv.py = am->mVertices[v].y;
			vv.pz = am->mVertices[v].z;

			// normal
			if (am->mNormals) {
				vv.nx = am->mNormals[v].x;
				vv.ny = am->mNormals[v].y;
				vv.nz = am->mNormals[v].z;
			}
			else {
				vv.nx = 0; vv.ny = 1; vv.nz = 0;
			}

			// uv (채널 0)
			if (am->mTextureCoords[0]) {
				vv.u = am->mTextureCoords[0][v].x;
				vv.v = am->mTextureCoords[0][v].y;
			}
			else {
				vv.u = vv.v = 0.0f;
			}

			if (am->mTangents && am->mBitangents) {
				const aiVector3D& t = am->mTangents[v];
				const aiVector3D& b = am->mBitangents[v];


				Vector3 T(am->mTangents[v].x, am->mTangents[v].y, am->mTangents[v].z);
				Vector3 B(am->mBitangents[v].x, am->mBitangents[v].y, am->mBitangents[v].z);
				Vector3 N(vv.nx, vv.ny, vv.nz);

				Vector3 C(N.y * T.z - N.z * T.y,
					N.z * T.x - N.x * T.z,
					N.x * T.y - N.y * T.x);

				float sign = ((C.x * B.x + C.y * B.y + C.z * B.z) < 0.0f) ? -1.0f : 1.0f;

				vv.tx = T.x; vv.ty = T.y; vv.tz = T.z; vv.tw = sign;
			}
			else {
				vv.tx = 1.0f; vv.ty = 0.0f; vv.tz = 0.0f; vv.tw = 1.0f;
			}

			cpu.vertices[v] = vv;
		}

		// indices
		cpu.indices.reserve(am->mNumFaces * 3);
		for (unsigned f = 0; f < am->mNumFaces; ++f) {
			const aiFace& face = am->mFaces[f];
			for (unsigned k = 0; k < face.mNumIndices; ++k)
				cpu.indices.push_back(static_cast<uint32_t>(face.mIndices[k]));
		}

		// submesh (aiMesh 하나 = 하나의 서브메시)
		cpu.submeshes.clear();
		SubMeshCPU sm{};
		sm.baseVertex = 0;
		sm.indexStart = 0;
		sm.indexCount = static_cast<uint32_t>(cpu.indices.size());
		sm.materialIndex = am->mMaterialIndex;
		cpu.submeshes.push_back(sm);

		// materials (장면 전체 리스트 복사)
		cpu.materials = sceneMaterials;

		// GPU 빌드
		RS_Part part;
		if (!part.mesh.Build(dev, cpu))
			throw std::runtime_error("part mesh build failed");

		// 네 렌더러가 MaterialGPU::Bind를 직접 쓰는 구조라면 유지
		part.materials.resize(cpu.materials.size());
		for (size_t i = 0; i < cpu.materials.size(); ++i)
			part.materials[i].Build(dev, cpu.materials[i], texDir);

		part.ownerNode = ownerNode;
		nodes[ownerNode].partIndices.push_back((int)parts.size());
		parts.push_back(std::move(part));
		};

	// 각 노드에 할당된 aiMesh들 생성
	std::function<void(const aiNode*)> collectMeshes = [&](const aiNode* an) {
		int owner = nameToIdx[an->mName.C_Str()];
		for (unsigned m = 0; m < an->mNumMeshes; ++m) {
			buildPartFromAiMesh(an->mMeshes[m], owner);
		}
		for (unsigned c = 0; c < an->mNumChildren; ++c)
			collectMeshes(an->mChildren[c]);
		};
	collectMeshes(sc->mRootNode);

	// ----------------------------------------------------------------------------
	// 3) 애니메이션(첫 개) 파싱
	// ----------------------------------------------------------------------------
	RS_Clip clip;
	if (sc->mNumAnimations > 0) {
		const aiAnimation* a = sc->mAnimations[0];
		clip.name = a->mName.C_Str(); // 예: "Walk"일 수도 있고 공백일 수도
		clip.duration = a->mDuration;
		clip.ticksPerSec = (a->mTicksPerSecond > 0.0) ? a->mTicksPerSecond : 25.0;

		clip.channels.reserve(a->mNumChannels);
		for (unsigned i = 0; i < a->mNumChannels; ++i) {
			const aiNodeAnim* na = a->mChannels[i];
			RS_Channel ch;
			ch.target = na->mNodeName.C_Str();

			for (unsigned k = 0; k < na->mNumPositionKeys; ++k)
				ch.T.push_back({ na->mPositionKeys[k].mTime, ToV3(na->mPositionKeys[k].mValue) });
			for (unsigned k = 0; k < na->mNumRotationKeys; ++k)
				ch.R.push_back({ na->mRotationKeys[k].mTime, ToQ(na->mRotationKeys[k].mValue) });
			for (unsigned k = 0; k < na->mNumScalingKeys; ++k)
				ch.S.push_back({ na->mScalingKeys[k].mTime, ToV3(na->mScalingKeys[k].mValue) });

			clip.map[ch.target] = (int)clip.channels.size();
			clip.channels.push_back(std::move(ch));
		}
	}

	up->mNodes = std::move(nodes);
	up->mParts = std::move(parts);
	up->mNameToNode = std::move(nameToIdx);
	up->mRoot = root;
	up->mClip = std::move(clip);

	return up;
}

// ============================================================================
// 포즈 평가
// ============================================================================
void RigidSkeletal::EvaluatePose(double tSec, bool loop)
{
	if (mClip.duration <= 0.0) {
		for (auto& n : mNodes) n.poseLocal = n.bindLocal;
	}
	else {
		const double tps = (mClip.ticksPerSec > 0.0) ? mClip.ticksPerSec : 25.0;
		const double T = tSec * tps; // seconds → ticks
		const double u = loop ? fmod_pos(T, mClip.duration)
			: std::clamp(T, 0.0, mClip.duration);

		for (int i = 0; i < (int)mNodes.size(); ++i)
			mNodes[i].poseLocal = SampleLocalOf(i, u);
	}

	// 글로벌 갱신(네 기존 코드 유지)
	std::function<void(int, const Matrix&)> updateGlobal = [&](int idx, const Matrix& parent) {
		mNodes[idx].poseGlobal = mNodes[idx].poseLocal * parent;
		for (int c : mNodes[idx].children)
			updateGlobal(c, mNodes[idx].poseGlobal);
		};
	updateGlobal(mRoot, Matrix::Identity);
}

// 기존 함수는 루프=true로 위임(호환)
void RigidSkeletal::EvaluatePose(double tSec) {
	EvaluatePose(tSec, /*loop*/true);
}


static void FillCB(ConstantBuffer& cb,
	const Matrix& world,
	const Matrix& view, const Matrix& proj,
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
	use.alphaCut = alphaCut; // -1: 비활성

	ctx->UpdateSubresource(useCB, 0, nullptr, &use, 0, 0);
	ctx->PSSetConstantBuffers(2, 1, &useCB);
}

// ============================================================================
// Opaque / Cutout / Transparent
// ============================================================================
void RigidSkeletal::DrawOpaqueOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel,
	const Matrix& view, const Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB,
	const Vector4& vLightDir, const Vector4& vLightColor,
	const Vector3& eyePos,
	const Vector3& kA, float ks, float shininess, const Vector3& Ia,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	for (const auto& part : mParts)
	{
		const auto& ranges = part.mesh.Ranges(); // 또는 Submeshes() (아래 3) 참고)
		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];
			if (mat.hasOpacity) continue;

			const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

			ConstantBuffer cb{};
			FillCB(cb, world, /*view*/view, /*proj*/proj, vLightDir, vLightColor);
			ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb0);
			ctx->PSSetConstantBuffers(0, 1, &cb0);

			mat.Bind(ctx);
			PushUseCB(ctx, useCB, mat, false, -1.0f,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}
void RigidSkeletal::DrawAlphaCutOnly(
	ID3D11DeviceContext* ctx,
	const DirectX::SimpleMath::Matrix& worldModel,
	const DirectX::SimpleMath::Matrix& view,
	const DirectX::SimpleMath::Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB, float alphaCut,
	const DirectX::SimpleMath::Vector4& vLightDir,
	const DirectX::SimpleMath::Vector4& vLightColor,
	const DirectX::SimpleMath::Vector3& eyePos,
	const DirectX::SimpleMath::Vector3& kA, float ks, float shininess,
	const DirectX::SimpleMath::Vector3& Ia,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	for (const auto& part : mParts)
	{
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
			PushUseCB(ctx, useCB, mat, /*useOpacity*/true, /*alphaCut*/alphaCut,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}

void RigidSkeletal::DrawTransparentOnly(
	ID3D11DeviceContext* ctx,
	const DirectX::SimpleMath::Matrix& worldModel,
	const DirectX::SimpleMath::Matrix& view,
	const DirectX::SimpleMath::Matrix& proj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB,
	const DirectX::SimpleMath::Vector4& vLightDir,
	const DirectX::SimpleMath::Vector4& vLightColor,
	const DirectX::SimpleMath::Vector3& eyePos,
	const DirectX::SimpleMath::Vector3& kA, float ks, float shininess,
	const DirectX::SimpleMath::Vector3& Ia,
	bool disableNormal, bool disableSpecular, bool disableEmissive)
{
	for (const auto& part : mParts)
	{
		const auto& ranges = part.mesh.Ranges();
		for (size_t i = 0; i < ranges.size(); ++i) {
			const auto& r = ranges[i];
			const auto& mat = part.materials[r.materialIndex];
			if (!mat.hasOpacity) continue; // 투명 패스: opacity 있는 애만

			const Matrix world = mNodes[part.ownerNode].poseGlobal * worldModel;

			ConstantBuffer cb{};
			FillCB(cb, world, view, proj, vLightDir, vLightColor);
			ctx->UpdateSubresource(cb0, 0, nullptr, &cb, 0, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb0);
			ctx->PSSetConstantBuffers(0, 1, &cb0);

			mat.Bind(ctx);
			PushUseCB(ctx, useCB, mat, /*useOpacity*/true, /*alphaCut*/-1.0f,
				disableNormal, disableSpecular, disableEmissive);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
			MaterialGPU::Unbind(ctx);
		}
	}
}

void RigidSkeletal::DrawDepthOnly(
	ID3D11DeviceContext* ctx,
	const Matrix& worldModel,
	const Matrix& lightView, const Matrix& lightProj,
	ID3D11Buffer* cb0, ID3D11Buffer* useCB,
	ID3D11VertexShader* vsDepth,
	ID3D11PixelShader* psDepth,
	ID3D11InputLayout* ilPNTT,
	float alphaCut)
{

	ctx->IASetInputLayout(ilPNTT);
	ctx->VSSetShader(vsDepth, nullptr, 0);
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

			// UseCB: alpha cut만 사용 (PS에서 clip)
			UseCB use{};
			use.useOpacity = mat.hasOpacity ? 1u : 0u;
			use.alphaCut = alphaCut;

			ctx->UpdateSubresource(useCB, 0, nullptr, &use, 0, 0);
			ctx->PSSetConstantBuffers(2, 1, &useCB);

			// txOpacity(t4) 필요하므로 머티리얼 바인딩(다른 텍스처가 같이 바인딩되어도 무방)
			mat.Bind(ctx);
			part.mesh.DrawSubmesh(ctx, (UINT)i);
		}
		MaterialGPU::Unbind(ctx);
	}
}
