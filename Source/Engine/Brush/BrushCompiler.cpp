#include "Engine/Brush/BrushCompiler.h"

#include <cmath>
#include <map>
#include <string>

#include <spdlog/spdlog.h>

#include "Engine/Brush/BrushManager.h"
#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

namespace
{
    // Split buffers before the u16 index range is exhausted.
    const u32 kMaxBufferVerts = 65000;

    struct Bucket
    {
        std::vector<CMeshBuffer<S3DVertex2TCoords>*> buffers;
    };

    CMeshBuffer<S3DVertex2TCoords>* newBucketBuffer(const std::string& materialName,
                                                    const std::string& shaderName)
    {
        auto* buf = new CMeshBuffer<S3DVertex2TCoords>();
        SMaterial& mat = buf->getMaterial();

        auto* driver = RenderManager::Get()->driver();
        if (!materialName.empty())
        {
            auto* tex = driver->getTexture(materialName.c_str());

            // Bare extensionless names (old texture-browser selections) —
            // resolve via the standard texture path convention
            if (!tex && materialName.find('/') == std::string::npos &&
                        materialName.find('\\') == std::string::npos)
            {
                const std::string resolved = "content/texture/" + materialName + ".png";
                tex = driver->getTexture(resolved.c_str());
            }

            if (tex)
                mat.setTexture(0, tex);
            else
                mat.setTexture(0, driver->getTexture("content/texture/color/magenta.png"));
        }

        const std::string& sn = shaderName.empty() ? std::string("phong_perpixel") : shaderName;
        const E_MATERIAL_TYPE resolved = ShaderMaterialManager::get(sn);
        if (resolved != EMT_SOLID)
            mat.MaterialType = resolved;

        // Same PBR defaults as RenderSystem: full roughness, dielectric.
        mat.Shininess = 0.0f;
        mat.SpecularColor.setAlpha(0);
        mat.Lighting = true;
        mat.GouraudShading = false;
        mat.NormalizeNormals = true;
        return buf;
    }
}

