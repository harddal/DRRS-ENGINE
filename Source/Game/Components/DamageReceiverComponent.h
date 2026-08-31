#pragma once

#include "anax/Component.hpp"

#include "irrlicht.h"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"

#include "Game/Components/ImpactSurface.h"

enum class DAMAGE_TYPE
{
	DEFAULT
};

// Where a hit landed and which way it was travelling.
//
// GameplaySystem::damageEntity() takes one of these so GoreManager can put
// blood at the wound and throw it downrange. It is optional and defaulted:
// a default-constructed context (valid == false) means "no idea where", and
// the gore falls back to the entity's bounding-box centre spraying upward —
// which is the right look for a script kill or a console 'hurt'.
//
// Callers that DO know should always pass it. Every hitscan weapon already
// holds raycastResult.point/.normal and its own aim direction one line above
// the damage call; explosions know their blast centre.
struct DamageContext
{
	bool valid = false;

	irr::core::vector3df point;      // world-space wound position
	irr::core::vector3df normal;     // surface normal at the wound (points back at the shooter)
	irr::core::vector3df direction;  // travel direction of whatever hit it

	// Radial damage: 'direction' is centre -> entity rather than a shot vector,
	// and 'normal' is its negation. Gore fans outward instead of downrange.
	bool explosive = false;

	DamageContext() = default;

	// Hitscan / projectile contact hit.
	//
	// 'direction' is normalized here rather than at the call site, so a caller
	// can hand over a raw difference — most usefully
	// (raycastResult.ray.End - raycastResult.ray.Start), which every hitscan
	// weapon has without needing to know its own aim variable's name.
	static DamageContext fromImpact(const irr::core::vector3df& point,
	                                const irr::core::vector3df& normal,
	                                const irr::core::vector3df& direction)
	{
		DamageContext c;
		c.valid     = true;
		c.point     = point;
		c.normal    = normal;
		c.direction = direction;

		if (c.direction.getLengthSQ() > 0.0001f)
			c.direction.normalize();
		else
			c.direction.set(0.0f, 1.0f, 0.0f);

		return c;
	}

	// Blast at 'centre' catching an entity whose origin is at 'target'.
	static DamageContext fromBlast(const irr::core::vector3df& centre,
	                               const irr::core::vector3df& target)
	{
		irr::core::vector3df away = target - centre;

		if (away.getLengthSQ() < 0.0001f)
			away.set(0.0f, 1.0f, 0.0f);
		away.normalize();

		DamageContext c;
		c.valid     = true;
		c.point     = target;
		c.normal    = -away;
		c.direction = away;
		c.explosive = true;
		return c;
	}
};

// Result of a GameplaySystem::damageEntity() call — lets attackers drive
// hit-confirmation feedback (hitmarkers, hit/kill sounds).
enum class HIT_RESULT
{
	NONE = 0,   // no damageable entity, or it was already dead
	HIT  = 1,   // damage landed
	KILL = 2    // this damage will kill it (health crosses zero this tick)
};

struct DamageReceiverComponent : anax::Component
{
	bool invulnerable, buddha;
    
	int
		damageReceived,
		// 100 by default
		threshold;
        
    int health;

    bool receivedDamage = false;

	DAMAGE_TYPE lastReceivedType;

	// --- Gore bookkeeping (runtime only, never serialized) ------------------
	// Set once GoreManager has taken this entity apart, so a corpse that keeps
	// absorbing fire is only gibbed on the frame it crosses the threshold.
	bool gibbed = false;

	// --- Fracture on death --------------------------------------------------
	// Props flagged here come apart into shards instead of playing the gore
	// theatre. Shards are raw pooled scene nodes with fake physics (no PhysX
	// actor, no ECS entity), so they cannot be shot or block the player — see
	// FractureManager for why that trade is worth it.
	bool fractureOnDeath = false;
	int  fractureCells   = 12;   // target shard count
	int  fractureProfile = 0;    // FRACTURE_PROFILE; int so cereal needs no mapping

	// Set for props modelled as a THIN SHELL with no wall thickness — barrels,
	// pipes, most hollow props. Such a mesh has no interior to expose, so the
	// cut faces are not capped (capping a shell stretches a membrane across the
	// fragment and reads as a stray flat plate) and the fragments render
	// double-sided (a shell viewed from inside is otherwise invisible).
	//
	// Leave OFF for anything modelled solid, which caps properly and gets the
	// interior material.
	bool fractureHollow = false;

	// IMPACT_SURFACE; stored as int so cereal needs no enum mapping, matching
	// fractureProfile above. Only consulted when the entity does NOT come apart
	// into shards on this hit.
	int impactSurface = IMPACT_AUTO;

	// Runtime only, never serialized. Mirrors 'gibbed': stops a prop that eats a
	// second round in the same frame from shattering twice.
	bool fractured = false;

	// True once health has crossed zero and the death tier has been resolved.
	// Distinct from DescriptorComponent::isAlive, which GameplaySystem::update()
	// flips a frame later.
	bool deathResolved = false;

    bool didReceiveDamage() { bool temp = receivedDamage; receivedDamage = false; return temp; }

	// How far past dead this entity has been taken, as a fraction of its own
	// health pool. 0.0 = died exactly on zero; 1.0 = absorbed a second full
	// health bar's worth of damage. Dimensionless on purpose, so a 400 HP
	// miniboss needs proportionally more punishment than a 100 HP zombie with
	// no per-NPC tuning. Keeps climbing after death — that is what lets a
	// corpse gib late.
	float overkillRatio() const
	{
		if (threshold <= 0)
			return 0.0f;

		return (damageReceived - threshold) / static_cast<float>(threshold);
	}

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(threshold));

        // Per-field try/catch so every .ent written before fracture existed
        // still loads — the same pattern MeshComponent uses for its later
        // additions. A missing tag throws, is swallowed, and the member keeps
        // its default.
        try { archive(CEREAL_NVP(fractureOnDeath)); } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(fractureCells));   } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(fractureProfile)); } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(fractureHollow));  } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(impactSurface));   } catch (cereal::Exception&) {}
    }

	DamageReceiverComponent() : 
		invulnerable(false), buddha(false), damageReceived(0), threshold(100), health(threshold), lastReceivedType(DAMAGE_TYPE::DEFAULT) {}
};
