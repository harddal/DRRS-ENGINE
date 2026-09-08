#include "GoreManager.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/DecalManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Sound/SoundManager.h"
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
	// 0 — so the same files serve as both particle sprites and decals.
	constexpr int   _blood_texture_count = 7;
	constexpr float _splatter_ray_length = 8.0f;

	// One place both the random pick and the warm-up build the path, so a
	// warmed set can never drift out of step with the set actually spawned.
	std::string bloodTexturePath(int n)
	{
		return g_texture_path + "particle/blood" + std::to_string(n) + ".png";
	}

	// How far past the wound to start the through-ray. A body is roughly half a
	// unit deep; starting inside it would just hit the target's own back faces.
	constexpr float _splatter_ray_offset = 0.6f;

	constexpr float _pool_ray_length = 3.0f;

	// A pool is several overlapping decals scattered around the drop point
	// rather than one disc — a single decal reads as a sticker, a cluster
	// reads as something that spread.
	constexpr int   _pool_decal_count  = 7;
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

	constexpr float _decal_lifetime = 180.0f;   // seconds; blood outlives bullet holes

	// --- Gib pool ----------------------------------------------------------
	constexpr size_t _gib_pool_size = 256;
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

	// Low-poly meat chunks from Tools/generate_gib_meshes.py — convex hulls,
	// flat-shaded, authored at unit extent so the scale range above still holds.
	// Eight silhouettes because a gib burst throws ten at once and repeats in a
	// single burst are what make a pool read as a pool.
	const char* _gib_meshes[] = {
		"content/mesh/gib/gib_chunk1.obj",
		"content/mesh/gib/gib_chunk2.obj",
		"content/mesh/gib/gib_chunk3.obj",
		"content/mesh/gib/gib_lump.obj",
		"content/mesh/gib/gib_sliver1.obj",
		"content/mesh/gib/gib_sliver2.obj",
		"content/mesh/gib/gib_slab.obj",
		"content/mesh/gib/gib_shard.obj"
	};

	// The gore material. ORM is generated from the ao/roughness pair by the same
	// script; see applyGibMaterial() for why it binds to two slots.
	const char* _gib_tex_colour = "content/texture/gib/others_0001_color_1k.jpg";
	const char* _gib_tex_normal = "content/texture/gib/others_0001_normal_opengl_1k.png";
	const char* _gib_tex_orm    = "content/texture/gib/others_0001_orm_1k.png";

	// --- Sound -------------------------------------------------------------
	// Extensionless bases for playRandomized3D's contiguous scan: gib_1..gib_2
	// is the body coming apart, impact_1..impact_3 is a chunk landing.
	const char* _snd_gib    = "content/sound/effect/gore/gib_";
	const char* _snd_impact = "content/sound/effect/gore/impact_";

	// A burst throws up to eighteen chunks that all land within a second or so.
	// Every one of them playing is a wall of noise and clips SoLoud's additive
	// mixer, so impacts share one pool: at most three at a time, cut in volume,
	// and only the first bounce of any given gib is heard.
	constexpr int   _snd_impact_max_voices = 3;
	constexpr float _snd_impact_volume     = 0.5f;
	const char*     _snd_impact_pool       = "gore_impact";

	// Below this the gib is settling rather than hitting, and a slap would read
	// as a glitch. World units/second.
	constexpr float _snd_impact_min_speed = 2.2f;

	// Radius of full volume before inverse-distance rolloff begins.
	//
	// loadOrGetSource() defaults every source to 30, which at this world scale
	// is most of a level — gore would play flat-out loud at any combat range,
	// panned but with no distance cue at all. The placed ambients in content
	// sit at 1.5; gunfire keeps the 30 default. Gore belongs in between: a body
	// coming apart should carry across a room, a single chunk landing should
	// not.
	constexpr float _snd_gib_min_dist    = 10.0f;
	constexpr float _snd_impact_min_dist = 4.0f;
}

// ---------------------------------------------------------------------------

