#pragma once

#include "PlayerData.h"

#include "anax/anax.hpp"

#include "HUDController.h"
#include "InteractionController.h"
#include "InventoryController.h"
#include "WeaponController.h"

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
	void setIsSwimming(bool swimming = true) { m_isSwimming = swimming; }
	bool isHeadUnderWater() { return m_isHeadUnderWater; }
	void setIHeadUnderWater(bool under = true) { m_isHeadUnderWater = under; }
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
	const int m_coyoteTime = 50;                // Grace period for jumping after leaving ground (ms)
	const int m_jumpBufferTime = 50;            // Time to buffer jump inputs (ms)
	const float m_maxJumpHorizontalSpeed = 1.5f; // Maximum horizontal speed during jump (prevents excessive forward velocity)

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
	
	// Stamina system
	float m_currentStamina = 100.0f;             // Current stamina (0-100) // Need to serialize this!!!
	int m_lastStaminaConsumedTime = 0;           // Last time stamina was consumed
	const float m_maxStamina = 100.0f;           // Maximum stamina
	const float m_jumpStaminaCost = 0.0f;       // Stamina cost per jump
	const float m_sprintStaminaDrain = 0.0f;    // Stamina drain per second while sprinting
	const float m_staminaRechargeRate = 20.0f;   // Stamina recharge per second
	const float m_minJumpStamina = 35.0f;        // Minimum stamina required to jump
	const float m_minSprintStamina = 1.0f;       // Minimum stamina to continue sprinting
	const int m_staminaRechargeDelay = 2000;     // Delay before stamina starts recharging (ms)
	
	// Footstep sound timing
	int m_lastFootstepTime = 0;                  // Last time a footstep sound played
	const int m_minFootstepInterval = 400;       // Minimum time between footsteps (ms)
	
	// Ground detection state (used for reliable footstep triggering)
	int m_airborneFrameCount = 0;                // Consecutive frames without ground collision

	HUDController         m_hudController;
	InteractionController m_interactionController;
	InventoryController   m_inventoryController;
	WeaponController      m_weaponController;

};

extern std::unique_ptr<PlayerController> g_PlayerController;
extern PlayerData g_PlayerData;
