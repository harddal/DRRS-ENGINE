#include "Engine/Prop/PropManager.h"

#include <fstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>

#include <spdlog/spdlog.h>

#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/Lightmapper/LightmapBaker.h"
#include "Engine/Renderer/SplatMap.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Types.h"
#include "Utility/Utility.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

PropManager* PropManager::s_Instance = nullptr;

PropManager::PropManager()
{
    s_Instance = this;
}

PropManager::~PropManager()
{
    clearAll();
    s_Instance = nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

uint32_t PropManager::addProp(StaticProp prop)
{
    prop.id = m_nextId++;
    spawnNode(prop);
    m_props.push_back(std::move(prop));
    return m_props.back().id;
}

void PropManager::removeProp(uint32_t id)
{
    for (auto it = m_props.begin(); it != m_props.end(); ++it)
    {
        if (it->id != id) continue;

        // Clean up lightmap GPU texture
        if (it->lightmap && it->lightmap->texture && it->node)
        {
            for (u32 m = 0; m < it->node->getMaterialCount(); m++)
            {
                if (it->node->getMaterial(m).TextureLayer[1].Texture == it->lightmap->texture)
                    it->node->getMaterial(m).setTexture(1, nullptr);
            }
            RenderManager::Get()->driver()->removeTexture(it->lightmap->texture);
        }
        delete it->lightmap;

        if (it->physicsActor)
        {
            it->physicsActor->release();
            it->physicsActor = nullptr;
        }

        if (it->node)
        {
            it->node->remove();
        }

        m_props.erase(it);
        return;
    }
}

void PropManager::clearAll()
{
    for (auto& batch : m_vegetationBatches)
        if (batch.node) batch.node->remove();
    m_vegetationBatches.clear();
    m_batchedMode = false;

    for (auto& prop : m_props)
    {
        if (prop.lightmap)
        {
            if (prop.lightmap->texture && prop.node)
            {
                for (u32 m = 0; m < prop.node->getMaterialCount(); m++)
                {
                    if (prop.node->getMaterial(m).TextureLayer[1].Texture == prop.lightmap->texture)
                        prop.node->getMaterial(m).setTexture(1, nullptr);
                }
                RenderManager::Get()->driver()->removeTexture(prop.lightmap->texture);
            }
            delete prop.lightmap;
        }

        if (prop.physicsActor)
        {
            prop.physicsActor->release();
            prop.physicsActor = nullptr;
        }

        if (prop.node)
        {
            prop.node->remove();
        }
    }
    m_props.clear();
    m_nextId = 0;
}

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------

StaticProp* PropManager::getProp(uint32_t id)
{
    for (auto& prop : m_props)
        if (prop.id == id) return &prop;
    return nullptr;
}

void PropManager::applyTransform(uint32_t id)
{
    StaticProp* prop = getProp(id);
    if (!prop || !prop->node) return;

    prop->node->setPosition(prop->position);
    prop->node->setRotation(prop->rotation);
    prop->node->setScale(prop->scale);
    prop->node->updateAbsolutePosition();

    if (prop->physicsActor)
    {
        physx::PxTransform t(
            physx::PxVec3(prop->position.X, prop->position.Y, prop->position.Z),
            Math::EulerToQuaternion(prop->rotation));
        prop->physicsActor->setGlobalPose(t);
    }
}

void PropManager::applyShaders(uint32_t id)
{
    StaticProp* prop = getProp(id);
    if (!prop || !prop->node) return;
    applyNodeShaders(*prop);
}

void PropManager::applyCollision(uint32_t id)
{
    StaticProp* prop = getProp(id);
    if (!prop || !prop->trimesh || !PhysicsManager::Get()) return;

    if (prop->physicsActor)
    {
        prop->physicsActor->release();
        prop->physicsActor = nullptr;
    }

    if (!prop->hasCollision) return;

    physx::PxVec3 pxPos(prop->position.X, prop->position.Y, prop->position.Z);
    physx::PxVec3 pxScale(prop->scale.X, prop->scale.Y, prop->scale.Z);
    physx::PxQuat pxRot = Math::EulerToQuaternion(prop->rotation);

    irr::scene::IMesh* cookedMesh = prop->trimesh->getMesh(0);
    if (prop->useConvexCollision)
        PhysicsManager::Get()->cookConvexMeshFromMemory(cookedMesh, prop->physicsActor, pxPos, pxRot, pxScale);
    else
        PhysicsManager::Get()->cookStaticTriangleMeshFromMemory(cookedMesh, prop->physicsActor, pxPos, pxRot, pxScale);
}

StaticProp* PropManager::getPropFromNode(irr::scene::ISceneNode* node)
{
    if (!node) return nullptr;
    for (auto& prop : m_props)
        if (prop.node == node) return &prop;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Internal — node spawning
// ---------------------------------------------------------------------------

void PropManager::spawnNode(StaticProp& prop)
{
    if (prop.mesh.empty()) return;

    auto* smgr   = RenderManager::Get()->sceneManager();
    auto* driver = RenderManager::Get()->driver();

    // Load mesh (mirrors RenderSystem::setMeshComponentData logic)
    const std::string ext = Utility::FileExtensionFromPath(prop.mesh);
    if (ext == "glb" || ext == "gltf")
    {
        // fastgltf backend; fall back to Assimp if it can't load the file
        prop.trimesh = RenderManager::Get()->gltf()->getMesh(prop.mesh.c_str());
        if (!prop.trimesh)
            prop.trimesh = RenderManager::Get()->assimp()->getMesh(prop.mesh.c_str());
    }
    else if (ext == "fbx")
    {
        try
        {
            prop.trimesh = RenderManager::Get()->assimp()->getMesh(prop.mesh.c_str());
        }
        catch (const std::exception& e)
        {
            spdlog::error("PropManager::spawnNode: Assimp failed to load '{}': {}", prop.mesh, e.what());
            return;
        }
    }
    else
        prop.trimesh = smgr->getMesh(prop.mesh.c_str());

    if (!prop.trimesh)
    {
        spdlog::error("PropManager::spawnNode: failed to load mesh '{}'", prop.mesh);
        return;
    }

    // Leave node ID at -1 (Irrlicht default) — props are selected by node pointer,
    // not by numeric ID, so they never conflict with ECS entity IDs.
    prop.node = smgr->addAnimatedMeshSceneNode(prop.trimesh);
    if (!prop.node) return;

    // Apply textures
    for (auto i = 0U; i < prop.textures.size(); ++i)
        prop.node->setMaterialTexture(i, driver->getTexture(prop.textures[i].c_str()));

    // Material flags — same as RenderSystem
    prop.node->setMaterialFlag(EMF_ANTI_ALIASING,       true);
    prop.node->setMaterialFlag(EMF_GOURAUD_SHADING,     false);
    prop.node->setMaterialFlag(EMF_LIGHTING,             true);
    prop.node->setMaterialFlag(EMF_NORMALIZE_NORMALS,    true);
    prop.node->setMaterialFlag(EMF_BILINEAR_FILTER,      false);
    prop.node->setMaterialFlag(EMF_TRILINEAR_FILTER,     true);
    prop.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER,   true);

	for (auto i = 0; i < prop.node->getMaterialCount(); i++)
	{
		prop.node->getMaterial(i).Shininess = prop.roughness;
		prop.node->getMaterial(i).SpecularColor.setAlpha(static_cast<irr::u32>(prop.emissive * 255.0f));
	}

    // Apply transform
    prop.node->setPosition(prop.position);
    prop.node->setRotation(prop.rotation);
    prop.node->setScale(prop.scale);
    prop.node->updateAbsolutePosition();

    // Triangle selector — required for ray-cast picking in the editor.
    // Props are never animated so we always use the mesh-exact selector.
    auto* selector = smgr->createTriangleSelector(prop.node);
    if (selector)
    {
        prop.node->setTriangleSelector(selector);
        selector->drop();
    }

    applyNodeShaders(prop);

    if (prop.hasCollision && prop.trimesh && PhysicsManager::Get())
    {
        physx::PxVec3 pxPos(prop.position.X, prop.position.Y, prop.position.Z);
        physx::PxVec3 pxScale(prop.scale.X, prop.scale.Y, prop.scale.Z);
        physx::PxQuat pxRot = Math::EulerToQuaternion(prop.rotation);

        irr::scene::IMesh* cookedMesh = prop.trimesh->getMesh(0);
        if (prop.useConvexCollision)
            PhysicsManager::Get()->cookConvexMeshFromMemory(cookedMesh, prop.physicsActor, pxPos, pxRot, pxScale);
        else
            PhysicsManager::Get()->cookStaticTriangleMeshFromMemory(cookedMesh, prop.physicsActor, pxPos, pxRot, pxScale);
    }
}

void PropManager::applyNodeShaders(StaticProp& prop)
{
    if (!prop.node) return;

    const irr::s32 defaultMat = static_cast<irr::s32>(ShaderMaterialManager::get(prop.defaultShader));

    for (u32 b = 0; b < prop.node->getMaterialCount(); ++b)
        prop.node->getMaterial(b).MaterialType = static_cast<E_MATERIAL_TYPE>(defaultMat);

    for (auto& ovr : prop.bufferShaderOverrides)
    {
        if (ovr.bufferIndex < prop.node->getMaterialCount())
        {
            const irr::s32 ovrMat = static_cast<irr::s32>(ShaderMaterialManager::get(ovr.shaderName));
            prop.node->getMaterial(ovr.bufferIndex).MaterialType = static_cast<E_MATERIAL_TYPE>(ovrMat);
        }
    }

    prop.node->setMaterialFlag(EMF_BACK_FACE_CULLING, !prop.isVegetation);

    // Apply generic MaterialTypeParams to all buffers; shader-specific blocks below may override.
    for (u32 b = 0; b < prop.node->getMaterialCount(); ++b)
        for (u32 p = 0; p < 8; ++p)
            prop.node->getMaterial(b).MaterialTypeParams[p] = prop.materialTypeParams[p];

    if (prop.defaultShader == "crystal" || prop.defaultShader == "phong_perpixel_transparent")
    {
        const irr::s32 crystalMat = static_cast<irr::s32>(
            ShaderMaterialManager::get("phong_perpixel_transparent"));

        for (u32 b = 0; b < prop.node->getMaterialCount(); ++b)
        {
            auto& m = prop.node->getMaterial(b);
            m.MaterialType              = static_cast<E_MATERIAL_TYPE>(crystalMat);
            m.MaterialTypeParams[2]     = prop.crystalGlow;    // uFresnelStrength
            m.MaterialTypeParams[3]     = prop.crystalShimmer; // uFresnelPower
            m.DiffuseColor.setAlpha(static_cast<irr::u32>(prop.crystalTransparency * 255.0f));
            m.BackfaceCulling           = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Per-frame update — vegetation distance culling
// ---------------------------------------------------------------------------

void PropManager::update()
{
    auto* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
    if (!cam) return;

    const vector3df camPos = cam->getAbsolutePosition();

    for (auto& prop : m_props)
    {
        if (!prop.node || prop.cullDistance <= 0.0f) continue;
        if (m_batchedMode && prop.isVegetation) continue;
        const float dist = (prop.node->getAbsolutePosition() - camPos).getLength();
        prop.node->setVisible(dist <= prop.cullDistance);
    }
}

// ---------------------------------------------------------------------------
// Vegetation batching
// ---------------------------------------------------------------------------

void PropManager::rebuildVegetationBatches()
{
    for (auto& batch : m_vegetationBatches)
        if (batch.node) batch.node->remove();
    m_vegetationBatches.clear();

    std::map<std::string, std::vector<StaticProp*>> groups;
    for (auto& prop : m_props)
        if (prop.isVegetation && prop.trimesh && prop.node)
            groups[prop.mesh].push_back(&prop);

    if (groups.empty()) return;

    auto* smgr   = RenderManager::Get()->sceneManager();
    auto* driver = RenderManager::Get()->driver();

    // Maximum vertices per merged mesh buffer before splitting into a new node.
    // u16 indices cap out at 65535; 60000 gives a small safety margin.
    const u32 MAX_VERTS = 60000u;

    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        const std::string& meshPath            = it->first;
        std::vector<StaticProp*>& propList     = it->second;

        if (propList.empty()) continue;

        StaticProp* first = propList[0];
        IMesh* templateMesh = first->trimesh->getMesh(0);
        if (!templateMesh) continue;

        const u32 bufCount = templateMesh->getMeshBufferCount();

        // Total vertices per prop instance (sum across all buffers)
        u32 vertsPerProp = 0;
        for (u32 b = 0; b < bufCount; ++b)
            vertsPerProp += templateMesh->getMeshBuffer(b)->getVertexCount();
        if (vertsPerProp == 0) continue;

        const u32 propsPerBatch = std::max(1u, MAX_VERTS / vertsPerProp);
        const u32 totalProps    = static_cast<u32>(propList.size());

        // Per-buffer joint correction: IrrAssimp stores each Assimp node's transform as a
        // joint LocalMatrix/GlobalMatrix and links mesh buffers via AttachedMeshes.  Irrlicht
        // applies this during skinMesh(), but the raw IMeshBuffer vertices are in Assimp-local
        // space.  We must bake the joint GlobalMatrix into the batch vertices so the result
        // matches what IAnimatedMeshSceneNode would render.
        std::vector<matrix4> bufferJointMatrix(bufCount); // identity by default
        irr::scene::ISkinnedMesh* skinned =
            dynamic_cast<irr::scene::ISkinnedMesh*>(first->trimesh);
        if (skinned)
        {
            const irr::core::array<irr::scene::ISkinnedMesh::SJoint*>& joints =
                skinned->getAllJoints();
            for (u32 j = 0; j < joints.size(); ++j)
            {
                for (u32 k = 0; k < joints[j]->AttachedMeshes.size(); ++k)
                {
                    const u32 bufIdx = joints[j]->AttachedMeshes[k];
                    if (bufIdx < bufCount)
                        bufferJointMatrix[bufIdx] = joints[j]->GlobalMatrix;
                }
            }
        }

        for (u32 start = 0; start < totalProps; start += propsPerBatch)
        {
            const u32 end = start + propsPerBatch < totalProps ? start + propsPerBatch : totalProps;

            SMesh* mergedMesh = new SMesh();

            for (u32 b = 0; b < bufCount; ++b)
            {
                IMeshBuffer* tmpl      = templateMesh->getMeshBuffer(b);
                const u32 srcVCount    = tmpl->getVertexCount();
                const u32 srcICount    = tmpl->getIndexCount();
                const u16* srcIndices  = static_cast<const u16*>(tmpl->getIndices());

                if (tmpl->getVertexType() == EVT_2TCOORDS)
                {
                    const S3DVertex2TCoords* srcVerts =
                        static_cast<const S3DVertex2TCoords*>(tmpl->getVertices());

                    SMeshBufferLightMap* out = new SMeshBufferLightMap();
                    out->Vertices.reallocate(srcVCount * (end - start));
                    out->Indices.reallocate(srcICount  * (end - start));

                    for (u32 pi = start; pi < end; ++pi)
                    {
                        matrix4 fullTransform = propList[pi]->node->getAbsoluteTransformation();
                        fullTransform *= bufferJointMatrix[b];
                        const u16 base = static_cast<u16>(out->Vertices.size());
                        for (u32 vi = 0; vi < srcVCount; ++vi)
                        {
                            S3DVertex2TCoords v = srcVerts[vi];
                            fullTransform.transformVect(v.Pos);
                            fullTransform.rotateVect(v.Normal);
                            out->Vertices.push_back(v);
                        }
                        for (u32 ii = 0; ii < srcICount; ++ii)
                            out->Indices.push_back(base + srcIndices[ii]);
                    }
                    out->recalculateBoundingBox();
                    mergedMesh->addMeshBuffer(out);
                    out->drop();
                }
                else
                {
                    // EVT_STANDARD or EVT_TANGENTS (downcast tangents to standard)
                    SMeshBuffer* out = new SMeshBuffer();
                    out->Vertices.reallocate(srcVCount * (end - start));
                    out->Indices.reallocate(srcICount  * (end - start));

                    for (u32 pi = start; pi < end; ++pi)
                    {
                        matrix4 fullTransform = propList[pi]->node->getAbsoluteTransformation();
                        fullTransform *= bufferJointMatrix[b];
                        const u16 base = static_cast<u16>(out->Vertices.size());

                        if (tmpl->getVertexType() == EVT_TANGENTS)
                        {
                            const S3DVertexTangents* srcVerts =
                                static_cast<const S3DVertexTangents*>(tmpl->getVertices());
                            for (u32 vi = 0; vi < srcVCount; ++vi)
                            {
                                S3DVertex v;
                                v.Pos     = srcVerts[vi].Pos;    fullTransform.transformVect(v.Pos);
                                v.Normal  = srcVerts[vi].Normal; fullTransform.rotateVect(v.Normal);
                                v.Color   = srcVerts[vi].Color;
                                v.TCoords = srcVerts[vi].TCoords;
                                out->Vertices.push_back(v);
                            }
                        }
                        else
                        {
                            const S3DVertex* srcVerts =
                                static_cast<const S3DVertex*>(tmpl->getVertices());
                            for (u32 vi = 0; vi < srcVCount; ++vi)
                            {
                                S3DVertex v = srcVerts[vi];
                                fullTransform.transformVect(v.Pos);
                                fullTransform.rotateVect(v.Normal);
                                out->Vertices.push_back(v);
                            }
                        }
                        for (u32 ii = 0; ii < srcICount; ++ii)
                            out->Indices.push_back(base + srcIndices[ii]);
                    }
                    out->recalculateBoundingBox();
                    mergedMesh->addMeshBuffer(out);
                    out->drop();
                }
            }

            mergedMesh->recalculateBoundingBox();

            IMeshSceneNode* batchNode = smgr->addMeshSceneNode(mergedMesh);
            mergedMesh->drop();

            if (!batchNode) continue;

            batchNode->setMaterialFlag(EMF_ANTI_ALIASING,     true);
            batchNode->setMaterialFlag(EMF_GOURAUD_SHADING,   false);
            batchNode->setMaterialFlag(EMF_LIGHTING,           true);
            batchNode->setMaterialFlag(EMF_NORMALIZE_NORMALS,  true);
            batchNode->setMaterialFlag(EMF_BILINEAR_FILTER,    false);
            batchNode->setMaterialFlag(EMF_TRILINEAR_FILTER,   true);
            batchNode->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
            batchNode->setMaterialFlag(EMF_BACK_FACE_CULLING,  false);

            // Copy textures from the template mesh buffer materials.  Assimp-loaded meshes
            // embed textures directly in the mesh buffer's SMaterial — prop.textures is
            // only populated for meshes that have their textures set explicitly in the editor.
            for (u32 b = 0; b < batchNode->getMaterialCount() && b < bufCount; ++b)
            {
                const video::SMaterial& tmplMat = templateMesh->getMeshBuffer(b)->getMaterial();
                for (u32 t = 0; t < video::MATERIAL_MAX_TEXTURES; ++t)
                    batchNode->getMaterial(b).setTexture(t, tmplMat.TextureLayer[t].Texture);
            }
            // Also apply any explicitly-assigned textures (slot 0 override from the editor)
            for (u32 i = 0; i < static_cast<u32>(first->textures.size()); ++i)
                batchNode->setMaterialTexture(i, driver->getTexture(first->textures[i].c_str()));

            const s32 defaultMat = static_cast<s32>(ShaderMaterialManager::get(first->defaultShader));
            for (u32 b = 0; b < batchNode->getMaterialCount(); ++b)
            {
                batchNode->getMaterial(b).MaterialType = static_cast<E_MATERIAL_TYPE>(defaultMat);
                batchNode->getMaterial(b).Shininess = 0.f;
                batchNode->getMaterial(b).SpecularColor.setAlpha(0);
            }
            for (auto& ovr : first->bufferShaderOverrides)
            {
                if (ovr.bufferIndex < batchNode->getMaterialCount())
                {
                    const s32 ovrMat = static_cast<s32>(ShaderMaterialManager::get(ovr.shaderName));
                    batchNode->getMaterial(ovr.bufferIndex).MaterialType =
                        static_cast<E_MATERIAL_TYPE>(ovrMat);
                }
            }

            VegetationBatch batch;
            batch.meshPath = meshPath;
            batch.node     = batchNode;
            m_vegetationBatches.push_back(std::move(batch));
        }
    }

    spdlog::info("PropManager: built {} vegetation batch node(s)", m_vegetationBatches.size());
}

void PropManager::setVegetationBatched(bool batched)
{
    m_batchedMode = batched;

    if (batched)
    {
        rebuildVegetationBatches();
        for (auto& prop : m_props)
            if (prop.isVegetation && prop.node)
                prop.node->setVisible(false);
    }
    else
    {
        for (auto& batch : m_vegetationBatches)
            if (batch.node) batch.node->remove();
        m_vegetationBatches.clear();

        for (auto& prop : m_props)
            if (prop.isVegetation && prop.node)
                prop.node->setVisible(true);
    }
}

// ---------------------------------------------------------------------------
// Lightmap integration
// ---------------------------------------------------------------------------

std::vector<PropManager::BakeTarget> PropManager::getBakeTargets()
{
    std::vector<BakeTarget> targets;
    for (auto& prop : m_props)
    {
        if (!prop.receivesLightmap || !prop.node || !prop.trimesh) continue;
        BakeTarget bt;
        bt.prop           = &prop;
        bt.worldTransform = prop.node->getAbsoluteTransformation();
        targets.push_back(bt);
    }
    return targets;
}

std::vector<PropManager::PropLightmapFiles> PropManager::collectPropLightmapFiles(
    irr::video::IVideoDriver* driver,
    const std::string&        tempDir)
{
    std::vector<PropLightmapFiles> result;

    for (auto& prop : m_props)
    {
        if (!prop.lightmap || !prop.lightmap->texture || !prop.node) continue;

        PropLightmapFiles plf;
        plf.propId = prop.id;

        // --- PNG: write to temp file, read back ---
        {
            ITexture* tex = prop.lightmap->texture;
            IImage* img = driver->createImage(
                tex,
                core::position2di(0, 0),
                tex->getSize());

            if (img)
            {
                const std::string tmpPath =
                    tempDir + "/prop_lightmap_" + std::to_string(prop.id) + "_tmp.png";

                driver->writeImageToFile(img, tmpPath.c_str());
                img->drop();

                std::ifstream ifs(tmpPath, std::ios::binary);
                if (ifs)
                {
                    plf.pngBytes.assign(
                        std::istreambuf_iterator<char>(ifs),
                        std::istreambuf_iterator<char>());
                }
                std::remove(tmpPath.c_str());
            }
        }

        // --- UVMesh: extract from EVT_2TCOORDS mesh (LMSV binary format) ---
        {
            IMesh* mesh = prop.node->getMesh() ? prop.node->getMesh()->getMesh(0) : nullptr;
            if (!mesh)
            {
                spdlog::warn("PropManager::collectPropLightmapFiles: no mesh on prop {}", prop.id);
                continue;
            }

            const u32 bufCount = mesh->getMeshBufferCount();

            // BinWriter inline helper
            std::vector<uint8_t> buf;
            auto write32 = [&](uint32_t v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
            };
            auto write16 = [&](uint16_t v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2);
            };
            auto writeF = [&](float v) {
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
            };

            write32(0x4C4D5356); // magic 'LMSV'
            write32(1);          // version
            write32(bufCount);

            for (u32 b = 0; b < bufCount; b++)
            {
                IMeshBuffer* mbuf = mesh->getMeshBuffer(b);
                const u32 vertCount  = mbuf->getVertexCount();
                const u32 indexCount = mbuf->getIndexCount();

                write32(vertCount);
                write32(indexCount);

                const S3DVertex2TCoords* verts =
                    static_cast<const S3DVertex2TCoords*>(mbuf->getVertices());

                for (u32 i = 0; i < vertCount; i++)
                {
                    writeF(verts[i].Pos.X);      writeF(verts[i].Pos.Y);      writeF(verts[i].Pos.Z);
                    writeF(verts[i].Normal.X);   writeF(verts[i].Normal.Y);   writeF(verts[i].Normal.Z);
                    writeF(verts[i].TCoords.X);  writeF(verts[i].TCoords.Y);
                    writeF(verts[i].TCoords2.X); writeF(verts[i].TCoords2.Y);
                }

                const u16* indices = static_cast<const u16*>(mbuf->getIndices());
                for (u32 i = 0; i < indexCount; i++)
                    write16(static_cast<uint16_t>(indices[i]));
            }

            plf.uvmeshBytes = std::move(buf);
        }

        if (!plf.pngBytes.empty() && !plf.uvmeshBytes.empty())
        {
            spdlog::info("PropManager::collectPropLightmapFiles: collected prop {} ({} PNG, {} uvmesh bytes)",
                prop.id, plf.pngBytes.size(), plf.uvmeshBytes.size());
            result.push_back(std::move(plf));
        }
        else
        {
            spdlog::warn("PropManager::collectPropLightmapFiles: skipping prop {} — data empty", prop.id);
        }
    }

    return result;
}

void PropManager::loadPropLightmaps(irr::io::IFileSystem* fs, irr::video::IVideoDriver* driver)
{
    for (auto& prop : m_props)
    {
        if (!prop.node) continue;

        const std::string uvmeshName = "prop_lightmap_" + std::to_string(prop.id) + ".uvmesh";
        irr::io::IReadFile* uvFile = fs->createAndOpenFile(uvmeshName.c_str());
        if (!uvFile) continue; // no lightmap baked for this prop

        std::vector<uint8_t> uvData(static_cast<size_t>(uvFile->getSize()));
        uvFile->read(uvData.data(), static_cast<u32>(uvData.size()));
        uvFile->drop();

        // Parse LMSV binary (same format as LightmapBaker)
        const uint8_t* ptr = uvData.data();
        const uint8_t* end = ptr + uvData.size();

        auto readU32 = [&]() -> uint32_t { uint32_t v; memcpy(&v, ptr, 4); ptr += 4; return v; };
        auto readU16 = [&]() -> uint16_t { uint16_t v; memcpy(&v, ptr, 2); ptr += 2; return v; };
        auto readF32 = [&]() -> float    { float    v; memcpy(&v, ptr, 4); ptr += 4; return v; };

        if (readU32() != 0x4C4D5356)
        {
            spdlog::warn("PropManager::loadPropLightmaps: bad magic for prop {}, skipping", prop.id);
            continue;
        }
        /*version =*/ readU32();
        const uint32_t bufCount = readU32();

        MeshLightmap* lm = new MeshLightmap();
        lm->buffers.resize(bufCount);

        for (uint32_t b = 0; b < bufCount && ptr < end; b++)
        {
            const uint32_t vertCount  = readU32();
            const uint32_t indexCount = readU32();

            lm->buffers[b].vertices.resize(vertCount);
            lm->buffers[b].indices.resize(indexCount);

            for (uint32_t vi = 0; vi < vertCount && ptr < end; vi++)
            {
                MeshLightmap::Vertex& v = lm->buffers[b].vertices[vi];
                v.objPos.X    = readF32(); v.objPos.Y    = readF32(); v.objPos.Z    = readF32();
                v.objNormal.X = readF32(); v.objNormal.Y = readF32(); v.objNormal.Z = readF32();
                v.uv1.X = readF32(); v.uv1.Y = readF32();
                v.uv2.X = readF32(); v.uv2.Y = readF32();
            }
            for (uint32_t ii = 0; ii < indexCount && ptr < end; ii++)
                lm->buffers[b].indices[ii] = readU16();
        }

        const std::string pngName = "prop_lightmap_" + std::to_string(prop.id) + ".png";
        lm->texture = driver->getTexture(pngName.c_str());
        if (!lm->texture)
        {
            spdlog::warn("PropManager::loadPropLightmaps: texture '{}' not found for prop {}", pngName, prop.id);
            delete lm;
            continue;
        }
        lm->width  = lm->texture->getSize().Width;
        lm->height = lm->texture->getSize().Height;

        // Remove old lightmap if any
        if (prop.lightmap)
        {
            if (prop.lightmap->texture && prop.node)
            {
                for (u32 m = 0; m < prop.node->getMaterialCount(); m++)
                {
                    if (prop.node->getMaterial(m).TextureLayer[1].Texture == prop.lightmap->texture)
                        prop.node->getMaterial(m).setTexture(1, nullptr);
                }
                driver->removeTexture(prop.lightmap->texture);
            }
            delete prop.lightmap;
        }
        prop.lightmap = lm;

        LightmapBaker::applyLightmapUVsToNode(*prop.lightmap, prop.node);

        // Free CPU vertex data — now on GPU
        { std::vector<MeshLightmap::BufferData>().swap(prop.lightmap->buffers); }

        spdlog::info("PropManager::loadPropLightmaps: applied lightmap to prop {}", prop.id);
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void PropManager::serialize(cereal::XMLOutputArchive& ar)
{
    ar.setNextName("props");
    ar.startNode();
    ar(cereal::make_nvp("count", static_cast<uint32_t>(m_props.size())));
    for (auto& prop : m_props)
        ar(cereal::make_nvp("prop", prop));
    ar.finishNode();
}

void PropManager::deserialize(cereal::XMLInputArchive& ar)
{
    clearAll();

    try
    {
        ar.startNode(); // <props>

        uint32_t count = 0;
        ar(cereal::make_nvp("count", count));

        m_props.reserve(count);
        uint32_t maxId = 0;

        for (uint32_t i = 0; i < count; i++)
        {
            StaticProp prop;
            ar(cereal::make_nvp("prop", prop));

            if (prop.id > maxId)
                maxId = prop.id;

            spawnNode(prop);
            m_props.push_back(std::move(prop));
        }

        // Restore m_nextId so new props get IDs after existing ones
        m_nextId = maxId + 1;

        ar.finishNode(); // </props>
    }
    catch (cereal::Exception& ex)
    {
        spdlog::warn("PropManager::deserialize: failed to read props — {}", ex.what());
    }
}

// ---------------------------------------------------------------------------
// Texture painting
// ---------------------------------------------------------------------------

void PropManager::enablePainting(uint32_t id, int splatResolution)
{
    StaticProp* prop = getProp(id);
    if (!prop || !prop->node || !prop->trimesh) return;

    auto* driver = RenderManager::Get()->driver();
    auto* smgr   = RenderManager::Get()->sceneManager();

    // Ensure UV1 exists — generate it via the lightmap unwrapper if absent.
    bool hasUV1 = false;
    if (prop->trimesh->getMeshBufferCount() > 0)
        hasUV1 = (prop->trimesh->getMeshBuffer(0)->getVertexType() == EVT_2TCOORDS);

    if (!hasUV1)
    {
        MeshLightmap lm;
        LightmapBaker::BakeSettings settings;
        settings.resolution = static_cast<uint32_t>(splatResolution);

        if (!LightmapBaker::unwrapMesh(prop->trimesh,
                                       prop->node->getAbsoluteTransformation(),
                                       settings, lm))
        {
            spdlog::error("PropManager::enablePainting: UV unwrap failed for prop {}", id);
            return;
        }

        LightmapBaker::applyLightmapUVsToNode(lm, prop->node);

        // Rebuild triangle selector — mesh buffers were replaced by applyLightmapUVsToNode.
        auto* sel = smgr->createTriangleSelector(prop->node);
        if (sel) { prop->node->setTriangleSelector(sel); sel->drop(); }
    }

    // Create blank splat map.
    prop->splatMap = new SplatMap();
    prop->splatMap->savePath = "content/texture/splat/prop_" + std::to_string(id) + ".png";
    prop->splatMap->create(splatResolution, driver,
                           "splat_prop_" + std::to_string(id));

    prop->splatMapPath  = prop->splatMap->savePath;
    prop->isPaintable   = true;
    prop->defaultShader = "terrain_blend";
    applyNodeShaders(*prop);

    // Bind splat map to slot 2, detail textures to slots 3-6.
    for (u32 b = 0; b < prop->node->getMaterialCount(); ++b)
    {
        prop->node->getMaterial(b).setTexture(2, prop->splatMap->gpuTexture);
        for (int d = 0; d < 4; ++d)
        {
            if (!prop->detailTextures[d].empty())
                prop->node->getMaterial(b).setTexture(3 + d,
                    driver->getTexture(prop->detailTextures[d].c_str()));
        }
    }
}

void PropManager::saveSplatMaps()
{
    auto* driver = RenderManager::Get()->driver();
    for (auto& prop : m_props)
    {
        if (prop.isPaintable && prop.splatMap)
            prop.splatMap->save(driver);
    }
}

void PropManager::loadSplatMaps()
{
    auto* driver = RenderManager::Get()->driver();
    for (auto& prop : m_props)
    {
        if (!prop.isPaintable || prop.splatMapPath.empty() || !prop.node) continue;

        if (!prop.splatMap)
            prop.splatMap = new SplatMap();

        if (!prop.splatMap->load(prop.splatMapPath, driver))
        {
            spdlog::warn("PropManager::loadSplatMaps: could not load '{}'", prop.splatMapPath);
            continue;
        }

        prop.splatMap->create(prop.splatMap->resolution, driver,
                              "splat_prop_" + std::to_string(prop.id));
        prop.splatMap->uploadToGPU();

        for (u32 b = 0; b < prop.node->getMaterialCount(); ++b)
        {
            prop.node->getMaterial(b).setTexture(2, prop.splatMap->gpuTexture);
            for (int d = 0; d < 4; ++d)
            {
                if (!prop.detailTextures[d].empty())
                    prop.node->getMaterial(b).setTexture(3 + d,
                        driver->getTexture(prop.detailTextures[d].c_str()));
            }
        }
    }
}
