#include "PlayerController.h"
#include "CameraFX.h"

#include "Engine/Engine.h"
#include "Engine/World/WorldManager.h"

#include <fstream>

#include "Game/Item/ItemDatabase.h"

#include "PlayerData.h"

//#define DISPLAY_PLAYER_STATS

std::unique_ptr<PlayerController> g_PlayerController;
PlayerData g_PlayerData;
CameraFX g_CameraFX;

#define vec3_t PxVec3


using namespace irr::core;
using namespace physx;

const float
	g_sensitivity      = 0.09f,
	g_topAngle         = -89.5f,
	g_bottomAngle      =  89.5f,
	g_walkSpeed        =  8.0f,   // units/sec — normal ground speed
	g_sprintSpeed      =  10.0f,   // units/sec — sprint
	g_crouchSpeed      =  5.0f,   // units/sec — crouched
	g_jumpSpeed        =  10.5f,   // units/sec — initial upward velocity
	g_gravity          = 20.0f,   // units/sec² — base downward acceleration (rising)
	g_fallGravityMult  =  1.4f,   // extra gravity while falling — snappier, less floaty arc
	g_climbSpeed       =  3.0f,   // units/sec — ladder climb rate (CONTENT_LADDER brushes)
	g_groundAccel      = 10.0f,   // GoldSrc sv_accelerate equivalent
	g_groundFriction   =  8.0f,   // GoldSrc sv_friction equivalent (default, overridden per material)
	g_airAccel         = 16.0f,   // air acceleration — responsive air steering
	g_airSpeedCap      =  7.0f,   // max units/sec gainable per direction in air (bounded boomer-style)
	g_headBobFrequency = 10.0f,
	g_headBobAmplitude =  0.03f;

bool g_hasFallen = false;
bool g_isOnSurface = false;
bool g_used = false;

int g_lastJumpTime = 0;
int g_lastStepTime = 0;
 
float g_headBobTimer = 0.0f;
float g_lastHeadBobValue = 0.0f;

// Friction per E_MANAGED_MATERIAL (index matches enum order).
// MAT_GLASS covers ice textures — intentionally low for slick surfaces.
static const float g_materialFriction[] = {
	8.0f,   // MAT_INVALID  — fallback, same as stone
	8.0f,   // MAT_EARTH    — soil/grass, good grip
	8.0f,  // MAT_GRAVEL   — loose but grippy
	5.0f,   // MAT_WATER    — slippery
	8.0f,   // MAT_STONE    — concrete/brick, normal
	6.0f,   // MAT_METAL    — somewhat slippery
	1.0f,   // MAT_GLASS    — glass/ice, very slippery
	8.0f,  // MAT_CARPET   — high grip
	8.0f,   // MAT_WOOD     — normal
};

void DisplayPlayerStats()
{
	{
		auto windowWidth = 320, windowHeight = 320;
		ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)));
		ImGui::SetNextWindowPos(ImVec2(0, 250));

		auto& ent_player = WorldManager::Get()->managerSystem()->getEntityByName("player");
		auto& transform = ent_player.getComponent<TransformComponent>();
		auto& camera = ent_player.getComponent<CameraComponent>();
		auto& physics = ent_player.getComponent<CCTComponent>();

		if (ImGui::Begin("PlayerInfo", reinterpret_cast<bool*>(1),
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("NDE PX %f", transform.position.X);
			ImGui::Text("NDE PY %f", transform.position.Y);
			ImGui::Text("NDE PZ %f", transform.position.Z);

			ImGui::Spacing();

			ImGui::Text("CAM PX %f", camera.camera->getPosition().X);
			ImGui::Text("CAM PY %f", camera.camera->getPosition().Y);
			ImGui::Text("CAM PZ %f", camera.camera->getPosition().Z);

			ImGui::Spacing();

			ImGui::Text("CCT PX %f", physics.controller->getPosition().x);
			ImGui::Text("CCT PY %f", physics.controller->getPosition().y);
			ImGui::Text("CCT PZ %f", physics.controller->getPosition().z);

			ImGui::Spacing();

			auto mat = 
				RenderManager::Get()->getMeshMaterialFromRay(transform.getPosition() + irr::core::vector3df(0.0f, 0.0f, 0.0f), transform.getPosition() + irr::core::vector3df(0.0f, -3.0f, 0.0f));
			std::string material =
				Engine::Get()->getMaterialBuilder().getMaterialName(Engine::Get()->getMaterialBuilder().getMaterialFromTexture(mat));

			ImGui::Text("TEX %s", mat.c_str());
			ImGui::Text("MAT %s", material.c_str());



			ImGui::End();
		}
	}
}

void DisplayStaminaBar(float currentStamina, float maxStamina)
{
	// // Get screen dimensions for positioning
	// ImGuiIO& io = ImGui::GetIO();
	// float windowWidth = 250.0f;
	// float windowHeight = 60.0f;
	
	// // Position in bottom-left corner with some padding
	// ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - windowHeight - 300));
	// ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
	
	// // Create window with minimal decoration
	// if (ImGui::Begin("Stamina", nullptr, 
	// 	ImGuiWindowFlags_NoTitleBar | 
	// 	ImGuiWindowFlags_NoResize | 
	// 	ImGuiWindowFlags_NoMove | 
	// 	ImGuiWindowFlags_NoSavedSettings))
	// {
	// 	// Stamina label
	// 	ImGui::Text("Stamina");
		
	// 	// Calculate stamina percentage for progress bar
	// 	float staminaPercent = currentStamina / maxStamina;
		
	// 	// Color based on stamina level (green -> yellow -> red)
	// 	ImVec4 barColor;
	// 	if (staminaPercent > 0.6f) {
	// 		barColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
	// 	} else if (staminaPercent > 0.3f) {
	// 		barColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
	// 	} else {
	// 		barColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
	// 	}
		
	// 	// Display numeric value
	// 	ImGui::PushStyleColor(ImGuiCol_Text, barColor);
	// 	ImGui::SameLine();
	// 	ImGui::Text("%.0f / %.0f", currentStamina, maxStamina);
	// 	ImGui::PopStyleColor();
		
	// 	ImGui::End();
	// }
}


