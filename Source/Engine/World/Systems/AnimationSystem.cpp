#include "Engine/World/Systems/AnimationSystem.h"

#include "Engine/Physics/PhysicsManager.h"

namespace
{
    // The one ground probe every migrated character uses.
    //
    // TWO TRAPS, BOTH ALREADY PAID FOR ELSEWHERE IN THIS PROJECT:
    //
    //  * RHG_CHARACTER is passed as an EXCLUDE mask, not an include one. The
    //    player capsule is tagged RHG_DYNAMIC so weapons and damage traces keep
    //    hitting it, which also makes it ground and wall to every probe - a foot
    //    probe would otherwise find the character's own capsule. PhysX's filter
    //    equation is a bitwise AND and cannot express "except", which is exactly
    //    why raycast() carries a separate exclude parameter.
    //
    //  * PhysicsManager::raycast IGNORES its 'group' argument and hardcodes
    //    RHG_STATIC | RHG_DYNAMIC (PhysicsManager.cpp, and it says so). Clip
    //    brushes carry only RHG_CLIP_* and are therefore invisible here, while
    //    the CCT does stand on them. A character standing on a clip brush will
    //    have its feet placed against whatever real geometry lies below it.
    //    Correct behaviour would need the source-type mask refactor that comment
    //    describes; until then this is a known limitation, not a surprise.
    class PhysXGroundProbe : public IAnimGroundProbe
    {
    public:
        bool probeGround(const irr::core::vector3df& fromWorld, float downDistance,
                         irr::core::vector3df& hitWorld,
                         irr::core::vector3df& normalWorld) override
        {
            PhysicsManager* physics = PhysicsManager::Get();
            if (!physics)
                return false;

            const RaycastData result = physics->raycast(
                fromWorld, irr::core::vector3df(0.0f, -1.0f, 0.0f), downDistance,
                RHG_ANY_HIT, RHG_CHARACTER);

            if (!result.hit)
                return false;

            hitWorld.set(result.data.block.position.x,
                         result.data.block.position.y,
                         result.data.block.position.z);

            normalWorld.set(result.data.block.normal.x,
                            result.data.block.normal.y,
                            result.data.block.normal.z);

            return true;
        }
    };

    PhysXGroundProbe g_groundProbe;
}

void AnimationSystem::onEntityRemoved(anax::Entity& entity)
{
    // detach(), NOT release(): RenderSystem::onEntityRemoved may already have
    // destroyed the scene node, and system removal callbacks have no guaranteed
    // order, so reaching through the node pointer here is a use-after-free.
    if (entity.hasComponent<AnimationComponent>())
        entity.getComponent<AnimationComponent>().graph.detach();
}

void AnimationSystem::update(irr::f32 dtMs)
{
    const float seconds = dtMs * 0.001f;

    auto& entities = getEntities();
    for (auto& entity : entities)
    {
        auto& anim = entity.getComponent<AnimationComponent>();
        auto& mesh = entity.getComponent<MeshComponent>();

        if (!anim.enabled)
        {
            anim.graph.release();
            continue;
        }

        // Binding is cheap once it has succeeded, and it has to be retried
        // every frame because the node is created by RenderSystem some frames
        // after the component is added.
        if (!anim.graph.bind(mesh))
            continue;

        anim.graph.setGroundProbe(&g_groundProbe);

        anim.graph.update(seconds);
    }
}
