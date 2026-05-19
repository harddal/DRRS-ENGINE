#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Renderer/Particle/ParticleSystemLoader.h"

#include <spdlog/spdlog.h>

#include "SPK.h"
#include "SPK_IRR.h"

using namespace SPK;
using namespace SPK::IRR;

ParticleManager* ParticleManager::s_Instance = nullptr;

ParticleManager::ParticleManager()
{
    if (s_Instance) { spdlog::error("[ParticleManager] Duplicate instance created"); }
    s_Instance = this;
}

ParticleManager::~ParticleManager()
{
    clear();
    s_Instance = nullptr;
}

bool ParticleManager::precache(const std::string& name, const std::string& path)
{
    if (m_effects.count(name))
        return true;

    ParticleLoadResult result = ParticleSystemLoader::load(path);
    if (result.baseID == NO_ID)
    {
        spdlog::error("[ParticleManager] precache failed for effect '{}' (path: {})", name, path);
        return false;
    }

    m_effects[name] = { result.baseID, result.updateRate };
    spdlog::info("[ParticleManager] Precached effect '{}'", name);
    return true;
}

uint32_t ParticleManager::spawn(const std::string& name, const Vector3D& pos, bool loop)
{
    auto it = m_effects.find(name);
    if (it == m_effects.end())
    {
        spdlog::error("[ParticleManager] spawn: unknown effect '{}'", name);
        return 0;
    }

    const BaseEffect& effect = it->second;

    System* system = SPK_Copy(System, effect.baseID);
    if (!system)
    {
        spdlog::error("[ParticleManager] spawn: SPK_Copy failed for effect '{}'", name);
        return 0;
    }

    IRRSystem* irrSystem = static_cast<IRRSystem*>(system);
    if (irrSystem)
    {
        irrSystem->setVisible(true);
        irrSystem->setPosition(spk2irr(pos));
        irrSystem->updateAbsolutePosition();
    }
    else
    {
        system->setTransformPosition(pos);
        system->updateTransform();
    }

    uint32_t handle = m_nextHandle++;
    ActiveInstance inst;
    inst.system     = system;
    inst.updateRate = effect.updateRate;
    inst.handle     = handle;
    inst.loop       = loop;
    inst.effectName = name;
    m_instances.push_back(inst);
    return handle;
}

void ParticleManager::destroy(uint32_t handle)
{
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it)
    {
        if (it->handle == handle)
        {
            SPK_Destroy(it->system);
            m_instances.erase(it);
            return;
        }
    }
}

void ParticleManager::update(float dt)
{
    auto it = m_instances.begin();
    while (it != m_instances.end())
    {
        if (!it->system->update(dt / it->updateRate))
        {
            if (it->loop)
            {
                // SPARK has no reset API — destroy and re-copy from the base template
                std::string effName = it->effectName;

                irr::core::vector3df pos(0.0f, 0.0f, 0.0f);
                IRRSystem* dying = static_cast<IRRSystem*>(it->system);
                if (dying) pos = dying->getAbsolutePosition();

                SPK_Destroy(it->system);
                it->system = nullptr;

                auto effIt = m_effects.find(effName);
                if (effIt == m_effects.end())
                {
                    it = m_instances.erase(it);
                    continue;
                }

                System* fresh = SPK_Copy(System, effIt->second.baseID);
                if (!fresh)
                {
                    it = m_instances.erase(it);
                    continue;
                }

                IRRSystem* irrFresh = static_cast<IRRSystem*>(fresh);
                if (irrFresh)
                {
                    irrFresh->setVisible(true);
                    irrFresh->setPosition(pos);
                    irrFresh->updateAbsolutePosition();
                }
                else
                {
                    fresh->setTransformPosition(Vector3D(pos.X, pos.Y, pos.Z));
                    fresh->updateTransform();
                }

                it->system = fresh;
            }
            else
            {
                SPK_Destroy(it->system);
                it = m_instances.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void ParticleManager::clear()
{
    for (auto& inst : m_instances)
        SPK_Destroy(inst.system);
    m_instances.clear();

    for (auto& pair : m_effects)
    {
        if (pair.second.baseID != NO_ID)
        {
            SPK_Destroy(pair.second.baseID);
            pair.second.baseID = NO_ID;
        }
    }
    m_effects.clear();
}