void PlayerController::init()
{
	g_PlayerData.inventoryData.size = irr::core::vector2di(9, 3);

#ifndef DISABLE_HUD_AND_INV
	m_hudController.init();
#endif

	m_interactionController.init();

#ifndef DISABLE_HUD_AND_INV
	m_inventoryController.init();
#endif

	m_weaponController.init();
}

vector3df PlayerController::Accelerate(vector3df& wishdir, vector3df& vel, float accel, float wishspeed, float dt)
{
	float currentspeed = vel.dotProduct(wishdir);
	float addspeed = wishspeed - currentspeed;
	if (addspeed <= 0.0f) return vel;
	float accelspeed = accel * wishspeed * dt;
	if (accelspeed > addspeed) accelspeed = addspeed;
	return vel + wishdir * accelspeed;
}

vector3df PlayerController::MoveGround(vector3df& wishdir, vector3df& vel, float friction, float accel, float wishspeed, float dt)
{
	float speed = vel.getLength();
	if (speed > 0.001f)
	{
		// GoldSrc-style: friction rate is at minimum wishspeed so stopping is always crisp
		float control = speed < wishspeed ? wishspeed : speed;
		float drop = control * friction * dt;
		float newspeed = std::max(speed - drop, 0.0f);
		vel *= newspeed / speed;
	}
	return Accelerate(wishdir, vel, accel, wishspeed, dt);
}

vector3df PlayerController::MoveAir(vector3df& wishdir, vector3df& vel, float accel, float airSpeedCap, float dt)
{
	// airSpeedCap limits speed gained per direction in air — allows steering
	// without accumulating Quake-style bunny-hop momentum.
	return Accelerate(wishdir, vel, accel, airSpeedCap, dt);
}

