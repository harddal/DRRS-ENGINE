#include "Game/Behavior/Classes/WeaponPickupBehavior.h"
#include "Engine/World/Components/TransformComponent.h"

void WeaponPickupBehavior::init(anax::Entity& entity)
{
    m_bobTimer = 0.0f;
    if (entity.hasComponent<TransformComponent>())
        m_baseY = entity.getComponent<TransformComponent>().position.Y;
}

void WeaponPickupBehavior::update(anax::Entity& entity, float dt)
{
    if (!entity.hasComponent<TransformComponent>()) return;
    auto& tc = entity.getComponent<TransformComponent>();

    m_bobTimer += dt * m_bobSpeed;
    tc.position.Y = m_baseY + std::sin(m_bobTimer) * 0.1f;
    tc.rotation.Y += dt * 0.1f;
}
