#include "GoreManager.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/DecalManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/RenderComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"

#include "Game/Components/DamageReceiverComponent.h"

GoreManager* GoreManager::s_Instance = nullptr;

namespace
{
	// Blood sprites already in the tree. They are RGBA dark-red blobs, which is
	// exactly the format decal.frag wants — it mixes toward white where alpha is
	// 0 — so the same five files serve as both particle sprites and decals.
	constexpr int   _blood_texture_count = 5;
	constexpr float _splatter_ray_length = 10.0f;

	// How far past the wound to start the through-ray. A body is roughly half a
	// unit deep; starting inside it would just hit the target's own back faces.
	constexpr float _splatter_ray_offset = 0.6f;

	constexpr float _pool_ray_length = 3.0f;

	// A pool is several overlapping decals scattered around the drop point
	// rather than one disc — a single decal reads as a sticker, a cluster
	// reads as something that spread.
	constexpr int   _pool_decal_count  = 5;
	constexpr float _pool_spread       = 0.55f;   // world units around the centre

	// Radial fan: how far out from the body a ray starts. Must clear the
	// target's own collision volume — skinned meshes get BOUNDING-BOX
	// selectors here, so a ray launched from the centre would splatter the
	// decal onto the NPC's own box in mid-air instead of the wall behind it.
	constexpr float _radial_ray_offset = 0.7f;

	// Wound spray size relative to damage. A pistol tap should still read; a
	// shotgun slug should not blot out the screen.
	constexpr float _spray_damage_divisor = 30.0f;
	constexpr float _spray_scale_min      = 0.45f;
	constexpr float _spray_scale_max      = 2.5f;

	// Master multiplier on the PARTICLE effects only — the airborne spray, not
	// the decals it leaves. Applied last, inside spray(). Becomes the
	// 'gore_spray_scale' cvar in phase 3; decal coverage is tuned separately
	// by the splatter counts below.
	constexpr float _spray_scale_master = 1.0f;

	constexpr float _decal_lifetime = 90.0f;   // seconds; blood outlives bullet holes

	// --- Gib pool ----------------------------------------------------------
	constexpr size_t _gib_pool_size = 48;
	constexpr float  _gib_lifetime  = 25000.0f;  // ms before a settled gib is recycled

	// Meat, not brass. WeaponEffects bounces casings at 0.45 and lets them ring;
	// a gib should slap down and stay, so it keeps far less of its speed and
	// stops tumbling faster.
	constexpr float _gib_restitution   = 0.25f;
	constexpr float _gib_spin_damp     = 0.35f;
	constexpr float _gib_gravity       = 13.0f;
	constexpr int   _gib_max_bounces   = 3;
	constexpr float _gib_surface_clear = 0.04f;

	// Minimum ms between bounce decals from one gib, so a fast tumbler cannot
	// eat the whole blood decal budget on its own.
	constexpr float _gib_decal_interval = 70.0f;

	const irr::video::SColor _gib_tint(255, 96, 12, 12);

	const char* _gib_primitives[] = {
		"content/mesh/primitive/cube.obj",
		"content/mesh/primitive/sphere.obj",
		"content/mesh/primitive/double_tetrahedron.obj"
	};
}

// ---------------------------------------------------------------------------

void GoreManager::precache()
{
	if (m_precached)
		return;

	auto* pm = ParticleManager::Get();
	if (!pm)
		return;

	// A missing effect is survivable — spray() checks the spawn handle — so log
	// and carry on rather than refusing to arm the whole system.
	if (!pm->precache("blood_spray", _asset_psys("blood_spray")))
		spdlog::warn("GoreManager: blood_spray.psys failed to load; wounds will be decal-only");

	if (!pm->precache("blood_mist", _asset_psys("blood_mist")))
		spdlog::warn("GoreManager: blood_mist.psys failed to load; gib bursts lose their cloud");

	m_precached = true;
}

