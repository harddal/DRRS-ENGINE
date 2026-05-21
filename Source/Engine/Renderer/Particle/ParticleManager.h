#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

#include <irrlicht.h>
#include "SPK.h"

struct ParticleSystemDef;

class ParticleManager
{
public:
    static ParticleManager* Get() { return s_Instance; }

    ParticleManager();
    ~ParticleManager();

    // Load and cache a named effect from a .psys file.
    // Safe to call multiple times with the same name — second call is a no-op.
    // Returns false if the file cannot be loaded.
    bool precache(const std::string& name, const std::string& path);

    // Register an in-memory def directly, bypassing file I/O.
    // Always overwrites any existing entry under 'name' (intended for "_preview").
    void precacheFromDef(const std::string& name, const ParticleSystemDef& def);

    // Spawn a live clone of a precached effect at world position pos.
    // loop = true: when the system sleeps it is re-initialized instead of destroyed.
    // Returns a non-zero handle usable with destroy(); 0 = failure.
    uint32_t spawn(const std::string& name, const SPK::Vector3D& pos, bool loop = false);

    // Force-destroy a specific active instance before it naturally finishes.
    void destroy(uint32_t handle);

    // Tick all active instances. Called from WorldManager::update() each game frame.
    void update(float dt);

    // Destroy all active instances AND all cached base systems.
    // Call on scene unload / game shutdown.
    void clear();

    // Reposition an active looping effect. No-op if handle is 0 or not found. O(1) lookup.
    void setPosition(uint32_t handle, const irr::core::vector3df& pos);

private:
    struct BaseEffect
    {
        SPK::SPK_ID baseID    = SPK::NO_ID;
        float       updateRate = 1.0f;
    };

    struct ActiveInstance
    {
        SPK::System* system     = nullptr;
        float        updateRate = 1.0f;
        uint32_t     handle     = 0;
        bool         loop       = false;
        std::string  effectName;
    };

    std::unordered_map<std::string, BaseEffect>  m_effects;
    std::unordered_map<uint32_t, ActiveInstance> m_instances;
    uint32_t                                     m_nextHandle = 1;

    static ParticleManager* s_Instance;
};
