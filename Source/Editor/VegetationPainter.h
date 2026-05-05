#pragma once

#include <vector>
#include <string>
#include <random>

#include <irrlicht.h>

struct PaletteEntry
{
    std::string meshPath;
    bool        enabled = true;
};

class VegetationPainter
{
public:
    VegetationPainter();

    void update(float dt);
    void drawBrush();

    bool isActive() const { return m_active; }

    // Settings — exposed directly to the editor UI
    bool                      m_active       = false;
    float                     m_radius       = 5.0f;
    int                       m_countPerTick = 3;
    float                     m_interval     = 0.15f;   // seconds between paint bursts
    float                     m_scaleMin     = 1.0f;
    float                     m_scaleMax     = 1.0f;
    float                     m_cullDistance = 80.0f;
    std::string               m_shader       = "foliage";
    std::vector<PaletteEntry> m_palette;

private:
    float                m_timer      = 0.15f;  // pre-charged so first click paints immediately
    irr::core::vector3df m_lastHit;
    bool                 m_hasLastHit = false;
    bool                 m_erasing    = false;
    std::mt19937         m_rng;

    void               paintAt(irr::core::vector3df center);
    void               eraseAt(irr::core::vector3df center);
    const PaletteEntry* pickEntry();
    float              randomFloat(float lo, float hi);
};
