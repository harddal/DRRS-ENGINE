#include "Engine/World/Systems/ParticleSystem.h"

#include <spdlog/spdlog.h>

#include "SPK.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/ParticleComponent.h"

static std::string resolveEffectName(const std::string& name, const std::string& path)
{
    if (!name.empty()) return name;

    std::string base = path;
    const auto sl = base.find_last_of("/\\");
    if (sl != std::string::npos) base = base.substr(sl + 1);
    if (base.size() > 5 && base.substr(base.size() - 5) == ".psys")
        base = base.substr(0, base.size() - 5);
    return base;
}

void ParticleSystem::onEntityAdded(anax::Entity& entity)
{
    auto* pm = ParticleManager::Get();
    if (!pm) return;

    auto& pc = entity.getComponent<ParticleComponent>();
    auto& tc = entity.getComponent<TransformComponent>();

    pc.handle = 0;

    if (pc.effectPath.empty())
        return;

    const std::string name = resolveEffectName(pc.effectName, pc.effectPath);

    if (!pm->precache(name, pc.effectPath))
    {
        spdlog::warn("[ParticleSystem] precache failed for '{}'", pc.effectPath);
        return;
    }

    irr::core::vector3df worldPos(0.0f, 0.0f, 0.0f);
    if (tc.node)
        worldPos = tc.node->getAbsolutePosition();
    else
        worldPos = tc.position;

    pc.handle = pm->spawn(name, SPK::Vector3D(worldPos.X, worldPos.Y, worldPos.Z), pc.loop);

    if (pc.handle == 0)
        spdlog::warn("[ParticleSystem] spawn failed for effect '{}'", name);
}

void ParticleSystem::onEntityRemoved(anax::Entity& entity)
{
    auto* pm = ParticleManager::Get();
    auto& pc = entity.getComponent<ParticleComponent>();

    if (pc.handle != 0 && pm)
    {
        pm->destroy(pc.handle);
        pc.handle = 0;
    }
}

void ParticleSystem::update(irr::f32 /*dt*/)
{
    auto* pm = ParticleManager::Get();
    if (!pm) return;

    for (auto& entity : getEntities())
    {
        auto& pc = entity.getComponent<ParticleComponent>();

        if (!pc.loop || pc.handle == 0)
            continue;

        auto& tc = entity.getComponent<TransformComponent>();
        irr::core::vector3df worldPos(0.0f, 0.0f, 0.0f);
        if (tc.node)
            worldPos = tc.node->getAbsolutePosition();
        else
            worldPos = tc.position;

        pm->setPosition(pc.handle, worldPos);
    }
}