// Register the blood effects with ParticleManager, re-registering them whenever
// they have gone away.
//
// This deliberately sits OUTSIDE the m_precached guard, and is retried from
// update(). Engine::clearScene() calls ParticleManager::clear(), which erases
// the entire effect table, and it runs on every editor<->game transition —
// while precache() is only ever reached once, from GameplaySystem::init() in
// the WorldManager constructor. A one-shot flag around these two calls meant
// the blood effects survived exactly one game session: every session after the
// first logged "spawn: unknown effect 'blood_spray'" and silently produced no
// blood until the process was restarted.
//
// ParticleManager::precache() returns early on a name it already holds, so the
// steady-state cost of calling this every frame is two hash lookups.
void GoreManager::ensureEffects()
{
	// A load failure means the .psys is genuinely absent — latch off rather than
	// re-reading a missing file (and re-logging it) once per frame forever.
	if (m_effectsUnavailable)
		return;

	auto* pm = ParticleManager::Get();
	if (!pm)
		return;

	// A missing effect is survivable — spray() checks the spawn handle — so log
	// and carry on rather than refusing to arm the whole system.
	bool ok = true;

	if (!pm->precache("blood_spray", _asset_psys("blood_spray")))
	{
		spdlog::warn("GoreManager: blood_spray.psys failed to load; wounds will be decal-only");
		ok = false;
	}

	if (!pm->precache("blood_mist", _asset_psys("blood_mist")))
	{
		spdlog::warn("GoreManager: blood_mist.psys failed to load; gib bursts lose their cloud");
		ok = false;
	}

	m_effectsUnavailable = !ok;
}

