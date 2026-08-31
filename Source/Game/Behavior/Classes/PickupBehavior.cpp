#include "Game/Behavior/Classes/PickupBehavior.h"

#include "Engine/World/Components/TransformComponent.h"
#include "Game/Components/InteractionComponent.h"
#include "Game/Components/ItemComponent.h"
#include "Engine/World/WorldManager.h"
#include "Engine/Sound/SoundManager.h"

#include "Game/Item/ItemDatabase.h"
#include "Game/Player/PlayerController.h"

#include <cmath>

void PickupBehavior::init(anax::Entity& entity)
{
    m_bobTimer  = 0.0f;
    m_collected = false;

    if (entity.hasComponent<TransformComponent>())
        m_baseY = entity.getComponent<TransformComponent>().position.Y;
}

std::string PickupBehavior::resolveItemId(anax::Entity& entity) const
{
    if (!m_itemId.empty())
        return m_itemId;

    // An item entity already names its item through ItemComponent, and the HUD
    // aim prompt reads it from there. Falling back to it means the id is stated
    // once rather than kept in step in two places.
    if (entity.hasComponent<ItemComponent>())
        return entity.getComponent<ItemComponent>().item;

    return std::string();
}

bool PickupBehavior::wouldGrantAnything(anax::Entity& entity) const
{
    if (!g_PlayerController)
        return false;

    auto* weapons = g_PlayerController->weaponController();
    if (!weapons)
        return false;

    const auto weapon = static_cast<PLAYER_WEAPON>(m_weaponType);

    // --- Weapon ---
    if (weapon != WEAP_NONE && !weapons->hasWeapon(weapon))
        return true;

    // --- Ammunition ---
    const AMMO_TYPE ammoType = (m_ammoType == _ammo_auto)
        ? weaponAmmoType(weapon)
        : static_cast<AMMO_TYPE>(m_ammoType);

    const int ammoAmount = (m_ammoAmount == _ammo_auto)
        ? weaponPickupAmmo(weapon)
        : m_ammoAmount;

    if (ammoAmount > 0 && ammoType > AMMO_NONE && ammoType < AMMO_COUNT
        && weapons->reserveAmmo(ammoType) < static_cast<unsigned int>(ammoReserveMax(ammoType)))
    {
        return true;
    }

    // --- Health ---
    // Only when it would actually heal. This is the rule medkit_small.asc
    // hand-rolled inline; it now applies to every health pickup for free.
    if (m_healthAmount > 0)
    {
        auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

        if (player.isValid() && player.hasComponent<DamageReceiverComponent>())
        {
            auto& damage = player.getComponent<DamageReceiverComponent>();
            if (damage.health < damage.threshold)
                return true;
        }
    }

    // --- Item ---
    // Always worth taking: there is no capacity, so an item pickup can never be
    // refused. An auto-use item that would do nothing right now is STORED rather
    // than wasted, which is still a change worth collecting for.
    const std::string itemId = resolveItemId(entity);

    if (!itemId.empty() && m_itemCount > 0 && ItemDatabase::Exists(itemId))
        return true;

    return false;
}

