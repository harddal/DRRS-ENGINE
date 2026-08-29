#include "Game/Behavior/Classes/WeaponPickupBehavior.h"

#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/WorldManager.h"
#include "Engine/Sound/SoundManager.h"

#include "Game/Player/PlayerController.h"

#include <cmath>

void WeaponPickupBehavior::init(anax::Entity& entity)
{
    m_bobTimer  = 0.0f;
    m_collected = false;

    if (entity.hasComponent<TransformComponent>())
        m_baseY = entity.getComponent<TransformComponent>().position.Y;
}

void WeaponPickupBehavior::update(anax::Entity& entity, float dt)
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

    const irr::core::vector3df playerPos = player.getComponent<TransformComponent>().getPosition();

    // Measured against the pickup's BASE height, not its bobbing one, so the
    // trigger does not breathe in and out with the animation.
    irr::core::vector3df toPickup = tc.getPosition() - playerPos;
    toPickup.Y = m_baseY - playerPos.Y;

    if (toPickup.getLengthSQ() > m_pickupRadius * m_pickupRadius)
        return;

    const auto weapon = static_cast<PLAYER_WEAPON>(m_weaponType);

    auto* weapons = g_PlayerController->weaponController();
    if (!weapons) return;

    // A weapon already carried grants its ammunition and nothing else — walking
    // over a spare should not yank the gun in your hands away mid-fight.
    // giveWeapon() reports which case this was.
    const bool wasNew = weapons->giveWeapon(weapon, true);

    // Only when a type was actually specified. Picking one here would mean
    // quietly filling some arbitrary pool; AMMO_NONE means "this pickup grants
    // no reserve", which is the truth for every weapon in the glTF set — they
    // all carry self-contained magazines and never read the shared pool.
    if (m_ammoAmount > 0 && m_ammoType != static_cast<int>(AMMO_NONE)
        && m_ammoType < static_cast<int>(AMMO_COUNT))
    {
        weapons->addAmmo(static_cast<AMMO_TYPE>(m_ammoType),
                         static_cast<unsigned int>(m_ammoAmount));
    }

    m_collected = true;

    SoundManager::Get()->sound()->play3D(
        wasNew ? "content/sound/weapon/equip.wav"
               : "content/sound/effect/pickup.wav",
        tc.getPosition());

    if (entity.hasComponent<DescriptorComponent>())
        WorldManager::Get()->killEntityByID(entity.getComponent<DescriptorComponent>().id);
}
