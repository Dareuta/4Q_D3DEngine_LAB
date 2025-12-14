// AssimpImporterEx.cpp
#include "../D3D_Core/pch.h"
#include "AssimpImporterEx.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>

using std::filesystem::path;

static unsigned MakeFlags(bool flipUV, bool leftHanded) {
	unsigned f = aiProcess_Triangulate
		| aiProcess_JoinIdenticalVertices
		| aiProcess_ImproveCacheLocality
		| aiProcess_SortByPType
		| aiProcess_CalcTangentSpace
		| aiProcess_GenNormals
		| aiProcess_Debone             // ← 불필요 본 제거
		| aiProcess_LimitBoneWeights;  // ← 보통 4개로 제한
	if (leftHanded) f |= aiProcess_ConvertToLeftHanded;
	if (flipUV)     f |= aiProcess_FlipUVs;
	return f;
}

static std::wstring Widen(const aiString& s) {
	std::string a = s.C_Str();
	return std::wstring(a.begin(), a.end());
}

static std::wstring FileOnly(const std::wstring& p) {
	return path(p).filename().wstring();
}

bool AssimpImporterEx::LoadFBX_PNTT_AndMaterials(
	const std::wstring& pathW, MeshData_PNTT& out, bool flipUV, bool leftHanded)
{
	std::string pathA(pathW.begin(), pathW.end());
	Assimp::Importer imp;
	imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false); // 이미 OK
	imp.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

	const aiScene* sc = imp.ReadFile(pathA.c_str(), MakeFlags(flipUV, leftHanded));
	if (!sc || !sc->mRootNode) return false;

	// 1) Materials (파일명만)
	out.materials.clear();
	out.materials.resize(sc->mNumMaterials);
	auto grabTex = [&](aiMaterial* m, aiTextureType t)->std::wstring {
		aiString p; if (AI_SUCCESS == m->GetTexture(t, 0, &p)) return FileOnly(Widen(p));
		return L"";
		};
	for (unsigned i = 0;i < sc->mNumMaterials;++i) {
		auto* m = sc->mMaterials[i];
		MaterialCPU mc;
		mc.diffuse = grabTex(m, aiTextureType_DIFFUSE);
		mc.normal = grabTex(m, aiTextureType_NORMALS);
		if (mc.normal.empty()) mc.normal = grabTex(m, aiTextureType_HEIGHT); // 일부 툴
		mc.specular = grabTex(m, aiTextureType_SPECULAR);
		mc.emissive = grabTex(m, aiTextureType_EMISSIVE);
		mc.opacity = grabTex(m, aiTextureType_OPACITY);
		out.materials[i] = mc;
	}

	// 2) Vertices / Indices / Submeshes
	size_t totalV = 0, totalI = 0;
	for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
		totalV += sc->mMeshes[mi]->mNumVertices;
		totalI += sc->mMeshes[mi]->mNumFaces * 3;
	}
	out.vertices.clear(); out.indices.clear(); out.submeshes.clear();
	out.vertices.reserve(totalV); out.indices.reserve(totalI);
	out.submeshes.reserve(sc->mNumMeshes);

	uint32_t baseV = 0, baseI = 0;
	for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
		const aiMesh* m = sc->mMeshes[mi];
		SubMeshCPU sm{}; sm.baseVertex = baseV; sm.indexStart = baseI; sm.materialIndex = m->mMaterialIndex;

		for (unsigned v = 0; v < m->mNumVertices; ++v) {
			auto& p = m->mVertices[v];
			aiVector3D n = m->HasNormals() ? m->mNormals[v] : aiVector3D(0, 1, 0);
			aiVector3D t(1, 0, 0); float w = 1.0f;
			if (m->HasTangentsAndBitangents()) { t = m->mTangents[v]; w = 1.0f; }
			aiVector3D uv = m->HasTextureCoords(0) ? m->mTextureCoords[0][v] : aiVector3D(0, 0, 0);
			out.vertices.push_back({ p.x,p.y,p.z, n.x,n.y,n.z, uv.x,uv.y, t.x,t.y,t.z, w });
		}
		for (unsigned f = 0; f < m->mNumFaces; ++f) {
			auto& face = m->mFaces[f];
			out.indices.push_back(baseV + face.mIndices[0]);
			out.indices.push_back(baseV + face.mIndices[1]);
			out.indices.push_back(baseV + face.mIndices[2]);
		}
		sm.indexCount = m->mNumFaces * 3;
		baseV += m->mNumVertices; baseI += sm.indexCount;
		out.submeshes.push_back(sm);
	}
	return true;
}