bool GoreManager::ensurePool()
{
	if (m_poolReady)
		return true;

	auto* rm = RenderManager::Get();
	if (!rm || !rm->sceneManager())
		return false;

	auto* smgr = rm->sceneManager();
	auto* manip = smgr->getMeshManipulator();

	// --- Meat texture -------------------------------------------------------
	// Generated rather than authored so the stand-in gibs need no art at all.
	// A little per-texel variation keeps them from reading as flat plastic.
	if (!m_gibTexture && rm->driver())
	{
		const irr::core::dimension2du size(16, 16);

		m_gibTexture = rm->driver()->addTexture(size, "gore_meat", irr::video::ECF_A8R8G8B8);

		if (m_gibTexture)
		{
			auto* px = static_cast<irr::u32*>(m_gibTexture->lock());

			if (px)
			{
				auto* rng = Engine::Get()->rng();

				for (irr::u32 i = 0; i < size.Width * size.Height; ++i)
				{
					const irr::u32 r = static_cast<irr::u32>(rng->getInt(70, 120));
					const irr::u32 g = static_cast<irr::u32>(rng->getInt(8,  22));
					const irr::u32 b = static_cast<irr::u32>(rng->getInt(8,  22));

					px[i] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
				}

				m_gibTexture->unlock();
			}
		}
	}

	// Private copies of the stand-in primitives. Vertex colours are set too, so
	// the gibs still read as meat if anything ever falls back to fixed-function.
	for (const char* path : _gib_primitives)
	{
		auto* src = smgr->getMesh(path);
		if (!src)
		{
			spdlog::warn("GoreManager: gib primitive '{}' missing", path);
			continue;
		}

		irr::scene::SMesh* copy = manip->createMeshCopy(src->getMesh(0));
		if (!copy)
			continue;

		manip->setVertexColors(copy, _gib_tint);

		m_gibMeshes.push_back(copy);
	}

	if (m_gibMeshes.empty())
	{
		spdlog::error("GoreManager: no gib meshes loaded; gibs disabled");
		m_poolReady = true;   // do not retry every kill
		return false;
	}

	m_gibs.resize(_gib_pool_size);

	for (auto& gib : m_gibs)
	{
		gib.node = smgr->addMeshSceneNode(m_gibMeshes[0]);
		if (!gib.node)
			continue;

		applyGibMaterial(gib.node);

		gib.node->setVisible(false);
	}

	m_poolReady = true;
	return true;
}

void GoreManager::applyGibMaterial(irr::scene::IMeshSceneNode* node) const
{
	if (!node)
		return;

	const auto perpixel = ShaderMaterialManager::get("phong_perpixel");

	if (perpixel != irr::video::EMT_SOLID)
		node->setMaterialType(perpixel);

	if (m_gibTexture)
		node->setMaterialTexture(0, m_gibTexture);

	node->setMaterialFlag(irr::video::EMF_LIGHTING, true);
	node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, true);

	// Meat is a DIELECTRIC. phong_perpixel reads metallic from SpecularColor's
	// ALPHA, and the .obj primitives arrive with it at 255 (COBJMeshFileLoader's
	// readColor sets alpha 255 unconditionally) - i.e. full metal. The shader's
	// diffuseFactor is (1 - metallic), so the albedo term is multiplied by zero
	// and all that survives is a specular reflection tinted by F0 = albedo. With
	// the dark red meat texture that is near-black, which is exactly how gibs
	// were rendering.
	//
	// Shell casings hit the identical default and look CORRECT, because brass
	// really is metal and a bright albedo makes the same maths read as shiny
	// brass. Same bug, opposite verdict - do not 'fix' WeaponEffects to match.
	//
	// RenderSystem does this for every entity mesh; raw pooled nodes never pass
	// through it, so it has to be done by hand here.
	for (irr::u32 i = 0; i < node->getMaterialCount(); ++i)
	{
		auto& mat = node->getMaterial(i);

		mat.Shininess = 0.0f;                // roughness 1.0 - meat is matte
		mat.SpecularColor.setAlpha(0);       // metallic 0 - dielectric
		mat.DiffuseColor.setAlpha(255);      // uAlpha - fully opaque
	}
}

GoreManager::Gib* GoreManager::acquireGib()
{
	Gib* oldest = nullptr;

	for (auto& gib : m_gibs)
	{
		if (!gib.node)
			continue;

		if (!gib.active)
			return &gib;

		if (!oldest || gib.spawnTime < oldest->spawnTime)
			oldest = &gib;
	}

	// Pool exhausted — steal the oldest rather than dropping the chunk.
	return oldest;
}

