#include "WeaponData.h"

#include <ISkinnedMesh.h>

#include <cmath>
#include <string>

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

	// Procedural idle breathing, for weapons that opted in because their idle clip
	// is a single frame. Advanced here rather than in the weapon's update() so it
	// keeps time even while a fire or reload clip is playing — a real hold does
	// not stop breathing to work the action.
	irr::core::vector3df breathPos(0.0f, 0.0f, 0.0f);
	irr::core::vector3df breathRot(0.0f, 0.0f, 0.0f);

	if (m_idleBreath.enabled)
	{
		m_idleBreathTime += dt_s;

		// Wrap to keep sinf's argument small enough to stay precise. The component
		// frequencies are deliberately incommensurate so there is no period that
		// would make this seamless; at three hours between wraps the sub-millimetre
		// discontinuity is not something a player can catch.
		if (m_idleBreathTime > 10000.0f)
			m_idleBreathTime -= 10000.0f;

		idleBreathOffsets(breathPos, breathRot);
	}

	// Single authoritative transform write: rest pose + sway + kick + breathing
	m_mesh.node->setPosition(m_viewPositionOffset + m_swayOffset + m_kickPos + breathPos);
	m_mesh.node->setRotation(m_viewRotationOffset + m_kickRot + breathRot);

	// Store current rotation for next frame
	m_lastCameraRotation = currentRotation;
}

bool PlayerWeapon::resolveMeshPart(const char* jointNamePrefix, MeshPart& outPart)
{
	outPart.buffers.clear();
	outPart.bone    = nullptr;
	outPart.visible = true;

	if (!m_mesh.node || !jointNamePrefix)
		return false;

	irr::scene::IAnimatedMesh* animated = m_mesh.node->getMesh();
	if (!animated || animated->getMeshType() != irr::scene::EAMT_SKINNED)
	{
		spdlog::warn("PlayerWeapon::resolveMeshPart('{}'): viewmodel mesh is not skinned", jointNamePrefix);
		return false;
	}

	auto* skinned = static_cast<irr::scene::ISkinnedMesh*>(animated);
	const irr::u32 bufferCount = static_cast<irr::u32>(m_mesh.node->getMaterialCount());
	const std::string prefix   = jointNamePrefix;

	const irr::core::array<irr::scene::ISkinnedMesh::SJoint*>& joints = skinned->getAllJoints();
	for (irr::u32 j = 0; j < joints.size(); ++j)
	{
		const irr::scene::ISkinnedMesh::SJoint* joint = joints[j];
		if (!joint || joint->AttachedMeshes.empty())
			continue;

		const std::string name = joint->Name.c_str();
		if (name.compare(0, prefix.size(), prefix) != 0)
			continue;

		for (irr::u32 a = 0; a < joint->AttachedMeshes.size(); ++a)
		{
			const irr::u32 buffer = joint->AttachedMeshes[a];
			if (buffer < bufferCount)
				outPart.buffers.push_back(buffer);
		}

		outPart.bone = m_mesh.node->getJointNode(joint->Name.c_str());
		break;
	}

	if (outPart.buffers.empty())
	{
		spdlog::warn("PlayerWeapon::resolveMeshPart('{}'): no joint with geometry matched — the part cannot be hidden",
			jointNamePrefix);
		return false;
	}

	outPart.material = m_mesh.node->getMaterial(outPart.buffers.front()).MaterialType;
	return true;
}

void PlayerWeapon::setMeshPartVisible(MeshPart& part, bool visible)
{
	if (!m_mesh.node || part.buffers.empty() || part.visible == visible)
		return;

	// Irrlicht has no per-mesh-buffer visibility flag, so this flips the material
	// type instead. CAnimatedMeshSceneNode::render() draws a buffer only when its
	// material's transparency matches the current pass, and viewmodels are drawn
	// by a bare render() after drawAll() has finished — where the pass is
	// ESNRP_NONE, so transparent buffers are skipped outright. That makes the swap
	// an exact hide, and it reverses for free.
	for (size_t b = 0; b < part.buffers.size(); ++b)
	{
		m_mesh.node->getMaterial(part.buffers[b]).MaterialType =
			visible ? part.material : irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	}

	part.visible = visible;
}

bool PlayerWeapon::meshPartWorldTransform(MeshPart& part, irr::core::matrix4& outWorld)
{
	if (!part.bone)
		return false;

	part.bone->updateAbsolutePosition();
	outWorld = part.bone->getAbsoluteTransformation();
	return true;
}

