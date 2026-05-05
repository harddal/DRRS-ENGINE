#include "TweenSystem.h"

#include "Engine/Renderer/RenderManager.h"

#include <spdlog/spdlog.h>
#include <cmath>

using namespace anax;
using namespace irr;
using namespace irr::core;
using namespace irr::scene;
using namespace irr::video;

void TweenSystem::onEntityAdded(Entity& entity)
{
    auto& mc = entity.getComponent<MeshComponent>();
    auto& tw = entity.getComponent<TweenComponent>();

    if (!mc.node || !mc.trimesh)
    {
        spdlog::warn("TweenSystem::onEntityAdded - entity '{}' has no mesh node, TweenComponent will be inactive",
            entity.getComponent<DescriptorComponent>().name);
        return;
    }

    const int bufCount = static_cast<int>(mc.trimesh->getMeshBufferCount());
    if (tw.movingBufferIndex < 0 || tw.movingBufferIndex >= bufCount)
    {
        spdlog::warn("TweenSystem::onEntityAdded - movingBufferIndex {} out of range ({} buffers) on entity '{}'",
            tw.movingBufferIndex, bufCount, entity.getComponent<DescriptorComponent>().name);
        return;
    }

    auto* smgr = RenderManager::Get()->sceneManager();

    // Build a single-buffer SMesh for the moving part
    auto* movingMesh = new SMesh();
    movingMesh->addMeshBuffer(mc.trimesh->getMeshBuffer(tw.movingBufferIndex));
    movingMesh->recalculateBoundingBox();

    // Create child node parented to mc.node so it inherits body transform
    auto* childNode = smgr->addMeshSceneNode(movingMesh, mc.node);
    movingMesh->drop();

    if (!childNode)
    {
        spdlog::warn("TweenSystem::onEntityAdded - failed to create child scene node for entity '{}'",
            entity.getComponent<DescriptorComponent>().name);
        return;
    }

    // Copy material from the parent node's corresponding slot
    if (mc.node->getMaterialCount() > static_cast<u32>(tw.movingBufferIndex))
        childNode->getMaterial(0) = mc.node->getMaterial(tw.movingBufferIndex);

    // Match parent node material flags
    childNode->setMaterialFlag(EMF_NORMALIZE_NORMALS,  true);
    childNode->setMaterialFlag(EMF_GOURAUD_SHADING,    false);
    childNode->setMaterialFlag(EMF_LIGHTING,           true);
    childNode->setMaterialFlag(EMF_TRILINEAR_FILTER,   true);
    childNode->setMaterialFlag(EMF_ANISOTROPIC_FILTER, true);

    // Hide the moving buffer on the parent so it doesn't render twice
    mc.node->getMaterial(tw.movingBufferIndex).ColorMask    = ECP_NONE;
    mc.node->getMaterial(tw.movingBufferIndex).ZWriteEnable = false;

    childNode->setPosition(tw.startPosition);
    childNode->setRotation(tw.startRotation);

    tw.movingNode = childNode;
    tw.t          = 0.0f;
    tw.active     = false;
}

void TweenSystem::onEntityRemoved(Entity& entity)
{
    // movingNode is a child of mc.node in the scene graph.
    // Irrlicht removes all children automatically when the parent is removed,
    // so calling remove() here would hit a dangling pointer. Just clear it.
    entity.getComponent<TweenComponent>().movingNode = nullptr;
}

void TweenSystem::update(irr::f32 dt)
{
    for (auto& entity : getEntities())
    {
        auto& tw = entity.getComponent<TweenComponent>();
        if (!tw.movingNode) continue;

        const float target = tw.active ? 1.0f : 0.0f;
        tw.t += (target - tw.t) * (1.0f - std::exp(-tw.speed * dt));

        tw.movingNode->setPosition(tw.startPosition.getInterpolated(tw.targetPosition, tw.t));
        tw.movingNode->setRotation(tw.startRotation.getInterpolated(tw.targetRotation, tw.t));
    }
}
