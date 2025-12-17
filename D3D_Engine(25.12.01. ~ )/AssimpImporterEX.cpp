// AssimpImporterEx.cpp
#include "../D3D_Core/pch.h"
#include "AssimpImporterEx.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>

using std::filesystem::path;

namespace
{
    // -------------------------------------------------------------------------
    // Import flags
    // -------------------------------------------------------------------------
    unsigned MakeFlags(bool flipUV, bool leftHanded)
    {
        unsigned f =
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType |
            aiProcess_CalcTangentSpace |
            aiProcess_GenNormals |
            aiProcess_Debone |            // Remove unused bones
            aiProcess_LimitBoneWeights;   // Usually keep <= 4 weights

        if (leftHanded) f |= aiProcess_ConvertToLeftHanded;
        if (flipUV)     f |= aiProcess_FlipUVs;
        return f;
    }

    // -------------------------------------------------------------------------
    // Small helpers
    // -------------------------------------------------------------------------
    std::wstring Widen(const aiString& s)
    {
        const std::string a = s.C_Str();
        return std::wstring(a.begin(), a.end());
    }

    // FBX/Assimp may store:
    //  - relative path: "Textures/albedo.png"  -> keep subfolder
    //  - absolute path: "C:\...\albedo.png"    -> keep as-is (Material join/fallback handles it)
    //  - embedded: "*0" (we don't support embedded in this engine flow)
    std::wstring GrabTexPath(aiMaterial* m, aiTextureType type)
    {
        if (!m || m->GetTextureCount(type) == 0)
            return L"";

        aiString p;
        if (m->GetTexture(type, 0, &p) != AI_SUCCESS)
            return L"";

        std::wstring w = Widen(p);
        if (!w.empty() && w[0] == L'*')
            return L""; // embedded texture reference (not supported here)

        // Keep relative subfolders if present
        return path(w).generic_wstring();
    }

    // Compute tangent handedness sign for normal mapping.
    // sign = dot(cross(N, T), B) < 0 ? -1 : +1
    float ComputeTangentSign(const aiVector3D& N, const aiVector3D& T, const aiVector3D& B)
    {
        const aiVector3D c(
            N.y * T.z - N.z * T.y,
            N.z * T.x - N.x * T.z,
            N.x * T.y - N.y * T.x
        );
        const float d = c.x * B.x + c.y * B.y + c.z * B.z;
        return (d < 0.0f) ? -1.0f : 1.0f;
    }

    // Material extraction policy for this engine:
    //  - t0: diffuse/baseColor
    //  - t1: normal
    //  - t2: "specular slot" reused as metallic map (PBR)
    //  - t3: "emissive slot" reused as roughness map (PBR)
    //  - t4: opacity (alpha cut)
    //
    // If PBR textures are missing, fall back to legacy maps where possible.
    MaterialCPU ExtractOneMaterial(aiMaterial* m)
    {
        MaterialCPU mc{};
        if (!m) return mc;

        // BaseColor (Diffuse first, then BaseColor)
        mc.diffuse = GrabTexPath(m, aiTextureType_DIFFUSE);
        if (mc.diffuse.empty())
            mc.diffuse = GrabTexPath(m, aiTextureType_BASE_COLOR);

        // Normal (Normals first, then Height)
        mc.normal = GrabTexPath(m, aiTextureType_NORMALS);
        if (mc.normal.empty())
            mc.normal = GrabTexPath(m, aiTextureType_HEIGHT);

        // Metallic (prefer METALNESS, fallback SPECULAR)
        {
            auto metal = GrabTexPath(m, aiTextureType_METALNESS);
            if (metal.empty())
                metal = GrabTexPath(m, aiTextureType_SPECULAR);
            mc.specular = metal; // reused slot = metallic
        }

        // Roughness (prefer DIFFUSE_ROUGHNESS, fallback SHININESS(gloss), then EMISSIVE)
        {
            auto rough = GrabTexPath(m, aiTextureType_DIFFUSE_ROUGHNESS);
            if (rough.empty())
                rough = GrabTexPath(m, aiTextureType_SHININESS); // may be glossiness depending on exporter
            if (rough.empty())
                rough = GrabTexPath(m, aiTextureType_EMISSIVE);
            mc.emissive = rough; // reused slot = roughness
        }

        // Opacity (alpha cut / masked)
        mc.opacity = GrabTexPath(m, aiTextureType_OPACITY);

        // Diffuse/Base color constants
        {
            aiColor3D kd(1, 1, 1);
            if (m->Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS) {
                mc.diffuseColor[0] = kd.r;
                mc.diffuseColor[1] = kd.g;
                mc.diffuseColor[2] = kd.b;
            }
        }

#if defined(AI_MATKEY_BASE_COLOR)
        {
            aiColor4D bc;
            if (m->Get(AI_MATKEY_BASE_COLOR, bc) == AI_SUCCESS) {
                mc.diffuseColor[0] = bc.r;
                mc.diffuseColor[1] = bc.g;
                mc.diffuseColor[2] = bc.b;
            }
        }
#endif
        return mc;
    }
} // namespace