void AssimpImporterEx::ConvertAiMeshToPNTT(const aiMesh* am, MeshData_PNTT& out)
{
	out.vertices.clear();
	out.indices.clear();
	out.submeshes.clear();
	// materials는 여기서 건드리지 않음 (ExtractMaterials() 결과를 호출측에서 채워넣으세요)

	if (!am) return;

	out.vertices.resize(am->mNumVertices);
	for (unsigned v = 0; v < am->mNumVertices; ++v) {
		VertexCPU_PNTT vv{};

		// Position
		vv.px = am->mVertices[v].x;
		vv.py = am->mVertices[v].y;
		vv.pz = am->mVertices[v].z;

		// Normal
		if (am->HasNormals()) {
			vv.nx = am->mNormals[v].x;
			vv.ny = am->mNormals[v].y;
			vv.nz = am->mNormals[v].z;
		}
		else {
			vv.nx = 0.0f; vv.ny = 1.0f; vv.nz = 0.0f;
		}

		// UV0
		if (am->HasTextureCoords(0)) {
			vv.u = am->mTextureCoords[0][v].x;
			vv.v = am->mTextureCoords[0][v].y;
		}
		else {
			vv.u = vv.v = 0.0f;
		}

		// Tangent + handedness (tw)
		if (am->HasTangentsAndBitangents()) {
			const aiVector3D& t = am->mTangents[v];
			const aiVector3D& b = am->mBitangents[v];
			aiVector3D n = am->HasNormals() ? am->mNormals[v] : aiVector3D(0, 1, 0);

			// cross(N,T)
			aiVector3D c(
				n.y * t.z - n.z * t.y,
				n.z * t.x - n.x * t.z,
				n.x * t.y - n.y * t.x
			);
			float sign = (c.x * b.x + c.y * b.y + c.z * b.z) < 0.0f ? -1.0f : 1.0f;

			vv.tx = t.x; vv.ty = t.y; vv.tz = t.z; vv.tw = sign;
		}
		else {
			vv.tx = 1.0f; vv.ty = 0.0f; vv.tz = 0.0f; vv.tw = 1.0f;
		}

		out.vertices[v] = vv;
	}

	// Indices (로컬 인덱스 그대로; baseVertex를 추가로 더하지 않는다)
	out.indices.reserve(am->mNumFaces * 3);
	for (unsigned f = 0; f < am->mNumFaces; ++f) {
		const aiFace& face = am->mFaces[f];
		// aiProcess_Triangulate 가정하에 3개
		for (unsigned k = 0; k < face.mNumIndices; ++k)
			out.indices.push_back(static_cast<uint32_t>(face.mIndices[k]));
	}

	// Submesh (aiMesh 하나 = 1개)
	SubMeshCPU sm{};
	sm.baseVertex = 0;
	sm.indexStart = 0;
	sm.indexCount = static_cast<uint32_t>(out.indices.size());
	sm.materialIndex = am->mMaterialIndex;
	out.submeshes.push_back(sm);
}

