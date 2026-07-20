// ===========================================================================
// GltfImport.cpp — fastgltf-based glTF/GLB import backend.
//
// THIS FILE IS THE ENGINE'S C++17 ISLAND for fastgltf. It is compiled with
// /std:c++17 via a per-file LanguageStandard override in the .vcxproj,
// together with fastgltf's own sources (Include/fastgltf/src/*.cpp) and
// simdjson (Include/simdjson/simdjson.cpp). Everything else in the engine is
// C++14. No fastgltf or C++17-only types may appear in GltfImport.h.
//
// The converter mirrors what the Assimp path (IrrAssimpImport) produces so
// the renderer/animation systems consume both identically:
//  - glTF right-handed -> Irrlicht left-handed: mirror across the Z plane
//    (equivalent of aiProcess_MakeLeftHanded) + flipped triangle winding
//    (aiProcess_FlipWindingOrder). UVs are used as-is: the Assimp chain
//    (importer V-flip + aiProcess_FlipUVs) is a net no-op for glTF.
//  - Scene nodes -> ISkinnedMesh joints; non-skinned meshes ride their
//    node via SJoint::AttachedMeshes (rigid animation).
//  - Skins -> per-vertex SWeights + inverse bind matrices written into
//    SJoint::GlobalInversedMatrix after finalize(), like applyBoneOffsets().
//  - Animations -> 30 fps keyframes, clips concatenated along the timeline
//    with a .anim clip-range XML written next to the mesh.
// ===========================================================================

#include "GltfImport.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <IFileSystem.h>
#include <IMeshCache.h>
#include <IReadFile.h>
#include <ISceneManager.h>
#include <ISkinnedMesh.h>
#include <IVideoDriver.h>

#include <spdlog/spdlog.h>

using namespace irr;

namespace
{

// ---------------------------------------------------------------------------
// Coordinate conversion (right-handed glTF -> left-handed Irrlicht)
// ---------------------------------------------------------------------------

core::vector3df toIrrVec3(const fastgltf::math::fvec3& v)
{
    return core::vector3df(v.x(), v.y(), -v.z());
}

// Scale is invariant under the Z mirror (S * diag(s) * S == diag(s))
core::vector3df toIrrScale(const fastgltf::math::fvec3& v)
{
    return core::vector3df(v.x(), v.y(), v.z());
}

// The Z mirror maps (x,y,z,w) -> (-x,-y,z,w); Irrlicht's opposite quaternion
// convention then negates w (see assimpToIrrQuaternion). The product of both,
// times -1 (same rotation), collapses to a single z negation.
core::quaternion toIrrQuat(const fastgltf::math::fquat& q)
{
    return core::quaternion(q.x(), q.y(), -q.z(), q.w());
}

// M' = S * M * S with S = diag(1,1,-1,1): negate every element with exactly
// one z index. Both libraries store column-major, element (r,c) at [c*4+r].
core::matrix4 toIrrMatrix(const fastgltf::math::fmat4x4& m)
{
    core::matrix4 out;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = m[c][r];
    out[2]  = -out[2];  out[6]  = -out[6];
    out[8]  = -out[8];  out[9]  = -out[9];
    out[11] = -out[11]; out[14] = -out[14];
    return out;
}

// Same clip-name cleanup as the Assimp path (cleanAnimName in
// IrrAssimpImport.cpp) so .anim files come out identical.
std::string cleanAnimName(const std::string& raw)
{
    std::vector<std::string> tokens;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, '|'))
        if (!token.empty())
            tokens.push_back(token);

    static const std::regex noise("armature|baselayer|base[_ ]layer", std::regex::icase);
    std::vector<std::string> kept;
    for (const std::string& t : tokens)
        if (!std::regex_match(t, noise))
            kept.push_back(t);

    if (kept.empty())
        return raw;

    std::string result;
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (i > 0) result += '_';
        result += kept[i];
    }
    return result;
}

struct AnimRange
{
    std::string name;
    int startFrame;
    int endFrame;
    bool loop;
};

// Static TRS of a node in Irrlicht space, used to synthesize keys for
// channels an animation doesn't target (Assimp's glTF importer does the
// same, so joints don't snap to identity on untargeted channels).
struct StaticTRS
{
    core::vector3df position;
    core::quaternion rotation;
    core::vector3df scale;
};

// ---------------------------------------------------------------------------
// GltfMeshBuilder — one-shot converter, all fastgltf state stays in here
// ---------------------------------------------------------------------------

class GltfMeshBuilder
{
public:
    GltfMeshBuilder(scene::ISceneManager* smgr, const io::path& filePath) :
        m_sceneManager(smgr),
        m_fileSystem(smgr->getFileSystem()),
        m_filePath(filePath)
    {
    }