void PickupBehavior::update(anax::Entity& entity, float dt)
{
    if (!entity.hasComponent<TransformComponent>()) return;
    auto& tc = entity.getComponent<TransformComponent>();

    // dt arrives in MILLISECONDS — every other behavior in the project converts
    // it (see MeleeZombieBehavior). Without this the bob timer advanced ~25
    // radians a frame, which is several full cycles per frame: the pickup did not
    // bob, it flickered.
    const float dt_s = dt * 0.001f;

    // TransformSystem syncs component -> node on its own, so this writes
    // tc.position/rotation and never touches tc.node directly.
    m_bobTimer += dt_s * m_bobSpeed;
    tc.position.Y  = m_baseY + std::sin(m_bobTimer) * 0.1f;
    tc.rotation.Y += dt_s * m_spinSpeed;

    if (tc.rotation.Y >= 360.0f)
        tc.rotation.Y -= 360.0f;

    // --- Collection ---------------------------------------------------------

    // Kills are queued to the end of the frame, so this keeps ticking after it
    // has been taken. Guarding here rather than relying on the entity vanishing
    // is what stops a single pickup arming the player several times over.
    if (m_collected) return;

    if (!g_PlayerController) return;

    auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
    if (!player.isValid() || !player.hasComponent<TransformComponent>()) return;

    // --- Trigger: press-to-take, or walk-over ---------------------------------
    if (m_requiresInteract)
    {
        if (!entity.hasComponent<InteractionComponent>())
            return; // asked to be interacted with, but has nothing to interact through

        auto& interaction = entity.getComponent<InteractionComponent>();

        if (!interaction.interact)
            return;

        // Consumed UNCONDITIONALLY, before the worth test below. It is a one-shot
        // flag raised by GameplaySystem::interact(); leaving it latched when the
        // pickup declines would re-fire it on every subsequent frame.
        interaction.interact = false;

        // m_pickupRadius is deliberately not tested here — the interact ray has
        // already enforced both aim and range.
    }
    else
    {
        const irr::core::vector3df playerPos = player.getComponent<TransformComponent>().getPosition();

        // Measured against the pickup's BASE height, not its bobbing one, so the
        // trigger does not breathe in and out with the animation.
        irr::core::vector3df toPickup = tc.getPosition() - playerPos;
        toPickup.Y = m_baseY - playerPos.Y;

        if (toPickup.getLengthSQ() > m_pickupRadius * m_pickupRadius)
            return;
    }

    // Tested BEFORE anything changes hands, so a pickup that would do nothing
    // stays in the world to come back for rather than being quietly eaten.
    if (!wouldGrantAnything(entity))
        return;

    auto* weapons = g_PlayerController->weaponController();
    if (!weapons) return;

    const auto weapon = static_cast<PLAYER_WEAPON>(m_weaponType);

    // --- Weapon --------------------------------------------------------------
    // A weapon already carried grants its ammunition and nothing else — walking
    // over a spare should not yank the gun in your hands away mid-fight.
    // giveWeapon() reports which case this was.
    const bool wasNewWeapon = weapons->giveWeapon(weapon, true);

    // --- Ammunition ----------------------------------------------------------
    const AMMO_TYPE ammoType = (m_ammoType == _ammo_auto)
        ? weaponAmmoType(weapon)
        : static_cast<AMMO_TYPE>(m_ammoType);

    const int ammoAmount = (m_ammoAmount == _ammo_auto)
        ? weaponPickupAmmo(weapon)
        : m_ammoAmount;

    if (ammoAmount > 0 && ammoType > AMMO_NONE && ammoType < AMMO_COUNT)
        weapons->addAmmo(ammoType, static_cast<unsigned int>(ammoAmount));

    // --- Health --------------------------------------------------------------
    // Into the COMPONENT, clamped at the threshold. g_PlayerData.currentHealth is
    // only a copy refreshed from here every frame, so writing to that would be
    // undone before anything read it.
    if (m_healthAmount > 0 && player.hasComponent<DamageReceiverComponent>())
    {
        auto& damage = player.getComponent<DamageReceiverComponent>();

        damage.health += m_healthAmount;

        if (damage.health > damage.threshold)
            damage.health = damage.threshold;
    }

    // --- Item ----------------------------------------------------------------
    // giveItem() decides used-now versus stored from the item's own autoUse flag.
    const std::string itemId = resolveItemId(entity);

    if (!itemId.empty() && m_itemCount > 0)
    {
        if (auto* inventory = g_PlayerController->inventoryController())
        {
            // Per-instance data rides along from the entity when it has any —
            // a part-spent charge placed in a level keeps its state.
            const std::string data = entity.hasComponent<ItemComponent>()
                ? entity.getComponent<ItemComponent>().data
                : std::string();

            inventory->giveItem(itemId, m_itemCount, data);
        }
    }

    m_collected = true;

    // Sound priority: the pickup's own override, then the item's, then the
    // generic pair. The override exists because the ammo piles converted from
    // .asc scripts each carried their own cue.
    std::string sound = m_pickupSound;

    if (sound.empty() && !itemId.empty())
    {
        const ItemDef& def = ItemDatabase::Get(itemId);
        if (def.valid() && !def.pickupSound.empty())
            sound = "content/sound/" + def.pickupSound + ".wav";
    }

    if (sound.empty())
    {
        sound = wasNewWeapon ? "content/sound/weapon/equip.wav"
                             : "content/sound/effect/pickup.wav";
    }

    SoundManager::Get()->sound()->play3D(sound.c_str(), tc.getPosition());

    if (entity.hasComponent<DescriptorComponent>())
        WorldManager::Get()->killEntityByID(entity.getComponent<DescriptorComponent>().id);
}
