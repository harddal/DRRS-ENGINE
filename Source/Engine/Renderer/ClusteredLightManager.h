#pragma once

// Clustered forward lighting.
//
// Once per frame (before any scene pass) update() gathers every visible light
// from the scene graph, assigns each to the view-space froxels its radius
// touches, and uploads three data textures the fragment shaders read with
// texelFetch:
//
//   unit 8  tLightData     RGBA32F, 4 texels per light:
//                            t0 = view-space pos.xyz, radius
//                            t1 = color.rgb, cos(innerCone)
//                            t2 = view-space dir.xyz, cos(outerCone)
//                                 (cosOuter = -2 marks a non-spot light)
//                            t3 = shadow slot index (-1 = casts no shadow), 0, 0, 0
//   unit 9  tClusterGrid   RGBA32F (GRID_X*GRID_Y wide, GRID_Z tall):
//                            x = ix + iy*GRID_X, y = iz
//                            texel = (offset, count, 0, 0) into the index list
//   unit 10 tLightIndices  R32F (INDEX_TEX_W x INDEX_TEX_H): light indices
//
// Units 8-10 sit above Irrlicht's 8 material texture slots, so its state
// tracker never touches them — bind once per frame and they stay bound.
//
// This replaces the per-draw-call gatherClosestLights() path (which sorted all
// scene lights on the CPU for every mesh buffer and capped lighting at 8 lights
// per object). The old path remains as a fallback: shaders branch on
// uUseClusters, and OnSetConstants still runs the gather when clustering is
// disabled or a preview scene is being rendered.

#include <vector>

#include "irrlicht.h"

class ClusteredLightManager
{
public:
    static constexpr int GRID_X = 16;
    static constexpr int GRID_Y = 8;
    static constexpr int GRID_Z = 24;
    static constexpr int MAX_LIGHTS  = 256;
    static constexpr int TEXELS_PER_LIGHT = 4;
    static constexpr int INDEX_TEX_W = 256;
    static constexpr int INDEX_TEX_H = 64;
    static constexpr int MAX_INDICES = INDEX_TEX_W * INDEX_TEX_H;
    static constexpr int MAX_SHADOW_CASTERS = 4;   // atlas quadrants

    ~ClusteredLightManager();

    // Build cluster data for the current frame and upload to texture units 8-10.
    // Call once per frame before the shadow / main scene passes. viewportSize is
    // the size of the target the main scene pass renders into (the scene RTT).
    void update(irr::video::IVideoDriver* driver,
                irr::scene::ISceneManager* sceneManager,
                const irr::core::dimension2du& viewportSize);

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const  { return m_enabled; }

    // Far end of the cluster z range. Lights and fragments beyond it fall out
    // of the last slice; raise it for very large outdoor scenes.
    void  setClusterFar(float f) { m_clusterFar = f; }
    float getClusterFar() const  { return m_clusterFar; }

    // Per-frame uniform values for the shaders (valid after update()).
    float tileWidthPx()  const { return m_tileW; }
    float tileHeightPx() const { return m_tileH; }
    float sliceScale()   const { return m_sliceScale; }
    float sliceBias()    const { return m_sliceBias; }

    int lightCount() const { return m_lightCount; }

    // Spotlights assigned a shadow atlas slot this frame (nearest to camera
    // first). RenderManager::drawShadowPass renders exactly these; their slot
    // index is already written into the light data texture (texel 3).
    int shadowCasterCount() const { return m_shadowCasterCount; }
    irr::scene::ILightSceneNode* shadowCaster(int slot) const { return m_shadowCasters[slot]; }

private:
    void createTextures();

    bool  m_enabled     = true;
    bool  m_texturesOk  = false;
    float m_clusterFar  = 1000.0f;

    float m_tileW = 1.0f, m_tileH = 1.0f;
    float m_sliceScale = 1.0f, m_sliceBias = 0.0f;
    int   m_lightCount = 0;
    bool  m_indexOverflowWarned = false;

    unsigned int m_texLightData = 0;
    unsigned int m_texGrid      = 0;
    unsigned int m_texIndices   = 0;

    int m_shadowCasterCount = 0;
    irr::scene::ILightSceneNode* m_shadowCasters[MAX_SHADOW_CASTERS] = {};

    // Persistent scratch buffers (cleared, not freed, each frame).
    std::vector<std::vector<irr::u16>> m_clusterLights; // per-cluster light indices
    std::vector<float>                 m_lightTexData;  // MAX_LIGHTS * 3 * 4 floats
    std::vector<float>                 m_gridTexData;   // clusters * 4 floats
    std::vector<float>                 m_indexTexData;  // MAX_INDICES floats
};