irr::core::vector3df PlayerWeapon::matchPartScale(const MeshPart& part,
                                                  const irr::core::vector3df& targetExtent) const
{
	const irr::core::vector3df fallback(m_viewScaleOffset);

	if (!m_mesh.node || !part.bone || part.buffers.empty())
		return fallback;

	irr::scene::IMesh* mesh = m_mesh.node->getMesh();
	if (!mesh || part.buffers.front() >= mesh->getMeshBufferCount())
		return fallback;

	const irr::core::vector3df partExtent =
		mesh->getMeshBuffer(part.buffers.front())->getBoundingBox().getExtent();
	const irr::core::vector3df worldScale =
		part.bone->getAbsoluteTransformation().getScale();

	// Part size in world units, and the stand-in's own size, as sortable axes
	float partAxes[3] = {
		partExtent.X * worldScale.X,
		partExtent.Y * worldScale.Y,
		partExtent.Z * worldScale.Z };
	float targetAxes[3] = { targetExtent.X, targetExtent.Y, targetExtent.Z };

	// Rank of each target axis (0 = shortest), so the ratio can be written back
	// to the right component after sorting.
	int rank[3] = { 0, 1, 2 };
	for (int a = 0; a < 3; ++a)
		for (int b = a + 1; b < 3; ++b)
			if (targetAxes[rank[b]] < targetAxes[rank[a]])
				std::swap(rank[a], rank[b]);

	std::sort(partAxes, partAxes + 3);

	irr::core::vector3df scale = fallback;
	float* out[3] = { &scale.X, &scale.Y, &scale.Z };

	for (int r = 0; r < 3; ++r)
	{
		const float targetAxis = targetAxes[rank[r]];
		if (targetAxis <= 0.0f || partAxes[r] <= 0.0f)
			return fallback;

		*out[rank[r]] = partAxes[r] / targetAxis;
	}

	return scale;
}

void PlayerWeapon::enableIdleBreathing(float scale)
{
	m_idleBreath.enabled = true;
	m_idleBreath.scale   = scale;

	// Stagger the starting phase per weapon so two weapons enabled with the same
	// settings are not breathing in lockstep after a switch.
	m_idleBreathTime = Engine::Get()->rng()->getFloat(0.0f, 100.0f);
}

// Sum of sines at incommensurate frequencies.
//
// The point of the irrational multipliers (root 2, 3, 5) is that no two
// components share a period, so the composite path never closes and the motion
// never visibly loops — which is the whole reason to do this procedurally
// instead of playing a short cyclic clip. A single sine per axis, or ratios like
// 2:1, would read as a mechanical wobble within a couple of seconds.
//
// The split of duties matters as much as the curve: breathing owns the vertical
// and the pitch (chest rise tips the muzzle), while the slower postural drift
// owns lateral position, yaw and roll. Driving everything from one oscillator is
// what makes procedural idles look like a bobbing prop rather than a held object.
void PlayerWeapon::idleBreathOffsets(irr::core::vector3df& outPosition,
                                     irr::core::vector3df& outRotation) const
{
	const float t   = m_idleBreathTime;
	const float tau = 6.2831853f;
	const float s   = m_idleBreath.scale;

	// Breathing, plus a weak second harmonic so the inhale is a little sharper
	// than the exhale instead of a symmetrical sine.
	const float w = tau * m_idleBreath.breathRate;
	const float breath = sinf(w * t) * 0.85f
	                   + sinf(w * 2.0f * t + 1.1f) * 0.15f;

	// Postural drift components
	const float d = tau * m_idleBreath.driftRate;
	const float d1 = sinf(d * 1.000f * t);
	const float d2 = sinf(d * 1.414f * t + 2.4f); // root 2
	const float d3 = sinf(d * 1.732f * t + 4.1f); // root 3
	const float d4 = sinf(d * 2.236f * t + 0.7f); // root 5

	outPosition.X = (d1 * 0.60f + d3 * 0.40f) * m_idleBreath.driftPosX * s;
	outPosition.Y = (breath * m_idleBreath.breathPosY
	                 + d2 * m_idleBreath.driftPosX * 0.25f) * s;
	outPosition.Z = (d4 * 0.70f + d2 * 0.30f) * m_idleBreath.driftPosZ * s;

	outRotation.X = (breath * m_idleBreath.breathPitch
	                 + d2 * m_idleBreath.driftYaw * 0.15f) * s;
	outRotation.Y = (d1 * 0.55f + d4 * 0.45f) * m_idleBreath.driftYaw * s;
	outRotation.Z = (d3 * 0.60f + d2 * 0.40f) * m_idleBreath.driftRoll * s;
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

bool PlayerWeapon::playAnimation(const std::string& name)
{
	if (!m_mesh.node)
		return false;

	const sAnimationData* clip = m_mesh.findAnimation(name);

	if (!clip)
	{
		spdlog::warn("In function PlayerWeapon::playAnimation() : '{}' has no animation '{}'", m_descriptor.name, name);

		return false;
	}

	m_mesh.lastPlayedAnimation = *clip;

	m_mesh.node->setLoopMode(clip->loop);
	m_mesh.node->setFrameLoop(clip->frames.X, clip->frames.Y);

	return true;
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

void PlayerWeapon::precacheSharedSounds()
{
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/equip.wav",   true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/unequip.wav", true);
}

// Both transition sounds share one pool group so cycling the mouse wheel through
// the whole weapon list stacks a couple of voices rather than a dozen.
void PlayerWeapon::playEquipSound()
{
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/equip", 0.04f, 2, -1.0f, "weapon_transition");
}

void PlayerWeapon::playUnequipSound()
{
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/unequip", 0.04f, 2, -1.0f, "weapon_transition");
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