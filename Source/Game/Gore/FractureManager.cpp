#include "FractureManager.h"
#include "FractureGeometry.h"

#include <cmath>

#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/RenderComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"

#include "Game/Components/DamageReceiverComponent.h"

FractureManager* FractureManager::s_Instance = nullptr;

namespace
{
	constexpr size_t _shard_pool_size = 64;
	constexpr float  _shard_lifetime  = 20000.0f;  // ms before a settled shard is recycled

	// Never generate fewer than a break's worth or more than the pool can hold
	// in one go — a 200-cell crate would evict its own shards mid-flight.
	constexpr int _min_cells = 2;
	constexpr int _max_cells = 32;

	constexpr float _shard_surface_clear = 0.03f;

	// Voronoi seeds are random, so ONE cached pattern per mesh would make every
	// crate in the level shatter along identical lines. A handful of variants,
	// picked at random per break, buys the variety back for a bounded one-off
	// generation cost instead of re-clipping the mesh on every death.
	constexpr int _pattern_variants = 3;

	// Upward bias added to every launch on top of its radial direction, so a
	// break pops rather than slumping outward along the floor.
	constexpr float _launch_up_bias = 0.3f;

	// Break puffs are borrowed effects, not purpose-built ones — keep them small.
	constexpr float _break_effect_scale = 0.35f;

	// De-clumping. Shards land on top of each other because they all launch from
	// roughly one point, and with no actor they have nothing to collide against.
	// A few frames of pairwise relaxation once they settle spreads the pile out.
	constexpr int   _separate_frames    = 90;     // budget per shard, then it stops
	constexpr float _separate_rate      = 0.12f;  // fraction of overlap per frame
	constexpr float _separate_max_step  = 0.05f;  // world units/frame, anti-pop clamp
	constexpr float _separate_probe_up  = 0.25f;
	constexpr float _separate_probe_down = 1.0f;
	constexpr float _separate_radius_scale = 0.6f;

	struct ProfileData
	{
		float restitution;
		float spinDamp;
		float gravity;
		int   maxBounces;

		// CUT FACES ONLY — the outer skin inherits the prop's own material.
		// phong_perpixel derives roughness from Shininess and metallic from
		// SpecularColor's ALPHA. Irrlicht's SMaterial defaults that alpha to 255
		// — full metal — and the shader's diffuseFactor is (1 - metallic), so a
		// default material multiplies its own albedo by zero and renders BLACK.
		// Every material here must set it explicitly; see applyShardMaterial().
		float shininess;
		irr::u32 specularAlpha;

		// Puff fired at the break. STAND-INS reusing effects that already exist
		// (there is no dust .psys in the tree yet); empty means no effect.
		const char* breakEffect;
	};

	// Stand-in interior colours, used only when no authored
	// content/texture/fracture/interior_<profile>.png is present.
	struct InteriorTint { irr::u8 r, g, b; int jitter; bool grain; };

	const InteriorTint _interior_tints[FRACTURE_PROFILE_COUNT] = {
		{ 150, 112,  68, 14, true  },   // WOOD  - pale timber, banded grain
		{ 138, 136, 130, 18, false },   // STONE - grey aggregate speckle
		{ 168, 172, 178, 10, false },   // METAL - bright torn steel
		{ 198, 220, 234,  8, false }    // GLASS - pale blue-white edge
	};

	// Wood tumbles and stops; stone barely bounces at all; metal rings and
	// skitters; glass skips a few times before settling.
	const ProfileData _profiles[FRACTURE_PROFILE_COUNT] = {
		//  rest  spin   grav  bnc  shin  specA  breakEffect
		{ 0.20f, 0.40f, 13.0f, 3,   0.0f,   0, "explosion_smoke" },  // WOOD  - matte dielectric
		{ 0.15f, 0.30f, 15.0f, 2,   0.0f,   0, "explosion_smoke" },  // STONE - matte dielectric
		{ 0.45f, 0.65f, 13.0f, 5,  32.0f, 180, "spark"           },  // METAL - torn steel, rough not mirror
		{ 0.30f, 0.55f, 12.0f, 4,  64.0f,   0, ""                }   // GLASS - glossy dielectric, no fitting effect in the tree yet
	};

