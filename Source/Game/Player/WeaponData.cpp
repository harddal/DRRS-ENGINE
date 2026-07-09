#include "WeaponData.h"

#include "Engine/World/WorldManager.h"

// Define the static member for ImpactBurnShaderCallback
float ImpactBurnShaderCallback::currentSpawnTime = 0.0f;

void PlayerWeapon::updateWeaponSway(float dt)
{
	if (!m_mesh.node)
		return;

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	float dt_s = dt / 1000.0f;

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df currentRotation = camera.camera->getRotation();

	// Calculate rotation delta (how much camera moved this frame)
	irr::core::vector3df rotationDelta = currentRotation - m_lastCameraRotation;

	// Sway target based on rotation delta.
	// Negative values create lag effect (weapon moves opposite to camera initially)
	float swayX = -rotationDelta.Y * m_swayAmount; // Horizontal camera movement
	float swayY = rotationDelta.X * m_swayAmount;  // Vertical camera movement

	// Clamp sway to prevent extreme offsets during fast camera movement
	const float maxSwayOffset = 0.15f; // Maximum offset in any direction
	swayX = std::max(-maxSwayOffset, std::min(swayX, maxSwayOffset));
	swayY = std::max(-maxSwayOffset, std::min(swayY, maxSwayOffset));

	// Smooth the sway offset itself (not the node position) so the lag effect is
	// preserved while the kick spring below stays instant and undiluted.
	float smoothingFactor = m_swaySmoothing * dt_s * 60.0f; // Normalize to 60 FPS baseline
	smoothingFactor = std::min(smoothingFactor, 1.0f);      // Clamp to prevent overshoot
	irr::core::vector3df swayTarget(swayX, swayY, 0.0f);
	m_swayOffset += (swayTarget - m_swayOffset) * smoothingFactor;

	// Exponential spring recovery for the view kick
	float posFactor = std::min(m_kickPosRecovery * dt_s, 1.0f);
	float rotFactor = std::min(m_kickRotRecovery * dt_s, 1.0f);
	m_kickPos -= m_kickPos * posFactor;
	m_kickRot -= m_kickRot * rotFactor;

	// Single authoritative transform write: rest pose + sway + kick
	m_mesh.node->setPosition(m_viewPositionOffset + m_swayOffset + m_kickPos);
	m_mesh.node->setRotation(m_viewRotationOffset + m_kickRot);

	// Store current rotation for next frame
	m_lastCameraRotation = currentRotation;
}

void PlayerWeapon::addViewKick(const irr::core::vector3df& posKick, const irr::core::vector3df& rotKick)
{
	m_kickPos += posKick;
	m_kickRot += rotKick;
}

void PlayerWeapon::resetViewKick()
{
	m_kickPos = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_kickRot = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_swayOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
}

// --- Hit-confirmation feedback (shared static state — one player, one crosshair) ---

static float s_hitFlashTime  = -1.0e9f;  // last confirmed hit (ms)
static float s_killFlashTime = -1.0e9f;  // last confirmed kill (ms)
static float s_lastHitTick   = -1.0e9f;  // hit-tick sound rate limiter (ms)

void PlayerWeapon::registerHitFeedback(HIT_RESULT result)
{
	if (result == HIT_RESULT::NONE)
		return;

	float now = Engine::Get()->getCurrentTime();

	if (result == HIT_RESULT::KILL)
	{
		s_killFlashTime = now;
		// Kill confirm: low, weighty ping — always plays
		SoundManager::Get()->sound()->play2D("content/sound/effect/ping.wav",
			false, 0, 0.6f, nullptr, false, 0.65f);
	}
	else
	{
		s_hitFlashTime = now;
		// Hit tick: short high ping — rate-limited so beam/auto weapons don't spam
		const float tickInterval = 90.0f; // ms
		if (now - s_lastHitTick >= tickInterval)
		{
			s_lastHitTick = now;
			SoundManager::Get()->sound()->play2D("content/sound/effect/ping.wav",
				false, 0, 0.3f, nullptr, false, 1.45f);
		}
	}
}

void PlayerWeapon::drawHitFeedback()
{
	static irr::video::ITexture* markerTex = nullptr;
	if (!markerTex)
	{
		markerTex = RenderManager::Get()->driver()->getTexture("content/texture/ui/cross-hit.png");
		if (!markerTex)
			return;
	}

	float now = Engine::Get()->getCurrentTime();

	const float killDuration = 320.0f;
	const float hitDuration  = 150.0f;

	bool showKill = (now - s_killFlashTime) < killDuration;
	bool showHit  = (now - s_hitFlashTime)  < hitDuration;
	if (!showKill && !showHit)
		return;

	// Kill display takes priority over a plain hit
	float progress = showKill
		? (now - s_killFlashTime) / killDuration
		: (now - s_hitFlashTime)  / hitDuration;

	irr::u32 alpha = (irr::u32)((1.0f - progress) * 255.0f);
	irr::video::SColor color = showKill
		? irr::video::SColor(alpha, 255, 60, 60)     // red — you dropped it
		: irr::video::SColor(alpha, 255, 255, 255);  // white — you tagged it

	// Marker expands slightly as it fades; kill marker is larger
	int half = (showKill ? 22 : 16) + (int)(progress * 5.0f);
	int cx = RenderManager::Get()->getConfiguration().width / 2;
	int cy = RenderManager::Get()->getConfiguration().height / 2;

	RenderManager::Get()->renderImage2DScaled(markerTex,
		irr::core::rect<irr::s32>(cx - half, cy - half, cx + half, cy + half), color);
}

irr::core::vector3df PlayerWeapon::getCrosshairAimPoint(float maxRange)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return irr::core::vector3df(0.0f, 0.0f, 0.0f);

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df camPos  = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (camera.camera->getTarget() - camPos).normalize();
	irr::core::vector3df rayEnd  = camPos + forward * maxRange;

	RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(camPos, rayEnd, true);
	return hit.hit ? hit.point : rayEnd;
}

irr::core::vector3df PlayerWeapon::getAimDirection(const irr::core::vector3df& origin, float maxRange)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return irr::core::vector3df(0.0f, 0.0f, 1.0f);

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df camPos  = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (camera.camera->getTarget() - camPos).normalize();

	irr::core::vector3df toAim = getCrosshairAimPoint(maxRange) - origin;

	// Degenerate cases: aim point behind/at the muzzle (wall hugging) — a converging
	// direction would swing wildly or point backwards, so fall back to camera forward.
	if (toAim.getLength() < 0.5f || toAim.dotProduct(forward) <= 0.0f)
		return forward;

	return toAim.normalize();
}