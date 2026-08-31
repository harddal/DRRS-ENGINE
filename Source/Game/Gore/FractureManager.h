#pragma once

// ---------------------------------------------------------------------------
// FractureManager — props that come apart when they die.
//
// A sibling to GoreManager rather than an extension of it. GoreManager is meat:
// it stamps a blood decal on every bounce, tints its chunks with a generated red
// texture and fires blood-mist psys. A shattering crate shares none of that, and
// threading an 'isMeat' flag through GoreManager would poison a clean module.
//
// What IS shared is the fake physics — gravity, one raycast per frame along the
// travel vector, reflect-and-damp, sleep after N bounces. That loop is lifted
// from GoreManager::updateGibs() with per-material constants swapped in, and for
// the same reason: shards cost no PhysX actor churn and no ECS entity traffic.
//
// The tradeoff that buys: shards cannot be shot, cannot be pushed, and do not
// block the player. They collide with the world and nothing else. For barrels,
// crates, glass and plaster — debris that is on the floor within two seconds —
// that is invisible.
//
// PHASE 1: shard geometry is a grid split of the mesh's local AABB. The voronoi
// cell clipper replaces buildGridPattern() in phase 2; nothing else changes.
// ---------------------------------------------------------------------------

#include <string>
#include <unordered_map>
#include <vector>

#include "irrlicht.h"

#include "anax/anax.hpp"

struct DamageContext;

// How a shard behaves once it is in the air. Stored on DamageReceiverComponent
// as an int so cereal does not need an enum mapping.
enum FRACTURE_PROFILE
{
	FRACTURE_WOOD  = 0,
	FRACTURE_STONE = 1,
	FRACTURE_METAL = 2,
	FRACTURE_GLASS = 3,

	FRACTURE_PROFILE_COUNT
};

const char* fractureProfileName(int profile);

class FractureManager
{
public:
	static FractureManager* Get() { return s_Instance; }

	static void create()
	{
		if (!s_Instance)
			s_Instance = new FractureManager();
	}

	static void destroy()
	{
		delete s_Instance;
		s_Instance = nullptr;
	}

	// Milliseconds, matching every other update() in the game layer.
	void update(float dt);

	// Take an entity apart and queue it for removal.
	//
	// Returns false when this entity cannot be fractured — no mesh, no node, or
	// a skinned mesh (see below). The caller is expected to fall through to the
	// normal gore path on false, so a mis-flagged NPC still dies properly
	// instead of dying silently.
	bool fracture(const anax::Entity& entity, const DamageContext& ctx);

	// Retire every shard in the air or on the floor, but keep the node pool and
	// the cached patterns. Called from Engine::clearScene() — shards are raw
	// scene nodes owned by nobody, so killAllEntities() does not touch them and
	// they would otherwise hang in space across a scene load or a mode switch.
	void clearScene();

	// Full teardown: removes the pooled nodes and drops every cached pattern
	// mesh. Shutdown only.
	void clear();

	// --- Tuning (console-visible from phase 3) ------------------------------
	bool enabled      = true;
	int  cellOverride = 0;      // >0 overrides every entity's fractureCells

private:
	FractureManager() = default;
	~FractureManager() = default;

	// One flying piece of prop. Deliberately NOT an ECS entity and NOT a PhysX
	// actor — see the header comment.
	//
	// Because these are raw nodes, TransformSystem never sees them and calling
	// node->setPosition() directly is CORRECT here, unlike anywhere that owns a
	// TransformComponent, where the system does the syncing.
	struct Shard
	{
		irr::scene::IMeshSceneNode* node = nullptr;

		irr::core::vector3df velocity;
		irr::core::vector3df angularVelocity;   // degrees/second
		irr::core::vector3df rotation;

		float spawnTime   = 0.0f;
		int   bounceCount = 0;
		int   profile     = FRACTURE_WOOD;
		bool  active        = false;
		bool  physicsActive = false;

		// De-clumping. 'radius' is the shard's bounding sphere in world units;
		// 'relaxFrames' is a per-shard budget that runs down to zero so the
		// separation pass costs nothing once a pile has spread out.
		float radius      = 0.0f;
		bool  settled     = false;
		int   relaxFrames = 0;
	};

	// Shard geometry for one source mesh, in the source mesh's LOCAL space.
	//
	// Cached and reused: the geometry depends only on the mesh path and the cell
	// count, so the first barrel to die pays the generation cost and every
	// barrel after it is free.
	//
	// Each cell mesh is re-centred on its own centroid, with the centroid kept
	// alongside — that is what lets a shard node tumble about its own middle
	// instead of pivoting around the prop's origin.
	struct FracturePattern
	{
		std::vector<irr::scene::IMesh*>   cells;
		std::vector<irr::core::vector3df> centroids;

		// Built from a thin-shell source: cuts were left uncapped, and the
		// fragments must render double-sided or they vanish from inside.
		bool hollow = false;
	};

