#pragma once

// ---------------------------------------------------------------------------
// GoreManager — blood, splatter and (from phase 2) gibs.
//
// Hangs off GameplaySystem::damageEntity(), which is the single chokepoint every
// weapon, script binding, console 'hurt' and fall-damage path already routes
// through. Severity comes from one number, DamageReceiverComponent::overkillRatio():
// how far past dead the killing blow took the entity, as a fraction of its own
// health pool.
//
//   TIER_HIT     still alive          spray + splatter behind the wound
//   TIER_DEATH   overkill < 0.25      death anim, bigger spray, pool under the body
//   TIER_MESSY   0.25 .. 1.0          death anim, heavy gush, partial gibs   [phase 2]
//   TIER_GIB     overkill >= 1.0      body deleted, full gib burst           [phase 2]
//
// Lives in Game rather than Engine because the tier rules are gameplay policy,
// and ticks from GameplaySystem::update() — which is game-mode only, so nothing
// here simulates in the editor viewport.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "irrlicht.h"

#include "anax/anax.hpp"

struct DamageContext;

enum GORE_TIER
{
	TIER_HIT   = 0,
	TIER_DEATH = 1,
	TIER_MESSY = 2,
	TIER_GIB   = 3
};

class GoreManager
{
public:
	static GoreManager* Get() { return s_Instance; }

	static void create()
	{
		if (!s_Instance)
			s_Instance = new GoreManager();
	}

	static void destroy()
	{
		delete s_Instance;
		s_Instance = nullptr;
	}

	// Load the blood effects. Idempotent — ParticleManager::precache() is a
	// no-op on a name it already holds, so this is safe to call per scene load.
	void precache();

	// Milliseconds, matching every other update() in the game layer.
	void update(float dt);

	// --- Entry points --------------------------------------------------------

	// A hit that did not kill. 'damage' scales the spray.
	void wound(const anax::Entity& entity, const DamageContext& ctx, unsigned int damage);

	// The killing blow. Resolves the tier from 'overkill' and stages the theatre.
	// Returns the tier it chose, so damageEntity() can tell the behavior layer
	// whether to suppress the death animation.
	GORE_TIER kill(const anax::Entity& entity, const DamageContext& ctx, float overkill);

	// A corpse that has absorbed enough further punishment to come apart.
	void gib(const anax::Entity& entity, const DamageContext& ctx, float overkill);

	// Standalone blood burst with no entity behind it — gore props, the console
	// 'gib' command, anything that wants the effect without a damage receiver.
	void burst(const irr::core::vector3df& pos,
	           const irr::core::vector3df& dir,
	           float power);

	// Retire every gib currently in the air or on the floor, but keep the node
	// pool, meshes and texture. Called from Engine::clearScene() — gibs are raw
	// scene nodes owned by nobody, so killAllEntities() does not touch them and
	// they would otherwise hang in space across a scene load or an
	// editor/game mode switch.
	void clearScene();

	// Full teardown: removes the pooled nodes, drops the mesh copies and the
	// generated texture. Shutdown only.
	//
	// Safe because Engine declares RenderManager before WorldManager, so members
	// destruct in reverse and the scene manager outlives the WorldManager dtor
	// that reaches this.
	void clear();

	// Throw loose gibs from a point without killing anything. Used by TIER_MESSY
	// and by burst(); 'power' scales both count and launch speed.
	void throwGibs(const irr::core::vector3df& pos,
	               const irr::core::vector3df& dir,
	               int count, float power);

	// --- Tuning (console-visible from phase 3) --------------------------------
	// 0 off | 1 blood only | 2 blood + gibs | 3 everything
	int   goreLevel   = 3;
	float messyRatio  = 0.25f;
	float gibRatio    = 1.0f;
	int   gibCount    = 10;    // base count at TIER_GIB, before the overkill scale

	GORE_TIER tierFor(float overkill) const;

private:
	GoreManager() = default;
	~GoreManager() = default;

