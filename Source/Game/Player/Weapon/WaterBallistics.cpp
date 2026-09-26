#include "WaterBallistics.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Sound/SoundManager.h"
#include "Engine/World/WorldManager.h"

// Windows.h defines min/max as macros and this project does not use NOMINMAX.
// These have to come BEFORE WeaponData.h, not merely before this file's own uses:
// that header calls std::max in an inline body, and the macro eats it there.
#undef min
#undef max

#include "Game/GameplaySystem.h"
#include "Game/Player/WeaponData.h"

#include <algorithm>
#include <cmath>
#include <vector>

using irr::core::vector3df;

namespace
{
	// Starting depths, in world units of water crossed before a shot is spent.
	// Both are live on the console — see the registrations in GameConsole.cpp —
	// so these are only the values the game starts with.
	//
	// SIZE THESE AGAINST THE POOL, NOT AGAINST REALITY. Crossing MORE than the
	// depth is not reduced damage, it is absorption: pierce() reports it as a
	// MISS. So in a pool deeper than the depth, every shot into the water
	// behaves identically — nothing happens — no matter what the depth says, and
	// the tunable looks inert. The partial-damage band is only ever this many
	// units wide. That is what makes a physically honest 2.0 (a real bullet dies
	// in about a metre and a half of water) read as a bug.
	//
	// Scale: the player capsule is 2.35 units tall, so one unit is roughly 0.75m
	// and swimming_pool.pak's pool is 12 units — about 9m — deep.
	//
	// BULLETS, 10.0. Full damage at the surface, half at 5 units (a bit over two
	// body heights down), and spent at 10 — so the bottom two units of that pool
	// stay genuine cover and diving deep is still an escape, while everything
	// above it is a readable gradient rather than an on/off wall.
	float s_penetrationDepth           = 10.0f;

	// PROJECTILES, 18.0. Further than bullets both because a warhead carries
	// further and because water transmits blast far better than it stops it: at
	// this depth nothing in a 12-unit pool is ever fully absorbed, so an
	// underwater detonation always lands something (about a third of its damage
	// on the pool floor) instead of a rocket silently disappearing.
	float s_projectilePenetrationDepth = 18.0f;

	// How far past a water face pierce() restarts the ray. Big enough that the
	// face it just crossed cannot be found again by the next cast, small enough
	// to be invisible at any sane depth.
	const float kSurfaceSkin = 0.01f;

	// Entry + exit for a few stacked pools. A ray still finding water past this
	// is a broken scene, not a shot worth chasing.
	const int kMaxWaterSurfaces = 8;

	// A crossing is only a splash if the segment actually changes side of the
	// surface plane; a shot skimming exactly along it is not a splash.
	const float kMinVerticalTravel = 1.0e-4f;

	// Splash coalescing. Without it a 20-per-second beam weapon standing on one
	// spot builds a permanent fountain, and a minigun does much the same. Two
	// splashes closer than this in space AND in time are treated as one; a
	// shotgun's pellets are spread wider than the radius at any range where the
	// spread is visible, so a genuine peppering still reads as several.
	const float kSplashMergeRadius   = 0.25f;  // world units
	const float kSplashMergeInterval = 100.0f; // ms

	// `water_debug 1`. Per-shot readout -- see the note in the header on why a
	// shot that over-runs the depth is a miss rather than a weak hit.
	bool s_debug = false;

	// --- Bubble trail -----------------------------------------------------

	// `water_bubbles` / `water_bubble_spacing`. Spacing is world units between
	// bubbles at density 1.0.
	bool  s_bubblesEnabled = true;
	float s_bubbleSpacing  = 0.3f;

	// Set by BubbleScope around each weapon's update() and persist().
	float s_scopeDensity = 1.0f;

	// No bubble within this distance of the ray origin. When the shooter is
	// submerged the ray starts at the muzzle, and the viewmodel is drawn over
	// the scene -- bubbles right there would show through the gun.
	const float kBubbleMuzzleClearance = 0.6f;