    // Returns a finalized ISkinnedMesh (refcount 1) or nullptr with outError set.
    scene::ISkinnedMesh* build(std::string& outError)
    {
        const std::string pathStr(m_filePath.c_str());

        auto data = fastgltf::GltfDataBuffer::FromPath(std::filesystem::path(pathStr));
        if (data.error() != fastgltf::Error::None)
        {
            outError = "fastgltf: could not read file '" + pathStr + "': " +
                       std::string(fastgltf::getErrorMessage(data.error()));
            return nullptr;
        }

        const std::filesystem::path directory =
            std::filesystem::path(pathStr).parent_path();

        // Extensions we parse (and mostly ignore) so that assets merely
        // *requiring* them don't fail to load outright.
        constexpr auto extensions =
            fastgltf::Extensions::KHR_materials_emissive_strength |
            fastgltf::Extensions::KHR_texture_transform |
            fastgltf::Extensions::KHR_materials_unlit |
            fastgltf::Extensions::KHR_materials_specular |
            fastgltf::Extensions::KHR_materials_ior;

        fastgltf::Parser parser(extensions);
        auto asset = parser.loadGltf(data.get(), directory,
            fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices);
        if (asset.error() != fastgltf::Error::None)
        {
            outError = "fastgltf: failed to parse '" + pathStr + "': " +
                       std::string(fastgltf::getErrorMessage(asset.error()));
            return nullptr;
        }

        m_asset = std::move(asset.get());

        if (m_asset.nodes.empty())
        {
            outError = "fastgltf: '" + pathStr + "' contains no nodes";
            return nullptr;
        }

        m_irrMesh = m_sceneManager->createSkinnedMesh();

        createMaterials();

        // Joints for every node of the displayed scene (parents first)
        m_nodeJoints.assign(m_asset.nodes.size(), nullptr);
        m_nodeStaticTRS.assign(m_asset.nodes.size(), StaticTRS());
        const size_t sceneIndex =
            m_asset.defaultScene.has_value() ? m_asset.defaultScene.value() : 0;
        if (sceneIndex < m_asset.scenes.size())
        {
            for (size_t rootIdx : m_asset.scenes[sceneIndex].nodeIndices)
                createNodeJoint(rootIdx, nullptr);
        }
        else
        {
            // No scene definition: treat parentless nodes as roots
            std::vector<bool> isChild(m_asset.nodes.size(), false);
            for (const fastgltf::Node& node : m_asset.nodes)
                for (size_t c : node.children)
                    isChild[c] = true;
            for (size_t i = 0; i < m_asset.nodes.size(); ++i)
                if (!isChild[i])
                    createNodeJoint(i, nullptr);
        }

        createMeshBuffers();
        createAnimations();

        m_irrMesh->setDirty();
        m_irrMesh->finalize();

        // After finalize(), like the Assimp path's applyBoneOffsets():
        // finalize() computes GlobalInversedMatrix from the node hierarchy;
        // the authored inverse bind matrices must override that.
        applyInverseBindMatrices();

        writeAnimFile();

        outError.clear();
        return m_irrMesh;
    }

private:
    // -----------------------------------------------------------------------
    // Node hierarchy
    // -----------------------------------------------------------------------

    void createNodeJoint(size_t nodeIndex, scene::ISkinnedMesh::SJoint* parent)
    {
        if (nodeIndex >= m_asset.nodes.size() || m_nodeJoints[nodeIndex])
            return;

        const fastgltf::Node& node = m_asset.nodes[nodeIndex];

        scene::ISkinnedMesh::SJoint* joint = m_irrMesh->addJoint(parent);
        if (!node.name.empty())
            joint->Name = node.name.c_str();
        else
            joint->Name = (core::stringc("gltf_node_") + core::stringc((int)nodeIndex));

        joint->LocalMatrix = toIrrMatrix(fastgltf::getTransformMatrix(node));
        if (parent)
            joint->GlobalMatrix = parent->GlobalMatrix * joint->LocalMatrix;
        else
            joint->GlobalMatrix = joint->LocalMatrix;

        m_nodeJoints[nodeIndex] = joint;
        m_nodeStaticTRS[nodeIndex] = computeStaticTRS(node, joint->LocalMatrix);

        for (size_t childIndex : node.children)
            createNodeJoint(childIndex, joint);
    }

    StaticTRS computeStaticTRS(const fastgltf::Node& node, const core::matrix4& localIrr)
    {
        StaticTRS out;
        if (const fastgltf::TRS* trs = std::get_if<fastgltf::TRS>(&node.transform))
        {
            out.position = toIrrVec3(trs->translation);
            out.rotation = toIrrQuat(trs->rotation);
            out.scale    = toIrrScale(trs->scale);
        }
        else
        {
            // Matrix node (glTF forbids these as animation targets, so this
            // only feeds synthesized filler keys for malformed assets)
            out.position = localIrr.getTranslation();
            out.scale    = localIrr.getScale();
            core::matrix4 rot = localIrr;
            if (!core::iszero(out.scale.X) && !core::iszero(out.scale.Y) && !core::iszero(out.scale.Z))
            {
                for (int r = 0; r < 3; ++r)
                {
                    rot[0 * 4 + r] /= out.scale.X;
                    rot[1 * 4 + r] /= out.scale.Y;
                    rot[2 * 4 + r] /= out.scale.Z;
                }
            }
            rot.setTranslation(core::vector3df(0, 0, 0));
            out.rotation = core::quaternion(rot);
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Buffers / images / textures
    // -----------------------------------------------------------------------

    // Raw bytes of a glTF buffer (works for GLB chunks, data URIs and
    // external .bin files thanks to Options::LoadExternalBuffers)
    const std::byte* getBufferBytes(size_t bufferIndex, size_t& outSize) const
    {
        outSize = 0;
        if (bufferIndex >= m_asset.buffers.size())
            return nullptr;
        const fastgltf::Buffer& buffer = m_asset.buffers[bufferIndex];

        if (const auto* arr = std::get_if<fastgltf::sources::Array>(&buffer.data))
        {
            outSize = arr->bytes.size();
            return arr->bytes.data();
        }
        if (const auto* vec = std::get_if<fastgltf::sources::Vector>(&buffer.data))
        {
            outSize = vec->bytes.size();
            return vec->bytes.data();
        }
        if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&buffer.data))
        {
            outSize = view->bytes.size();
            return view->bytes.data();
        }
        return nullptr;
    }