void GoreManager::precache()
{
	// Ahead of the guard below: these have their own flags, so a precache() on a
	// later scene load still gets a chance to warm textures that could not be
	// fetched the first time round, and to re-register the particle effects that
	// the last clearScene() destroyed.
	warmBloodTextures();
	ensureEffects();

	if (m_precached)
		return;

	// Warm the gore samples and set their 3D falloff. playRandomized3D would
	// load them lazily, but the first gib burst is exactly the frame that can
	// least afford a decode — and it is the frame that plays all five at once.
	//
	// The min-distance pass has to happen here rather than at the call site:
	// it is a property of the SOURCE, not the voice, and playRandomized3D
	// exposes no way to reach the source it picked. Doing it once on the
	// preloaded pointers covers every later variant roll, since the variant
	// scan resolves through the same m_sources cache.
	if (auto* sm = SoundManager::Get())
	{
		if (auto* snd = sm->sound())
		{
			for (int i = 1; i <= 2; ++i)
			{
				const std::string file = std::string(_snd_gib) + std::to_string(i) + ".wav";

				if (SoundSource* src = snd->getSoundSource(file.c_str(), true))
					src->setDefaultMinDistance(_snd_gib_min_dist);
				else
					spdlog::warn("GoreManager: missing gore sample '{}'", file);
			}

			for (int i = 1; i <= 3; ++i)
			{
				const std::string file = std::string(_snd_impact) + std::to_string(i) + ".wav";

				if (SoundSource* src = snd->getSoundSource(file.c_str(), true))
					src->setDefaultMinDistance(_snd_impact_min_dist);
				else
					spdlog::warn("GoreManager: missing gore sample '{}'", file);
			}
		}
	}

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

	// --- Meat material ------------------------------------------------------
	// A missing map is survivable — applyGibMaterial() binds only what loaded,
	// and the shader's uHas* gates fall back to the scalar uniforms — so a
	// warning is enough. Losing the colour map is what actually hurts, since
	// phong_perpixel takes albedo from tDiffuse alone and never reads vertex
	// colour; without it a gib renders in the flat material colour.
	if (rm->driver())
	{
		if (!m_gibTexture)
			m_gibTexture = rm->driver()->getTexture(_gib_tex_colour);

		if (!m_gibNormal)
			m_gibNormal = rm->driver()->getTexture(_gib_tex_normal);

		if (!m_gibORM)
			m_gibORM = rm->driver()->getTexture(_gib_tex_orm);

		if (!m_gibTexture)
			spdlog::warn("GoreManager: gib colour map '{}' missing", _gib_tex_colour);
	}

	// Private copies of the chunk meshes. Vertex colours are set too, so the
	// gibs still read as meat if anything ever falls back to fixed-function.
	for (const char* path : _gib_meshes)
	{
		auto* src = smgr->getMesh(path);
		if (!src)
		{
			spdlog::warn("GoreManager: gib mesh '{}' missing", path);
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
		node->setMaterialTexture(SLOT_DIFFUSE, m_gibTexture);

	if (m_gibNormal)
		node->setMaterialTexture(SLOT_NORMAL, m_gibNormal);

	// ORM goes into BOTH slots on purpose. RenderManager decides whether the R
	// channel is real ambient occlusion by testing whether the roughness and
	// metallic slots hold the SAME texture pointer — a standalone greyscale
	// roughness map has R == G, so reading AO from one unconditionally would
	// darken every rough surface in the game. Binding one texture twice is the
	// signal, and it is what GltfImport does for every imported material.
	if (m_gibORM)
	{
		node->setMaterialTexture(SLOT_ROUGHNESS, m_gibORM);
		node->setMaterialTexture(SLOT_METALLIC,  m_gibORM);
	}

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
	//
	// These are the FALLBACK values now: with the ORM map bound the shader takes
	// roughness and metallic per-texel and ignores both fields. They still have
	// to be right, because a missing texture file drops straight back to them.
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

				// Slap on the FIRST bounce only, and only if it arrived with
				// some speed behind it. Later bounces are the chunk settling;
				// eighteen of those overlapping is mush, not impact.
				if (gib.bounceCount == 0 && speed >= _snd_impact_min_speed)
				{
					if (auto* sm = SoundManager::Get())
					{
						if (auto* snd = sm->sound())
						{
							snd->playRandomized3D(_snd_impact, hit.point, 0.12f,
								_snd_impact_max_voices, _snd_impact_volume, _snd_impact_pool);
						}
					}
				}

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
	// Build everything on the first frame the renderer is up rather than on the
	// first gib. ensurePool() alone loads eight meshes and three maps and builds
	// 128 scene nodes; paying that mid-fight is a visible hitch, and the first
	// gib is the worst possible frame to pay it in.
	//
	// All three calls are self-guarding, so this costs a couple of bool tests and
	// two hash lookups per frame afterwards. The texture warm and the pool build
	// are not per-scene in practice either: the pool nodes are raw nodes owned by
	// no entity, so Engine::clearScene() retires them (GoreManager::clearScene())
	// instead of destroying them, and m_poolReady only resets in clear() at
	// shutdown. ensureEffects() IS per-scene — clearScene() destroys the particle
	// effects outright — which is exactly why it is retried here.
	warmBloodTextures();
	ensureEffects();
	ensurePool();

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

	// The maps are file textures out of the driver's cache, not something this
	// manager created, so they are shared and must NOT be removed here — the
	// driver frees the cache at shutdown. Dropping the pointers is the whole job;
	// ensurePool() re-fetches them and the cache hands back the same objects.
	m_gibTexture = nullptr;
	m_gibNormal  = nullptr;
	m_gibORM     = nullptr;
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
	return bloodTexturePath(Engine::Get()->rng()->getInt(1, _blood_texture_count));
}

void GoreManager::warmBloodTextures()
{
	if (m_bloodWarmed)
		return;

	auto* rm = RenderManager::Get();

	// Not an error — the renderer may simply not be up yet. update() calls back
	// every frame until it is, which is still long before any gore can happen.
	if (!rm || !rm->driver())
		return;

	// DecalManager::spawn() resolves its texture from the PATH on every call and
	// the driver caches by name, so one getTexture() per file is the whole job:
	// every later spawn hits that cache. Without it the first gib burst pays for
	// up to seven PNG decodes in the one frame it can least afford them.
	for (int n = 1; n <= _blood_texture_count; ++n)
	{
		const std::string path = bloodTexturePath(n);

		if (!rm->driver()->getTexture(path.c_str()))
			spdlog::warn("GoreManager: blood decal texture '{}' missing", path);
	}

	m_bloodWarmed = true;
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

		// The wet pop. burst() plays its own, and this path does not route
		// through burst(), so there is no double-up.
		if (auto* sm = SoundManager::Get())
		{
			if (auto* snd = sm->sound())
				snd->playRandomized3D(_snd_gib, centre, 0.08f);
		}

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

	if (auto* sm = SoundManager::Get())
	{
		if (auto* snd = sm->sound())
			snd->playRandomized3D(_snd_gib, pos, 0.08f);
	}

	spray("blood_spray", pos, -d, irr::core::clamp(power * 1.5f, 1.0f, 4.0f));
	spray("blood_mist", pos, irr::core::vector3df(0.0f, 1.0f, 0.0f), irr::core::clamp(power, 1.0f, 3.0f));

	poolUnder(pos, 1.0f + power * 0.4f);

	throwGibs(pos, d, irr::core::clamp(static_cast<int>(gibCount * power * 0.8f), 5, 16), power);

	// Radial splatter so floor, walls and ceiling all catch something.
	splatterAround(pos, 16, 0.85f * power, 1.0f);
}