	// One flying chunk of meat. Deliberately NOT an ECS entity and NOT a PhysX
	// actor: a raw scene node with the same fake physics WeaponEffects uses for
	// shell casings, so a burst costs no actor churn and no entity queue traffic.
	//
	// Because these are raw nodes, TransformSystem never sees them and calling
	// node->setPosition() directly is correct here — unlike anywhere that owns a
	// TransformComponent, where the system does the syncing.
	struct Gib
	{
		irr::scene::IMeshSceneNode* node = nullptr;

		irr::core::vector3df velocity;
		irr::core::vector3df angularVelocity;   // degrees/second
		irr::core::vector3df rotation;

		float spawnTime      = 0.0f;
		float lastTrailDecal = 0.0f;
		int   bounceCount    = 0;
		bool  active         = false;
		bool  physicsActive  = false;
	};

	// Build the node pool and the tinted stand-in meshes. Deferred rather than
	// done in precache() because it needs RenderManager, which is not guaranteed
	// to be up when GameplaySystem::init() runs.
	bool ensurePool();

	void updateGibs(float dt);

	// Bind the meat texture and the lit material onto a gib node.
	//
	// Must be re-applied after EVERY setMesh(): CMeshSceneNode::setMesh calls
	// copyMaterials(), which clears the node's materials and reloads them from
	// the new mesh's buffers — silently discarding whatever was set at pool
	// construction. A gib would render with the raw .obj material from its
	// second use onward.
	void applyGibMaterial(irr::scene::IMeshSceneNode* node) const;

	// A free slot, or the OLDEST ACTIVE one when the pool is dry.
	//
	// WeaponEffects::acquireShell() returns null on exhaustion and silently drops
	// the casing, which is right for brass and wrong for gore: a rocket into a
	// group is exactly when the pool runs out and exactly when the effect matters
	// most. Recycling means a burst always produces a full complement.
	Gib* acquireGib();

	// Blood spray at a wound, aimed along 'dir'. 'scale' multiplies the effect.
	void spray(const std::string& effect,
	           const irr::core::vector3df& pos,
	           const irr::core::vector3df& dir,
	           float scale);

	// Cast through the body along 'dir' and stamp a splatter decal on whatever
	// is behind it. 'count' decals in a cone; 'spread' is the cone half-angle
	// in radians. No-op when the ray finds nothing (open air behind the target).
	void splatterBehind(const irr::core::vector3df& origin,
	                    const irr::core::vector3df& dir,
	                    float size, int count, float spread);

	// Fan rays outward from 'centre' in all directions and stamp a decal wherever
	// each one lands — this is what actually coats the floor and the surrounding
	// walls, as opposed to splatterBehind() which only paints what is directly
	// downrange of the shot.
	//
	// 'upBias' is the highest Y a ray direction may take: -0.1 keeps everything
	// heading floorward, 1.0 lets a gib burst reach the ceiling.
	void splatterAround(const irr::core::vector3df& centre,
	                    int count, float size, float upBias);

	// Blood pooling down from 'pos' onto the floor, as a scattered cluster
	// rather than one disc.
	void poolUnder(const irr::core::vector3df& pos, float size);

	// A random one of blood1..blood5, which are RGBA dark-red blobs and so work
	// directly as MODULATE decals (decal.frag mixes toward white on alpha 0).
	std::string randomBloodTexture() const;

	// Bounding-box centre of the entity in world space, or its transform
	// position when it has no mesh. The fallback wound position.
	static irr::core::vector3df bodyCentre(const anax::Entity& entity);

	// Take the body off the screen this frame and queue the entity for removal.
	// Visibility goes through RenderComponent, not the node: RenderSystem
	// re-applies node->setVisible(render.isVisible) every frame, so hiding the
	// node alone is undone before it is ever presented.
	static void removeBody(const anax::Entity& entity);

	bool m_precached = false;
	bool m_poolReady = false;

	std::vector<Gib> m_gibs;

	// Copies of the primitive meshes rather than the shared scene-manager
	// assets, which are handed out to every other caller of getMesh().
	std::vector<irr::scene::IMesh*> m_gibMeshes;

	// Generated meat texture. phong_perpixel takes albedo ONLY from tDiffuse and
	// never reads vertex colour, so tinting the mesh would have been invisible —
	// the colour has to arrive as a texture for a lit gib to be red.
	irr::video::ITexture* m_gibTexture = nullptr;

	static GoreManager* s_Instance;
};