	// Bubbles are kept this far below every surface. One shared system serves
	// every pool, so nothing can kill a bubble at its own pool's surface; the
	// .psys instead caps how far a bubble can ever rise (~0.55 units, see the
	// RISE BUDGET note in water_bubbles.psys) and trails stay deeper than that.
	// Grow this if that budget grows, or bubbles float up out of the water.
	const float kBubbleSurfaceClearance = 0.6f;

	// Hard cap per trail; a long underwater run widens the spacing instead of
	// emitting more. A swimmer firing down a long canal would otherwise pour
	// hundreds of bubbles into the pool per shot.
	const int kMaxBubblesPerTrail = 48;

	// A projectile's wake reads heavier than a bullet's.
	const float kProjectileWakeDensity = 2.0f;

	// Trail coalescing, same idea as the splash ring. The mining laser casts
	// the SAME ray twice per tick (beam visual + damage) at 20 Hz; without this
	// it refills one line every tick and builds a thickening column. Only
	// EMITTED trails are recorded, so a steady beam still produces a steady
	// stream rather than being suppressed forever.
	const float kTrailMergeRadius   = 0.25f; // world units, both endpoints
	const float kTrailMergeInterval = 60.0f; // ms

	struct RecentTrail { vector3df start, end; float time = -1.0e9f; };
	RecentTrail s_recentTrails[8];
	int         s_recentTrailWrite = 0;

	bool trailWasJustHere(const vector3df& start, const vector3df& end, float now)
	{
		for (const auto& recent : s_recentTrails)
		{
			if (now - recent.time > kTrailMergeInterval)
				continue;
			if (recent.start.getDistanceFrom(start) <= kTrailMergeRadius &&
			    recent.end.getDistanceFrom(end)     <= kTrailMergeRadius)
				return true;
		}

		return false;
	}

	// The one live water_bubbles instance. clear() kills it on every
	// editor<->game switch, so it is re-checked (cheaply) before every trail.
	uint32_t s_bubbleHandle        = 0;
	float    s_bubbleRetryAfterMs  = -1.0e9f;
	const float kBubbleRetryInterval = 1000.0f; // ms between spawn attempts on failure

	uint32_t bubbleSystem(ParticleManager* pm, float now)
	{
		if (pm->isAlive(s_bubbleHandle))
			return s_bubbleHandle;

		// Throttled: before GameplaySystem::ensureEffects() has registered the
		// effect (or if the .psys is missing) spawn() logs an error per call,
		// and a minigun would print twenty a second.
		if (now < s_bubbleRetryAfterMs)
			return 0;

		// At the origin and never moved: emitAlongLine positions are world space.
		s_bubbleHandle = pm->spawnPersistent("water_bubbles", SPK::Vector3D(0.0f, 0.0f, 0.0f));
		if (!s_bubbleHandle)
			s_bubbleRetryAfterMs = now + kBubbleRetryInterval;

		return s_bubbleHandle;
	}

	struct RecentSplash { vector3df position; float time = -1.0e9f; };
	RecentSplash s_recentSplashes[8];
	int          s_recentSplashWrite = 0;

	bool splashWasJustHere(const vector3df& position, float now)
	{
		for (const auto& recent : s_recentSplashes)
		{
			if (now - recent.time > kSplashMergeInterval)
				continue;
			if (recent.position.getDistanceFrom(position) <= kSplashMergeRadius)
				return true;
		}

		return false;
	}

	const std::vector<std::pair<vector3df, vector3df>>& waterZones()
	{
		static const std::vector<std::pair<vector3df, vector3df>> s_none;

		auto* world = WorldManager::Get();
		if (!world)
			return s_none;

		return world->gameplaySystem()->waterZones();
	}

