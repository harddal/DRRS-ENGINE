#pragma once

#include <irrlicht.h>

struct StaticProp;
struct RaycastResultData;

class TexturePainter
{
public:
    bool  active        = false;
    float radius        = 2.0f;   // world-unit brush radius
    float strength      = 0.5f;   // paint amount per tick, [0,1]
    int   activeChannel = 0;      // 0=layer A (R), 1=B (G), 2=C (B), 3=D (A)

    StaticProp* targetProp = nullptr;

    void update(float dt);
    void drawBrush();

private:
    irr::core::vector3df m_lastHit;
    bool                 m_hasLastHit = false;

    void paintAt(const RaycastResultData& hit);

    // Find UV1 at the raycast hit point by locating the matching triangle in
    // the mesh buffer and barycentric-interpolating UV1 across its vertices.
    bool getTriangleUV1(irr::scene::IAnimatedMeshSceneNode* node,
                        const irr::core::triangle3df&        worldTri,
                        irr::core::vector3df                 hitPoint,
                        irr::core::vector2df&                outUV) const;
};