    video::ITexture* createEmbeddedTexture(const void* bytes, size_t size, size_t imageIndex)
    {
        video::IVideoDriver* driver = m_sceneManager->getVideoDriver();

        core::stringc texName(m_filePath);
        texName += "_embedded_";
        texName += (int)imageIndex;

        video::ITexture* texture = driver->findTexture(texName.c_str());
        if (texture)
            return texture;

        io::IReadFile* memFile = m_fileSystem->createMemoryReadFile(
            const_cast<void*>(bytes), (s32)size, texName.c_str(), false);
        if (!memFile)
            return nullptr;

        texture = driver->getTexture(memFile);
        memFile->drop();
        return texture;
    }

    video::ITexture* resolveTexture(size_t textureIndex)
    {
        if (textureIndex >= m_asset.textures.size())
            return nullptr;
        const fastgltf::Texture& tex = m_asset.textures[textureIndex];
        if (!tex.imageIndex.has_value())
            return nullptr;
        const size_t imageIndex = tex.imageIndex.value();
        if (imageIndex >= m_asset.images.size())
            return nullptr;
        const fastgltf::Image& image = m_asset.images[imageIndex];

        video::IVideoDriver* driver = m_sceneManager->getVideoDriver();
        const core::stringc fileDir = m_fileSystem->getFileDir(m_filePath);

        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
        {
            // External image file — same resolution order as the Assimp path
            const core::stringc path(std::string(uri->uri.path()).c_str());
            if (m_fileSystem->existFile(path))
                return driver->getTexture(path);
            if (m_fileSystem->existFile(fileDir + "/" + path))
                return driver->getTexture(fileDir + "/" + path);
            const core::stringc basename = m_fileSystem->getFileBasename(path);
            if (m_fileSystem->existFile(fileDir + "/" + basename))
                return driver->getTexture(fileDir + "/" + basename);
            spdlog::warn("GltfImport: texture '{}' referenced by '{}' not found",
                path.c_str(), m_filePath.c_str());
            return nullptr;
        }
        if (const auto* arr = std::get_if<fastgltf::sources::Array>(&image.data))
        {
            return createEmbeddedTexture(arr->bytes.data(), arr->bytes.size(), imageIndex);
        }
        if (const auto* vec = std::get_if<fastgltf::sources::Vector>(&image.data))
        {
            return createEmbeddedTexture(vec->bytes.data(), vec->bytes.size(), imageIndex);
        }
        if (const auto* bufView = std::get_if<fastgltf::sources::BufferView>(&image.data))
        {
            // Image bytes live inside a glTF buffer (typical for .glb)
            if (bufView->bufferViewIndex >= m_asset.bufferViews.size())
                return nullptr;
            const fastgltf::BufferView& view = m_asset.bufferViews[bufView->bufferViewIndex];
            size_t bufSize = 0;
            const std::byte* bytes = getBufferBytes(view.bufferIndex, bufSize);
            if (!bytes || view.byteOffset + view.byteLength > bufSize)
                return nullptr;
            return createEmbeddedTexture(bytes + view.byteOffset, view.byteLength, imageIndex);
        }
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // Materials
    // -----------------------------------------------------------------------

    static video::SColor toIrrColor(float r, float g, float b, float a)
    {
        const auto clamp255 = [](float v) -> u32 {
            return (u32)core::clamp(v * 255.f, 0.f, 255.f);
        };
        return video::SColor(clamp255(a), clamp255(r), clamp255(g), clamp255(b));
    }

    void createMaterials()
    {
        m_materials.clear();
        for (const fastgltf::Material& mat : m_asset.materials)
        {
            video::SMaterial irrMaterial;
            irrMaterial.MaterialType = video::EMT_SOLID;

            const auto& base = mat.pbrData.baseColorFactor;
            irrMaterial.DiffuseColor = toIrrColor(base.x(), base.y(), base.z(), base.w());

            const auto& emissive = mat.emissiveFactor;
            irrMaterial.EmissiveColor = toIrrColor(emissive.x(), emissive.y(), emissive.z(), 1.f);

            if (mat.pbrData.baseColorTexture.has_value())
                irrMaterial.TextureLayer[0].Texture =
                    resolveTexture(mat.pbrData.baseColorTexture.value().textureIndex);

            // Slot 1 stays free — it is reserved for baked lightmaps
            // (phong_perpixel treats any non-null slot 1 as a lightmap).

            m_materials.push_back(irrMaterial);
        }
    }

    // -----------------------------------------------------------------------
    // Mesh buffers
    // -----------------------------------------------------------------------

    void createMeshBuffers()
    {
        for (size_t nodeIndex = 0; nodeIndex < m_asset.nodes.size(); ++nodeIndex)
        {
            scene::ISkinnedMesh::SJoint* joint = m_nodeJoints[nodeIndex];
            const fastgltf::Node& node = m_asset.nodes[nodeIndex];
            if (!joint || !node.meshIndex.has_value())
                continue;
            const size_t meshIndex = node.meshIndex.value();
            if (meshIndex >= m_asset.meshes.size())
                continue;

            const bool skinned = node.skinIndex.has_value() &&
                                 node.skinIndex.value() < m_asset.skins.size();

            for (const fastgltf::Primitive& prim : m_asset.meshes[meshIndex].primitives)
            {
                // A primitive spanning more than 65535 vertices is split
                // across several buffers; localToGlobal maps each buffer's
                // vertices back to primitive vertex indices (empty = identity)
                std::vector<u32> bufferIds;
                std::vector<std::vector<u32>> localToGlobal;
                createPrimitiveBuffers(prim, nodeIndex, bufferIds, localToGlobal);
                if (bufferIds.empty())
                    continue;

                if (skinned)
                    addPrimitiveWeights(prim, node.skinIndex.value(), bufferIds, localToGlobal);
                else
                    for (u32 bufferId : bufferIds)
                        joint->AttachedMeshes.push_back(bufferId);
            }
        }
    }

    // Vertex/index data of one primitive, fully converted to Irrlicht space,
    // ready to be packed into one or more u16-indexed buffers
    struct PrimitiveData
    {
        std::vector<core::vector3df> positions;
        std::vector<core::vector3df> normals;
        std::vector<core::vector2df> uv;
        std::vector<core::vector2df> uv2;
        std::vector<core::vector3df> tangents;
        std::vector<core::vector3df> binormals;
        std::vector<video::SColor>   colors;
        std::vector<u32>             indices; // winding already flipped
        bool hasUv2 = false;
        bool hasTangents = false;
    };

    bool readPrimitiveData(const fastgltf::Primitive& prim, size_t nodeIndex, PrimitiveData& data)
    {
        if (prim.type != fastgltf::PrimitiveType::Triangles)
        {
            spdlog::warn("GltfImport: '{}' node {} has a non-triangle primitive — skipped",
                m_filePath.c_str(), nodeIndex);
            return false;
        }

        const auto posIt = prim.findAttribute("POSITION");
        if (posIt == prim.attributes.cend())
        {
            spdlog::warn("GltfImport: '{}' node {} primitive has no POSITION — skipped",
                m_filePath.c_str(), nodeIndex);
            return false;
        }
        const fastgltf::Accessor& posAccessor = m_asset.accessors[posIt->accessorIndex];
        const size_t vertexCount = posAccessor.count;
        if (vertexCount == 0)
            return false;

        data.positions.resize(vertexCount);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(m_asset, posAccessor,
            [&](const fastgltf::math::fvec3& v, size_t idx) {
                data.positions[idx] = toIrrVec3(v);
            });

        const auto normalIt  = prim.findAttribute("NORMAL");
        const auto uvIt      = prim.findAttribute("TEXCOORD_0");
        const auto uv2It     = prim.findAttribute("TEXCOORD_1");
        const auto tangentIt = prim.findAttribute("TANGENT");
        const auto colorIt   = prim.findAttribute("COLOR_0");

        const bool hasNormals = normalIt != prim.attributes.cend();
        data.hasUv2      = uv2It != prim.attributes.cend();
        data.hasTangents = tangentIt != prim.attributes.cend() && hasNormals;

        data.normals.assign(vertexCount, core::vector3df(0.f, 0.f, 0.f));
        if (hasNormals)
        {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(m_asset,
                m_asset.accessors[normalIt->accessorIndex],
                [&](const fastgltf::math::fvec3& n, size_t idx) {
                    data.normals[idx] = toIrrVec3(n);
                });
        }

        // glTF UVs are top-left origin like Irrlicht's — no flip (the Assimp
        // chain's importer V-flip + aiProcess_FlipUVs cancel out)
        data.uv.assign(vertexCount, core::vector2df(0.f, 0.f));
        if (uvIt != prim.attributes.cend())
        {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(m_asset,
                m_asset.accessors[uvIt->accessorIndex],
                [&](const fastgltf::math::fvec2& uv, size_t idx) {
                    data.uv[idx] = core::vector2df(uv.x(), uv.y());
                });
        }

        if (data.hasUv2)
        {
            data.uv2.assign(vertexCount, core::vector2df(0.f, 0.f));
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(m_asset,
                m_asset.accessors[uv2It->accessorIndex],
                [&](const fastgltf::math::fvec2& uv, size_t idx) {
                    data.uv2[idx] = core::vector2df(uv.x(), uv.y());
                });
        }

        data.colors.assign(vertexCount, video::SColor(255, 255, 255, 255));
        if (colorIt != prim.attributes.cend())
        {
            // fastgltf converts normalized ubyte/ushort to 0..1 floats for us
            const fastgltf::Accessor& colAccessor = m_asset.accessors[colorIt->accessorIndex];
            if (colAccessor.type == fastgltf::AccessorType::Vec4)
            {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(m_asset, colAccessor,
                    [&](const fastgltf::math::fvec4& c, size_t idx) {
                        data.colors[idx] = toIrrColor(c.x(), c.y(), c.z(), c.w());
                    });
            }
            else if (colAccessor.type == fastgltf::AccessorType::Vec3)
            {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(m_asset, colAccessor,
                    [&](const fastgltf::math::fvec3& c, size_t idx) {
                        data.colors[idx] = toIrrColor(c.x(), c.y(), c.z(), 1.f);
                    });
            }
        }

        // Indices — flipped winding restores front faces after the Z mirror
        if (prim.indicesAccessor.has_value())
        {
            const fastgltf::Accessor& idxAccessor = m_asset.accessors[prim.indicesAccessor.value()];
            std::vector<u32> rawIndices(idxAccessor.count);
            fastgltf::iterateAccessorWithIndex<u32>(m_asset, idxAccessor,
                [&](u32 value, size_t idx) { rawIndices[idx] = value; });

            const size_t indexCount = rawIndices.size() - (rawIndices.size() % 3);
            data.indices.resize(indexCount);
            for (size_t t = 0; t + 2 < indexCount; t += 3)
            {
                data.indices[t]     = rawIndices[t];
                data.indices[t + 1] = rawIndices[t + 2];
                data.indices[t + 2] = rawIndices[t + 1];
            }
        }
        else
        {
            // Non-indexed triangles (Options::GenerateMeshIndices normally
            // prevents this branch from being taken)
            const size_t indexCount = vertexCount - (vertexCount % 3);
            data.indices.resize(indexCount);
            for (size_t t = 0; t + 2 < indexCount; t += 3)
            {
                data.indices[t]     = (u32)t;
                data.indices[t + 1] = (u32)t + 2;
                data.indices[t + 2] = (u32)t + 1;
            }
        }

        // Drop triangles with out-of-range indices (malformed asset) — the
        // splitter and the skin remap index arrays with these values
        size_t w = 0;
        for (size_t t = 0; t + 2 < data.indices.size(); t += 3)
        {
            if (data.indices[t] < vertexCount &&
                data.indices[t + 1] < vertexCount &&
                data.indices[t + 2] < vertexCount)
            {
                data.indices[w]     = data.indices[t];
                data.indices[w + 1] = data.indices[t + 1];
                data.indices[w + 2] = data.indices[t + 2];
                w += 3;
            }
        }
        if (w != data.indices.size())
        {
            spdlog::warn("GltfImport: '{}' node {} primitive has out-of-range indices — {} triangle(s) dropped",
                m_filePath.c_str(), nodeIndex, (data.indices.size() - w) / 3);
            data.indices.resize(w);
        }

        if (!hasNormals)
        {
            // Area-weighted accumulation of face normals (rough equivalent of
            // the Assimp path's aiProcess_GenSmoothNormals)
            for (size_t t = 0; t + 2 < data.indices.size(); t += 3)
            {
                const core::vector3df p0 = data.positions[data.indices[t]];
                const core::vector3df faceNormal =
                    (data.positions[data.indices[t + 1]] - p0)
                        .crossProduct(data.positions[data.indices[t + 2]] - p0);
                data.normals[data.indices[t]]     += faceNormal;
                data.normals[data.indices[t + 1]] += faceNormal;
                data.normals[data.indices[t + 2]] += faceNormal;
            }
            for (size_t j = 0; j < vertexCount; ++j)
            {
                if (data.normals[j].getLengthSQ() > 0.f)
                    data.normals[j].normalize();
                else
                    data.normals[j] = core::vector3df(0.f, 1.f, 0.f);
            }
        }

        if (data.hasTangents)
        {
            data.tangents.assign(vertexCount, core::vector3df(0.f, 0.f, 0.f));
            data.binormals.assign(vertexCount, core::vector3df(0.f, 0.f, 0.f));
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(m_asset,
                m_asset.accessors[tangentIt->accessorIndex],
                [&](const fastgltf::math::fvec4& t, size_t idx) {
                    const core::vector3df tangent = toIrrVec3(
                        fastgltf::math::fvec3(t.x(), t.y(), t.z()));
                    data.tangents[idx] = tangent;
                    // Mirroring flips the bitangent handedness: B = (N x T) * -w
                    data.binormals[idx] = data.normals[idx].crossProduct(tangent) * -t.w();
                });
        }

        return true;
    }

    // Build one SSkinMeshBuffer from primitive data. localToGlobal maps
    // buffer-local vertex indices to primitive vertex indices; empty means
    // identity (the buffer holds every primitive vertex in order).
    u32 buildChunkBuffer(const PrimitiveData& data, const fastgltf::Primitive& prim,
                         const std::vector<u32>& localToGlobal,
                         const std::vector<u32>& chunkIndices)
    {
        const u32 vertexCount = localToGlobal.empty()
            ? (u32)data.positions.size() : (u32)localToGlobal.size();

        scene::SSkinMeshBuffer* irrBuffer = m_irrMesh->addMeshBuffer();
        const u32 bufferId = m_irrMesh->getMeshBufferCount() - 1;

        // Same priority as the Assimp path: a second UV set wins over tangents
        irrBuffer->VertexType = data.hasUv2 ? video::EVT_2TCOORDS
                              : (data.hasTangents ? video::EVT_TANGENTS : video::EVT_STANDARD);

        switch (irrBuffer->VertexType)
        {
        case video::EVT_STANDARD: irrBuffer->Vertices_Standard.set_used(vertexCount); break;
        case video::EVT_2TCOORDS: irrBuffer->Vertices_2TCoords.set_used(vertexCount); break;
        case video::EVT_TANGENTS: irrBuffer->Vertices_Tangents.set_used(vertexCount); break;
        }

        for (u32 j = 0; j < vertexCount; ++j)
        {
            const u32 g = localToGlobal.empty() ? j : localToGlobal[j];
            irrBuffer->getPosition(j) = data.positions[g];
            irrBuffer->getNormal(j)   = data.normals[g];
            irrBuffer->getTCoords(j)  = data.uv[g];
            switch (irrBuffer->VertexType)
            {
            case video::EVT_STANDARD:
                irrBuffer->Vertices_Standard[j].Color = data.colors[g];
                break;
            case video::EVT_2TCOORDS:
                irrBuffer->Vertices_2TCoords[j].Color = data.colors[g];
                irrBuffer->Vertices_2TCoords[j].TCoords2 = data.uv2[g];
                break;
            case video::EVT_TANGENTS:
                irrBuffer->Vertices_Tangents[j].Color = data.colors[g];
                irrBuffer->Vertices_Tangents[j].Tangent = data.tangents[g];
                irrBuffer->Vertices_Tangents[j].Binormal = data.binormals[g];
                break;
            }
        }

        irrBuffer->Indices.set_used((u32)chunkIndices.size());
        for (u32 t = 0; t < (u32)chunkIndices.size(); ++t)
            irrBuffer->Indices[t] = (u16)chunkIndices[t];

        if (prim.materialIndex.has_value() && prim.materialIndex.value() < m_materials.size())
            irrBuffer->Material = m_materials[prim.materialIndex.value()];
        else
            irrBuffer->Material.MaterialType = video::EMT_SOLID;

        // Bounding box from the converted vertices (avoids virtual dispatch
        // into Irrlicht.dll, same as the Assimp path)
        core::aabbox3df bbox(irrBuffer->getPosition(0));
        for (u32 j = 1; j < vertexCount; ++j)
            bbox.addInternalPoint(irrBuffer->getPosition(j));
        irrBuffer->BoundingBox = bbox;

        return bufferId;
    }

    // SSkinMeshBuffer indices are u16, so a buffer can address at most 65535
    // vertices. Primitives above the limit are split into multiple buffers
    // along triangle bounds (equivalent of aiProcess_SplitLargeMeshes).
    void createPrimitiveBuffers(const fastgltf::Primitive& prim, size_t nodeIndex,
                                std::vector<u32>& outBufferIds,
                                std::vector<std::vector<u32>>& outLocalToGlobal)
    {
        PrimitiveData data;
        if (!readPrimitiveData(prim, nodeIndex, data))
            return;
        if (data.indices.empty())
            return;

        const size_t kMaxVertices = 65535;
        const size_t vertexCount = data.positions.size();

        if (vertexCount <= kMaxVertices)
        {
            outBufferIds.push_back(buildChunkBuffer(data, prim, std::vector<u32>(), data.indices));
            outLocalToGlobal.emplace_back(); // identity
            return;
        }

        // Greedy triangle packing. remapGen stamps which chunk a vertex was
        // last remapped in, so the table doesn't need clearing per chunk.
        std::vector<u32> remap(vertexCount, 0);
        std::vector<u32> remapGen(vertexCount, 0);
        u32 generation = 1;

        std::vector<u32> localToGlobal;
        std::vector<u32> chunkIndices;

        for (size_t t = 0; t + 2 < data.indices.size(); t += 3)
        {
            const u32 tri[3] = { data.indices[t], data.indices[t + 1], data.indices[t + 2] };

            size_t newVerts = 0;
            for (int k = 0; k < 3; ++k)
                if (remapGen[tri[k]] != generation)
                    ++newVerts; // duplicates within a triangle over-count; harmless

            if (localToGlobal.size() + newVerts > kMaxVertices)
            {
                outBufferIds.push_back(buildChunkBuffer(data, prim, localToGlobal, chunkIndices));
                outLocalToGlobal.push_back(std::move(localToGlobal));
                localToGlobal = std::vector<u32>();
                chunkIndices.clear();
                ++generation;
            }

            for (int k = 0; k < 3; ++k)
            {
                const u32 v = tri[k];
                if (remapGen[v] != generation)
                {
                    remapGen[v] = generation;
                    remap[v] = (u32)localToGlobal.size();
                    localToGlobal.push_back(v);
                }
                chunkIndices.push_back(remap[v]);
            }
        }

        if (!chunkIndices.empty())
        {
            outBufferIds.push_back(buildChunkBuffer(data, prim, localToGlobal, chunkIndices));
            outLocalToGlobal.push_back(std::move(localToGlobal));
        }

        spdlog::info("GltfImport: '{}' node {} primitive has {} vertices — split into {} u16-indexable buffers",
            m_filePath.c_str(), nodeIndex, vertexCount, outBufferIds.size());
    }

    // -----------------------------------------------------------------------
    // Skinning
    // -----------------------------------------------------------------------

    // Weights are read once per primitive (indexed by primitive vertex), then
    // written per buffer with vertex ids remapped through localToGlobal.
    void addPrimitiveWeights(const fastgltf::Primitive& prim, size_t skinIndex,
                             const std::vector<u32>& bufferIds,
                             const std::vector<std::vector<u32>>& localToGlobal)
    {
        const fastgltf::Skin& skin = m_asset.skins[skinIndex];

        const auto jointsIt  = prim.findAttribute("JOINTS_0");
        const auto weightsIt = prim.findAttribute("WEIGHTS_0");
        if (jointsIt == prim.attributes.cend() || weightsIt == prim.attributes.cend())
            return;

        const fastgltf::Accessor& jointsAccessor  = m_asset.accessors[jointsIt->accessorIndex];
        const fastgltf::Accessor& weightsAccessor = m_asset.accessors[weightsIt->accessorIndex];

        const size_t count = std::min(jointsAccessor.count, weightsAccessor.count);
        std::vector<fastgltf::math::uvec4> jointIds(count);
        std::vector<fastgltf::math::fvec4> weights(count);

        fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(m_asset, jointsAccessor,
            [&](const fastgltf::math::uvec4& v, size_t idx) { if (idx < count) jointIds[idx] = v; });
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(m_asset, weightsAccessor,
            [&](const fastgltf::math::fvec4& v, size_t idx) { if (idx < count) weights[idx] = v; });

        for (size_t b = 0; b < bufferIds.size(); ++b)
        {
            const u32 bufferId = bufferIds[b];
            const std::vector<u32>& toGlobal = localToGlobal[b];
            const u32 bufferVertexCount = m_irrMesh->getMeshBuffer(bufferId)->getVertexCount();

            for (u32 local = 0; local < bufferVertexCount; ++local)
            {
                const size_t global = toGlobal.empty() ? local : toGlobal[local];
                if (global >= count)
                    continue;

                for (int k = 0; k < 4; ++k)
                {
                    const float strength = weights[global][(size_t)k];
                    if (strength <= 0.f)
                        continue;
                    const size_t skinJoint = jointIds[global][(size_t)k];
                    if (skinJoint >= skin.joints.size())
                        continue;
                    const size_t jointNode = skin.joints[skinJoint];
                    scene::ISkinnedMesh::SJoint* joint =
                        (jointNode < m_nodeJoints.size()) ? m_nodeJoints[jointNode] : nullptr;
                    if (!joint)
                        continue;

                    scene::ISkinnedMesh::SWeight* irrWeight = m_irrMesh->addWeight(joint);
                    irrWeight->buffer_id = (u16)bufferId;
                    irrWeight->vertex_id = local;
                    irrWeight->strength = strength;
                }
            }
        }
    }

    void applyInverseBindMatrices()
    {
        for (const fastgltf::Skin& skin : m_asset.skins)
        {
            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                const size_t nodeIndex = skin.joints[j];
                scene::ISkinnedMesh::SJoint* joint =
                    (nodeIndex < m_nodeJoints.size()) ? m_nodeJoints[nodeIndex] : nullptr;
                if (!joint)
                    continue;

                if (skin.inverseBindMatrices.has_value())
                {
                    const fastgltf::Accessor& ibmAccessor =
                        m_asset.accessors[skin.inverseBindMatrices.value()];
                    if (j < ibmAccessor.count)
                    {
                        const auto ibm = fastgltf::getAccessorElement<fastgltf::math::fmat4x4>(
                            m_asset, ibmAccessor, j);
                        joint->GlobalInversedMatrix = toIrrMatrix(ibm);
                        continue;
                    }
                }
                joint->GlobalInversedMatrix = core::matrix4(); // identity
            }
        }
    }

