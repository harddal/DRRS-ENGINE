#pragma once

#include "PlayerData.h"

#include "anax/anax.hpp"

#include "Engine/Resource/MaterialBuilder.h"   // E_MANAGED_MATERIAL

#include "HUDController.h"
#include "InteractionController.h"
#include "InventoryController.h"
#include "WeaponController.h"

namespace physx { class PxRigidDynamic; }

//#define DISABLE_HUD_AND_INV
//#define DISPLAY_PLAYER_STATS

// DEBUG: Temporary, need to implement into controller class
#define PLAYER_HEIGHT 1.75f

class PlayerController
{
public:
    PlayerController() {}

    void init();
    void update(float dt);
    void updateUI(float dt);
    void destroy();

    void pause();
    void resume();
	
    void playFootStepSound(anax::Entity& player, int _time, int _delay);
    void playJumpSound(anax::Entity& player);

    bool isMoving() { return m_isMoving; }

	void lockPlayer(bool lock = true) { m_locked = lock; }
	void unlockPlayer() { m_locked = false; }
	bool isPlayerLocked() { return m_locked; }

	int getCurrentHealth() { return g_PlayerData.currentHealth; }
	int getMaxHealth() { return WorldManager::Get()->managerSystem()->getEntityByName("player").getComponent<DamageReceiverComponent>().threshold; }

	void setIsWeaponEquipped(bool is = true) { g_PlayerData.isWeaponEquipped = is; }

	PlayerData getPlayerData() { return g_PlayerData; }
	void loadPlayerData(std::string file);
	void savePlayerData(std::string file);

	bool isSwimming() { return m_isSwimming; }

	// Surface the player is standing on, resolved once per grounded frame.
	E_MANAGED_MATERIAL groundMaterial() const { return m_groundMaterial; }
	void setIsSwimming(bool swimming = true) { m_isSwimming = swimming; }
	bool isHeadUnderWater() { return m_isHeadUnderWater; }
	void setIHeadUnderWater(bool under = true) { m_isHeadUnderWater = under; }
	// Set per frame by GameplaySystem's ladder-brush volume test (swim pattern)
	bool isOnLadder() { return m_isOnLadder; }
	void setOnLadder(bool on = true) { m_isOnLadder = on; }
	bool isBlocking() { return m_isBlocking; }
	void setIsBlocking (bool blocking = true) { m_isBlocking = blocking; }

	HUDController         *hudController() { return &m_hudController; }
	InteractionController *interactionController() { return &m_interactionController; }
	InventoryController   *inventoryController() { return &m_inventoryController; }
	WeaponController      *weaponController() { return &m_weaponController; }

protected:
	irr::core::vector3df Accelerate(irr::core::vector3df& accelDir, irr::core::vector3df& prevVelocity, float accelerate, float max_velocity, float dt);
	irr::core::vector3df MoveGround(irr::core::vector3df& accelDir, irr::core::vector3df& prevVelocity, float friction, float ground_accelerate, float max_velocity_ground, float dt);
	irr::core::vector3df MoveAir(irr::core::vector3df& accelDir, irr::core::vector3df& prevVelocity, float air_accelerate, float max_velocity_air, float dt);

private:
    bool m_locked = false, m_isMoving = false, m_firstUpdate = true, m_isSwimming = false, m_isHeadUnderWater = false, m_isBlocking = false, m_isSliding = false;

	// Ladder climbing (CONTENT_LADDER brush volumes)
	bool m_isOnLadder = false;
	int  m_ladderIgnoreUntil = 0;   // jump-detach grace: the volume test re-grabs
	                                // every frame, so detaching needs a timer

	// Clean camera orientation (no FX baked in) — driven by mouse input,
	// used as the authoritative base so recoil offsets decay back to the
	// player's actual aim point rather than permanently shifting it.
	float m_cameraPitch = 0.0f;  // X rotation (up/down), degrees
	float m_cameraYaw   = 0.0f;  // Y rotation (left/right), degrees

	// Death state
	bool m_isDead = false;
	float m_deathTime = 0.0f;