	// Parametric span of [start,end] inside one AABB, slab method. Returns false
	// when the segment misses the box entirely.
	bool segmentInsideBox(const vector3df& start, const vector3df& delta,
	                      const vector3df& boxMin, const vector3df& boxMax,
	                      float& outEnter, float& outExit)
	{
		float enter = 0.0f;
		float exit  = 1.0f;

		const float s[3] = { start.X, start.Y, start.Z };
		const float d[3] = { delta.X, delta.Y, delta.Z };
		const float lo[3] = { boxMin.X, boxMin.Y, boxMin.Z };
		const float hi[3] = { boxMax.X, boxMax.Y, boxMax.Z };

		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::fabs(d[axis]) < 1.0e-6f)
			{
				// Parallel to this pair of slabs: either wholly between them or
				// wholly outside, and outside means no overlap at all.
				if (s[axis] < lo[axis] || s[axis] > hi[axis])
					return false;
				continue;
			}

			float t0 = (lo[axis] - s[axis]) / d[axis];
			float t1 = (hi[axis] - s[axis]) / d[axis];
			if (t0 > t1)
				std::swap(t0, t1);

			enter = std::max(enter, t0);
			exit  = std::min(exit,  t1);

			if (enter > exit)
				return false;
		}

		outEnter = enter;
		outExit  = exit;
		return true;
	}

	// Every [t0,t1] span of [start,end] that is underwater, merged so overlapping
	// pools are charged once rather than twice.
	//
	// 'topInset' lowers each volume's top face by that many units first -- the
	// bubble trail uses it to stay under the surface. Zero (every ballistics
	// caller) is the volume exactly as registered.
	void submergedSpans(const vector3df& start, const vector3df& end,
	                    std::vector<std::pair<float, float>>& out,
	                    float topInset = 0.0f)
	{
		out.clear();

		const vector3df delta = end - start;

		for (const auto& zone : waterZones())
		{
			vector3df top = zone.second;
			top.Y -= topInset;
			if (top.Y <= zone.first.Y)
				continue; // shallower than the inset: nothing left to fill

			float enter = 0.0f, exit = 0.0f;
			if (!segmentInsideBox(start, delta, zone.first, top, enter, exit))
				continue;
			if (exit <= enter)
				continue;

			out.emplace_back(enter, exit);
		}

		if (out.size() < 2)
			return;

		std::sort(out.begin(), out.end());

		size_t write = 0;
		for (size_t read = 1; read < out.size(); ++read)
		{
			if (out[read].first <= out[write].second)
				out[write].second = std::max(out[write].second, out[read].second);
			else
				out[++write] = out[read];
		}

		out.resize(write + 1);
	}

	void spawnSplash(const vector3df& point)
	{
		// Lift clear of the surface plane. The ripple is a horizontal quad and the
		// water is a horizontal plane, so spawning them coplanar z-fights.
		const vector3df position = point + vector3df(0.0f, 0.03f, 0.0f);

		const float now = Engine::Get() ? Engine::Get()->getCurrentTime() : 0.0f;
		if (splashWasJustHere(position, now))
			return;

		s_recentSplashes[s_recentSplashWrite] = { position, now };
		s_recentSplashWrite = (s_recentSplashWrite + 1) % 8;

		if (auto* pm = ParticleManager::Get())
		{
			const uint32_t handle = pm->spawn("water_splash", SPK::IRR::irr2spk(position));

			// Water surfaces are horizontal, so this is +Y in practice. Aiming the
			// effect's spheric emitters explicitly keeps it honest if a tilted water
			// volume ever appears.
			if (handle)
				pm->setEmitterDirection(handle, vector3df(0.0f, 1.0f, 0.0f));
		}

		// Silent until content/sound/effect/water_impact.wav (or water_impact1.wav,
		// water_impact2.wav, ... for variants) is dropped in: SoundEngine scans for
		// the file lazily on first use and no-ops when there is nothing to play.
		if (SoundManager::Get() && SoundManager::Get()->sound())
		{
			SoundManager::Get()->sound()->playRandomized3D(
				"content/sound/effect/water_impact", position, 0.08f, 4, -1.0f, "water_impact");
		}
	}
}