	bool ensurePool();

	void updateShards(float dt);

	// Ease settled shards apart so a break does not leave one interpenetrating
	// heap. Pairwise, horizontal-only, budgeted — see the constants in the .cpp.
	// Shards carry no PhysX actor, so nothing else stops them overlapping.
	void separateSettledShards(float dt);

	// Interior material for cut faces. Prefers an authored
	// content/texture/fracture/interior_<profile>.png, otherwise generates a
	// tinted stand-in so the feature needs no art to function. Cached per
	// profile; the driver owns the texture, so this never drops them.
	irr::video::ITexture* interiorTexture(int profile);

	// Small puff at the break. Currently reuses existing effects as stand-ins.
	void spawnBreakEffect(int profile, const irr::core::vector3df& pos);

	// Bind the source prop's texture and the lit material onto a shard node.
	//
	// Must be re-applied after EVERY setMesh(): CMeshSceneNode::setMesh calls
	// copyMaterials(), which clears the node's materials and reloads them from
	// the new mesh's buffers — silently discarding whatever was set before. A
	// shard would render with the raw pattern material from its second use on.
	// 'profile' also supplies the PBR fields: phong_perpixel reads roughness from
	// Shininess and metallic from SpecularColor's ALPHA, which Irrlicht defaults
	// to 255 (full metal). Since diffuseFactor is (1 - metallic), a material left
	// at defaults renders BLACK. RenderSystem sets these for every entity mesh;
	// raw pooled nodes never go through it, so they must set them here.
	// 'skinMat' is the SOURCE PROP's material, copied onto buffer 0 wholesale so
	// a shard is shaded exactly like the thing it broke off. The profile only
	// supplies the CUT FACES — it describes how a material breaks, not how its
	// surface is shaded.
	void applyShardMaterial(irr::scene::IMeshSceneNode* node,
	                        irr::video::ITexture* texture,
	                        int profile,
	                        const irr::video::SMaterial& skinMat,
	                        bool hollow);

	// A free slot, or the OLDEST ACTIVE one when the pool is dry.
	//
	// Recycling rather than dropping, for GoreManager's reason: a stack of
	// crates going up at once is exactly when the pool runs out and exactly
	// when the effect matters most.
	Shard* acquireShard();

	// Cached lookup, keyed on mesh path + cell count. Never returns a pattern
	// with zero cells; null on failure.
	//
	// Needs the source mesh itself from phase 2 on: cells are cut FROM the real
	// geometry rather than built as boxes, which is what lets a fragment keep
	// the model's own UVs.
	//
	// 'variant' distinguishes several independently-seeded patterns for the same
	// mesh, so repeated breaks of the same prop do not share one crack pattern.
	const FracturePattern* patternFor(const std::string& meshPath,
	                                  irr::scene::IMesh* src,
	                                  const irr::core::aabbox3df& localBounds,
	                                  int cells, int variant, bool hollow);

	// Voronoi shard generator: scatter seeds in 'box', then clip the SOURCE
	// GEOMETRY to each seed's cell and cap the cuts. See FractureGeometry.
	//
	// Cells that come back empty are dropped, so the pattern can hold fewer
	// shards than were asked for — that is correct for a non-convex prop.
	static void buildVoronoiPattern(irr::scene::IMesh* src,
	                                const irr::core::aabbox3df& box, int cells,
	                                bool hollow, FracturePattern& out);

	// One axis-aligned box as an SMesh, 24 verts / 12 tris, re-centred on its
	// own centre. Only the pool's placeholder mesh uses this now.
	static irr::scene::SMesh* buildBoxMesh(const irr::core::aabbox3df& cell,
	                                       const irr::core::aabbox3df& whole);

	// Throw one pattern's cells from a world transform.
	void launch(const FracturePattern& pattern,
	            const irr::core::matrix4& world,
	            const irr::core::vector3df& nodeScale,
	            const irr::core::vector3df& nodeRotation,
	            irr::video::ITexture* texture,
	            int profile,
	            const irr::video::SMaterial& skinMat,
	            const DamageContext& ctx,
	            const irr::core::vector3df& centre);

	// Take the body off the screen this frame and queue the entity for removal.
	//
	// Visibility goes through RenderComponent, not the node: RenderSystem
	// re-applies node->setVisible(render.isVisible) every frame, so hiding the
	// node alone is undone before the frame is ever presented.
	static void removeBody(const anax::Entity& entity);

	bool m_poolReady = false;

	// Bound to every pool node at construction so a recycled node always has
	// a valid mesh, even before its first real use. Dropped in clear().
	irr::scene::IMesh* m_placeholder = nullptr;

	irr::video::ITexture* m_interiorTex[FRACTURE_PROFILE_COUNT] = {};

	std::vector<Shard> m_shards;

	std::unordered_map<std::string, FracturePattern> m_patterns;

	static FractureManager* s_Instance;
};
