#pragma once

#include "irrlicht.h"

#include <vector>

// ---------------------------------------------------------------------------
// WeaponEffects — shared muzzle flash / tracer / shell / impact VFX.
//
// Each weapon composes one instance and configures it with a WeaponEffectsDesc
// (its "character sheet") in init(). Call muzzleFlash()/spawnTracer()/
// ejectShell()/impact() from fire paths and update(dt) from persist() so
// in-flight effects keep simulating while the weapon is holstered.
// ---------------------------------------------------------------------------

struct WeaponEffectsDesc
{
	// --- Muzzle flash (muzzleJointName = nullptr disables) ------------------
	const char* muzzleJointName = "FIRESPOT";        // "MUZZLE" on minigun
	irr::core::vector3df muzzleFallbackOffset;       // parented to weapon node if joint missing
	irr::video::SColor flashColor = irr::video::SColor(255, 255, 204, 76);
	float flashSize         = 0.8f;
	float flashSizeVariance = 0.3f;                  // ± fraction per shot
	float flashDuration     = 50.0f;                 // ms
	const char* flashTexture = "content/texture/particle/star_05.png";
	irr::video::E_MATERIAL_TYPE flashMaterial = irr::video::EMT_TRANSPARENT_ADD_COLOR;
	bool  flashLight = true;
	irr::video::SColorf lightColor = irr::video::SColorf(1.0f, 0.8f, 0.3f);
	float lightRadius = 3.0f;

	// --- Tracers (tracerPoolSize = 0 disables) ------------------------------
	int   tracerFrequency     = 3;                   // every Nth shot
	float tracerSpeed         = 400.0f;              // units/sec along the flight path
	float tracerSegmentLength = 4.0f;                // visible bright segment length
	float tracerWidth         = 0.1f;
	irr::video::SColor tracerColor = irr::video::SColor(255, 255, 200, 100);
	const char* tracerTexture = "content/texture/particle/trace_07.png";
	int   tracerPoolSize      = 16;

	// --- Shell casings (shellPoolSize = 0 or shellMesh = nullptr disables) --
	const char* shellMesh    = nullptr;              // e.g. "content/mesh/prop/shells/shellsmall.obj"
	const char* shellTexture = "content/mesh/prop/shells/shellsColor.png";
	const char* shellEjectJoint = "BRASS";           // "EJECT" on minigun; nullptr = offset mode
	irr::core::vector3df shellEjectOffset;           // offset mode: (right, up, forward) from weapon node
	float shellScale    = 1.0f;
	float shellSpeed    = 8.0f;                      // ejection speed, units/sec
	int   shellPoolSize = 64;
	const char* shellBounceSoundBase = "content/sound/prop/shell"; // playRandomized3D base path

	// --- Impacts (impactParticle = nullptr disables) -------------------------
	const char* impactParticle = "spark";            // ParticleManager effect name
	const char* impactDecal    = "content/texture/fx/bullet_hole.png"; // nullptr = no decal
	float impactDecalSize      = 0.2f;
};

class WeaponEffects
{
public:
	void init(irr::scene::IAnimatedMeshSceneNode* weaponNode, const WeaponEffectsDesc& desc);
	void destroy();          // idempotent — WeaponController re-inits on editor↔game switches
	void update(float dt);   // flash fade, tracer motion, shell physics (call from persist())

	// Show the flash with per-shot randomized size + texture roll.
	void muzzleFlash();

	// Register a shot for tracer emission; only every desc.tracerFrequency-th
	// call actually launches a segment (counter lives here).
	void spawnTracer(const irr::core::vector3df& start, const irr::core::vector3df& end);

	// Pooled fake-physics shell casing from the eject joint (or offset port).
	void ejectShell();

	// Impact particles fanned along the surface normal + optional decal.
	void impact(const irr::core::vector3df& point, const irr::core::vector3df& normal);

	// Explosion theater at 'pos': brief point-light flash, scorch decal,
	// lingering smoke psys if the "explosion_smoke" asset exists, and
	// proximity-scaled camera feedback (shake + directional shove away from
	// the blast + FOV crunch). Call from detonation sites.
	//
	// surfaceNormal: the impact surface normal when the detonation was a
	// contact hit — the scorch is projected onto that surface (walls and
	// ceilings included). Pass a zero vector for timer/airburst detonations;
	// a short downward raycast then finds the floor, or skips the decal.
	void explosionAt(const irr::core::vector3df& pos,
		irr::video::SColorf lightColor = irr::video::SColorf(1.0f, 0.75f, 0.35f),
		float lightRadius  = 9.0f,
		float shakePeak    = 3.5f,
		float shakeMaxDist = 5.0f,
		float shakeDurMs   = 300.0f,
		const irr::core::vector3df& surfaceNormal = irr::core::vector3df(0.0f, 0.0f, 0.0f));

private:
	void createFlashNodes();
	void updateFlash(float dt);
	void updateTracers(float dt);
	void updateShells(float dt);

	WeaponEffectsDesc m_desc;

	irr::scene::IAnimatedMeshSceneNode* m_weaponNode = nullptr;
	irr::scene::ISceneNode*             m_muzzleNode = nullptr; // joint, or weapon node fallback

	// Muzzle flash
	irr::scene::IBillboardSceneNode* m_flashNode  = nullptr;
	irr::scene::ILightSceneNode*     m_flashLight = nullptr;
	float m_flashTime = 1.0e9f; // elapsed ms; >= duration means hidden

	// Tracers — pooled moving segments
	struct Tracer
	{
		irr::scene::IMeshSceneNode* node = nullptr;
		irr::core::vector3df start, dir;
		float distance = 0.0f;   // total flight length
		float traveled = 0.0f;
		bool  active   = false;
	};
	std::vector<Tracer> m_tracers;
	int m_shotCounter = 0;

	// Explosion light flashes — small pool, faded out in update()
	struct ExplosionLight
	{
		irr::scene::ILightSceneNode* node = nullptr;
		irr::video::SColorf color;
		float time     = 1.0e9f;   // elapsed ms
		float duration = 150.0f;
		bool  active   = false;
	};
	std::vector<ExplosionLight> m_explosionLights;
	void updateExplosionLights(float dt);

	// Shell casings — pooled fake physics (no ECS/PhysX)
	struct Shell
	{
		irr::scene::IMeshSceneNode* node = nullptr;
		irr::core::vector3df velocity;        // units/second
		irr::core::vector3df angularVelocity; // degrees/second
		irr::core::vector3df rotation;
		float spawnTime     = 0.0f;
		bool  active        = false;
		bool  physicsActive = false;
		int   bounceCount   = 0;
	};
	std::vector<Shell> m_shells;
	float m_lastShellBounceSound = 0.0f;
};