void GoreManager::throwGibs(const irr::core::vector3df& pos,
                            const irr::core::vector3df& dir,
                            int count, float power)
{
	if (goreLevel < 2 || count <= 0)
		return;

	if (!ensurePool() || m_gibMeshes.empty())
		return;

	auto* rng  = Engine::Get()->rng();
	const float now = static_cast<float>(Engine::Get()->getCurrentTime());

	irr::core::vector3df forward = dir;
	if (forward.getLengthSQ() < 0.0001f)
		forward.set(0.0f, 1.0f, 0.0f);
	forward.normalize();

	for (int i = 0; i < count; ++i)
	{
		Gib* gib = acquireGib();
		if (!gib || !gib->node)
			break;

		// Random direction biased downrange, with a strong upward component so
		// the burst reads as a pop rather than a spill across the floor.
		irr::core::vector3df v(
			rng->getFloat(-1.0f, 1.0f),
			rng->getFloat( 0.2f, 1.0f),
			rng->getFloat(-1.0f, 1.0f));

		if (v.getLengthSQ() < 0.0001f)
			v.set(0.0f, 1.0f, 0.0f);
		v.normalize();

		v = (v + forward * 0.85f);
		v.normalize();

		gib->velocity = v * (rng->getFloat(2.5f, 6.0f) * power);

		gib->angularVelocity.set(
			rng->getFloat(-720.0f, 720.0f),
			rng->getFloat(-720.0f, 720.0f),
			rng->getFloat(-720.0f, 720.0f));

		gib->rotation.set(
			rng->getFloat(0.0f, 360.0f),
			rng->getFloat(0.0f, 360.0f),
			rng->getFloat(0.0f, 360.0f));

		gib->spawnTime      = now;
		gib->lastTrailDecal = now;
		gib->bounceCount    = 0;
		gib->active         = true;
		gib->physicsActive  = true;

		// Per-axis independent scale: uniform lumps read as dice when they
		// tumble, irregular ones read as meat.
		gib->node->setMesh(m_gibMeshes[rng->getInt(0, static_cast<int>(m_gibMeshes.size()) - 1)]);

		// setMesh() just wiped the node's materials — rebind before it draws.
		applyGibMaterial(gib->node);

		gib->node->setScale(irr::core::vector3df(
			rng->getFloat(0.06f, 0.19f),
			rng->getFloat(0.06f, 0.19f),
			rng->getFloat(0.06f, 0.19f)));

		gib->node->setPosition(pos);
		gib->node->setRotation(gib->rotation);
		gib->node->setVisible(true);
	}
}

void GoreManager::updateGibs(float dt)
{
	if (m_gibs.empty())
		return;

	auto* rm = RenderManager::Get();

	const float dt_s = dt * 0.001f;
	const float now  = static_cast<float>(Engine::Get()->getCurrentTime());

	for (auto& gib : m_gibs)
	{
		if (!gib.active || !gib.node)
			continue;

		if (now - gib.spawnTime >= _gib_lifetime)
		{
			gib.active = false;
			gib.node->setVisible(false);
			continue;
		}

		if (!gib.physicsActive)
			continue;

		gib.velocity.Y -= _gib_gravity * dt_s;

		const irr::core::vector3df pos = gib.node->getPosition();
		irr::core::vector3df newPos    = pos + gib.velocity * dt_s;

		const float speed = gib.velocity.getLength();

		if (speed > 0.001f && rm)
		{
			// Cast along travel, not straight down — catches walls, ramps and
			// ceilings the same way the shell casings do.
			const irr::core::vector3df travel = gib.velocity / speed;

			RaycastResultData hit = rm->raycastWorldPosition(pos, newPos + travel * 0.1f, true);

			if (hit.hit)
			{
				irr::core::vector3df n = hit.normal;

				const float dot = gib.velocity.dotProduct(n);
				gib.velocity = (gib.velocity - n * (2.0f * dot)) * _gib_restitution;
				gib.angularVelocity *= _gib_spin_damp;

				newPos = hit.point + n * _gib_surface_clear;

				// Leave blood where it struck, throttled per gib.
				if (now - gib.lastTrailDecal >= _gib_decal_interval && rm->decals())
				{
					rm->decals()->spawn(hit.point, n, 0.5f, randomBloodTexture(),
						_decal_lifetime, DECAL_BLOOD);

					gib.lastTrailDecal = now;
				}

				if (++gib.bounceCount >= _gib_max_bounces)
				{
					gib.physicsActive = false;
					gib.velocity.set(0.0f, 0.0f, 0.0f);
					gib.angularVelocity.set(0.0f, 0.0f, 0.0f);
				}
			}
		}

		gib.node->setPosition(newPos);

		gib.rotation += gib.angularVelocity * dt_s;
		gib.node->setRotation(gib.rotation);
	}
}

void GoreManager::update(float dt)
{
	updateGibs(dt);
}