namespace BrushCompiler
{

SMesh* buildChunkMesh(const std::vector<const Brush*>& brushes, MeshFilter filter, irr::u32 clipMask)
{
    auto* driver = RenderManager::Get()->driver();

    // Bucket faces by material + shader
    std::map<std::string, Bucket> buckets;

    for (const Brush* brush : brushes)
    {
        if (!brush || !brush->geometryValid)
            continue;

        if (filter == MeshFilter::CLIP)
        {
            if (!brush->isToolBrush() || brush->clipMask() != clipMask)
                continue;
        }
        else if (brush->isToolBrush() != (filter == MeshFilter::TOOL))
        {
            continue;
        }

        for (const BrushFace& face : brush->faces)
        {
            if ((face.flags & FACE_NODRAW) && filter == MeshFilter::RENDER)
                continue;
            if (face.loop.size() < 3)
                continue;

            const std::string key = face.materialName + "\n" + face.shaderName;
            Bucket& bucket = buckets[key];

            if (bucket.buffers.empty() ||
                bucket.buffers.back()->Vertices.size() + face.loop.size() > kMaxBufferVerts)
            {
                bucket.buffers.push_back(newBucketBuffer(face.materialName, face.shaderName));
            }
            auto* buf = bucket.buffers.back();

            // scale = world units per texture repeat; shift = world units
            const float su = (std::fabs(face.scaleU) > 1e-6f) ? face.scaleU : 1.0f;
            const float sv = (std::fabs(face.scaleV) > 1e-6f) ? face.scaleV : 1.0f;

            const u16 base = static_cast<u16>(buf->Vertices.size());
            // Overlay meshes carry their transparency in the vertex alpha —
            // EMT_TRANSPARENT_VERTEX_ALPHA reads it, so opaque tool textures
            // still render see-through.
            const SColor white = (filter == MeshFilter::TOOL)
                ? SColor(160, 255, 255, 255)
                : SColor(255, 255, 255, 255);

            for (u16 idx : face.loop)
            {
                const vector3df& p = brush->verts[idx];
                S3DVertex2TCoords v;
                v.Pos    = p;
                v.Normal = face.plane.Normal;
                v.Color  = white;
                v.TCoords.X = (p.dotProduct(face.uAxis) + face.shiftU) / su;
                v.TCoords.Y = (p.dotProduct(face.vAxis) + face.shiftV) / sv;
                v.TCoords2  = v.TCoords;    // placeholder until a lightmap bake
                buf->Vertices.push_back(v);
            }

            // Fan triangulation.  Loops are angle-sorted so the cross-product
            // normal of (v0, vi, vi+1) equals the outward plane normal — the
            // engine's front-face convention (see DecalManager's cube).
            for (size_t i = 1; i + 1 < face.loop.size(); i++)
            {
                const vector3df& a = brush->verts[face.loop[0]];
                const vector3df& b = brush->verts[face.loop[i]];
                const vector3df& c = brush->verts[face.loop[i + 1]];
                // Skip slivers from collinear loop points (kept in loops to
                // avoid T-junction cracks between adjacent faces).
                if ((b - a).crossProduct(c - a).getLengthSQ() < 1e-10f)
                    continue;
                buf->Indices.push_back(base);
                buf->Indices.push_back(base + static_cast<u16>(i));
                buf->Indices.push_back(base + static_cast<u16>(i + 1));
            }
        }
    }

    SMesh* mesh = nullptr;
    for (auto& pair : buckets)
    {
        for (auto* buf : pair.second.buffers)
        {
            if (buf->Indices.size() == 0)
            {
                buf->drop();
                continue;
            }
            if (!mesh)
                mesh = new SMesh();
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
    }

    if (mesh)
        mesh->recalculateBoundingBox();
    return mesh;
}

void updateChunkNode(BrushChunk& chunk, const std::vector<const Brush*>& brushes)
{
    auto* smgr = RenderManager::Get()->sceneManager();

    // ---- runtime render node (structural brushes) ----
    // No early return on an empty mesh: a chunk holding only tool brushes has
    // no render node but still needs its overlay node updated below.
    SMesh* mesh = buildChunkMesh(brushes, MeshFilter::RENDER);
    if (!mesh)
    {
        if (chunk.node)
        {
            chunk.node->remove();
            chunk.node = nullptr;
        }
    }
    else
    {
        auto* animMesh = new SAnimatedMesh(mesh);
        mesh->drop();
        animMesh->setHardwareMappingHint(EHM_STATIC);

        if (!chunk.node)
        {
            // Chunk verts are world-space; node stays at the origin.  Node ID is
            // left at the Irrlicht default (-1) — chunks are resolved by node
            // pointer via BrushManager::getChunkFromNode, never by entity ID.
            chunk.node = smgr->addAnimatedMeshSceneNode(animMesh);
        }
        else
        {
            chunk.node->setMesh(animMesh);
        }
        animMesh->drop();

        if (!chunk.node)
        {
            spdlog::error("BrushCompiler: failed to create chunk scene node");
        }
        else
        {
            // setMesh re-copies materials from the buffers; re-apply uniform flags.
            chunk.node->setMaterialFlag(EMF_ANTI_ALIASING,     true);
            chunk.node->setMaterialFlag(EMF_GOURAUD_SHADING,   false);
            chunk.node->setMaterialFlag(EMF_LIGHTING,          true);
            chunk.node->setMaterialFlag(EMF_NORMALIZE_NORMALS, true);
            chunk.node->setMaterialFlag(EMF_BACK_FACE_CULLING, true);
            chunk.node->setMaterialFlag(EMF_BILINEAR_FILTER,   false);
            chunk.node->setMaterialFlag(EMF_TRILINEAR_FILTER,  true);
            chunk.node->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);
        }
    }

    // ---- editor-only tool-brush overlay ----
    // Sibling node, never a child of chunk.node (the lightmap baker extracts
    // meshes from chunk.node).  Registered as a debug node: excluded from the
    // shadow / depth pre-pass / SSAO / bloom passes and drawn post-tonemap,
    // where transparent mesh buffers are drawn explicitly.  No triangle
    // selector, so cursor picking ignores it.
    if (!Engine::Get()->isEditorMode())
        return;

    SMesh* toolMesh = buildChunkMesh(brushes, MeshFilter::TOOL);
    if (!toolMesh)
    {
        if (chunk.toolNode)
        {
            // Unregister BEFORE remove — RenderManager walks m_debugNodes
            // every frame and a stale pointer there is a crash.
            RenderManager::Get()->unregisterDebugNode(chunk.toolNode);
            chunk.toolNode->remove();
            chunk.toolNode = nullptr;
        }
        return;
    }

    auto* toolAnim = new SAnimatedMesh(toolMesh);
    toolMesh->drop();
    toolAnim->setHardwareMappingHint(EHM_STATIC);

    if (!chunk.toolNode)
    {
        chunk.toolNode = smgr->addAnimatedMeshSceneNode(toolAnim);
        if (chunk.toolNode)
            RenderManager::Get()->registerDebugNode(chunk.toolNode);
    }
    else
    {
        chunk.toolNode->setMesh(toolAnim);
    }
    toolAnim->drop();

    if (!chunk.toolNode)
    {
        spdlog::error("BrushCompiler: failed to create tool overlay node");
        return;
    }

    // Trigger-zone overlay recipe (RenderSystem), except vertex alpha carries
    // the transparency so opaque tool textures still render see-through.
    chunk.toolNode->setMaterialType(EMT_TRANSPARENT_VERTEX_ALPHA);
    chunk.toolNode->setMaterialFlag(EMF_ZWRITE_ENABLE,     false);
    chunk.toolNode->setMaterialFlag(EMF_BACK_FACE_CULLING, false);
    chunk.toolNode->setMaterialFlag(EMF_LIGHTING,          false);
    chunk.toolNode->setVisible(BrushManager::Get()->isToolOverlayVisible());
}

} // namespace BrushCompiler
