#pragma once

// What a NON-fracturing, NON-lethal (or lethal-but-unshatterable) hit reads as
// when it lands on an entity. Drives the impact particle + decal picked in
// GameplaySystem::damageEntity(): FLESH bleeds through GoreManager, everything
// else spits the matching debris, NONE is silent.
//
// Referenced from two places, hence its own header:
//   - DamageReceiverComponent::impactSurface  — the per-entity override
//   - EntityBehavior::bloodType()             — a behaviour's self-declared type
//
// AUTO is almost always the right value on the component:
//   - the entity's behaviour gets first say via EntityBehavior::bloodType()
//     (a creature behaviour returns FLESH, a turret could return METAL, ...)
//   - otherwise the entity's first diffuse texture name is classified through
//     MaterialBuilder (crate_wood -> WOOD, barrel_metal -> METAL, ...)
//   - an unclassifiable texture (or no mesh) falls back to a generic debris puff
// Set an explicit value on the component only when that chain guesses wrong.
enum IMPACT_SURFACE
{
	IMPACT_AUTO = 0,
	IMPACT_FLESH,
	IMPACT_WOOD,
	IMPACT_METAL,
	IMPACT_STONE,
	IMPACT_GLASS,
	IMPACT_DIRT,
	IMPACT_NONE,

	IMPACT_SURFACE_COUNT
};