void GoreManager::clearScene()
{
	for (auto& gib : m_gibs)
	{
		gib.active        = false;
		gib.physicsActive = false;
		gib.bounceCount   = 0;
		gib.velocity.set(0.0f, 0.0f, 0.0f);
		gib.angularVelocity.set(0.0f, 0.0f, 0.0f);

		if (gib.node)
			gib.node->setVisible(false);
	}
}

void GoreManager::clear()
{
	for (auto& gib : m_gibs)
	{
		if (gib.node)
			gib.node->remove();
	}
	m_gibs.clear();

	// Nodes are removed first, so their grab on each mesh is released before the
	// drop that matches createMeshCopy's initial reference.
	for (auto* mesh : m_gibMeshes)
	{
		if (mesh)
			mesh->drop();
	}
	m_gibMeshes.clear();

	// The driver owns textures added through addTexture; removing it here keeps
	// a scene reload from accumulating one 'gore_meat' per load.
	if (m_gibTexture && RenderManager::Get() && RenderManager::Get()->driver())
		RenderManager::Get()->driver()->removeTexture(m_gibTexture);

	m_gibTexture = nullptr;
	m_poolReady  = false;
}

// ---------------------------------------------------------------------------

GORE_TIER GoreManager::tierFor(float overkill) const
{
	if (overkill >= gibRatio)
		return TIER_GIB;

	if (overkill >= messyRatio)
		return TIER_MESSY;

	return TIER_DEATH;
}

irr::core::vector3df GoreManager::bodyCentre(const anax::Entity& entity)
{
	if (entity.hasComponent<MeshComponent>())
	{
		auto& mesh = entity.getComponent<MeshComponent>();

		if (mesh.node)
		{
			return mesh.node->getTransformedBoundingBox().getCenter();
		}
	}

	if (entity.hasComponent<TransformComponent>())
		return entity.getComponent<TransformComponent>().position;

	return irr::core::vector3df(0.0f, 0.0f, 0.0f);
}

std::string GoreManager::randomBloodTexture() const
{
	const int n = Engine::Get()->rng()->getInt(1, _blood_texture_count);

	return g_texture_path + "particle/blood" + std::to_string(n) + ".png";
}

// ---------------------------------------------------------------------------

void GoreManager::spray(const std::string& effect,
                        const irr::core::vector3df& pos,
                        const irr::core::vector3df& dir,
                        float scale)
{
	auto* pm = ParticleManager::Get();
	if (!pm)
		return;

	const uint32_t handle = pm->spawn(effect, SPK::Vector3D(pos.X, pos.Y, pos.Z));
	if (!handle)
		return;

	// Direction has to be pushed immediately after spawn — the manager overrides
	// the emitters on the live clone, and a burst effect starts emitting on its
	// first update.
	pm->setEmitterDirection(handle, dir);

	const float finalScale = scale * _spray_scale_master;

	if (finalScale > 0.0f && finalScale != 1.0f)
		pm->setScale(handle, finalScale);
}

