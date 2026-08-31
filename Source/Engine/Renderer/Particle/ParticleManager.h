#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

#include <irrlicht.h>

#include "SPK.h"

#include "Engine/Renderer/Spark/include/RenderingAPIs/Irrlicht/SPK_IRR_DEF.h"

struct ParticleSystemDef;

class ParticleManager
{
public:
    static ParticleManager* Get() { return s_Instance; }

    ParticleManager();
    ~ParticleManager();

	static void destroy() {
		delete s_Instance;
		s_Instance = nullptr;
	}

    // Load and cache a named effect from a .psys file.
    // Safe to call multiple times with the same name — second call is a no-op.
    // Returns false if the file cannot be loaded.
    bool precache(const std::string& name, const std::string& path);

    // Runtime toggle between the GLSL soft-particle material and the original
    // fixed-function EMT_ONETEXTURE_BLEND on every cached template and live
    // instance. Renderers are shared between templates and their SPK_Copy
    // clones, so the flip is immediate and idempotent.
    void setSoftParticleShader(bool enabled);

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

    // Override the emission direction on all spheric/straight emitters in an active instance.
    // No-op if handle is 0 or not found. Call immediately after spawn() for directional effects.
    void setEmitterDirection(uint32_t handle, const irr::core::vector3df& dir);

    // Scale zone dimensions, renderer quad sizes, and emitter forces for an effect.
    // Point-renderer effects are unaffected (no runtime size getter in SPARK).
    //
    // IMPORTANT — this is EFFECT-WIDE, not per-instance. SPARK's Group copy
    // constructor copies the renderer, model and emitter POINTERS, so every
    // SPK_Copy clone shares them with the base template and with each other.
    // There is no such thing as scaling one live clone: the newest call wins for
    // every instance of that effect, and the scale persists on the template.
    //
    // 'scale' is therefore ABSOLUTE and measured against the effect as authored
    // in its .psys — 1.0 restores the file's values, 2.0 is twice them. Passing
    // the same scale twice is a no-op.
    //
    // (This used to multiply from current values, which — because the renderer is
    // shared — compounded on every single spawn. A wound spraying at 0.5 shrank
    // the shared blood template to 0.5^n and the effect vanished after a handful
    // of shots. Explosions scaled by TurretBehavior decayed the same way.)
    void setScale(uint32_t handle, float scale);

private:
    struct BaseEffect
    {
        SPK::SPK_ID baseID    = SPK::NO_ID;
        float       updateRate = 1.0f;

        // Scale currently baked into this effect's shared renderer/zones/emitters,
        // relative to the authored .psys. Lets setScale() convert an absolute
        // request into the delta it has to multiply by, so repeated calls cannot
        // compound. Reset to 1.0 whenever the effect is re-loaded from file.
        float appliedScale = 1.0f;
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
