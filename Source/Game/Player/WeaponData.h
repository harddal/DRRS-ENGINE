#pragma once

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

#include "Engine/World/Components.h"

#include "Engine/Resource/FilePaths.h"

#include "Game/Components/DamageReceiverComponent.h"

#define _weapon_crosshair_size 64
#define _weapon_crosshair2x_size 128
#define _weapon_crosshair3x_size 256
#define _weapon_crosshair_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair_size / 2))
#define _weapon_crosshair2x_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair2x_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair2x_size / 2))
#define _weapon_crosshair3x_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair3x_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair3x_size / 2))

enum PLAYER_WEAPON
{
	WEAP_NONE,
	WEAP_MELEE,
	WEAP_PISTOL,
	WEAP_SHOTGUN,
	WEAP_MININGLASER,
	WEAP_BOLTDRIVER,
	WEAP_MINIGUN,
	WEAP_ROCKETLAUNCHER,
	WEAP_GRENADE,
	WEAP_BIORIFLE,
	WEAP_FUELROD,
	WEAP_COUNT
};

enum AMMO_TYPE
{
	AMMO_NONE,
	AMMO_LIGHT,
	AMMO_HEAVY,
	AMMO_ENERGY,
	AMMO_ROCKET,
	AMMO_COUNT
};

struct BurnDecal
{
	irr::scene::ISceneNode* node = nullptr;
	float spawnTime = 0.0f;
	float maxLifetime = 1500.0f;
	unsigned int id = 0; // For debugging
};

struct WeaponProjectile
{
	anax::Entity entity;  // Store entity handle directly HACK: Needs to use the Entities ID not a copy, es no bueno!
	entityid id = _entity_null_value; // This is the fix ^ however not implemented NOIMP
	entityid targetId = _entity_null_value;
	bool useTracking = false;
	bool isTrackingActive = false; // Is it actively allowed to turn yet?
	float distanceTraveled = 0.0f; // Track distance for launch phase
	float trackingStartDistance = 3.0f; // Distance before tracking engages
	irr::core::vector3df velocity;
	irr::core::vector3df previousPosition;  // Position from last frame for swept collision
	float speed = 50.0f;
	float lifetime = 0.0f;
	float maxLifetime = 5000.0f; // 5 seconds
	irr::core::vector3df initialRotation;  // Rotation to apply once node is created
	bool orientationApplied = false;       // Track if rotation has been applied
	irr::scene::IParticleSystemSceneNode* trailParticles = nullptr; // Trail effect (Irrlicht, legacy)
	SPK::System* sparkSystem = nullptr; // Trail/bolt effect (SPARK)
	SoundHandle flyingSound; // Looping 3D flight sound
	bool isBouncing  = false; // If true, reflects off surfaces and detonates on timer instead of impact
	int  bounceCount = 0;    // Number of times this projectile has bounced
};

class PlayerWeapon
{
public:
	virtual void precache() = 0;
	virtual void init()     = 0;
	virtual void update()   = 0;
	virtual void persist()  = 0;
	virtual void destroy()  = 0;

	virtual void equip()    = 0;
	virtual void unequip()  = 0;
	virtual void idle()     = 0;
	virtual void move()     = 0;
	virtual void fire()     = 0;
	virtual void reload()   = 0;

	// Animated unequip support — weapons that want a transition animation override these.
	// Default: instant hide (same as unequip()).
	virtual void startUnequip() { unequip(); }
	virtual bool isUnequipping() const { return false; }

	// Composes sway + view kick and writes the viewmodel node transform once per
	// frame. Called by WeaponController after update(); weapons must not write
	// m_mesh.node position/rotation themselves or the two will fight.
	virtual void updateWeaponSway(float dt);

	// Instant viewmodel kick (position snap-back / rotation punch) recovered by an
	// exponential spring inside updateWeaponSway(). Call from fire()/swing paths.
	void addViewKick(const irr::core::vector3df& posKick, const irr::core::vector3df& rotKick);
	void resetViewKick();

	// Hit-confirmation feedback. Weapons pass the HIT_RESULT from
	// GameplaySystem::damageEntity(); HIT flashes the hitmarker + plays a tick
	// (rate-limited), KILL shows a larger red marker + kill sound. NONE is a no-op.
	static void registerHitFeedback(HIT_RESULT result);

	// Draws the fading hitmarker over the crosshair. Called once per frame by
	// WeaponController::update() — weapons never call this themselves.
	static void drawHitFeedback();

	// World point under the crosshair: camera-center ray hit, or the ray's far end.
	irr::core::vector3df getCrosshairAimPoint(float maxRange = 1000.0f);

	// Converging-aim direction: from 'origin' (typically the muzzle bone, which sits
	// below-right of screen centre) toward the crosshair aim point, so muzzle-origin
	// rays/projectiles land on the crosshair instead of running parallel to the
	// camera ray. Falls back to camera forward when the aim point is too close to
	// converge on safely.
	irr::core::vector3df getAimDirection(const irr::core::vector3df& origin, float maxRange = 1000.0f);