	// Crouch lerp state
	float m_currentCrouchHeight = 2.0f;  // Current capsule height
	float m_targetCrouchHeight = 2.0f;   // Target capsule height
	float m_crouchLerpSpeed = 8.0f;      // Speed of the transition

	// Jump feel improvements
	int m_lastGroundedTime = 0;          // Last time player was on ground (for coyote time)
	int m_lastJumpInputTime = -1000;     // Last time jump was pressed (for buffering)
	bool m_isJumping = false;            // Currently in a jump (for jump cut)
	bool m_jumpConsumed = false;         // Whether current jump press has been used
	
	// Jump tuning constants
	const float m_jumpCutMultiplier = 0.5f;      // Velocity reduction on early release
	const int m_coyoteTime = 110;               // Grace period for jumping after leaving ground (ms)
	const int m_jumpBufferTime = 120;           // Time to buffer jump inputs (ms)

	// Fall damage tuning (landing speed, units/sec — see m_lastAirVelocityY)
	const float m_fallDamageMinSpeed = 15.0f;    // below this: no damage, just landing bob
	const float m_fallDamageMaxSpeed = 25.0f;    // at/above this: instant kill
	const float m_fallDamageMax      = 30.0f;    // falloff damage just under the instakill speed

	// Dodge mechanic (Unreal-style double-tap)
	int   m_lastForwardTapTime  = -1000;
	int   m_lastBackwardTapTime = -1000;
	int   m_lastLeftTapTime     = -1000;
	int   m_lastRightTapTime    = -1000;
	bool  m_prevForwardPressed  = false;
	bool  m_prevBackwardPressed = false;
	bool  m_prevLeftPressed     = false;
	bool  m_prevRightPressed    = false;
	int   m_lastDodgeTime       = -10000;
	bool  m_isDodging           = false;
	int   m_dodgeStartTime      = 0;
	const int   m_dodgeDoubleTapWindow = 500;
	const int   m_dodgeCooldown        = 1000;
	const float m_dodgeSpeed           = 10.0f; // units/sec burst (~1.2x sprint speed)
	const int   m_dodgeDuration        = 500;

	float m_lastAirVelocityY = 0.0f;
	irr::core::vector3df m_lastSlideWorldAccel = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	irr::core::vector3df m_lastSlopeNormal    = irr::core::vector3df(0.0f, 1.0f, 0.0f);

	// Ground surface under the player, resolved by the single downward raycast in
	// the grounded movement branch. Friction, footsteps, jump sounds and the debug
	// overlay all read this instead of casting their own identical rays — each of
	// those rays walks the whole scene graph, so four of them per frame was by far
	// the most expensive thing about surface-material lookup.
	//
	// Refreshed every grounded frame; while airborne it holds the last surface
	// stood on, which is what the jump sound wants anyway.
	E_MANAGED_MATERIAL m_groundMaterial = MAT_INVALID;

	// Footstep sound timing
	int m_lastFootstepTime = 0;                  // Last time a footstep sound played
	const int m_minFootstepInterval = 400;       // Minimum time between footsteps (ms)

	// Ground detection state (used for reliable footstep triggering)
	int m_airborneFrameCount = 0;                // Consecutive frames without ground collision

	// Movement/physics state — deliberately members, not function-local statics.
	// GameManager fully destroys and reconstructs PlayerController on every
	// game<->editor toggle, but a local static in update() would survive that
	// swap, carrying stale state (e.g. velocity from falling into the void)
	// into the new instance. m_smoothedY/m_lastCCTPosition are seeded from the
	// live CCT the first time update() runs on this instance (see m_firstUpdate).
	irr::core::vector3df m_playerVelocity  = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	irr::core::vector3df m_lastCCTPosition = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	float m_smoothedY  = 0.0f;
	bool  m_isCrouched = false;
	physx::PxRigidDynamic* m_groundMover = nullptr;
	irr::core::vector3df   m_groundMoverLastPos = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	HUDController         m_hudController;
	InteractionController m_interactionController;
	InventoryController   m_inventoryController;
	WeaponController      m_weaponController;

};

extern std::unique_ptr<PlayerController> g_PlayerController;
extern PlayerData g_PlayerData;