//// 장면의 모든 재질을 MaterialCPU 배열로 추출(파일명만 보관)
//void AssimpImporterEx::ExtractMaterials(const aiScene* sc, std::vector<MaterialCPU>& out)
//{
//	out.clear();
//	if (!sc) return;
//	out.resize(sc->mNumMaterials);
//
//	auto grabTex = [&](aiMaterial* m, aiTextureType t) -> std::wstring {
//		aiString p;
//		if (m && m->GetTextureCount(t) > 0 && m->GetTexture(t, 0, &p) == AI_SUCCESS) {
//			return FileOnly(Widen(p)); // 파일명만
//		}
//		return L"";
//		};
//
//	for (unsigned i = 0; i < sc->mNumMaterials; ++i) {
//		aiColor3D kd(1, 1, 1);
//		if (aiMaterial* m = sc->mMaterials[i]) {	//		
//			MaterialCPU mc{};
//			mc.diffuse = grabTex(m, aiTextureType_DIFFUSE);
//			mc.normal = grabTex(m, aiTextureType_NORMALS);
//			if (mc.normal.empty()) mc.normal = grabTex(m, aiTextureType_HEIGHT); // 일부 툴은 HEIGHT에 노말맵 둠
//			mc.specular = grabTex(m, aiTextureType_SPECULAR);
//			mc.emissive = grabTex(m, aiTextureType_EMISSIVE);
//			mc.opacity = grabTex(m, aiTextureType_OPACITY);
//
//			mc.diffuseColor[0] = kd.r;
//			mc.diffuseColor[1] = kd.g;
//			mc.diffuseColor[2] = kd.b;
//			out[i] = std::move(mc);
//		}
//	}
//}
//

// 장면의 모든 재질을 MaterialCPU 배열로 추출(파일명만 보관)
void AssimpImporterEx::ExtractMaterials(const aiScene* sc, std::vector<MaterialCPU>& out)
{
	out.clear();
	if (!sc) return;
	out.resize(sc->mNumMaterials);

	auto grabTex = [&](aiMaterial* m, aiTextureType t) -> std::wstring {
		aiString p;
		if (m && m->GetTextureCount(t) > 0 && m->GetTexture(t, 0, &p) == AI_SUCCESS) {
			//return FileOnly(Widen(p)); // 파일명만
			return path(Widen(p)).generic_wstring(); // 상대경로/서브폴더 유지

		}
		return L"";
		};

	for (unsigned i = 0; i < sc->mNumMaterials; ++i) {
		aiMaterial* m = sc->mMaterials[i];
		MaterialCPU mc{};

		if (m) {
			// 1) 텍스처들
			mc.diffuse = grabTex(m, aiTextureType_DIFFUSE);
#if defined(aiTextureType_BASE_COLOR)
			if (mc.diffuse.empty()) mc.diffuse = grabTex(m, aiTextureType_BASE_COLOR); // PBR 계열 대비
#endif
			mc.normal = grabTex(m, aiTextureType_NORMALS);
			if (mc.normal.empty()) mc.normal = grabTex(m, aiTextureType_HEIGHT); // 어떤 툴은 HEIGHT에 노말을 넣음
			mc.specular = grabTex(m, aiTextureType_SPECULAR);
			mc.emissive = grabTex(m, aiTextureType_EMISSIVE);
			mc.opacity = grabTex(m, aiTextureType_OPACITY);

			// 2) 색상(Kd) 읽기 — 이거 없으면 기본값(1,1,1)이라 항상 하양!
			aiColor3D kd(1, 1, 1);
			if (m->Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS) {
				mc.diffuseColor[0] = kd.r;
				mc.diffuseColor[1] = kd.g;
				mc.diffuseColor[2] = kd.b;
			}

			// (선택) PBR Base Color도 지원하면 더 좋음
			// 어떤 에셋은 "$clr.base"나 BASE_COLOR에 들어감
#if defined(AI_MATKEY_BASE_COLOR)
			aiColor4D bc;
			if (m->Get(AI_MATKEY_BASE_COLOR, bc) == AI_SUCCESS) {
				mc.diffuseColor[0] = bc.r;
				mc.diffuseColor[1] = bc.g;
				mc.diffuseColor[2] = bc.b;
			}
#else
			// 매크로가 없다면 문자열 키로도 시도 가능 (Assimp 버전에 따라 다름)
			aiColor4D bc;
			if (m->Get("$clr.base", 0, 0, bc) == AI_SUCCESS) {
				mc.diffuseColor[0] = bc.r;
				mc.diffuseColor[1] = bc.g;
				mc.diffuseColor[2] = bc.b;
			}
#endif
		}

		out[i] = std::move(mc);
	}
}