#include "Editor/VegetationPainter.h"

#include <cmath>

#include <IMGUI/imgui.h>

#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Prop/PropManager.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

static const float k_pi = 3.14159265f;

VegetationPainter::VegetationPainter()
    : m_rng(std::random_device{}())
{
}

float VegetationPainter::randomFloat(float lo, float hi)
{
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(m_rng);
}

const PaletteEntry* VegetationPainter::pickEntry()
{
    // Collect enabled entries, pick one at random
    std::vector<const PaletteEntry*> enabled;
    for (const auto& e : m_palette)
        if (e.enabled) enabled.push_back(&e);

    if (enabled.empty()) return nullptr;

    std::uniform_int_distribution<int> dist(0, static_cast<int>(enabled.size()) - 1);
    return enabled[dist(m_rng)];
}

void VegetationPainter::update(float dt)
{
    if (!m_active || !PropManager::Get()) return;

    // Suppress painting when the cursor is over or interacting with any ImGui widget.
    // WantCaptureMouse can't be used here — it's always true due to the fullscreen
    // background editor window. IsAnyItemHovered/Active correctly distinguishes
    // actual UI panels from the 3D viewport.
    if (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive()) return;

    // Update brush position every frame regardless of painting
    {
        vector3df hit;
        if (RenderManager::Get()->getNodeFromCursorPosition(hit))
        {
            m_lastHit    = hit;
            m_hasLastHit = true;
        }
    }

    m_erasing = InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LSHIFT);

    if (!InputManager::Get()->isMouseButtonPressed(0, true)) // left button held
    {
        // Reset to interval so the very first frame of the next press paints immediately
        m_timer = m_interval;
        return;
    }

    m_timer += dt;
    if (m_timer < m_interval) return;
    m_timer = 0.0f;

    if (m_hasLastHit)
    {
        if (m_erasing)
            eraseAt(m_lastHit);
        else
            paintAt(m_lastHit);
    }
}

void VegetationPainter::paintAt(vector3df center)
{
    if (!pickEntry()) return;

    // Temporarily remove triangle selectors from all existing vegetation props
    // so downward raycasts hit the underlying scene geometry, not the plants.
    // Selectors are restored before any new props are placed.
    using SelectorSave = std::pair<irr::scene::ISceneNode*, irr::scene::ITriangleSelector*>;
    std::vector<SelectorSave> saved;
    for (const auto& p : PropManager::Get()->getAllProps())
    {
        if (!p.isVegetation || !p.node) continue;
        irr::scene::ITriangleSelector* sel = p.node->getTriangleSelector();
        if (!sel) continue;
        sel->grab();
        p.node->setTriangleSelector(nullptr);
        saved.push_back({ p.node, sel });
    }

    // Compute all scatter positions before placing any props
    struct Placement { std::string meshPath; vector3df pos; float rotY; float scale; };
    std::vector<Placement> placements;

    int attempts = 0;
    const int maxAttempts = m_countPerTick * 10;

    while (static_cast<int>(placements.size()) < m_countPerTick && attempts < maxAttempts)
    {
        ++attempts;

        float ox = randomFloat(-m_radius, m_radius);
        float oz = randomFloat(-m_radius, m_radius);
        if (ox * ox + oz * oz > m_radius * m_radius) continue;

        vector3df rayStart(center.X + ox, center.Y + 500.f, center.Z + oz);
        vector3df rayEnd  (center.X + ox, center.Y - 500.f, center.Z + oz);
        vector3df hitPoint;

        if (!RenderManager::Get()->getNodeFromRaycast(rayStart, rayEnd, hitPoint)) continue;

        const PaletteEntry* chosen = pickEntry();
        if (!chosen) continue;

        placements.push_back({ chosen->meshPath, hitPoint,
                               randomFloat(0.f, 360.f),
                               randomFloat(m_scaleMin, m_scaleMax) });
    }

    // Restore triangle selectors before spawning so newly placed props
    // are in a consistent state
    for (auto& s : saved)
    {
        s.first->setTriangleSelector(s.second);
        s.second->drop();
    }

    for (const auto& pd : placements)
    {
        StaticProp prop;
        prop.mesh          = pd.meshPath;
        prop.position      = pd.pos;
        prop.rotation      = { 0.f, pd.rotY, 0.f };
        prop.scale         = { pd.scale, pd.scale, pd.scale };
        prop.defaultShader = m_shader;
        prop.isVegetation  = true;
        prop.cullDistance  = m_cullDistance;
        PropManager::Get()->addProp(prop);
    }
}

void VegetationPainter::eraseAt(vector3df center)
{
    auto* pm = PropManager::Get();
    const auto& props = pm->getAllProps();

    std::vector<uint32_t> toRemove;
    for (const auto& p : props)
    {
        if (!p.isVegetation) continue;
        float dx = p.position.X - center.X;
        float dz = p.position.Z - center.Z;
        if (dx * dx + dz * dz <= m_radius * m_radius)
            toRemove.push_back(p.id);
    }

    for (uint32_t id : toRemove)
        pm->removeProp(id);
}

void VegetationPainter::drawBrush()
{
    if (!m_hasLastHit) return;

    const SColor color = m_erasing ? SColor(255, 220, 80, 80) : SColor(255, 100, 220, 100);
    const int    segments = 24;
    const float  step     = 2.0f * k_pi / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i)
    {
        float a0 = i       * step;
        float a1 = (i + 1) * step;
        vector3df p0(m_lastHit.X + cosf(a0) * m_radius, m_lastHit.Y + 0.05f, m_lastHit.Z + sinf(a0) * m_radius);
        vector3df p1(m_lastHit.X + cosf(a1) * m_radius, m_lastHit.Y + 0.05f, m_lastHit.Z + sinf(a1) * m_radius);
        RenderManager::Get()->renderLine3DOverlay(Line3D(irr::core::line3df(p0, p1), color));
    }
}