	// Debug accessors for the viewmodel positioning tool in WeaponController
	irr::core::vector3df& debugViewPositionOffset() { return m_viewPositionOffset; }
	irr::core::vector3df& debugViewRotationOffset() { return m_viewRotationOffset; }
	irr::scene::IAnimatedMeshSceneNode* debugViewmodelNode() { return m_mesh.node; }
	const std::string& debugWeaponName() const { return m_descriptor.name; }

protected:
	bool picked_up;

	unsigned int m_current_ammo;

	irr::core::vector3df m_viewPositionOffset, m_viewRotationOffset, m_viewScaleOffset;

	PLAYER_WEAPON m_weapon_type;
	AMMO_TYPE m_ammo_type;

	DescriptorComponent m_descriptor;
	MeshComponent m_mesh;

	// Weapon sway variables
	irr::core::vector3df m_lastCameraRotation;
	irr::core::vector3df m_swayOffset;       // smoothed sway displacement (lags camera)
	float m_swayAmount = 0.02f;
	float m_swaySmoothing = 0.1f;

	// View kick spring state (shared by all weapons)
	irr::core::vector3df m_kickPos;          // positional kick offset
	irr::core::vector3df m_kickRot;          // rotational kick offset (degrees)
	float m_kickPosRecovery = 15.0f;         // spring rate, per second
	float m_kickRotRecovery = 10.0f;         // spring rate, per second

};

// Shader callback for plasma ball animation
class PlasmaShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
	virtual void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData)
	{
		// Pass time to shader for animation
		irr::f32 time = Engine::Get()->getDeltaTime() / 1000.0f; // Convert to seconds
		services->setPixelShaderConstant("uTime", &time, 1);

		// Get depth texture from deferred renderer
		//auto* depthTexture = RenderManager::Get()->renderer()->getMRT(2);

		// Set texture samplers
		irr::s32 tex0 = 0;
		services->setPixelShaderConstant("texture1", &tex0, 1);

		irr::s32 depthTex = 1;
		services->setPixelShaderConstant("uDepthTexture", &depthTex, 1);

		// Pass screen dimensions for depth sampling
		irr::core::dimension2du screenSize = RenderManager::Get()->driver()->getScreenSize();
		irr::f32 screenDims[2] = { (irr::f32)screenSize.Width, (irr::f32)screenSize.Height };
		services->setPixelShaderConstant("uScreenSize", screenDims, 2);

		// Pass camera far plane for linear depth calculation in vertex shader
		irr::scene::ICameraSceneNode* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (cam) {
			irr::f32 farDist = cam->getFarValue();
			services->setVertexShaderConstant("CamFar", &farDist, 1);
		}
	}
};

// Shader callback for impact burn effect with time-based animation
class ImpactBurnShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
	float maxLifetime = 1500.0f; // Total lifetime in ms

	virtual void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData)
	{
		// The userData parameter is set to the material type param when we set it
		// However, Irrlicht doesn't pass this automatically, so we need another approach
		// We'll pass spawn time through a global variable that gets updated before rendering
		
		// For now, let's try reading from the driver's current transformation matrix's user data
		// Actually, the simplest approach: pass it as MaterialTypeParam and read it here
		// But we need to access the current material being rendered...
		
		// WORKAROUND: We'll use a static variable updated by the rendering code
		// This is not thread-safe but Irrlicht renders on a single thread
		static float currentSpawnTime = 0.0f;
		
		// Calculate normalized burn time (0 = just spawned, 1 = fully cooled)
		float currentTime = Engine::Get()->getCurrentTime();
		float elapsed = currentTime - currentSpawnTime;
		float burnTime = std::min(1.0f, elapsed / maxLifetime);

		services->setPixelShaderConstant("uBurnTime", &burnTime, 1);

		// Set texture samplers
		irr::s32 tex0 = 0;
		services->setPixelShaderConstant("texture1", &tex0, 1);

		irr::s32 depthTex = 1;
		services->setPixelShaderConstant("uDepthTexture", &depthTex, 1);

		// Pass screen dimensions for depth sampling
		irr::core::dimension2du screenSize = RenderManager::Get()->driver()->getScreenSize();
		irr::f32 screenDims[2] = { (irr::f32)screenSize.Width, (irr::f32)screenSize.Height };
		services->setPixelShaderConstant("uScreenSize", screenDims, 2);

		// Pass camera far plane for linear depth calculation
		irr::scene::ICameraSceneNode* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (cam) {
			irr::f32 farDist = cam->getFarValue();
			services->setVertexShaderConstant("CamFar", &farDist, 1);
		}
	}
	
	// This will be set before each decal is rendered  
	static void setCurrentSpawnTime(float time) {
		currentSpawnTime = time;
	}
	
private:
	static float currentSpawnTime;
};