void GoreManager::splatterBehind(const irr::core::vector3df& origin,
                                 const irr::core::vector3df& dir,
                                 float size, int count, float spread)
{
	auto* rm = RenderManager::Get();
	if (!rm || !rm->decals())
		return;

	auto* rng = Engine::Get()->rng();

	// Build a basis around the shot so the cone can be spread in two axes.
	irr::core::vector3df forward = dir;
	if (forward.getLengthSQ() < 0.0001f)
		return;
	forward.normalize();

	irr::core::vector3df up = (fabsf(forward.Y) < 0.95f) ? irr::core::vector3df(0, 1, 0)
	                                                     : irr::core::vector3df(1, 0, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	up = right.crossProduct(forward).normalize();

	// Start past the target so the ray does not immediately hit its own back
	// faces. raycastWorldPosition has no node-exclusion parameter, so clearing
	// the body by offset is the available option.
	const irr::core::vector3df start = origin + forward * _splatter_ray_offset;

	for (int i = 0; i < count; ++i)
	{
		irr::core::vector3df d = forward
			+ right * rng->getFloat(-spread, spread)
			+ up    * rng->getFloat(-spread, spread);
		d.normalize();

		RaycastResultData hit = rm->raycastWorldPosition(start, start + d * _splatter_ray_length, true);

		if (!hit.hit)
			continue;

		const float s = size * rng->getFloat(0.7f, 1.35f);

		rm->decals()->spawn(hit.point, hit.normal, s, randomBloodTexture(), _decal_lifetime, DECAL_BLOOD);
	}
}

void GoreManager::splatterAround(const irr::core::vector3df& centre,
                                 int count, float size, float upBias)
{
	auto* rm = RenderManager::Get();
	if (!rm || !rm->decals() || count <= 0)
		return;

	auto* rng = Engine::Get()->rng();

	for (int i = 0; i < count; ++i)
	{
		// Y is biased downward: blood mostly goes on the floor and the lower
		// half of nearby walls, and an even sphere would waste most rays on
		// ceilings that are usually out of reach.
		irr::core::vector3df d(
			rng->getFloat(-1.0f, 1.0f),
			rng->getFloat(-1.0f, upBias),
			rng->getFloat(-1.0f, 1.0f));

		if (d.getLengthSQ() < 0.0001f)
			continue;
		d.normalize();

		// Start clear of the body's own collision volume, or the decal lands on
		// the NPC's bounding box in mid-air instead of the surface behind it.
		const irr::core::vector3df start = centre + d * _radial_ray_offset;

		RaycastResultData hit = rm->raycastWorldPosition(start, start + d * _splatter_ray_length, true);

		if (!hit.hit)
			continue;

		rm->decals()->spawn(hit.point, hit.normal, size * rng->getFloat(0.65f, 1.4f),
			randomBloodTexture(), _decal_lifetime, DECAL_BLOOD);
	}
}

void GoreManager::poolUnder(const irr::core::vector3df& pos, float size)
{
	auto* rm = RenderManager::Get();
	if (!rm || !rm->decals())
		return;

	auto* rng = Engine::Get()->rng();

	const irr::core::vector3df down(0.0f, -1.0f, 0.0f);

	// Several offset drops rather than one disc, so the pool has a ragged edge
	// and follows whatever the floor is actually doing underneath.
	for (int i = 0; i < _pool_decal_count; ++i)
	{
		const irr::core::vector3df offset(
			rng->getFloat(-_pool_spread, _pool_spread),
			0.0f,
			rng->getFloat(-_pool_spread, _pool_spread));

		const irr::core::vector3df start = pos + offset;

		RaycastResultData hit = rm->raycastWorldPosition(start, start + down * _pool_ray_length, true);

		if (!hit.hit)
			continue;

		rm->decals()->spawn(hit.point, hit.normal, size * rng->getFloat(0.6f, 1.25f),
			randomBloodTexture(), _decal_lifetime, DECAL_BLOOD);
	}
}

// ---------------------------------------------------------------------------

void GoreManager::wound(const anax::Entity& entity, const DamageContext& ctx, unsigned int damage)
{
	if (goreLevel <= 0)
		return;

	precache();

	const irr::core::vector3df point = ctx.valid ? ctx.point : bodyCentre(entity);

	// Blood leaves the wound back along the incoming shot, which is what the
	// surface normal already describes for a contact hit. Without a context we
	// have no shot to work from, so it goes up.
	irr::core::vector3df out = ctx.valid ? ctx.normal : irr::core::vector3df(0.0f, 1.0f, 0.0f);
	if (out.getLengthSQ() < 0.0001f)
		out.set(0.0f, 1.0f, 0.0f);
	out.normalize();

	const float scale = irr::core::clamp(
		damage / _spray_damage_divisor, _spray_scale_min, _spray_scale_max);

	spray("blood_spray", point, out, scale);

	// Splatter needs a through-direction; an entity-centre fallback has none.
	if (ctx.valid && !ctx.explosive)
	{
		splatterBehind(point, ctx.direction, 0.55f * scale, 3, 0.35f);
	}
}

GORE_TIER GoreManager::kill(const anax::Entity& entity, const DamageContext& ctx, float overkill)
{
	const GORE_TIER tier = tierFor(overkill);

	if (goreLevel <= 0)
		return tier;

	precache();

	const irr::core::vector3df point = ctx.valid ? ctx.point : bodyCentre(entity);

	irr::core::vector3df out = ctx.valid ? ctx.normal : irr::core::vector3df(0.0f, 1.0f, 0.0f);
	if (out.getLengthSQ() < 0.0001f)
		out.set(0.0f, 1.0f, 0.0f);
	out.normalize();

	// Death spray is bigger than a wound and grows with how hard the kill was.
	const float scale = irr::core::clamp(1.2f + overkill, 1.2f, 3.5f);

	spray("blood_spray", point, out, scale);

	if (ctx.valid && !ctx.explosive)
	{
		const int count = (tier >= TIER_MESSY) ? 8 : 5;

		splatterBehind(point, ctx.direction, 0.8f * scale, count, 0.45f);
	}

	// Radial fan on every death, not just gibs — this is what puts blood on the
	// floor around the body and up the nearby walls. Kept floorward (upBias
	// -0.1) for an ordinary death; TIER_GIB re-fans wider below.
	splatterAround(bodyCentre(entity), (tier >= TIER_MESSY) ? 10 : 6,
		0.7f + overkill * 0.25f, -0.1f);

	// Pool under the body. Cast from the entity centre rather than the wound so
	// a headshot does not pool halfway up a wall.
	poolUnder(bodyCentre(entity), 0.9f + overkill * 0.5f);

	const irr::core::vector3df downrange = ctx.valid
		? ctx.direction : irr::core::vector3df(0.0f, 1.0f, 0.0f);

	if (tier == TIER_GIB)
	{
		// The body is gone this frame — hidden immediately, entity queued for
		// removal. Doing it in this order matters: killEntityByID is deferred to
		// end of frame, so without the visibility change there is a tick where
		// the corpse and its own gibs are both on screen.
		removeBody(entity);

		const irr::core::vector3df centre = bodyCentre(entity);

		spray("blood_mist", centre, irr::core::vector3df(0.0f, 1.0f, 0.0f),
			irr::core::clamp(1.0f + overkill, 1.0f, 3.0f));

		const int count = irr::core::clamp(
			static_cast<int>(gibCount * (0.8f + overkill * 0.5f)), 6, 18);

		throwGibs(centre, downrange, count, irr::core::clamp(0.9f + overkill * 0.4f, 0.9f, 2.2f));

		// Radial splatter — the body was standing here and now it is not. Full
		// sphere (upBias 1.0) so a gib burst reaches the ceiling too.
		splatterAround(centre, 18, 0.95f, 1.0f);
	}
	else if (tier == TIER_MESSY)
	{
		// The body survives and plays its death animation, but it visibly lost
		// something — this is the band most shotgun and explosive kills land in.
		throwGibs(point, downrange, Engine::Get()->rng()->getInt(2, 4), 1.0f);
	}

	return tier;
}

void GoreManager::gib(const anax::Entity& entity, const DamageContext& ctx, float overkill)
{
	if (goreLevel <= 0)
		return;

	precache();

	// Centre, not the wound: a corpse gibbed by a final pistol round should come
	// apart around the body, not out of the one hole that finished it.
	const irr::core::vector3df centre = bodyCentre(entity);
	const irr::core::vector3df dir    = ctx.valid ? ctx.direction : irr::core::vector3df(0.0f, 1.0f, 0.0f);

	removeBody(entity);

	burst(centre, dir, irr::core::clamp(1.0f + overkill * 0.4f, 1.0f, 2.2f));
}

void GoreManager::removeBody(const anax::Entity& entity)
{
	// Visibility must go through the component. RenderSystem re-applies
	// node->setVisible(render.isVisible) every frame, so hiding the node alone
	// is undone before the frame is ever presented.
	if (entity.hasComponent<RenderComponent>())
		entity.getComponent<RenderComponent>().isVisible = false;

	if (entity.hasComponent<MeshComponent>())
	{
		auto& mesh = entity.getComponent<MeshComponent>();

		mesh.isVisible = false;

		if (mesh.node)
			mesh.node->setVisible(false);
	}

	if (entity.hasComponent<DescriptorComponent>())
	{
		auto& desc = entity.getComponent<DescriptorComponent>();

		desc.isAlive = false;

		WorldManager::Get()->killEntityByID(desc.id);
	}
}

void GoreManager::burst(const irr::core::vector3df& pos,
                        const irr::core::vector3df& dir,
                        float power)
{
	if (goreLevel <= 0)
		return;

	precache();

	irr::core::vector3df d = dir;
	if (d.getLengthSQ() < 0.0001f)
		d.set(0.0f, 1.0f, 0.0f);
	d.normalize();

	spray("blood_spray", pos, -d, irr::core::clamp(power * 1.5f, 1.0f, 4.0f));
	spray("blood_mist", pos, irr::core::vector3df(0.0f, 1.0f, 0.0f), irr::core::clamp(power, 1.0f, 3.0f));

	poolUnder(pos, 1.0f + power * 0.4f);

	throwGibs(pos, d, irr::core::clamp(static_cast<int>(gibCount * power * 0.8f), 5, 16), power);

	// Radial splatter so floor, walls and ceiling all catch something.
	splatterAround(pos, 16, 0.85f * power, 1.0f);
}
