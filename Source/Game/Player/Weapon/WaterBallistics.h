#pragma once

// RaycastResultData is held BY VALUE in Shot, so a forward declaration will not
// do. This is the same header WeaponData.h already pulls, so no weapon pays for
// it twice.
#include "Engine/Renderer/RenderManager.h"

struct WeaponProjectile;

// ---------------------------------------------------------------------------
// WaterBallistics — one place that decides what water does to a shot.
//
// THE SINGLE KNOB. Both penetration depths and the splash live here and nowhere
// else; the weapons only plumb. If water should stop bullets sooner, eat rockets
// faster, or splash differently, this file is the whole edit. Both depths are
// also live on the console (water_penetration / water_projectile_penetration),
// so the curve can be dialled in without a rebuild.
//
// WHY THIS EXISTS AT ALL. A water entity is an ET_MARKER, but RenderSystem gives
// every mesh entity a triangle selector — so raycastWorldPosition STOPS at the
// water surface and hands back a marker that every weapon's
// `type == ET_STATIC || ET_DYNAMIC` chain quietly drops. Before this, a shot into
// a pool produced nothing: no splash, no damage, and nothing behind the water
// could be hit at all. pierce() walks the ray through those surfaces so the shot
// carries on, and charges it for the water it crossed on the way.
//
// Geometry note: entering/leaving water is decided against the water volumes'
// AABBs (GameplaySystem::getWaterZones), NOT against the surface normal the
// raycast reports. A ray leaving a pool through the BOTTOM face gets its normal
// flipped back toward the ray origin by raycastWorldPosition, so it arrives here
// looking exactly like an upward-facing surface hit — normal-based tests put
// splashes on the pool floor.
// ---------------------------------------------------------------------------
namespace WaterBallistics
{
	// --- Tunables (console-backed) -----------------------------------------

	// World units of water a hitscan shot crosses before it is spent. Damage
	// falls off linearly to zero over this distance; a shot that runs out stops
	// in the water and reaches nothing.
	float  penetrationDepth();
	void   setPenetrationDepth(float units);

	// Same, for projectiles. Larger by default: a rocket carries much further
	// through water than a bullet does.
	float  projectilePenetrationDepth();
	void   setProjectilePenetrationDepth(float units);

	// Bubble trail master switch (`water_bubbles`) and base spacing between
	// bubbles in world units (`water_bubble_spacing`) at density 1.0.
	bool   bubblesEnabled();
	void   setBubblesEnabled(bool enabled);
	float  bubbleSpacing();
	void   setBubbleSpacing(float units);

	// --- Diagnostics --------------------------------------------------------

	// Per-shot readout of what the water actually charged: units crossed, the
	// depth they were charged against, the resulting damage scale, and whether
	// the shot was absorbed outright. Off by default; `water_debug 1`.
	//
	// Worth reaching for before concluding the falloff is broken. A shot that
	// crosses MORE than the penetration depth is not "reduced", it is a MISS --
	// so in a pool deeper than the depth setting, every shot into the water
	// reads the same (nothing happens) no matter what the depth is set to. The
	// partial-damage band is only ever 'penetrationDepth' units wide.
	bool debugEnabled();
	void setDebugEnabled(bool enabled);

	// --- Queries ------------------------------------------------------------

	// World units of [start,end] that lie inside a water volume. Overlapping
	// pools are merged, never double-charged.
	float submergedLength(const irr::core::vector3df& start,
	                      const irr::core::vector3df& end);

	// Is this point inside any water volume? Decided against the volumes' AABBs
	// like everything else here. submergedLength() cannot stand in for it: a
	// zero-length segment always reports 0.
	bool isSubmerged(const irr::core::vector3df& point);

	// Is this raycast-hit node a water surface? Anything doing its own collision
	// rays (shell casings, shards) must skip these, or it lands ON the water: the
	// water entity carries a triangle selector like any other mesh. Only
	// populated in game mode.
	bool isWaterNode(const irr::scene::ISceneNode* node);

	// Damage multiplier for something that crossed 'submerged' units of water:
	// 1.0 dry, falling linearly to 0.0 at the matching penetration depth.
	float damageScale(float submerged);
	float projectileDamageScale(float submerged);

	// Splash at every point [start,end] crosses a water SURFACE, in either
	// direction — a shot fired up out of a pool breaks the surface just as a shot
	// fired down into one does. Returns the number of crossings.
	int splashAlong(const irr::core::vector3df& start,
	                const irr::core::vector3df& end);

