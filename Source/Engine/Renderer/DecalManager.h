#pragma once

// Screen-space decals (bullet holes, scorch marks, blood).
//
// Each decal is an oriented box volume. A fragment shader reconstructs the
// world position of whatever the geometry prepass (raw unit 14) says is at
// each covered pixel, rejects positions outside the box, and projects the
// decal texture onto the surviving surface — so decals wrap corners, stairs,
// and curved geometry with no per-triangle work and no z-fighting.
//
// Boxes render with depth test off (back faces, so the camera can sit inside
// the volume) using MODULATE blending (dst * src) onto the tonemapped
// backbuffer after the post-process chain: unaffected pixels multiply by 1.0,
// decaled pixels darken toward the decal color. Like SSAO, applying this
// pre-tonemap would be crushed by the filmic curve at high exposure.

#include <string>
#include <vector>

#include "irrlicht.h"

// Decals compete for one fixed budget. Without a class, a single gib burst —
// which spends a dozen slots at once, plus more on every bounce — silently
// evicts every bullet hole in the level. Eviction prefers the oldest decal of
// the SAME class, so gore and gunfire each keep their own share.
enum DECAL_CLASS
{
    DECAL_GENERIC = 0,   // bullet holes, scorch marks
    DECAL_BLOOD   = 1
};

class DecalShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    // Set per decal by DecalManager::render() before each draw.
    irr::core::matrix4   decalInv;   // world -> decal local (unit cube)
    irr::core::vector3df dirView;    // projection direction in view space
    float                fade = 1.0f;

    void OnSetMaterial(const irr::video::SMaterial&) override {}
    void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override;
};

class DecalManager
{
public:
    // Requires RenderManager to be fully constructed (compiles the decal shader).
    DecalManager();
    ~DecalManager();

    // Project a decal onto the geometry around 'pos'. The decal projects along
    // -normal; size is the world-space edge length of the projection box.
    void spawn(const irr::core::vector3df& pos,
               const irr::core::vector3df& normal,
               float size,
               const std::string& texture,
               float lifetime = 60.0f,
               DECAL_CLASS cls = DECAL_GENERIC);

    void update(float dt);
    void render();   // call after the post-process chain (tonemapped backbuffer)

    void clear() { m_decals.clear(); }

private:
    struct Decal
    {
        irr::core::matrix4    world;     // decal local (unit cube) -> world
        irr::core::matrix4    invWorld;  // world -> decal local
        irr::core::vector3df  dir;       // world-space projection direction (-normal)
        irr::video::ITexture* texture;
        float life, maxLife;
        DECAL_CLASS cls;
    };

    // Raised from 64 for the gore system. A single gib kill spends roughly 80
    // between the radial fan, the pool and the gibs' own bounce trails, so this
    // holds about four of them before blood starts evicting blood.
    //
    // Each decal is one draw call (a box, depth test off, sampling the prepass),
    // so this is the first number to lower if the frame rate dips in a fight.
    static constexpr size_t MAX_DECALS   = 320;   // oldest of its class evicted beyond this
    static constexpr float  FADE_WINDOW  = 5.0f;  // fade-out seconds at end of life

    std::vector<Decal>        m_decals;
    irr::scene::SMesh*        m_boxMesh  = nullptr;
    irr::s32                  m_material = -1;
    DecalShaderCallback*      m_callback = nullptr;
};
