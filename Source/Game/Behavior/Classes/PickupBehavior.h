#pragma once

#include <string>

#include "Game/Behavior/EntityBehavior.h"
#include "Game/Player/WeaponData.h"

// ---------------------------------------------------------------------------
// PickupBehavior — anything lying in the world waiting to be taken.
//
// ONE trigger in TWO styles — walk-over, or press-to-take via requiresInteract —
// and FOUR grant channels. A pickup can hand over any combination of a
// weapon, reserve ammunition, health and an inventory item; leave a channel at
// its default and it grants nothing on that channel.
//
// The channels stay separate because the destinations genuinely are: weapons go
// to WeaponController, ammunition to the reserve pools, health into the player's
// DamageReceiverComponent, and items into the pouch. Folding ammunition and
// health into the item model to share this trigger would mean inventing items
// that can never appear in the inventory — no icon to look at, no description to
// read, nothing to select — and then maintaining that fiction everywhere. What
// they actually share is how you take them off the floor, which is this class.
//
// This replaces three pickup paths that had drifted apart over the years: this
// behavior's radius for weapons, AngelScript onPlayerCollide for health and the
// old ammo piles, and a hard-coded ItemComponent branch in GameplaySystem for
// items. Walk-over versus press-to-take is now a property on one class rather
// than the difference between three systems.
//
// Registered under BOTH "Pickup" and "WeaponPickup" — the older name is what
// every already-placed .ent file says, and renaming a behavior must not silently
// turn placed pickups into inert props.
// ---------------------------------------------------------------------------
class PickupBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "weaponType",   BehaviorPropType::INT,    &m_weaponType   },
            { "ammoAmount",   BehaviorPropType::INT,    &m_ammoAmount   },
            { "ammoType",     BehaviorPropType::INT,    &m_ammoType     },
            { "healthAmount", BehaviorPropType::INT,    &m_healthAmount },
            { "itemId",       BehaviorPropType::STRING, &m_itemId       },
            { "itemCount",    BehaviorPropType::INT,    &m_itemCount    },
            { "bobSpeed",     BehaviorPropType::FLOAT,  &m_bobSpeed     },
            { "spinSpeed",    BehaviorPropType::FLOAT,  &m_spinSpeed    },
            { "pickupRadius", BehaviorPropType::FLOAT,  &m_pickupRadius },
            { "requiresInteract", BehaviorPropType::BOOL, &m_requiresInteract },
            { "pickupSound",  BehaviorPropType::STRING, &m_pickupSound  },
        };
    }

private:
    // --- Weapon channel ------------------------------------------------------
    // WEAP_NONE means "grants no weapon", which is what makes an ammo-only or
    // item-only pickup work with no extra code: giveWeapon(WEAP_NONE) finds the
    // slot already owned, returns false and switches nothing.
    int   m_weaponType = static_cast<int>(WEAP_NONE);

    // --- Ammunition channel --------------------------------------------------
    // _ammo_auto means "however many weaponPickupAmmo() says for this weapon" /
    // "whichever pool that weapon drinks from". AMMO_NONE keeps its own distinct
    // meaning: grant the weapon and no ammunition at all.
    int   m_ammoAmount = _ammo_auto;
    int   m_ammoType   = _ammo_auto;

    // --- Health channel ------------------------------------------------------
    // Straight into DamageReceiverComponent, clamped at the threshold. Zero
    // disables the channel. A pickup that would heal nothing because the player
    // is already full is REFUSED rather than consumed — see update().
    int   m_healthAmount = 0;

    // --- Item channel --------------------------------------------------------
    // An ItemDef id, i.e. the .item filename stem. Whether it is used on the
    // spot or stored is the ITEM's business (ItemDef::autoUse), not the pickup's:
    // a medkit is used and a potion is kept wherever either happens to be lying.
    // Falls back to the entity's own ItemComponent when left empty, so an item
    // entity does not have to name its item twice. ItemComponent stays the
    // world-side answer to "what is this thing" — the HUD's aim prompt reads it
    // for the item's name — and this behavior is what turns that into a grant.
    std::string m_itemId;
    int         m_itemCount = 1;

    float m_bobSpeed   = 1.5f;  // radians/sec through the bob cycle
    float m_spinSpeed  = 45.0f; // degrees/sec about Y

    // Deliberately its OWN property rather than being derived from the entity's
    // scale. The old script pickup path built its trigger box out of the
    // transform scale, which entangles how big a pickup LOOKS with how close you
    // must get to it — and the weapon models are scaled to 0.01, which would
    // leave a trigger a couple of centimetres wide.
    float m_pickupRadius = 1.0f;

    // Press-to-take rather than walk-over. The two styles were fractured across
    // three systems; this is the switch between them on one.
    //
    // When set, m_pickupRadius is IGNORED: the interact ray already enforces
    // both aim and range (_player_interact_distance), and requiring the radius
    // as well would mean a pickup you are looking at but standing slightly too
    // far from silently does nothing when you press the key.
    bool m_requiresInteract = false;

    // Overrides the collection sound. Empty falls back to the item's own
    // pickupsound, then to the generic pair (a draw cue for a new weapon, a blip
    // for a top-up).
    std::string m_pickupSound;

    // Kills are queued to the end of the frame, so this entity keeps ticking
    // after it has been collected. Without this the pickup fires again on every
    // remaining frame — several sounds, and several grants.
    bool  m_collected = false;

    float m_bobTimer = 0.0f;
    float m_baseY    = 0.0f;

    // True when taking this pickup would change something. Everything is tested
    // BEFORE anything is handed over, so a pickup that turns out to be worthless
    // can be left in the world rather than silently eaten.
    bool wouldGrantAnything(anax::Entity& entity) const;

    // m_itemId, or the entity's ItemComponent when that is empty.
    std::string resolveItemId(anax::Entity& entity) const;
};