void PlayerController::update(float dt)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid()) {
		return;
	}

	g_PlayerData.currentHealth = player.getComponent<DamageReceiverComponent>().health;

	auto currentTime = static_cast<int>(Engine::Get()->getCurrentTime());

	if (g_PlayerData.currentHealth <= 0 && !m_isDead)
	{
		m_isDead = true;
		m_deathTime = currentTime;
		lockPlayer(true);
		m_targetCrouchHeight = 0.2f;
		player.getComponent<SoundComponent>().play("die");
	}

	auto& transform = player.getComponent<TransformComponent>();
	auto& camera = player.getComponent<CameraComponent>();
	auto& cct = player.getComponent<CCTComponent>();
	auto& sound = player.getComponent<SoundComponent>();
	auto& damage = player.getComponent<DamageReceiverComponent>();

	transform.node->setRotation(vector3df(transform.node->getRotation().X, transform.node->getRotation().Y, 0.0f));

	static vector3df lastPlayerPosition = transform.position;

	float
		x_move = 0.0,
		z_move = 0.0,
		targetSpeed = g_walkSpeed;

	vector2df mouseDelta;

	// Seed clean pitch/yaw from the actual camera on very first frame only.
	// After that we own these values exclusively — camera node rotation is
	// never read back, so FX offsets cannot accumulate into the base aim.
	if (m_firstUpdate)
	{
		m_cameraPitch = camera.camera->getRotation().X;
		m_cameraYaw   = camera.camera->getRotation().Y;
		InputManager::Get()->centerMouse();

		// Seed CCT-position-derived tracking state from the live controller so
		// this fresh instance doesn't start from (0,0,0) and read a huge bogus
		// delta on its first frame.
		vector3df cctPos(
			static_cast<float>(cct.controller->getPosition().x),
			static_cast<float>(cct.controller->getPosition().y),
			static_cast<float>(cct.controller->getPosition().z));
		m_smoothedY = cctPos.Y;
		m_lastCCTPosition = cctPos;

		m_firstUpdate = false;
	}

	if (!isPlayerLocked() && !g_PlayerInventoryIsDisplaying)
	{
		mouseDelta = InputManager::Get()->getMouseDelta();

		// Accumulate mouse delta into the clean base rotation only.
		m_cameraYaw   -= mouseDelta.X * g_sensitivity;
		m_cameraPitch -= mouseDelta.Y * g_sensitivity;

		if (m_cameraPitch > g_bottomAngle) m_cameraPitch = g_bottomAngle;
		if (m_cameraPitch < g_topAngle)    m_cameraPitch = g_topAngle;
	}

	// Build final rotation starting from the clean base (no FX contamination).
	vector3df cameraRotation(m_cameraPitch, m_cameraYaw, 0.0f);

	if (m_isDead)
	{
		float deathDuration = 1000.0f;
		float t = (Engine::Get()->getCurrentTime() - m_deathTime) / deathDuration;
		if (t > 1.0f) t = 1.0f;
		t = t * t * (3.0f - 2.0f * t);
		cameraRotation.Z = -60.0f * t;
	}

	// --- Camera FX: recoil kick + screen shake ---
	// Applied as a pure additive overlay — never written back into
	// m_cameraPitch/m_cameraYaw, so recovery truly returns to aim point.
	float fxY = 0.0f;
	if (!m_isDead)
	{
		float fxPitch = 0.0f, fxYaw = 0.0f, fxRoll = 0.0f, fxFovDeg = 0.0f;
		g_CameraFX.tick(dt, fxPitch, fxYaw, fxRoll, fxY, fxFovDeg);
		cameraRotation.X += fxPitch;
		cameraRotation.Y += fxYaw;
		cameraRotation.Z += fxRoll;

		// FOV kick — fire punch widens, nearby explosions crunch in.
		// Base FOV captured once before any kick is ever applied; nothing else
		// in the engine calls setFOV, so PlayerController owns it from here.
		static const float s_baseFov = camera.camera->getFOV();
		camera.camera->setFOV(s_baseFov + fxFovDeg * irr::core::DEGTORAD);

		// Clamp after FX to prevent extremes.
		if (cameraRotation.X > g_bottomAngle) cameraRotation.X = g_bottomAngle;
		if (cameraRotation.X < g_topAngle)    cameraRotation.X = g_topAngle;
	}

	transform.setRotation(vector3df(cameraRotation.X, cameraRotation.Y, cameraRotation.Z));

	// Ground detection set from collision flags after CCT move (with buffering to prevent jitter)

	if (m_isCrouched)
	{
		targetSpeed = g_crouchSpeed;
	}
	else if (InputManager::Get()->isActionPressed("sprint"))
	{
		targetSpeed = g_sprintSpeed;
	}
	else
	{
		targetSpeed = g_walkSpeed;
	}

	if (!isPlayerLocked())
	{
		if (InputManager::Get()->isActionPressed("forward") && !InputManager::Get()->isActionPressed("backward"))
		{
			z_move += 1.0f;
		}
		if (InputManager::Get()->isActionPressed("backward") && !InputManager::Get()->isActionPressed("forward"))
		{
			z_move -= 1.0f;
		}
		if (InputManager::Get()->isActionPressed("strafel") && !InputManager::Get()->isActionPressed("strafer"))
		{
			x_move -= 1.0f;
		}
		if (InputManager::Get()->isActionPressed("strafer") && !InputManager::Get()->isActionPressed("strafel"))
		{
			x_move += 1.0f;
		}
	}

	// Ladder climbing (CONTENT_LADDER brush volumes, flag set per frame by
	// GameplaySystem).  Fixed-vertical controls: forward climbs up, backward
	// climbs down, strafe steps off sideways.  Water takes precedence, and
	// the jump-detach grace timer stops the volume from instantly re-grabbing.
	const bool onLadder = m_isOnLadder && !isSwimming() && currentTime >= m_ladderIgnoreUntil;
	if (onLadder)
	{
		// Ladder movement is fully input-driven: set velocity directly and
		// consume the inputs so the accel paths are skipped — MoveAir has no
		// air friction, so momentum carried onto the ladder would otherwise
		// persist and the capsule drifts while hanging still.
		m_playerVelocity.Y = z_move * g_climbSpeed;
		m_playerVelocity.X = x_move * g_climbSpeed;   // sidestep off at climb rate
		m_playerVelocity.Z = 0.0f;                    // never drive into the wall
		x_move = 0.0f;
		z_move = 0.0f;
	}

	// True on any frame a jump fires — used to skip ground friction so horizontal
	// momentum carries into the jump (fluid chained re-jumps).
	bool jumpedThisFrame = false;

	// Track when player is grounded for coyote time
	if (g_isOnSurface)
	{
		m_lastGroundedTime = currentTime;
	}

	// Check for buffered jump when landing
	if (g_isOnSurface && g_hasFallen && !isSwimming())
	{
		g_hasFallen = false;
		//playJumpSound(player);

		// Execute buffered jump if jump was pressed recently
		if (!m_isCrouched && currentTime - m_lastJumpInputTime < m_jumpBufferTime)
		{
			// Start buffered jump
			m_playerVelocity.Y = g_jumpSpeed;

			m_isJumping = true;
			m_jumpConsumed = true;  // Mark input as consumed to prevent repeat jumps
			jumpedThisFrame = true;
			g_lastJumpTime = currentTime;
			m_lastJumpInputTime = -1000; // Clear buffer
		}
	}
	if (!g_isOnSurface)
	{
		g_hasFallen = true;
	}

	if (!isPlayerLocked())
	{
		// Track jump input for buffering and reset consumed flag when button released
		if (InputManager::Get()->isActionPressed("jump"))
		{
			if (!m_jumpConsumed)
				m_lastJumpInputTime = currentTime;

			if (onLadder)
			{
				// Detach: hop off and ignore the ladder briefly so the volume
				// test doesn't re-grab us on the next frame
				if (!m_jumpConsumed)
				{
					m_playerVelocity.Y = g_jumpSpeed * 0.6f;

					m_isJumping = true;
					m_jumpConsumed = true;
					jumpedThisFrame = true;
					m_ladderIgnoreUntil = currentTime + 400;
				}
			}
			else if (!isSwimming())
			{
				// No cooldown: tap-to-rejump fires the instant we touch ground.
				// m_jumpConsumed (reset on button release) keeps it tap-based, not auto-bhop.
				bool canJump = (g_isOnSurface || currentTime - m_lastGroundedTime < m_coyoteTime) &&
				               !m_isCrouched &&
				               !m_jumpConsumed;

				if (canJump)
				{
					m_playerVelocity.Y = g_jumpSpeed;

					m_isJumping = true;
					m_jumpConsumed = true;
					jumpedThisFrame = true;
					g_lastJumpTime = currentTime;
				}
			}
			else
			{
				// Swim up
				m_playerVelocity.Y = g_walkSpeed;
			}
		}
		// Jump cut: reduce velocity when jump button released mid-jump
		else if (InputManager::Get()->isActionReleased("jump"))
		{
			// Reset consumed flag so next press can trigger jump
			m_jumpConsumed = false;
			
			// Apply jump cut if releasing during upward movement
			if (m_isJumping && m_playerVelocity.Y > 0)
			{
				m_playerVelocity.Y *= m_jumpCutMultiplier;
				m_isJumping = false;
			}
		}
		if (InputManager::Get()->isActionPressed("crouch"))
		{
			if (!isSwimming())
			{
				// Set target height for smooth lerp transition to crouched state
				m_targetCrouchHeight = 0.5f;
				m_isCrouched = true;
			}
			else
			{
				// Swim down - apply to vertical velocity
				m_playerVelocity.Y = -g_walkSpeed;
			}
		}
		else if (m_isCrouched)
		{
			// Raycast in the center and at each edge of the controller to prevent standing up inside of geometry
			if (!PhysicsManager::Get()->raycast(transform.getPosition() + irr::core::vector3df(0.0f, 0.57f, 0.0f), irr::core::vector3df(0.0, 1.0, 0.0), 0.5).hit &&
				!PhysicsManager::Get()->raycast(transform.getPosition() + irr::core::vector3df(0.25f, 0.57f, 0.0f), irr::core::vector3df(0.0, 1.0, 0.0), 0.5).hit &&
				!PhysicsManager::Get()->raycast(transform.getPosition() + irr::core::vector3df(-0.25f, 0.57f, 0.0f), irr::core::vector3df(0.0, 1.0, 0.0), 0.5).hit &&
				!PhysicsManager::Get()->raycast(transform.getPosition() + irr::core::vector3df(0.0f, 0.57f, 0.25f), irr::core::vector3df(0.0, 1.0, 0.0), 0.5).hit &&
				!PhysicsManager::Get()->raycast(transform.getPosition() + irr::core::vector3df(0.0f, 0.57f, -0.25f), irr::core::vector3df(0.0, 1.0, 0.0), 0.5).hit)
			{
				// Set target height for smooth lerp transition to standing state
				m_targetCrouchHeight = 2.0f;
				m_isCrouched = false;
			}
		}

		if (InputManager::Get()->isActionPressed("use") && !g_used)
		{
			auto raycast_data = RenderManager::Get()->raycastWorldPosition(
				RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition(),
				RenderManager::Get()->sceneManager()->getActiveCamera()->getTarget(), true);

			if (raycast_data.hit)
			{
				if (RenderManager::Get()->getRaycastLength(RenderManager::Get()->sceneManager()->getActiveCamera()->getPosition(), raycast_data) < _player_interact_distance)
				{
					WorldManager::Get()->gameplaySystem()->interact(raycast_data.node->getID());
					g_used = true;
				}
			}
		}
		if (InputManager::Get()->isActionReleased("use"))
		{
			g_used = false;
		}

		// Double-tap dodge detection (Unreal-style)
		if (!isSwimming() && !onLadder)
		{
			bool lftPressed = InputManager::Get()->isActionPressed("strafel");
			bool rgtPressed = InputManager::Get()->isActionPressed("strafer");

			auto tryDodge = [&](bool pressed, bool& prevPressed, int& lastTapTime, float dvX, float dvZ)
			{
				if (pressed && !prevPressed)
				{
					if (g_isOnSurface && !m_isDodging && !m_isCrouched &&
						currentTime - m_lastDodgeTime >= m_dodgeCooldown)
					{
						if (currentTime - lastTapTime < m_dodgeDoubleTapWindow)
						{
							// Local space (X=strafe, Z=forward) — displacement step applies camera yaw.
							m_playerVelocity.X = dvX * m_dodgeSpeed;
							m_playerVelocity.Z = dvZ * m_dodgeSpeed;
							m_playerVelocity.Y = g_jumpSpeed * 0.4f;
							m_isDodging      = true;
							m_dodgeStartTime = currentTime;
							m_lastDodgeTime  = currentTime;
							sound.play("jump");
						}
						lastTapTime = currentTime;
					}
					else if (!m_isDodging)
					{
						lastTapTime = currentTime;
					}
				}
				prevPressed = pressed;
			};

			//tryDodge(lftPressed, m_prevLeftPressed,  m_lastLeftTapTime,  -1.0f, 0.0f);
			//tryDodge(rgtPressed, m_prevRightPressed, m_lastRightTapTime,  1.0f, 0.0f);
		}
	}

	// Clear dodge when duration expires or player lands
	if (m_isDodging)
	{
		if (currentTime - m_dodgeStartTime >= m_dodgeDuration ||
			(g_isOnSurface && m_playerVelocity.Y <= 0.0f))
		{
			m_isDodging = false;
		}
	}

	// Apply gravity continuously (unless swimming or holding a ladder).
	// Falling uses stronger gravity for a snappier, less floaty arc.
	if (!isSwimming() && !onLadder)
	{
		float gravity = (m_playerVelocity.Y < 0.0f) ? g_gravity * g_fallGravityMult : g_gravity;
		m_playerVelocity.Y -= gravity * (dt / 1000.0f);
	}

	// Reset vertical velocity and jump state when landing
	if (g_isOnSurface && m_playerVelocity.Y < 0)
	{
		if (m_lastAirVelocityY < -4.0f)
		{
			// Power curve: grows slowly near jump speed, aggressively on high falls.
			// ~6.5 m/s (normal jump) → ~0.6°, ~11 m/s (3m fall) → ~2.8°, caps at 4°
			float excess = -m_lastAirVelocityY - 4.0f;
			float magnitude = powf(excess, 1.5f) * 0.15f;
			g_CameraFX.addLandingBob(std::min(magnitude, 4.0f));
		}

		// Fall damage: linear falloff between min/max landing speed, instant
		// kill at/above max. Routed through damageEntity() (the same chokepoint
		// weapons use) so invulnerable/buddha and the hurt sound just work.
		if (!m_isDead)
		{
			float fallSpeed = -m_lastAirVelocityY;
			auto& descriptor = player.getComponent<DescriptorComponent>();

			if (fallSpeed >= m_fallDamageMaxSpeed)
			{
				WorldManager::Get()->gameplaySystem()->damageEntity(descriptor.id, 9999);
			}
			else if (fallSpeed > m_fallDamageMinSpeed)
			{
				float t = (fallSpeed - m_fallDamageMinSpeed) / (m_fallDamageMaxSpeed - m_fallDamageMinSpeed);
				unsigned int fallDamage = static_cast<unsigned int>(m_fallDamageMax * t);
				if (fallDamage > 0)
					WorldManager::Get()->gameplaySystem()->damageEntity(descriptor.id, fallDamage);
			}
		}

		m_playerVelocity.Y = 0;
		m_isJumping = false;
		m_lastAirVelocityY = 0.0f;
	}

	// Prepare horizontal movement input
	vector3df horizontal_move = vector3df(x_move, 0, z_move);
	vector3df wishdir = horizontal_move;
	if (wishdir.getLength() > 0.001f) wishdir.normalize();

	// Apply ground or air movement to horizontal velocity only
	vector3df horizontal_velocity = vector3df(m_playerVelocity.X, 0, m_playerVelocity.Z);

	if (g_isOnSurface && jumpedThisFrame)
	{
		// Jumping this frame: skip ground friction entirely so horizontal momentum
		// carries into the jump (fluid chained re-jumps). Still bounded by air
		// accel/cap, so this is momentum preservation, not exponential bhop gain.
		if (!m_isDodging)
			horizontal_velocity = MoveAir(wishdir, horizontal_velocity, g_airAccel, g_airSpeedCap, dt / 1000.0f);
	}
	else if (g_isOnSurface)
	{
		auto surfaceTex = RenderManager::Get()->getMeshMaterialFromRay(
			transform.getPosition(),
			transform.getPosition() + irr::core::vector3df(0.0f, -2.0f, 0.0f));
		auto surfaceMat = Engine::Get()->getMaterialBuilder().getMaterialFromTexture(surfaceTex);
		float surfaceFriction = g_materialFriction[static_cast<int>(surfaceMat)];

		m_isSliding = false;

		// Raycast for slope normal on every grounded frame — used both for ice
		// slide gravity and for stick-to-slope on all surfaces. Cache last valid
		// result so a missed frame doesn't lose the normal.
		{
			irr::core::triangle3df hitTriangle;
			irr::core::vector3df   hitPoint;
			irr::core::line3df     slopeRay(
				transform.getPosition(),
				transform.getPosition() + irr::core::vector3df(0.0f, -2.0f, 0.0f));

			auto* collMgr = RenderManager::Get()->sceneManager()->getSceneCollisionManager();
			if (collMgr->getSceneNodeAndCollisionPointFromRay(slopeRay, hitPoint, hitTriangle))
			{
				irr::core::vector3df n = hitTriangle.getNormal();
				n.normalize();
				if (n.Y < 0.0f) n = -n;
				m_lastSlopeNormal = n;
			}
		}

		if (surfaceFriction < 3.0f)
		{
			// Ice: skip MoveGround friction (it would zero out slide velocity before it
			// builds, because control = max(speed, wishspeed) starts at walkSpeed even
			// when stationary). Allow minimal player steering via Accelerate only.
			horizontal_velocity = Accelerate(wishdir, horizontal_velocity, g_groundAccel * 0.15f, targetSpeed, dt / 1000.0f);

			irr::core::vector3df gravVec(0.0f, -g_gravity, 0.0f);
			m_lastSlideWorldAccel = gravVec - m_lastSlopeNormal * gravVec.dotProduct(m_lastSlopeNormal);

			float yawRad = deg2rad(m_cameraYaw);
			float localX = m_lastSlideWorldAccel.X * cosf(yawRad) - m_lastSlideWorldAccel.Z * sinf(yawRad);
			float localZ = m_lastSlideWorldAccel.X * sinf(yawRad) + m_lastSlideWorldAccel.Z * cosf(yawRad);

			horizontal_velocity.X += localX * (dt / 1000.0f);
			horizontal_velocity.Z += localZ * (dt / 1000.0f);

			float slideSpeed = horizontal_velocity.getLength();
			if (slideSpeed > targetSpeed * 1.5f)
				horizontal_velocity *= (targetSpeed * 1.5f) / slideSpeed;

			// Suppress footsteps only during passive sliding (no player input).
			// Walking against the slide (e.g. up-slope with W) still makes noise.
			m_isSliding = (m_lastSlideWorldAccel.getLength() > 0.5f) && (wishdir.getLength() < 0.01f);
		}
		else
		{
			m_lastSlideWorldAccel = irr::core::vector3df(0.0f, 0.0f, 0.0f);
			horizontal_velocity = MoveGround(wishdir, horizontal_velocity, surfaceFriction, g_groundAccel, targetSpeed, dt / 1000.0f);

			// Downward-raycast steep slope handling (catches moderate slopes).
			const float maxWalkSlopeY = 0.5f;
			if (m_lastSlopeNormal.Y < maxWalkSlopeY && m_lastSlopeNormal.Y > 0.1f)
			{
				float yawRad = deg2rad(m_cameraYaw);

				irr::core::vector3df slopeHN(m_lastSlopeNormal.X, 0.0f, m_lastSlopeNormal.Z);
				float slopeHNLen = slopeHN.getLength();
				if (slopeHNLen > 0.01f)
				{
					slopeHN /= slopeHNLen;
					float localNX = slopeHN.X * cosf(yawRad) - slopeHN.Z * sinf(yawRad);
					float localNZ = slopeHN.X * sinf(yawRad) + slopeHN.Z * cosf(yawRad);
					irr::core::vector3df slopeHN_local(localNX, 0.0f, localNZ);

					float penetration = -horizontal_velocity.dotProduct(slopeHN_local);
					if (penetration > 0.0f)
						horizontal_velocity += slopeHN_local * penetration;
				}

				irr::core::vector3df gravVec(0.0f, -g_gravity, 0.0f);
				m_lastSlideWorldAccel = gravVec - m_lastSlopeNormal * gravVec.dotProduct(m_lastSlopeNormal);

				float localX = m_lastSlideWorldAccel.X * cosf(yawRad) - m_lastSlideWorldAccel.Z * sinf(yawRad);
				float localZ = m_lastSlideWorldAccel.X * sinf(yawRad) + m_lastSlideWorldAccel.Z * cosf(yawRad);
				horizontal_velocity.X += localX * (dt / 1000.0f);
				horizontal_velocity.Z += localZ * (dt / 1000.0f);
			}
		}

		// Forward ray: catches near-vertical surfaces the downward ray misses entirely
		// because the ray hits the floor rather than the slope face. Cast in the
		// movement direction; if anything steep is within reach, cancel into it.
		{
			float yawRad = deg2rad(m_cameraYaw);

			// World-space direction of current horizontal movement (prefer wishdir so
			// we block the player before they reach the surface, not just after).
			irr::core::vector3df checkDir = wishdir.getLength() > 0.01f ? wishdir : horizontal_velocity;
			float worldDirX = checkDir.Z * sinf(yawRad) + checkDir.X * cosf(yawRad);
			float worldDirZ = checkDir.Z * cosf(yawRad) - checkDir.X * sinf(yawRad);
			irr::core::vector3df worldDir(worldDirX, 0.0f, worldDirZ);

			if (worldDir.getLength() > 0.01f)
			{
				worldDir.normalize();

				irr::core::triangle3df fwdTri;
				irr::core::vector3df   fwdHit;
				irr::core::line3df     fwdRay(
					transform.getPosition(),
					transform.getPosition() + worldDir * 0.75f);

				auto* collMgr = RenderManager::Get()->sceneManager()->getSceneCollisionManager();
				if (collMgr->getSceneNodeAndCollisionPointFromRay(fwdRay, fwdHit, fwdTri))
				{
					irr::core::vector3df wallN = fwdTri.getNormal();
					wallN.normalize();
					if (wallN.Y < 0.0f) wallN = -wallN;

					const float maxWalkSlopeY = 0.5f;
					if (wallN.Y < maxWalkSlopeY)
					{
						irr::core::vector3df wallHN(wallN.X, 0.0f, wallN.Z);
						float wallHNLen = wallHN.getLength();
						if (wallHNLen > 0.01f)
						{
							wallHN /= wallHNLen;
							float localNX = wallHN.X * cosf(yawRad) - wallHN.Z * sinf(yawRad);
							float localNZ = wallHN.X * sinf(yawRad) + wallHN.Z * cosf(yawRad);
							irr::core::vector3df wallHN_local(localNX, 0.0f, localNZ);

							float penetration = -horizontal_velocity.dotProduct(wallHN_local);
							if (penetration > 0.0f)
								horizontal_velocity += wallHN_local * penetration;
						}
					}
				}
			}
		}
	}
	else
	{
		m_isSliding = false;
		m_lastSlideWorldAccel = irr::core::vector3df(0.0f, 0.0f, 0.0f);
		m_lastSlopeNormal     = irr::core::vector3df(0.0f, 1.0f, 0.0f);
		m_lastAirVelocityY = m_playerVelocity.Y;
		if (!m_isDodging)
			horizontal_velocity = MoveAir(wishdir, horizontal_velocity, g_airAccel, g_airSpeedCap, dt / 1000.0f);
		// While dodging, preserve the impulse velocity — no steering input applied
	}

	// Update player velocity with new horizontal velocity
	m_playerVelocity.X = horizontal_velocity.X;
	m_playerVelocity.Z = horizontal_velocity.Z;

	// Stick-to-slope: when sliding on ice, override Y so the total displacement
	// is tangent to the slope surface. Without this, horizontal slide velocity
	// penetrates the slope geometry and the CCT deflects the player upward each
	// frame, causing a bounce. The required Y is derived by solving:
	//   dot(velocity_world, slopeNormal) = 0
	//   worldVelX*n.X + Y*n.Y + worldVelZ*n.Z = 0  →  Y = -(worldVelX*n.X + worldVelZ*n.Z) / n.Y
	float moveDirection = deg2rad(m_cameraYaw);
	// slopeNormal.Y < 0.999 means the surface is sloped enough to matter (~2.5°).
	// Flat ground has Y = 1.0 so this is a no-op there.
	if (g_isOnSurface && !jumpedThisFrame && m_lastSlopeNormal.Y < 0.999f && m_lastSlopeNormal.Y > 0.3f)
	{
		float worldVelX = m_playerVelocity.Z * sinf(moveDirection) + m_playerVelocity.X * sinf(moveDirection + __pi / 2.0f);
		float worldVelZ = m_playerVelocity.Z * cosf(moveDirection) + m_playerVelocity.X * cosf(moveDirection + __pi / 2.0f);
		float stickY = -(worldVelX * m_lastSlopeNormal.X + worldVelZ * m_lastSlopeNormal.Z) / m_lastSlopeNormal.Y;
		if (stickY < 0.0f)
			m_playerVelocity.Y = stickY;
	}

	// Configure collision filters to detect static geometry
	PxFilterData filterData;
	filterData.word0 = RHG_STATIC | RHG_DYNAMIC | RHG_CLIP_PLAYER;  // static, kinematic/dynamic, and playerclip brushes
	
	physx::PxControllerFilters filters(&filterData);

	float elapsedSeconds = dt / 1000.0f;

	cct.displacement = physx::PxVec3(
		(m_playerVelocity.Z * sinf(moveDirection) + m_playerVelocity.X * sinf(moveDirection + __pi / 2.0f)) * elapsedSeconds,
		m_playerVelocity.Y * elapsedSeconds,
		(m_playerVelocity.Z * cosf(moveDirection) + m_playerVelocity.X * cosf(moveDirection + __pi / 2.0f)) * elapsedSeconds);

	// Ride kinematic movers (elevators, doors): PhysX CCTs are not carried by
	// kinematic platforms, so track the actor under our feet and add its
	// per-frame pose delta to our displacement while grounded.
	{
		physx::PxRigidDynamic* mover = nullptr;
		if (g_isOnSurface)
		{
			const physx::PxExtendedVec3 foot = cct.controller->getFootPosition();
			auto ray = PhysicsManager::Get()->raycast(
				irr::core::vector3df(static_cast<float>(foot.x),
				                     static_cast<float>(foot.y) + 0.2f,
				                     static_cast<float>(foot.z)),
				irr::core::vector3df(0.0f, -1.0f, 0.0f), 0.6);
			if (ray.hit && ray.data.hasBlock && ray.data.block.actor)
			{
				auto* dyn = ray.data.block.actor->is<physx::PxRigidDynamic>();
				// The CCT's own capsule is also a kinematic dynamic — skip self
				if (dyn && dyn != cct.controller->getActor() &&
					(dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
					mover = dyn;
			}
		}

		if (mover)
		{
			const physx::PxVec3 pos = mover->getGlobalPose().p;
			if (mover == m_groundMover)
				cct.displacement += pos - physx::PxVec3(m_groundMoverLastPos.X, m_groundMoverLastPos.Y, m_groundMoverLastPos.Z);
			m_groundMoverLastPos = irr::core::vector3df(pos.x, pos.y, pos.z);
		}
		m_groundMover = mover;
	}

	PxControllerCollisionFlags collisionFlags = cct.controller->move(cct.displacement, 0.001f, elapsedSeconds, filters);
	
	// Frame-based buffering to prevent collision flag flicker from causing head bob jitter
	// Requires multiple consecutive airborne frames before switching state
	const int requiredAirborneFrames = 3;  // Must be airborne for 3 frames to switch state
	
	bool hasGroundCollision = static_cast<bool>(collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN);
	
	if (hasGroundCollision)
	{
		// Immediately set grounded when collision detected
		g_isOnSurface = true;
		m_airborneFrameCount = 0;
	}
	else
	{
		// Count consecutive airborne frames
		m_airborneFrameCount++;
		
		// Only switch to airborne after enough consecutive airborne frames
		if (m_airborneFrameCount >= requiredAirborneFrames)
		{
			g_isOnSurface = false;
		}
		// Otherwise stay grounded (ignores single-frame flickers)
	}

	// Position update: snap horizontal for instant, snappy response; smooth only
	// the vertical axis (framerate-independent) to absorb stair-step and PhysX
	// overlap-recovery jitter without dragging horizontal movement behind input.
	vector3df targetPosition = vector3df(
		static_cast<float>(cct.controller->getPosition().x),
		static_cast<float>(cct.controller->getPosition().y),
		static_cast<float>(cct.controller->getPosition().z));

	const float kVerticalSmoothRate = 18.0f;  // higher = snappier, lower = smoother
	float yFactor = 1.0f - expf(-kVerticalSmoothRate * (dt / 1000.0f));
	m_smoothedY += (targetPosition.Y - m_smoothedY) * yFactor;

	vector3df smoothedPosition(targetPosition.X, m_smoothedY, targetPosition.Z);

	transform.setPosition(smoothedPosition);

	if (cct.hitboxNode)
	{
		auto* capsule  = static_cast<physx::PxCapsuleController*>(cct.controller);
		float h        = static_cast<float>(capsule->getHeight());
		float r        = static_cast<float>(capsule->getRadius());
		float diameter = 2.0f * r;
		// smoothedPosition is already the PhysX capsule centre (foot + r + h/2),
		// so no additional offset is needed — the cube origin is also at its centre.
		cct.hitboxNode->setPosition(smoothedPosition);
		cct.hitboxNode->setScale(vector3df(diameter, h + 2.0f * r, diameter));
	}

	// Smooth crouch transition using lerp
	if (abs(m_currentCrouchHeight - m_targetCrouchHeight) > 0.01f)
	{
		// Lerp current height towards target height
		float lerpAmount = m_crouchLerpSpeed * (dt / 1000.0f);
		m_currentCrouchHeight += (m_targetCrouchHeight - m_currentCrouchHeight) * lerpAmount;
		
		// Clamp to target if very close to avoid jittering
		if (abs(m_currentCrouchHeight - m_targetCrouchHeight) < 0.01f)
		{
			m_currentCrouchHeight = m_targetCrouchHeight;
		}
		
		// Apply the lerped height to the capsule controller
		cct.controller->resize(m_currentCrouchHeight * IRR_PHYSX_DIM_SCALAR);
	}

	// Movement detection for head bob - use actual CCT position, not interpolated transform
	vector3df currentCCTPosition = vector3df(
		static_cast<float>(cct.controller->getPosition().x),
		static_cast<float>(cct.controller->getPosition().y),
		static_cast<float>(cct.controller->getPosition().z));

	if (currentCCTPosition.X > m_lastCCTPosition.X || currentCCTPosition.Z > m_lastCCTPosition.Z ||
		currentCCTPosition.X < m_lastCCTPosition.X || currentCCTPosition.Z < m_lastCCTPosition.Z)
	{
		m_isMoving = true;
		
		// Head bob runs whenever moving (not tied to ground state to avoid jitter)
		// Scale head bob frequency based on movement modifier
		// Base speed (1.0) + sprint (1.0) or crouch (-0.7)
		float speedMultiplier = targetSpeed / g_walkSpeed;
		float speedBasedFrequency = g_headBobFrequency * speedMultiplier;
		
		// Update head bob timer when moving (frequency scales with speed)
		g_headBobTimer += speedBasedFrequency * (dt / 1000.0f);
		
		// Calculate head bob value
		float bobValue = sin(g_headBobTimer);
		float bobOffset = bobValue * g_headBobAmplitude;
		
		// Calculate base camera offset based on current crouch height (lerped smoothly)
		// Map height from [0.5, 2.0] to camera offset [0.25, 0.8]
		float baseOffset = 0.25f + (m_currentCrouchHeight - 0.5f) * ((0.8f - 0.25f) / (2.0f - 0.5f));
		
		// Combine base offset + head bob + landing dip
		camera.offset.Y = baseOffset + bobOffset + fxY;
		
		// Play footstep when bob crosses zero going down (foot hits ground)
		// Use airborne frame count instead of g_isOnSurface for more reliable ground detection
		// m_airborneFrameCount < 3 means player is effectively on ground (even if PhysX collision flags flicker)
		if (!isSwimming() && !onLadder && !m_isSliding && m_airborneFrameCount < 3)
		{
			// Only play if enough time has elapsed since last footstep
			if (/*g_lastHeadBobValue > 0.0f && bobValue <= 0.0f &&*/ currentTime - m_lastFootstepTime >= m_minFootstepInterval && !isSwimming())
			{
				playFootStepSound(player, currentTime, 0);

				m_lastFootstepTime = currentTime;
			}
		}
		
		g_lastHeadBobValue = bobValue;
	}
	else
	{
		m_isMoving = false;
		
		// Immediately snap camera to rest position when stopped
		float targetY = 0.25f + (m_currentCrouchHeight - 0.5f) * ((0.8f - 0.25f) / (2.0f - 0.5f));
		camera.offset.Y = targetY + fxY;
		
		// Reset head bob state
		g_headBobTimer = 0.0f;
		g_lastHeadBobValue = 0.0f;
	}

	m_lastCCTPosition = currentCCTPosition;
	lastPlayerPosition = transform.getPosition();

	// CRITICAL: Update weapon AFTER all camera/CCT updates complete
	// This ensures effects spawn with current frame's camera/player position

	if (!m_isDead)
	{
		m_weaponController.update();
	}

	if (damage.didReceiveDamage() && !m_isDead)
	{
		sound.play("damage" + std::to_string(rand() % 2 + 1));
	}
#ifdef DISPLAY_PLAYER_STATS
	DisplayPlayerStats();
#endif

	m_hudController.update(g_PlayerData, m_inventoryController.isInventoryDisplaying());
	m_interactionController.update(g_PlayerData);
}

void PlayerController::updateUI(float dt)
{

}

void PlayerController::destroy()
{
#ifndef DISABLE_HUD_AND_INV
	m_hudController.destroy();
#endif

	m_interactionController.destroy();

#ifndef DISABLE_HUD_AND_INV
	m_inventoryController.destroy();
#endif

	m_weaponController.destroy();

	m_firstUpdate = true;

	g_PlayerData.inventoryData.contents.clear();
}

void PlayerController::pause()
{

}

void PlayerController::resume()
{

}

void PlayerController::playFootStepSound(anax::Entity& player, int _time, int _delay)
{
    auto& transform = player.getComponent<TransformComponent>();

    int n = rand() % 4 + 1;

    std::string material =
        Engine::Get()->getMaterialBuilder().getMaterialName(Engine::Get()->getMaterialBuilder().getMaterialFromTexture(
            RenderManager::Get()->getMeshMaterialFromRay(transform.getPosition() + irr::core::vector3df(0.0f, 0.0f, 0.0f), transform.getPosition() + irr::core::vector3df(0.0f, -2.0f, 0.0f))));

	// Play a default sound if the material is invalid
    if (material == "invalid")
    {
		static int step = 2;

		step == 2 ? step = 1 : step = 2;

		SoundManager::Get()->sound()->play2D(std::string("content/sound/player/footstep/default" + std::to_string(step) + ".wav").c_str(), false, 0, 0.2f);

        return;
    }

    player.getComponent<SoundComponent>().play(material + std::to_string(n));
}

void PlayerController::playJumpSound(anax::Entity& player)
{
    auto& transform = player.getComponent<TransformComponent>();

    std::string material =
		Engine::Get()->getMaterialBuilder().getMaterialName(Engine::Get()->getMaterialBuilder().getMaterialFromTexture(
			RenderManager::Get()->getMeshMaterialFromRay(transform.getPosition(), transform.getPosition() + irr::core::vector3df(0, -2.0, 0))));

    if (material == "invalid")
        return;

    player.getComponent<SoundComponent>().play(material + "jump");
}

void PlayerController::loadPlayerData(std::string file)
{
	tinyxml2::XMLDocument xml;
	if (xml.LoadFile(file.c_str()) != tinyxml2::XML_NO_ERROR) {
		spdlog::error("Failed to load player save data \'" + file + "\' in PlayerController::loadPlayerData()");
		return;
	}

	try {
		auto root = xml.FirstChild()->NextSibling()->FirstChild();

		for (auto dataNode = root->FirstChild()->FirstChildElement();
			dataNode != nullptr;
			dataNode = dataNode->NextSiblingElement()) {
			if (std::string(dataNode->Name()) == "value0") {
				WorldManager::Get()->managerSystem()->getEntityByName("player").getComponent<DamageReceiverComponent>().damageReceived = 100 - atoi(dataNode->GetText());
			}
		}

		auto equipped_item = ItemDatabase::GetItemByName(std::string(root->NextSibling()->FirstChild()->Value()));
		for (auto dataNode = root->NextSibling()->FirstChild()->FirstChild()->FirstChildElement();
			dataNode != nullptr;
			dataNode = dataNode->NextSiblingElement()) {
			if (std::string(dataNode->Name()) == "data1") {
				if (dataNode->GetText()) {
					//equipped_item.data1 = dataNode->GetText();
					continue;
				}
			}
			if (std::string(dataNode->Name()) == "data2") {
				if (dataNode->GetText()) {
					//equipped_item.data2 = dataNode->GetText();
					continue;
				}
			}
			if (std::string(dataNode->Name()) == "data3") {
				if (dataNode->GetText()) {
					//equipped_item.data3 = dataNode->GetText();
					continue;
				}
			}
			if (std::string(dataNode->Name()) == "data4") {
				if (dataNode->GetText()) {
					//equipped_item.data4 = dataNode->GetText();
					continue;
				}
			}
		}

		m_inventoryController.equipTwoHand(equipped_item);

		auto counter = 0U;
		for (
			tinyxml2::XMLNode* itemNode = root->NextSibling()->NextSibling()->FirstChild();
			itemNode != nullptr;
			itemNode = itemNode->NextSibling()) {

			if (std::string(itemNode->Value()) == "null_item") {
				counter++;

				continue;
			}

			auto item = ItemDatabase::GetItemByName(std::string(itemNode->Value()));
			for (auto dataNode = itemNode->FirstChild()->FirstChildElement();
				dataNode != nullptr;
				dataNode = dataNode->NextSiblingElement()) {
				if (std::string(dataNode->Name()) == "data1") {
					if (dataNode->GetText()) {
						//item.data1 = dataNode->GetText();
						continue;
					}
				}
				if (std::string(dataNode->Name()) == "data2") {
					if (dataNode->GetText()) {
						//item.data2 = dataNode->GetText();
						continue;
					}
				}
				if (std::string(dataNode->Name()) == "data3") {
					if (dataNode->GetText()) {
						//item.data3 = dataNode->GetText();
						continue;
					}
				}
				if (std::string(dataNode->Name()) == "data4") {
					if (dataNode->GetText()) {
						//item.data4 = dataNode->GetText();
						continue;
					}
				}
			}

			g_PlayerData.inventoryData.contents.at(counter) = item;

			counter++;
		}
	}
	catch (...) {
		spdlog::error("Failed to deserialize player data \'" + file + "\' in PlayerController::loadPlayerData()");
		return;
	}
}

void PlayerController::savePlayerData(std::string file)
{
	std::ofstream ofs_scene(file);
	cereal::XMLOutputArchive archive(ofs_scene);

	try {
		archive.setNextName("data");
		archive.startNode();
		{
			archive.setNextName("health");
			archive.startNode();
			archive(g_PlayerData.currentHealth);
			archive.finishNode();
		}
		archive.finishNode();

		archive.setNextName("equipped");
		archive.startNode();
		{
			archive.setNextName(m_inventoryController.getItemTwoHand().name.c_str());
			archive.startNode();
			archive(m_inventoryController.getItemTwoHandCopy());
			archive.finishNode();
		}
		archive.finishNode();

		archive.setNextName("inventory");
		archive.startNode();

		for (auto& item : g_PlayerData.inventoryData.contents) {
			archive.setNextName(item.name.c_str());
			archive.startNode();
			archive(item);
			archive.finishNode();
		}

		archive.finishNode();
	}
	catch (...) {
		spdlog::error("Failed to serialize player data in PlayerController::savePlayerData()");
	}
}
