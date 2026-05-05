#include "Editor/TexturePainter.h"

#include <cmath>

#include <IMGUI/imgui.h>

#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/SplatMap.h"
#include "Engine/Prop/PropManager.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

static const float k_pi = 3.14159265f;

void TexturePainter::update(float dt)
{
    if (!active || !targetProp || !targetProp->node || !targetProp->splatMap) return;
    if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive()) return;

    // Build camera ray from cursor position.
    auto* smgr   = RenderManager::Get()->sceneManager();
    auto* cam    = smgr->getActiveCamera();
    auto  pos2d  = RenderManager::Get()->device()->getCursorControl()->getPosition();
    auto  ray    = smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(pos2d, cam);

    auto result = RenderManager::Get()->raycastWorldPosition(ray.start, ray.end, true);

    if (result.hit && result.node == targetProp->node)
    {
        m_lastHit    = result.point;
        m_hasLastHit = true;

        if (InputManager::Get()->isMouseButtonPressed(0, true))
            paintAt(result);
    }
    else
    {
        m_hasLastHit = false;
    }
}

void TexturePainter::drawBrush()
{
    if (!m_hasLastHit) return;

    const SColor color   = SColor(255, 100, 180, 255);
    const int    segs    = 24;
    const float  step    = 2.0f * k_pi / static_cast<float>(segs);

    for (int i = 0; i < segs; ++i)
    {
        float a0 = i       * step;
        float a1 = (i + 1) * step;
        vector3df p0(m_lastHit.X + cosf(a0) * radius, m_lastHit.Y + 0.05f, m_lastHit.Z + sinf(a0) * radius);
        vector3df p1(m_lastHit.X + cosf(a1) * radius, m_lastHit.Y + 0.05f, m_lastHit.Z + sinf(a1) * radius);
        RenderManager::Get()->renderLine3DOverlay(Line3D(line3df(p0, p1), color));
    }
}

void TexturePainter::paintAt(const RaycastResultData& hit)
{
    vector2df uv;
    if (!getTriangleUV1(targetProp->node, hit.tri, hit.point, uv)) return;

    SplatMap* splat = targetProp->splatMap;
    int cx = static_cast<int>(uv.X * splat->resolution);
    int cy = static_cast<int>(uv.Y * splat->resolution);

    // Convert world-space radius to pixel-space radius using UV density estimate.
    // We approximate: if the mesh has unit UV coverage over its bounding box,
    // 1 world unit ≈ resolution / bboxSize pixels. Use the node's scale as a proxy.
    float worldScale = targetProp->node->getAbsoluteTransformation().getScale().X;
    float pixelRadius = radius * splat->resolution / (worldScale > 0.f ? worldScale * 10.f : 10.f);
    pixelRadius = std::max(pixelRadius, 2.0f);

    splat->paint(cx, cy, activeChannel, strength, pixelRadius);
    splat->uploadToGPU();

    // Keep the GPU texture bound on the node material (slot 2).
    for (u32 b = 0; b < targetProp->node->getMaterialCount(); ++b)
        targetProp->node->getMaterial(b).setTexture(2, splat->gpuTexture);
}

bool TexturePainter::getTriangleUV1(
    IAnimatedMeshSceneNode* node,
    const triangle3df&      worldTri,
    vector3df               hitPoint,
    vector2df&              outUV) const
{
    if (!node || !node->getMesh()) return false;

    const matrix4& world = node->getAbsoluteTransformation();
    IMesh*         mesh  = node->getMesh()->getMesh(0);

    // Target centroid — triangles are uniquely identified by centroid in world space.
    vector3df hitCenter = (worldTri.pointA + worldTri.pointB + worldTri.pointC) / 3.0f;

    for (u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
    {
        IMeshBuffer* buf = mesh->getMeshBuffer(b);
        if (buf->getVertexType() != EVT_2TCOORDS) continue;

        auto*       verts    = static_cast<S3DVertex2TCoords*>(buf->getVertices());
        u32         idxCount = buf->getIndexCount();
        bool        is32bit  = (buf->getIndexType() == EIT_32BIT);
        const void* rawIdx   = buf->getIndices();

        auto idx = [&](u32 i) -> u32
        {
            return is32bit
                ? reinterpret_cast<const u32*>(rawIdx)[i]
                : reinterpret_cast<const u16*>(rawIdx)[i];
        };

        for (u32 i = 0; i + 2 < idxCount; i += 3)
        {
            vector3df wa, wb, wc;
            world.transformVect(wa, verts[idx(i+0)].Pos);
            world.transformVect(wb, verts[idx(i+1)].Pos);
            world.transformVect(wc, verts[idx(i+2)].Pos);

            vector3df triCenter = (wa + wb + wc) / 3.0f;
            if (triCenter.getDistanceFrom(hitCenter) > 0.05f) continue;

            // Barycentric interpolation of UV1 at hitPoint.
            vector3df v0 = wb - wa;
            vector3df v1 = wc - wa;
            vector3df v2 = hitPoint - wa;

            float d00 = v0.dotProduct(v0);
            float d01 = v0.dotProduct(v1);
            float d11 = v1.dotProduct(v1);
            float d20 = v2.dotProduct(v0);
            float d21 = v2.dotProduct(v1);
            float denom = d00 * d11 - d01 * d01;
            if (fabsf(denom) < 1e-10f) continue;

            float bv = (d11 * d20 - d01 * d21) / denom;
            float bw = (d00 * d21 - d01 * d20) / denom;
            float bu = 1.0f - bv - bw;

            vector2df uv1a = verts[idx(i+0)].TCoords2;
            vector2df uv1b = verts[idx(i+1)].TCoords2;
            vector2df uv1c = verts[idx(i+2)].TCoords2;

            outUV = uv1a * bu + uv1b * bv + uv1c * bw;
            return true;
        }
    }
    return false;
}