// -----------------------------------------------------------------------------
// Load FBX: Mesh(PNTT) + Materials
// -----------------------------------------------------------------------------
bool AssimpImporterEx::LoadFBX_PNTT_AndMaterials(
    const std::wstring& pathW, MeshData_PNTT& out, bool flipUV, bool leftHanded)
{
    const std::string pathA(pathW.begin(), pathW.end());

    Assimp::Importer imp;
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    imp.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

    const aiScene* sc = imp.ReadFile(pathA.c_str(), MakeFlags(flipUV, leftHanded));
    if (!sc || !sc->mRootNode) return false;

    // -------------------------------------------------------------------------
    // 1) Materials
    // -------------------------------------------------------------------------
    out.materials.clear();
    out.materials.resize(sc->mNumMaterials);

    for (unsigned i = 0; i < sc->mNumMaterials; ++i)
        out.materials[i] = ExtractOneMaterial(sc->mMaterials[i]);

    // -------------------------------------------------------------------------
    // 2) Mesh aggregation (all aiMesh into one big vertex/index buffer + submeshes)
    // -------------------------------------------------------------------------
    size_t totalV = 0, totalI = 0;
    for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
        const aiMesh* m = sc->mMeshes[mi];
        totalV += m->mNumVertices;
        totalI += m->mNumFaces * 3;
    }

    out.vertices.clear();
    out.indices.clear();
    out.submeshes.clear();

    out.vertices.reserve(totalV);
    out.indices.reserve(totalI);
    out.submeshes.reserve(sc->mNumMeshes);

    uint32_t baseV = 0;
    uint32_t baseI = 0;

    for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi)
    {
        const aiMesh* m = sc->mMeshes[mi];

        SubMeshCPU sm{};
        sm.baseVertex = baseV;
        sm.indexStart = baseI;
        sm.materialIndex = m->mMaterialIndex;

        // Vertices
        for (unsigned v = 0; v < m->mNumVertices; ++v)
        {
            const aiVector3D& p = m->mVertices[v];
            const aiVector3D n = m->HasNormals() ? m->mNormals[v] : aiVector3D(0, 1, 0);

            aiVector3D t(1, 0, 0);
            float tw = 1.0f;

            if (m->HasTangentsAndBitangents())
            {
                const aiVector3D& T = m->mTangents[v];
                const aiVector3D& B = m->mBitangents[v];
                const aiVector3D N = m->HasNormals() ? m->mNormals[v] : aiVector3D(0, 1, 0);

                t = T;
                tw = ComputeTangentSign(N, T, B);
            }

            const aiVector3D uv = m->HasTextureCoords(0) ? m->mTextureCoords[0][v] : aiVector3D(0, 0, 0);

            out.vertices.push_back({
                p.x, p.y, p.z,
                n.x, n.y, n.z,
                uv.x, uv.y,
                t.x, t.y, t.z,
                tw
                });
        }

        // Indices (global indexing = baseV + faceIndices)
        for (unsigned f = 0; f < m->mNumFaces; ++f)
        {
            const aiFace& face = m->mFaces[f];
            out.indices.push_back(baseV + face.mIndices[0]);
            out.indices.push_back(baseV + face.mIndices[1]);
            out.indices.push_back(baseV + face.mIndices[2]);
        }
         
        sm.indexCount = m->mNumFaces * 3;

        baseV += m->mNumVertices;
        baseI += sm.indexCount;

        out.submeshes.push_back(sm);
    }

    return true;
}

// -----------------------------------------------------------------------------
// Convert a single aiMesh -> MeshData_PNTT (one submesh)
// -----------------------------------------------------------------------------
void AssimpImporterEx::ConvertAiMeshToPNTT(const aiMesh* am, MeshData_PNTT& out)
{
    out.vertices.clear();
    out.indices.clear();
    out.submeshes.clear();
    // NOTE: materials are handled by ExtractMaterials() from caller side

    if (!am) return;

    out.vertices.resize(am->mNumVertices);

    for (unsigned v = 0; v < am->mNumVertices; ++v)
    {
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
            vv.u = 0.0f; vv.v = 0.0f;
        }

        // Tangent + handedness
        if (am->HasTangentsAndBitangents())
        {
            const aiVector3D& T = am->mTangents[v];
            const aiVector3D& B = am->mBitangents[v];
            const aiVector3D N = am->HasNormals() ? am->mNormals[v] : aiVector3D(0, 1, 0);

            vv.tx = T.x; vv.ty = T.y; vv.tz = T.z;
            vv.tw = ComputeTangentSign(N, T, B);
        }
        else
        {
            vv.tx = 1.0f; vv.ty = 0.0f; vv.tz = 0.0f;
            vv.tw = 1.0f;
        }

        out.vertices[v] = vv;
    }

    // Indices (local indices only)
    out.indices.reserve(am->mNumFaces * 3);
    for (unsigned f = 0; f < am->mNumFaces; ++f) {
        const aiFace& face = am->mFaces[f];
        for (unsigned k = 0; k < face.mNumIndices; ++k)
            out.indices.push_back(static_cast<uint32_t>(face.mIndices[k]));
    }

    // Submesh
    SubMeshCPU sm{};
    sm.baseVertex = 0;
    sm.indexStart = 0;
    sm.indexCount = static_cast<uint32_t>(out.indices.size());
    sm.materialIndex = am->mMaterialIndex;
    out.submeshes.push_back(sm);
}

// -----------------------------------------------------------------------------
// Extract all materials from a scene (same policy as LoadFBX_PNTT_AndMaterials)
// -----------------------------------------------------------------------------
void AssimpImporterEx::ExtractMaterials(const aiScene* sc, std::vector<MaterialCPU>& out)
{
    out.clear();
    if (!sc) return;

    out.resize(sc->mNumMaterials);
    for (unsigned i = 0; i < sc->mNumMaterials; ++i)
        out[i] = ExtractOneMaterial(sc->mMaterials[i]);
}