    // -----------------------------------------------------------------------
    // Animations
    // -----------------------------------------------------------------------

    void createAnimations()
    {
        if (m_asset.animations.empty())
            return;

        const float targetFPS = 30.f;
        m_irrMesh->setAnimationSpeed(targetFPS);

        double frameOffset = 0.0;
        for (size_t a = 0; a < m_asset.animations.size(); ++a)
        {
            const fastgltf::Animation& anim = m_asset.animations[a];

            double clipEndSeconds = 0.0;
            // Which channels each targeted node received in THIS clip
            // (bit 0 = position, 1 = rotation, 2 = scale)
            std::map<size_t, unsigned> targetedChannels;

            for (const fastgltf::AnimationChannel& channel : anim.channels)
            {
                if (!channel.nodeIndex.has_value())
                    continue;
                if (channel.path == fastgltf::AnimationPath::Weights)
                {
                    spdlog::warn("GltfImport: '{}' animation '{}' has a morph-target channel — not supported",
                        m_filePath.c_str(), std::string(anim.name).c_str());
                    continue;
                }
                const size_t nodeIndex = channel.nodeIndex.value();
                scene::ISkinnedMesh::SJoint* joint =
                    (nodeIndex < m_nodeJoints.size()) ? m_nodeJoints[nodeIndex] : nullptr;
                if (!joint || channel.samplerIndex >= anim.samplers.size())
                    continue;

                const fastgltf::AnimationSampler& sampler = anim.samplers[channel.samplerIndex];
                const fastgltf::Accessor& input  = m_asset.accessors[sampler.inputAccessor];
                const fastgltf::Accessor& output = m_asset.accessors[sampler.outputAccessor];

                // CUBICSPLINE outputs (in-tangent, value, out-tangent) triples;
                // Irrlicht keys are linear so we keep the values only. STEP is
                // likewise approximated as linear.
                const bool cubic =
                    sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline;

                std::vector<float> times(input.count);
                fastgltf::iterateAccessorWithIndex<float>(m_asset, input,
                    [&](float t, size_t idx) { times[idx] = t; });

                for (size_t k = 0; k < times.size(); ++k)
                {
                    const size_t outIdx = cubic ? k * 3 + 1 : k;
                    if (outIdx >= output.count)
                        break;
                    const float frame = (float)(times[k] * targetFPS + frameOffset);
                    if (times[k] > clipEndSeconds)
                        clipEndSeconds = times[k];

                    switch (channel.path)
                    {
                    case fastgltf::AnimationPath::Translation:
                    {
                        const auto v = fastgltf::getAccessorElement<fastgltf::math::fvec3>(
                            m_asset, output, outIdx);
                        scene::ISkinnedMesh::SPositionKey* key = m_irrMesh->addPositionKey(joint);
                        key->frame = frame;
                        key->position = toIrrVec3(v);
                        targetedChannels[nodeIndex] |= 1u;
                        break;
                    }
                    case fastgltf::AnimationPath::Rotation:
                    {
                        const auto v = fastgltf::getAccessorElement<fastgltf::math::fvec4>(
                            m_asset, output, outIdx);
                        scene::ISkinnedMesh::SRotationKey* key = m_irrMesh->addRotationKey(joint);
                        key->frame = frame;
                        key->rotation = toIrrQuat(fastgltf::math::fquat(v.x(), v.y(), v.z(), v.w()));
                        targetedChannels[nodeIndex] |= 2u;
                        break;
                    }
                    case fastgltf::AnimationPath::Scale:
                    {
                        const auto v = fastgltf::getAccessorElement<fastgltf::math::fvec3>(
                            m_asset, output, outIdx);
                        scene::ISkinnedMesh::SScaleKey* key = m_irrMesh->addScaleKey(joint);
                        key->frame = frame;
                        key->scale = toIrrScale(v);
                        targetedChannels[nodeIndex] |= 4u;
                        break;
                    }
                    default:
                        break;
                    }
                }
            }

            // Fill channels this clip doesn't animate with the node's static
            // pose, like Assimp's glTF importer, so joints don't interpolate
            // toward another clip's keys or snap to identity.
            for (const auto& entry : targetedChannels)
            {
                const size_t nodeIndex = entry.first;
                const unsigned mask = entry.second;
                scene::ISkinnedMesh::SJoint* joint = m_nodeJoints[nodeIndex];
                const StaticTRS& trs = m_nodeStaticTRS[nodeIndex];

                if (!(mask & 1u))
                {
                    scene::ISkinnedMesh::SPositionKey* key = m_irrMesh->addPositionKey(joint);
                    key->frame = (float)frameOffset;
                    key->position = trs.position;
                }
                if (!(mask & 2u))
                {
                    scene::ISkinnedMesh::SRotationKey* key = m_irrMesh->addRotationKey(joint);
                    key->frame = (float)frameOffset;
                    key->rotation = trs.rotation;
                }
                if (!(mask & 4u))
                {
                    scene::ISkinnedMesh::SScaleKey* key = m_irrMesh->addScaleKey(joint);
                    key->frame = (float)frameOffset;
                    key->scale = trs.scale;
                }
            }

            const double clipDuration = clipEndSeconds * targetFPS;
            AnimRange range;
            range.name       = cleanAnimName(std::string(anim.name));
            if (range.name.empty())
                range.name = "clip_" + std::to_string(a);
            range.startFrame = (int)frameOffset;
            range.endFrame   = (int)(frameOffset + clipDuration);
            range.loop       = true;
            m_animRanges.push_back(range);

            spdlog::info("GltfImport: '{}' clip '{}' frames {}-{}", m_filePath.c_str(),
                range.name, range.startFrame, range.endFrame);

            frameOffset += clipDuration;
        }
    }