	const ProfileData& profileData(int profile)
	{
		if (profile < 0 || profile >= FRACTURE_PROFILE_COUNT)
			return _profiles[FRACTURE_WOOD];

		return _profiles[profile];
	}

	// Irrlicht's own createCubeMesh winds front faces CLOCKWISE as seen from
	// outside: its -Z quad is listed counter-clockwise (0,1,2,3) and indexed
	// (0,2,1),(0,3,2). Every face below follows that same convention — quad
	// corners listed CCW from outside, indices reversed on emit. Get this
	// backwards and the shards render inside-out under back-face culling.
	struct FaceAxes
	{
		irr::core::vector3df normal;
		irr::core::vector3df u;
		irr::core::vector3df v;
	};

	const FaceAxes _box_faces[6] = {
		{ {  0.0f,  0.0f, -1.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f } },  // -Z
		{ {  0.0f,  0.0f,  1.0f }, { -1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f } },  // +Z
		{ { -1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },  // -X
		{ {  1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f } },  // +X
		{ {  0.0f, -1.0f,  0.0f }, {  0.0f, 0.0f,  1.0f }, { 1.0f, 0.0f, 0.0f } },  // -Y
		{ {  0.0f,  1.0f,  0.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f } }   // +Y
	};

	// Safe divide for UV normalisation — a perfectly flat prop has a zero extent
	// on one axis and would otherwise produce inf UVs.
	float safeExtent(float e)
	{
		return (e > 0.0001f) ? e : 1.0f;
	}
}

const char* fractureProfileName(int profile)
{
	switch (profile)
	{
	case FRACTURE_WOOD:  return "Wood";
	case FRACTURE_STONE: return "Stone";
	case FRACTURE_METAL: return "Metal";
	case FRACTURE_GLASS: return "Glass";
	default:             return "Wood";
	}
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

irr::scene::SMesh* FractureManager::buildBoxMesh(const irr::core::aabbox3df& cell,
                                                 const irr::core::aabbox3df& whole)
{
	using namespace irr;

	auto* buffer = new scene::SMeshBuffer();

	buffer->Vertices.reallocate(24);
	buffer->Indices.reallocate(36);

	const core::vector3df centre = cell.getCenter();
	const core::vector3df half   = cell.getExtent() * 0.5f;

	const core::vector3df wmin = whole.MinEdge;
	const core::vector3df wext = whole.getExtent();

	const video::SColor white(255, 255, 255, 255);

	for (const FaceAxes& face : _box_faces)
	{
		// Face centre, then the two in-plane half-spans. Multiplying the axis
		// component-wise by the half-extent picks the matching span without a
		// per-face switch.
		const core::vector3df fc = centre + core::vector3df(
			face.normal.X * half.X, face.normal.Y * half.Y, face.normal.Z * half.Z);

		const core::vector3df hu(
			face.u.X * half.X, face.u.Y * half.Y, face.u.Z * half.Z);
		const core::vector3df hv(
			face.v.X * half.X, face.v.Y * half.Y, face.v.Z * half.Z);

		const core::vector3df corner[4] = {
			fc - hu - hv,
			fc + hu - hv,
			fc + hu + hv,
			fc - hu + hv
		};

		const u16 base = static_cast<u16>(buffer->Vertices.size());

		for (const core::vector3df& p : corner)
		{
			// Planar UV normalised against the WHOLE prop, not this cell, so the
			// source texture stays continuous across the break.
			float tu, tv;

			if (face.normal.X != 0.0f)
			{
				tu =        (p.Z - wmin.Z) / safeExtent(wext.Z);
				tv = 1.0f - (p.Y - wmin.Y) / safeExtent(wext.Y);
			}
			else if (face.normal.Y != 0.0f)
			{
				tu =        (p.X - wmin.X) / safeExtent(wext.X);
				tv = 1.0f - (p.Z - wmin.Z) / safeExtent(wext.Z);
			}
			else
			{
				tu =        (p.X - wmin.X) / safeExtent(wext.X);
				tv = 1.0f - (p.Y - wmin.Y) / safeExtent(wext.Y);
			}

			// Re-centred on the cell's own middle: the shard node carries the
			// centroid as its position, so the mesh must sit about the origin or
			// it would tumble around the prop's pivot instead of its own.
			buffer->Vertices.push_back(video::S3DVertex(
				p - centre, face.normal, white, core::vector2df(tu, tv)));
		}

		buffer->Indices.push_back(static_cast<u16>(base + 0));
		buffer->Indices.push_back(static_cast<u16>(base + 2));
		buffer->Indices.push_back(static_cast<u16>(base + 1));

		buffer->Indices.push_back(static_cast<u16>(base + 0));
		buffer->Indices.push_back(static_cast<u16>(base + 3));
		buffer->Indices.push_back(static_cast<u16>(base + 2));
	}

	buffer->recalculateBoundingBox();

	auto* mesh = new scene::SMesh();
	mesh->addMeshBuffer(buffer);
	buffer->drop();

	mesh->recalculateBoundingBox();

	return mesh;
}

void FractureManager::buildVoronoiPattern(irr::scene::IMesh* src,
                                          const irr::core::aabbox3df& box, int cells,
                                          bool hollow, FracturePattern& out)
{
	using namespace irr;

	const int target = core::clamp(cells, _min_cells, _max_cells);

	std::vector<core::vector3df> seeds;
	FractureGeometry::scatterSeeds(box, target, seeds);

	std::vector<FractureGeometry::Plane> planes;

	out.hollow = hollow;

	out.cells.reserve(seeds.size());
	out.centroids.reserve(seeds.size());

	for (size_t i = 0; i < seeds.size(); ++i)
	{
		FractureGeometry::buildCellPlanes(seeds, i, box, planes);

		FractureGeometry::Cell cell;

		// Empty cells are expected, not failures: a seed can land in the hollow
		// of a non-convex prop, or inside the AABB but outside the mesh.
		if (!FractureGeometry::clipMeshToCell(src, planes, cell, !hollow))
			continue;

		out.cells.push_back(cell.mesh);
		out.centroids.push_back(cell.centroid);
	}
}

const FractureManager::FracturePattern* FractureManager::patternFor(
	const std::string& meshPath,
	irr::scene::IMesh* src,
	const irr::core::aabbox3df& localBounds,
	int cells, int variant, bool hollow)
{
	// 'hollow' belongs in the key: the same mesh cut as a shell and as a solid
	// produce different geometry, and sharing one cache slot would hand a
	// capped pattern to a shell prop or vice versa.
	const std::string key = meshPath + "|" + std::to_string(cells) + "|" +
		std::to_string(variant) + (hollow ? "|shell" : "|solid");

	auto it = m_patterns.find(key);
	if (it != m_patterns.end())
		return &it->second;

	FracturePattern pattern;
	buildVoronoiPattern(src, localBounds, cells, hollow, pattern);

	if (pattern.cells.empty())
	{
		spdlog::warn("FractureManager: pattern generation produced no cells for '{}'", meshPath);
		return nullptr;
	}

	auto inserted = m_patterns.emplace(key, std::move(pattern));

	spdlog::info("FractureManager: cached shard pattern for '{}' ({} cells)",
		meshPath, inserted.first->second.cells.size());

	return &inserted.first->second;
}

// ---------------------------------------------------------------------------
// Pool
// ---------------------------------------------------------------------------

bool FractureManager::ensurePool()
{
	if (m_poolReady)
		return !m_shards.empty();

	auto* rm = RenderManager::Get();
	if (!rm || !rm->sceneManager())
		return false;

	auto* smgr = rm->sceneManager();

	// A node needs SOME mesh at construction; every real use overwrites it via
	// setMesh(). Kept alive for the pool's lifetime so a recycled node always
	// has something valid bound.
	m_placeholder = buildBoxMesh(
		irr::core::aabbox3df(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f),
		irr::core::aabbox3df(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f));

	if (!m_placeholder)
	{
		spdlog::error("FractureManager: placeholder mesh failed; fracture disabled");
		m_poolReady = true;   // do not retry on every death
		return false;
	}

	m_shards.resize(_shard_pool_size);

	for (auto& shard : m_shards)
	{
		shard.node = smgr->addMeshSceneNode(m_placeholder);
		if (!shard.node)
			continue;

		applyShardMaterial(shard.node, nullptr, FRACTURE_WOOD, irr::video::SMaterial(), false);

		shard.node->setVisible(false);
	}

	m_poolReady = true;
	return true;
}

irr::video::ITexture* FractureManager::interiorTexture(int profile)
{
	if (profile < 0 || profile >= FRACTURE_PROFILE_COUNT)
		profile = FRACTURE_WOOD;

	if (m_interiorTex[profile])
		return m_interiorTex[profile];

	auto* rm = RenderManager::Get();
	if (!rm || !rm->driver())
		return nullptr;

	auto* drv = rm->driver();

	// An authored texture always wins. Checked through the filesystem rather than
	// handed straight to getTexture(), because a miss there logs an error every
	// time and this runs on the first break of every profile.
	const std::string authored =
		std::string("content/texture/fracture/interior_") + fractureProfileName(profile) + ".png";

	if (rm->device() && rm->device()->getFileSystem()->existFile(authored.c_str()))
	{
		m_interiorTex[profile] = drv->getTexture(authored.c_str());

		if (m_interiorTex[profile])
		{
			spdlog::info("FractureManager: interior texture '{}'", authored);
			return m_interiorTex[profile];
		}
	}

	// Otherwise generate a stand-in so the feature needs no art to work at all.
	// Same trick GoreManager uses for its meat texture: phong_perpixel takes
	// albedo ONLY from tDiffuse and never reads vertex colour, so a tint has to
	// arrive as a texture or a lit shard simply will not show it.
	const irr::core::dimension2du size(32, 32);

	const std::string name = std::string("fracture_interior_") + fractureProfileName(profile);

	m_interiorTex[profile] = drv->addTexture(size, name.c_str(), irr::video::ECF_A8R8G8B8);

	if (!m_interiorTex[profile])
		return nullptr;

	auto* px = static_cast<irr::u32*>(m_interiorTex[profile]->lock());

	if (px)
	{
		auto* rng = Engine::Get()->rng();

		const InteriorTint& tint = _interior_tints[profile];

		for (irr::u32 y = 0; y < size.Height; ++y)
		for (irr::u32 x = 0; x < size.Width;  ++x)
		{
			// Wood gets banded grain along one axis; everything else gets
			// isotropic speckle. Crude, but it stops a cut face reading as flat
			// plastic, which is the whole job of a stand-in.
			int v = rng->getInt(-tint.jitter, tint.jitter);

			if (tint.grain)
				v += static_cast<int>(12.0f * std::sin(static_cast<float>(y) * 1.7f));

			const irr::u32 r = static_cast<irr::u32>(irr::core::clamp(static_cast<int>(tint.r) + v, 0, 255));
			const irr::u32 g = static_cast<irr::u32>(irr::core::clamp(static_cast<int>(tint.g) + v, 0, 255));
			const irr::u32 b = static_cast<irr::u32>(irr::core::clamp(static_cast<int>(tint.b) + v, 0, 255));

			px[y * size.Width + x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
		}

		m_interiorTex[profile]->unlock();
	}

	return m_interiorTex[profile];
}

void FractureManager::applyShardMaterial(irr::scene::IMeshSceneNode* node,
                                         irr::video::ITexture* texture,
                                         int profile,
                                         const irr::video::SMaterial& skinMat,
                                         bool hollow)
{
	if (!node)
		return;

	const auto perpixel = ShaderMaterialManager::get("phong_perpixel");

	const ProfileData& prof = profileData(profile);

	irr::video::ITexture* const interior = interiorTexture(profile);

	// Buffer 0 is the outer skin, buffer 1+ are cut faces. Bound per-material
	// rather than through node->setMaterialTexture(), which would stamp one
	// texture onto every buffer and undo the split.
	for (irr::u32 i = 0; i < node->getMaterialCount(); ++i)
	{
		auto& mat = node->getMaterial(i);

		if (i == 0)
		{
			// The skin INHERITS the prop's material wholesale — roughness,
			// metallic, shader params, the lot. Forcing the profile's values
			// here is what made metal shards look like chrome next to the matte
			// barrel they came off: the profile describes how a material BREAKS,
			// not how its surface is shaded. The prop is the authority on that.
			mat = skinMat;
			mat.setTexture(0, texture);
		}
		else
		{
			// Cut faces have no source material to inherit, so the profile
			// supplies them. A torn interior is its own surface and is allowed
			// to differ from the skin.
			mat.setTexture(0, interior);

			mat.Shininess = prof.shininess;
			mat.SpecularColor.setAlpha(prof.specularAlpha);
			mat.DiffuseColor.setAlpha(255);          // uAlpha — fully opaque

			for (irr::u32 pIdx = 0; pIdx < 8; ++pIdx)
				mat.MaterialTypeParams[pIdx] = 0.0f;
		}

		if (perpixel != irr::video::EMT_SOLID)
			mat.MaterialType = perpixel;

		mat.Lighting = true;

		// A thin shell has no back side to hide. Cull it and a fragment seen
		// from inside the barrel simply is not drawn — which reads as a hole.
		mat.BackfaceCulling = !hollow;
	}
}

FractureManager::Shard* FractureManager::acquireShard()
{
	Shard* oldest = nullptr;

	for (auto& shard : m_shards)
	{
		if (!shard.node)
			continue;

		if (!shard.active)
			return &shard;

		if (!oldest || shard.spawnTime < oldest->spawnTime)
			oldest = &shard;
	}

	return oldest;
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------

void FractureManager::launch(const FracturePattern& pattern,
                             const irr::core::matrix4& world,
                             const irr::core::vector3df& nodeScale,
                             const irr::core::vector3df& nodeRotation,
                             irr::video::ITexture* texture,
                             int profile,
                             const irr::video::SMaterial& skinMat,
                             const DamageContext& ctx,
                             const irr::core::vector3df& centre)
{
	using namespace irr;

	auto* rng = Engine::Get()->rng();
	const float now = static_cast<float>(Engine::Get()->getCurrentTime());

	for (size_t i = 0; i < pattern.cells.size(); ++i)
	{
		Shard* shard = acquireShard();
		if (!shard || !shard->node)
			break;

		// matrix4 has no by-value transformVect in this Irrlicht — only the
		// in-place and out-param forms.
		core::vector3df worldPos;
		world.transformVect(worldPos, pattern.centroids[i]);

		// --- Impulse -------------------------------------------------------
		// This is where a break reads right or wrong, so the three cases are
		// spelled out rather than folded together.
		core::vector3df dir;
		float speed;

		if (ctx.valid)
		{
			core::vector3df away = worldPos - ctx.point;
			float dist = away.getLength();

			if (dist < 0.0001f)
			{
				away.set(0.0f, 1.0f, 0.0f);
				dist = 0.0001f;
			}
			else
			{
				away /= dist;
			}

			if (ctx.explosive)
			{
				// Pure radial from the blast centre with an inverse-square-ish
				// falloff — a blast has no downrange to blend toward.
				dir   = away;
				speed = core::clamp(7.5f / (1.0f + dist * dist), 1.2f, 7.5f);
			}
			else
			{
				// Contact hit: the struck face blows out along the shot, the far
				// side barely moves.
				dir   = away * 0.6f + ctx.direction * 0.4f;
				speed = core::clamp(5.5f / (1.0f + dist * 2.5f), 0.8f, 5.5f);
			}
		}
		else
		{
			// Script kill or console 'hurt' — no idea where it was hit, so it
			// slumps apart rather than exploding.
			dir   = worldPos - centre;
			speed = rng->getFloat(1.0f, 2.2f);
		}

		dir.Y += _launch_up_bias;

		if (dir.getLengthSQ() < 0.0001f)
			dir.set(0.0f, 1.0f, 0.0f);
		dir.normalize();

		shard->velocity = dir * speed * rng->getFloat(0.8f, 1.25f);

		// Spin scales with how hard it was thrown, so a gentle collapse does not
		// have shards whirling like sawblades.
		const float spin = core::clamp(speed * 110.0f, 90.0f, 620.0f);

		shard->angularVelocity.set(
			rng->getFloat(-spin, spin),
			rng->getFloat(-spin, spin),
			rng->getFloat(-spin, spin));

		// Starts at the prop's own orientation, so frame one of the break looks
		// exactly like the intact prop before the pieces separate.
		shard->rotation = nodeRotation;

		shard->spawnTime     = now;
		shard->bounceCount   = 0;
		shard->profile       = profile;
		shard->active        = true;
		shard->physicsActive = true;
		shard->settled       = false;
		shard->relaxFrames   = 0;

		// Bounding sphere in WORLD units — the separation pass compares this
		// against real distances, so the node's scale has to be folded in.
		{
			const core::vector3df ext = pattern.cells[i]->getBoundingBox().getExtent();
			const float maxScale = core::max_(core::max_(nodeScale.X, nodeScale.Y), nodeScale.Z);

			// Half the diagonal is the CIRCUMSCRIBED sphere, which badly
			// overstates a flat shard and would shove neighbours out until
			// there were visible gaps. Pulled in toward the real silhouette.
			shard->radius = ext.getLength() * 0.5f * maxScale * _separate_radius_scale;
		}

		shard->node->setMesh(pattern.cells[i]);

		// setMesh() just wiped the node's materials — rebind before it draws.
		applyShardMaterial(shard->node, texture, profile, skinMat, pattern.hollow);

		shard->node->setScale(nodeScale);
		shard->node->setPosition(worldPos);
		shard->node->setRotation(shard->rotation);
		shard->node->setVisible(true);
	}
}

void FractureManager::spawnBreakEffect(int profile, const irr::core::vector3df& pos)
{
	const ProfileData& prof = profileData(profile);

	if (!prof.breakEffect || !prof.breakEffect[0])
		return;

	auto* pm = ParticleManager::Get();
	if (!pm)
		return;

	// Precache is a no-op on a name it already holds, so this is safe per break
	// and saves the manager needing a scene-load hook of its own.
	if (!pm->precache(prof.breakEffect, _asset_psys(prof.breakEffect)))
		return;

	const uint32_t handle = pm->spawn(prof.breakEffect, SPK::Vector3D(pos.X, pos.Y, pos.Z));
	if (!handle)
		return;

	// Scaled well down: these are stand-ins borrowed from the explosion and
	// impact effects, and at full size a breaking crate reads as a detonation.
	pm->setScale(handle, _break_effect_scale);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

bool FractureManager::fracture(const anax::Entity& entity, const DamageContext& ctx)
{
	using namespace irr;

	if (!enabled)
		return false;

	if (!entity.hasComponent<MeshComponent>())
		return false;

	auto& mesh = entity.getComponent<MeshComponent>();

	// Phase 1 fractures the bind-pose AABB, which for a skinned mesh means
	// box debris at whatever pose the mesh was authored in — nothing like where
	// the NPC is actually standing. Refuse, and let the caller fall through to
	// the gore path, which already takes bodies apart properly.
	if (mesh.isAnimated)
	{
		spdlog::warn("FractureManager: '{}' is animated; fracture skipped (gore path instead)",
			mesh.mesh);
		return false;
	}

	if (!mesh.node || !mesh.trimesh)
		return false;

	scene::IMesh* src = mesh.trimesh->getMesh(0);
	if (!src)
		return false;

	const core::aabbox3df box = src->getBoundingBox();

	if (box.getExtent().getLengthSQ() < 1.0e-6f)
	{
		spdlog::warn("FractureManager: '{}' has a degenerate bounding box; fracture skipped",
			mesh.mesh);
		return false;
	}

	if (!ensurePool())
		return false;

	int  cells   = 12;
	int  profile = FRACTURE_WOOD;
	bool hollow  = false;

	if (entity.hasComponent<DamageReceiverComponent>())
	{
		const auto& dam = entity.getComponent<DamageReceiverComponent>();

		cells   = dam.fractureCells;
		profile = dam.fractureProfile;
		hollow  = dam.fractureHollow;
	}

	if (cellOverride > 0)
		cells = cellOverride;

	const int variant = Engine::Get()->rng()->getInt(0, _pattern_variants - 1);

	const FracturePattern* pattern = patternFor(mesh.mesh, src, box, cells, variant, hollow);
	if (!pattern)
		return false;

	video::ITexture* texture = nullptr;

	if (!mesh.textures.empty() && RenderManager::Get()->driver())
		texture = RenderManager::Get()->driver()->getTexture(mesh.textures[0].c_str());

	// The node's absolute transformation already composes position, rotation and
	// scale in the right order — using it sidesteps matrix4::operator* entirely,
	// which computes other*this and is exactly the kind of thing that silently
	// works for a prop at the origin and breaks for a rotated one.
	const core::matrix4 world = mesh.node->getAbsoluteTransformation();

	core::vector3df centre;
	world.transformVect(centre, box.getCenter());

	// Material 0 of the live node, which is what RenderSystem already set up
	// from the .ent (roughness, metallic, shader params). Taking it from the
	// node rather than rebuilding it from MeshComponent means the shards match
	// whatever the prop actually looks like right now.
	const irr::video::SMaterial skinMat = mesh.node->getMaterialCount() > 0
		? mesh.node->getMaterial(0)
		: irr::video::SMaterial();

	launch(*pattern, world, mesh.node->getScale(), mesh.node->getRotation(),
		texture, profile, skinMat, ctx, centre);

	spawnBreakEffect(profile, centre);

	removeBody(entity);

	return true;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void FractureManager::updateShards(float dt)
{
	using namespace irr;

	if (m_shards.empty())
		return;

	auto* rm = RenderManager::Get();

	const float dt_s = dt * 0.001f;
	const float now  = static_cast<float>(Engine::Get()->getCurrentTime());

	for (auto& shard : m_shards)
	{
		if (!shard.active || !shard.node)
			continue;

		if (now - shard.spawnTime >= _shard_lifetime)
		{
			shard.active  = false;
			shard.settled = false;
			shard.node->setVisible(false);
			continue;
		}

		if (!shard.physicsActive)
			continue;

		const ProfileData& prof = profileData(shard.profile);

		shard.velocity.Y -= prof.gravity * dt_s;

		const core::vector3df pos = shard.node->getPosition();
		core::vector3df newPos    = pos + shard.velocity * dt_s;

		const float speed = shard.velocity.getLength();

		if (speed > 0.001f && rm)
		{
			// Cast along travel, not straight down — catches walls, ramps and
			// ceilings the same way the shell casings and gibs do.
			const core::vector3df travel = shard.velocity / speed;

			RaycastResultData hit = rm->raycastWorldPosition(pos, newPos + travel * 0.1f, true);

			if (hit.hit)
			{
				const core::vector3df n = hit.normal;

				const float dot = shard.velocity.dotProduct(n);

				shard.velocity = (shard.velocity - n * (2.0f * dot)) * prof.restitution;
				shard.angularVelocity *= prof.spinDamp;

				newPos = hit.point + n * _shard_surface_clear;

				if (++shard.bounceCount >= prof.maxBounces)
				{
					shard.physicsActive = false;
					shard.velocity.set(0.0f, 0.0f, 0.0f);
					shard.angularVelocity.set(0.0f, 0.0f, 0.0f);

					// Down but not out: now it starts shouldering its
					// neighbours out of the way.
					shard.settled     = true;
					shard.relaxFrames = _separate_frames;
				}
			}
		}

		shard.node->setPosition(newPos);

		shard.rotation += shard.angularVelocity * dt_s;
		shard.node->setRotation(shard.rotation);
	}
}

void FractureManager::separateSettledShards(float dt)
{
	using namespace irr;

	auto* rm = RenderManager::Get();

	const float dt_s = dt * 0.001f;

	// Only shards that have come to rest take part. Separating them mid-flight
	// would look like they were repelling each other in mid-air; the pile is
	// only a problem once it IS a pile.
	for (auto& a : m_shards)
	{
		if (!a.active || !a.node || !a.settled || a.relaxFrames <= 0)
			continue;

		--a.relaxFrames;

		core::vector3df push(0.0f, 0.0f, 0.0f);
		int contacts = 0;

		const core::vector3df pa = a.node->getPosition();

		for (auto& b : m_shards)
		{
			if (&a == &b || !b.active || !b.node || !b.settled)
				continue;

			core::vector3df d = pa - b.node->getPosition();

			// Resolve HORIZONTALLY only. Pushing along the true centre-to-centre
			// vector lifts shards off the floor and they end up hovering, since
			// nothing pulls them back down once physicsActive is false.
			d.Y = 0.0f;

			const float minDist = a.radius + b.radius;
			float dist = d.getLength();

			if (dist >= minDist)
				continue;

			if (dist < 1.0e-4f)
			{
				// Exactly stacked — no separating direction exists, so invent
				// one rather than dividing by zero.
				auto* rng = Engine::Get()->rng();

				d.set(rng->getFloat(-1.0f, 1.0f), 0.0f, rng->getFloat(-1.0f, 1.0f));

				if (d.getLengthSQ() < 1.0e-6f)
					d.set(1.0f, 0.0f, 0.0f);

				dist = 0.0001f;
			}

			d /= dist;

			push += d * (minDist - dist);
			++contacts;
		}

		if (!contacts)
		{
			// Nothing overlapping any more — stop early rather than burning the
			// rest of the budget.
			a.relaxFrames = 0;
			continue;
		}

		// Half the overlap each (the other shard resolves its own half), eased
		// over several frames so a pile creeps apart instead of popping.
		core::vector3df step = push * (0.5f * _separate_rate * dt_s * 60.0f);

		const float stepLen = step.getLength();

		if (stepLen < 1.0e-5f)
			continue;

		if (stepLen > _separate_max_step)
			step *= _separate_max_step / stepLen;

		core::vector3df target = pa + step;

		// Do not shove a shard through a wall. Same raycast the flight path
		// uses, so a shard wedged in a corner just stays wedged.
		if (rm)
		{
			RaycastResultData blocked = rm->raycastWorldPosition(pa, target, true);

			if (blocked.hit)
				continue;

			// Re-seat on whatever is underneath: sliding sideways off the edge
			// of a step would otherwise leave it floating at the old height.
			const core::vector3df from = target + core::vector3df(0.0f, _separate_probe_up, 0.0f);
			const core::vector3df to   = target - core::vector3df(0.0f, _separate_probe_down, 0.0f);

			RaycastResultData ground = rm->raycastWorldPosition(from, to, true);

			if (ground.hit)
				target.Y = ground.point.Y + _shard_surface_clear;
		}

		a.node->setPosition(target);
	}
}

void FractureManager::update(float dt)
{
	updateShards(dt);
	separateSettledShards(dt);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void FractureManager::removeBody(const anax::Entity& entity)
{
	// Visibility must go through the component. RenderSystem re-applies
	// node->setVisible(render.isVisible) every frame, so hiding the node alone
	// is undone before the frame is ever presented — and killEntityByID is
	// deferred to end of frame, so without this there is a tick where the intact
	// prop and its own shards are both on screen.
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

void FractureManager::clearScene()
{
	for (auto& shard : m_shards)
	{
		shard.active        = false;
		shard.physicsActive = false;
		shard.settled       = false;
		shard.relaxFrames   = 0;
		shard.bounceCount   = 0;
		shard.velocity.set(0.0f, 0.0f, 0.0f);
		shard.angularVelocity.set(0.0f, 0.0f, 0.0f);

		if (shard.node)
			shard.node->setVisible(false);
	}
}

void FractureManager::clear()
{
	// Nodes first: remove() drops each node's reference to whatever pattern mesh
	// it currently holds, so the drops below are the last ones outstanding.
	for (auto& shard : m_shards)
	{
		if (shard.node)
			shard.node->remove();

		shard.node   = nullptr;
		shard.active = false;
	}

	m_shards.clear();

	for (auto& entry : m_patterns)
		for (auto* cell : entry.second.cells)
			if (cell)
				cell->drop();

	m_patterns.clear();

	if (m_placeholder)
	{
		m_placeholder->drop();
		m_placeholder = nullptr;
	}

	m_poolReady = false;
}