	// --- Bubble trail -------------------------------------------------------

	// Leave a stream of bubbles along every part of [start,end] that is
	// underwater. 'density' multiplies the bubble count (1.0 = the base
	// spacing, 0 = none). Trims the stretch nearest the muzzle when the shooter
	// is submerged, keeps clear of the surface so bubbles never rise out of it,
	// caps the count per trail, and drops a trail that repeats one emitted a
	// moment ago (the mining laser casts the same ray twice per tick).
	//
	// pierce() calls this itself with the current BubbleScope density, so
	// hitscan weapons get trails without doing anything. Call it directly only
	// for paths that do not go through pierce().
	void bubbleTrail(const irr::core::vector3df& start,
	                 const irr::core::vector3df& end,
	                 float density = 1.0f);

	// The density pierce() uses for its trail. WeaponController holds one of
	// these around the active weapon's update() so the weapon's own
	// PlayerWeapon::underwaterBubbleDensity() reaches pierce() without a new
	// parameter at every call site. Outside any scope (NPC weapons, turrets,
	// anything else that calls pierce()) the density is 1.0. Nesting-safe.
	struct BubbleScope
	{
		explicit BubbleScope(float density);
		~BubbleScope();

		BubbleScope(const BubbleScope&) = delete;
		BubbleScope& operator=(const BubbleScope&) = delete;

	private:
		float m_previous;
	};

	// The density a BubbleScope has currently set (1.0 outside any scope).
	float currentBubbleDensity();

	// --- Hitscan ------------------------------------------------------------

	struct Shot
	{
		// The first SOLID thing the shot reached. hit == false when it reached
		// nothing, including when water absorbed it first (see 'spent').
		RaycastResultData hit;

		// Where the shot visibly ends: the hit point, the point in the water
		// where it ran out, or the far end of the ray. Tracers want this.
		irr::core::vector3df endPoint;

		float submerged = 0.0f;  // units of water crossed before endPoint
		float scale     = 1.0f;  // damage multiplier, 1.0 dry .. 0.0 spent
		bool  spent     = false; // water absorbed it; implies hit.hit == false

		// Damage after falloff, rounded. Weapons pass their own base damage.
		unsigned int scaled(float baseDamage) const;
	};

	// The first SOLID thing on start->end, looking straight through water faces.
	// No splash, no bubbles, no charge -- pure query. hit == false if nothing
	// solid is on the line.
	//
	// This is what the crosshair aim point must use. A plain raycast stops on the
	// water surface, and the muzzle (offset below/right of the eye) converging on
	// THAT point crosses the eye ray there and then diverges from it: fired from
	// above, every round lands beyond where the crosshair points, further the
	// deeper it goes. Underwater there is no surface in the way, which is why it
	// only ever showed up from outside the pool.
	RaycastResultData castThroughWater(const irr::core::vector3df& start,
	                                   const irr::core::vector3df& end,
	                                   bool excludeDebugNodes = true);

	// Cast start->end, stepping THROUGH any water surfaces met on the way and
	// splashing at each, and charge the shot for the water it crossed.
	//
	// Replaces the weapon's own raycastWorldPosition call outright — on a map
	// with no water it returns exactly what that call would have.
	Shot pierce(const irr::core::vector3df& start,
	            const irr::core::vector3df& end,
	            bool excludeDebugNodes = true);

	// --- Projectiles --------------------------------------------------------

	// Projectiles already pass through water for free: they only detonate on
	// ET_STATIC/ET_DYNAMIC or world geometry, and water is neither. So this does
	// not have to clear a path — it only has to make the crossing visible and
	// keep the tally.
	//
	// Call once per projectile per frame with the segment it just swept, from the
	// weapon's own update loop (right where previousPosition is refreshed).
	// Splashes on every surface crossing and adds the submerged travel to
	// WeaponProjectile::submergedDistance.
	void stepProjectile(WeaponProjectile& projectile,
	                    const irr::core::vector3df& from,
	                    const irr::core::vector3df& to);

	// Damage multiplier for a projectile detonating now, from the water it has
	// crossed so far. Apply to direct AND splash damage — an explosion that has
	// swum six units should not still be full strength.
	float projectileScale(const WeaponProjectile& projectile);
}