    // Same .anim XML the Assimp path emits (writeAnimFile in IrrAssimpImport)
    void writeAnimFile()
    {
        if (m_animRanges.empty())
            return;

        std::string meshPath = m_filePath.c_str();
        const size_t dot = meshPath.find_last_of('.');
        const std::string animPath =
            (dot != std::string::npos ? meshPath.substr(0, dot) : meshPath) + ".anim";

        // Don't overwrite an existing hand-edited file
        if (m_fileSystem->existFile(animPath.c_str()))
            return;

        FILE* f = fopen(animPath.c_str(), "w");
        if (!f)
        {
            spdlog::error("GltfImport: failed to write .anim file '{}'", animPath);
            return;
        }

        fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
        fprintf(f, "<animation>\n");
        fprintf(f, "\t<fps>30</fps>\n");
        fprintf(f, "\t<animationList size=\"dynamic\">\n");
        for (size_t i = 0; i < m_animRanges.size(); ++i)
        {
            const AnimRange& r = m_animRanges[i];
            fprintf(f, "\t\t<a%zu  name=\"%s\" loop=\"%d\">%d,%d</a%zu>\n",
                i, r.name.c_str(), r.loop ? 1 : 0, r.startFrame, r.endFrame, i);
        }
        fprintf(f, "\t</animationList>\n");
        fprintf(f, "</animation>\n");
        fclose(f);

        spdlog::info("GltfImport: generated '{}' — {} clip(s)", animPath, m_animRanges.size());
    }