namespace WaterBallistics
{

bool debugEnabled()                { return s_debug; }
void setDebugEnabled(bool enabled) { s_debug = enabled; }

float penetrationDepth()                     { return s_penetrationDepth; }
void  setPenetrationDepth(float units)       { s_penetrationDepth = std::max(0.0f, units); }

float projectilePenetrationDepth()               { return s_projectilePenetrationDepth; }
void  setProjectilePenetrationDepth(float units) { s_projectilePenetrationDepth = std::max(0.0f, units); }

float submergedLength(const vector3df& start, const vector3df& end)
{
	const vector3df delta = end - start;
	const float length = delta.getLength();
	if (length <= 1.0e-5f)
		return 0.0f;

	std::vector<std::pair<float, float>> spans;
	submergedSpans(start, end, spans);

	float total = 0.0f;
	for (const auto& span : spans)
		total += (span.second - span.first) * length;

	return total;
}

bool isSubmerged(const vector3df& point)
{
	for (const auto& zone : waterZones())
	{
		if (point.X >= zone.first.X && point.X <= zone.second.X &&
		    point.Y >= zone.first.Y && point.Y <= zone.second.Y &&
		    point.Z >= zone.first.Z && point.Z <= zone.second.Z)
			return true;
	}

	return false;
}

bool isWaterNode(const irr::scene::ISceneNode* node)
{
	auto* world = WorldManager::Get();
	return world && world->gameplaySystem()->isWaterNode(node);
}

// Zero depth is the off switch: water stops charging for anything and no shot is
// ever spent, which restores the pre-penetration behaviour minus the blocking.
static float falloff(float submerged, float depth)
{
	if (depth <= 0.0f)
		return 1.0f;

	return std::max(0.0f, std::min(1.0f, 1.0f - (submerged / depth)));
}

float damageScale(float submerged)           { return falloff(submerged, s_penetrationDepth); }
float projectileDamageScale(float submerged) { return falloff(submerged, s_projectilePenetrationDepth); }

int splashAlong(const vector3df& start, const vector3df& end)
{
	const float dy = end.Y - start.Y;
	if (std::fabs(dy) < kMinVerticalTravel)
		return 0;

	int crossings = 0;

	for (const auto& zone : waterZones())
	{
		// The surface is the TOP of the volume. Decided against the AABB rather
		// than against a reported surface normal: raycastWorldPosition flips an
		// inside hit's normal back toward the ray origin, so a shot leaving
		// through the pool FLOOR arrives looking like an upward-facing surface.
		const float surfaceY = zone.second.Y;

		const float t = (surfaceY - start.Y) / dy;
		if (t < 0.0f || t > 1.0f)
			continue;

		const vector3df point = start + (end - start) * t;

		if (point.X < zone.first.X || point.X > zone.second.X ||
		    point.Z < zone.first.Z || point.Z > zone.second.Z)
			continue;

		spawnSplash(point);
		++crossings;
	}

	return crossings;
}

bool  bubblesEnabled()                { return s_bubblesEnabled; }
void  setBubblesEnabled(bool enabled) { s_bubblesEnabled = enabled; }
float bubbleSpacing()                 { return s_bubbleSpacing; }
void  setBubbleSpacing(float units)   { s_bubbleSpacing = std::max(0.02f, units); }

float currentBubbleDensity() { return s_scopeDensity; }

BubbleScope::BubbleScope(float density) : m_previous(s_scopeDensity)
{
	s_scopeDensity = std::max(0.0f, density);
}

BubbleScope::~BubbleScope()
{
	s_scopeDensity = m_previous;
}

// Shared by hitscan trails and projectile wakes. 'muzzleClearance' is only for
// a ray that starts at a gun: a projectile's per-frame segment starts wherever
// the projectile was last frame, and trimming each one would leave a dotted wake.
static void emitTrail(const vector3df& start, const vector3df& end,
                      float density, float muzzleClearance)
{
	if (!s_bubblesEnabled || density <= 0.0f)
		return;

	const vector3df delta  = end - start;
	const float     length = delta.getLength();
	if (length <= 1.0e-4f)
		return;

	// Cheap reject first: the dry common case costs one pass over the zones.
	std::vector<std::pair<float, float>> spans;
	submergedSpans(start, end, spans, kBubbleSurfaceClearance);
	if (spans.empty())
		return;

	// Trim the stretch nearest the muzzle, dropping anything left too short.
	const float tClear = muzzleClearance / length;
	float total = 0.0f;
	for (auto& span : spans)
	{
		span.first = std::max(span.first, tClear);
		if (span.second > span.first)
			total += (span.second - span.first) * length;
	}

	if (total <= 1.0e-3f)
		return;

	auto* pm = ParticleManager::Get();
	if (!pm)
		return;

	const float now = Engine::Get() ? Engine::Get()->getCurrentTime() : 0.0f;
	if (trailWasJustHere(start, end, now))
		return;

	const uint32_t handle = bubbleSystem(pm, now);
	if (!handle)
		return;

	// Spacing from density, widened so no trail exceeds the cap.
	float step = s_bubbleSpacing / density;
	if (total / step > static_cast<float>(kMaxBubblesPerTrail))
		step = total / static_cast<float>(kMaxBubblesPerTrail);

	for (const auto& span : spans)
	{
		if (span.second <= span.first)
			continue;

		// Random phase so back-to-back projectile segments (and parallel pellets)
		// do not all put their bubbles on the same lattice.
		const float offset = Engine::Get() ? Engine::Get()->rng()->getFloat(0.0f, step) : 0.0f;

		pm->emitAlongLine(handle,
		                  start + delta * span.first,
		                  start + delta * span.second,
		                  step, offset);
	}

	s_recentTrails[s_recentTrailWrite] = { start, end, now };
	s_recentTrailWrite = (s_recentTrailWrite + 1) % 8;

	if (s_debug)
		spdlog::info("[water_debug] bubble trail {:.2f}u underwater, step {:.2f}u (density x{:.2f})",
		             total, step, density);
}

void bubbleTrail(const vector3df& start, const vector3df& end, float density)
{
	emitTrail(start, end, density, kBubbleMuzzleClearance);
}

unsigned int Shot::scaled(float baseDamage) const
{
	const float scaledDamage = baseDamage * scale;
	if (scaledDamage <= 0.0f)
		return 0;

	return static_cast<unsigned int>(scaledDamage + 0.5f);
}

// Point at which the shot has crossed 'target' units of water. Only called for a
// shot that IS spent, so the target is always reached before the segment ends.
static vector3df pointAtSubmerged(const vector3df& start, const vector3df& end, float target)
{
	const vector3df delta = end - start;
	const float length = delta.getLength();
	if (length <= 1.0e-5f)
		return end;

	std::vector<std::pair<float, float>> spans;
	submergedSpans(start, end, spans);

	float travelled = 0.0f;
	for (const auto& span : spans)
	{
		const float spanLength = (span.second - span.first) * length;

		if (travelled + spanLength >= target)
		{
			const float into = (target - travelled) / length;
			return start + delta * (span.first + into);
		}

		travelled += spanLength;
	}

	return end;
}

RaycastResultData castThroughWater(const vector3df& start, const vector3df& end, bool excludeDebugNodes)
{
	vector3df direction = end - start;
	const float rayLength = direction.getLength();
	if (rayLength <= 1.0e-5f)
		return RaycastResultData();

	direction /= rayLength;

	auto* gameplay = WorldManager::Get() ? WorldManager::Get()->gameplaySystem() : nullptr;

	// Walk forward through water faces. Each cast re-aims at the ORIGINAL end so
	// the shot cannot drift, and restarts a hair past the face just crossed so
	// that face cannot be found again.
	RaycastResultData result;
	vector3df from = start;
	bool solid = false;

	for (int surface = 0; surface < kMaxWaterSurfaces; ++surface)
	{
		result = RenderManager::Get()->raycastWorldPosition(from, end, excludeDebugNodes);

		if (!result.hit || !result.node)
			break;

		if (!gameplay || !gameplay->isWaterNode(result.node))
		{
			solid = true;
			break;
		}

		from = result.point + direction * kSurfaceSkin;

		// Stepped past the far end — everything left of the ray was water.
		if ((from - start).getLength() >= rayLength)
			break;
	}

	// Falling out of the loop on the counter leaves a WATER hit in 'result'.
	// Reporting that as the thing the shot struck would hand the weapon a marker
	// entity to damage, so anything that is not a confirmed solid is a miss.
	if (!solid)
		result = RaycastResultData();

	return result;
}

Shot pierce(const vector3df& start, const vector3df& end, bool excludeDebugNodes)
{
	Shot out;
	out.endPoint = end;

	if ((end - start).getLength() <= 1.0e-5f)
		return out;

	const RaycastResultData result = castThroughWater(start, end, excludeDebugNodes);

	const vector3df reached = (result.hit && result.node) ? result.point : end;

	out.hit       = result;
	out.submerged = submergedLength(start, reached);

	const float depth = penetrationDepth();
	if (depth > 0.0f && out.submerged >= depth)
	{
		// Water ate it. Reported as a miss so every weapon's existing
		// `if (hit && node)` chain skips damage, impact and decal for free.
		out.spent    = true;
		out.scale    = 0.0f;
		out.hit      = RaycastResultData();
		out.endPoint = pointAtSubmerged(start, reached, depth);
	}
	else
	{
		out.scale    = damageScale(out.submerged);
		out.endPoint = reached;
	}

	// Splash only as far as the shot actually got: a spent round must not break
	// the far surface of a pool it never reached.
	splashAlong(start, out.endPoint);

	// Same rule for the trail: it ends where the shot does, so an absorbed round
	// leaves no bubbles past the point the water stopped it.
	bubbleTrail(start, out.endPoint, s_scopeDensity);

	if (s_debug)
	{
		const bool solidHit = out.hit.hit && out.hit.node;

		if (out.submerged <= 0.0f && !solidHit)
			spdlog::info("[water_debug] shot crossed no water, hit nothing");
		else
			spdlog::info("[water_debug] crossed {:.2f}u of water (depth {:.2f}u) -> damage x{:.2f}{}{}",
			             out.submerged, penetrationDepth(), out.scale,
			             out.spent   ? "  ABSORBED (reported as a MISS -- no damage, no impact)" : "",
			             (!out.spent && solidHit) ? "  solid hit" : "");
	}

	return out;
}

void stepProjectile(WeaponProjectile& projectile, const vector3df& from, const vector3df& to)
{
	splashAlong(from, to);

	// Wake: no muzzle trim, since this segment starts wherever the projectile
	// was last frame, not at a gun.
	emitTrail(from, to, s_scopeDensity * kProjectileWakeDensity, 0.0f);

	const float crossed = submergedLength(from, to);
	projectile.submergedDistance += crossed;

	if (s_debug && crossed > 0.0f)
		spdlog::info("[water_debug] projectile now {:.2f}u submerged (depth {:.2f}u) -> damage x{:.2f}",
		             projectile.submergedDistance, projectilePenetrationDepth(),
		             projectileDamageScale(projectile.submergedDistance));
}

float projectileScale(const WeaponProjectile& projectile)
{
	return projectileDamageScale(projectile.submergedDistance);
}

}
