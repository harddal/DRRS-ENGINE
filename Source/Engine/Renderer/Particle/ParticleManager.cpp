#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Renderer/Particle/ParticleSystemLoader.h"
#include "Engine/Renderer/Particle/ParticleSystemDef.h"

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

void ParticleManager::precacheFromDef(const std::string& name, const ParticleSystemDef& def)
{
    auto it = m_effects.find(name);
    if (it != m_effects.end())
    {
        if (it->second.baseID != NO_ID)
            SPK_Destroy(it->second.baseID);
        m_effects.erase(it);
    }

    ParticleLoadResult result = ParticleSystemLoader::buildFromDef(def);
    if (result.baseID == NO_ID)
    {
        spdlog::error("[ParticleManager] precacheFromDef failed for '{}'", name);
        return;
    }
    m_effects[name] = { result.baseID, result.updateRate };
    spdlog::info("[ParticleManager] precacheFromDef registered '{}'", name);
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
    m_instances[handle] = inst;
    return handle;
}

void ParticleManager::destroy(uint32_t handle)
{
    auto it = m_instances.find(handle);
    if (it != m_instances.end())
    {
        SPK_Destroy(it->second.system);
        m_instances.erase(it);
    }
}

void ParticleManager::update(float dt)
{
    const float dtS = dt * 0.001f;  // dt is in ms; SPARK lifetimes are in seconds
    for (auto it = m_instances.begin(); it != m_instances.end(); )
    {
        auto& inst = it->second;
        if (!inst.system->update(dtS * inst.updateRate))
        {
            if (inst.loop)
            {
                // SPARK has no reset API — destroy and re-copy from the base template
                std::string effName = inst.effectName;

                irr::core::vector3df pos(0.0f, 0.0f, 0.0f);
                IRRSystem* dying = static_cast<IRRSystem*>(inst.system);
                if (dying) pos = dying->getAbsolutePosition();

                SPK_Destroy(inst.system);
                inst.system = nullptr;

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

                inst.system = fresh;
                ++it;
            }
            else
            {
                SPK_Destroy(inst.system);
                it = m_instances.erase(it);
            }
        }
        else
        {
            ++it;
        }
    }
}

void ParticleManager::clear()
{
    for (auto& pair : m_instances)
        SPK_Destroy(pair.second.system);
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

void ParticleManager::setPosition(uint32_t handle, const irr::core::vector3df& pos)
{
    if (handle == 0) return;
    auto it = m_instances.find(handle);
    if (it == m_instances.end()) return;

    auto& inst = it->second;
    IRRSystem* irrSystem = static_cast<IRRSystem*>(inst.system);
    if (irrSystem)
    {
        irrSystem->setPosition(pos);
        irrSystem->updateAbsolutePosition();
    }
    else
    {
        inst.system->setTransformPosition(Vector3D(pos.X, pos.Y, pos.Z));
        inst.system->updateTransform();
    }
}