    // -----------------------------------------------------------------------

    scene::ISceneManager* m_sceneManager;
    io::IFileSystem* m_fileSystem;
    io::path m_filePath;

    fastgltf::Asset m_asset;
    scene::ISkinnedMesh* m_irrMesh = nullptr;

    std::vector<scene::ISkinnedMesh::SJoint*> m_nodeJoints; // node index -> joint
    std::vector<StaticTRS> m_nodeStaticTRS;                 // node index -> static pose
    core::array<video::SMaterial> m_materials;
    std::vector<AnimRange> m_animRanges;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// GltfImport — the C++14-safe facade
// ---------------------------------------------------------------------------

GltfImport::GltfImport(scene::ISceneManager* smgr) :
    m_sceneManager(smgr)
{
}

GltfImport::~GltfImport()
{
}

scene::IAnimatedMesh* GltfImport::getMesh(const io::path& path)
{
    scene::IMeshCache* cache = m_sceneManager->getMeshCache();
    scene::IAnimatedMesh* mesh = cache->getMeshByName(path);
    if (mesh)
        return mesh;

    if (!isLoadable(path))
    {
        m_error = std::string("GltfImport: '") + path.c_str() + "' is not a .gltf/.glb file";
        return nullptr;
    }

    GltfMeshBuilder builder(m_sceneManager, path);
    scene::ISkinnedMesh* skinned = nullptr;
    try
    {
        skinned = builder.build(m_error);
    }
    catch (const std::exception& e)
    {
        // A std::bad_alloc (or similar) here means fastgltf tried to allocate on
        // a corrupted count — the simdjson parse state was clobbered by an OOB
        // write from another (non-instrumented) subsystem during scene load.
        // Fail this mesh gracefully instead of taking down the editor.
        m_error = std::string("GltfImport: exception loading '") + path.c_str() +
                  "': " + e.what() + " (parse state corrupted by another subsystem)";
        spdlog::error("{}", m_error);
        return nullptr;
    }
    if (!skinned)
    {
        spdlog::error("{}", m_error);
        return nullptr;
    }

    cache->addMesh(path, skinned);
    skinned->drop(); // the cache holds the reference now

    return skinned;
}

bool GltfImport::isLoadable(const io::path& path)
{
    core::stringc p(path);
    p.make_lower();
    const s32 dot = p.findLast('.');
    if (dot < 0)
        return false;
    const core::stringc ext = p.subString(dot + 1, p.size() - dot - 1);
    return ext == "gltf" || ext == "glb";
}
