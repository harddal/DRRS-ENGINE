#pragma once

#include "anax/anax.hpp"
#include "Engine/World/Components.h"

#include "Engine/Resource/AnimatedSpriteLoader.h"

#include <irrlicht.h>

class AnimationCallback : public irr::scene::IAnimationEndCallBack
{
public:
    // Can only call once per frame per mesh since the state (isEnded) resets when the function is executed
	bool hasAnimationEnded()
	{
		auto b = isEnded;
		isEnded = false;
		return b;
	}

protected:
	void OnAnimationEnd(irr::scene::IAnimatedMeshSceneNode* node) override
	{
		isEnded = true;
	}

	bool isEnded = true;
};

class GameEngine;

class RenderSystem
    : public anax::System<anax::Requires<DescriptorComponent, TransformComponent, RenderComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;
    void onEntityRemoved(anax::Entity& entity) override;

    void update();

    void setMeshComponentData(anax::Entity& entity);
    void setDebugMeshComponentData(anax::Entity& entity);
    void setLightComponentData(anax::Entity& entity);
    void setBillboardComponentData(anax::Entity& entity);
    void setDebugSpriteComponentData(anax::Entity& entity);

	void forceTransformUpdate(bool updateLights = true);

    void remove(std::string name);

    void setVisible(std::string name, bool visible);

	void setFrame(entityid id, irr::s32 frame);
	void setFrameLoop(entityid id, irr::s32 begin, irr::s32 end);
    
    void setFrame(std::string name, irr::s32 frame);
    void setFrameLoop(std::string name, irr::s32 begin, irr::s32 end);

	void playAnimation(entityid id, std::string animation);
    void loopAnimation(entityid id, std::string animation);
	void playAnimation(const anax::Entity& entity, std::string animation);

	bool swapMesh(entityid id, std::string file);
    void setNodeMaterialType(entityid id, irr::video::E_MATERIAL_TYPE material_type);

    // Node-wide shader + per-buffer overrides, in that order. Idempotent;
    // called at the end of setMeshComponentData() and by the editor after any
    // edit to the override list, which is what makes a REMOVED override fall
    // back to the node's own shader.
    static void applyMeshShaders(MeshComponent& meshComponent);

    // The override half on its own. applyMeshShaders() calls it; nothing else
    // should, or the node-wide reset it depends on will not have happened.
    static void applyBufferShaderOverrides(MeshComponent& meshComponent);

    // Rebuilds (recalculate = true) or restores (false) the mesh's vertex normals
    // in place and flags the buffers dirty, so the editor checkbox takes effect on
    // the current frame rather than at the next mesh reload. The as-loaded normals
    // are snapshotted the first time a given mesh is recalculated, which is what
    // makes `false` a real undo instead of a no-op.
    //
    // propagateToSharedMeshes mirrors the flag onto every other entity holding the
    // same cached IMesh, since the normals themselves are shared. Editor edits want
    // it; the scene-load path does not, as the siblings are not built yet.
    static void applyRecalculateNormals(MeshComponent& meshComponent, bool recalculate,
                                        bool propagateToSharedMeshes = false);
    
	void setLightData(LightComponent& light)
	{
		light.node->setLightType(static_cast<irr::video::E_LIGHT_TYPE>(light.type));
		light.data = light.node->getLightData();
		light.data.OuterCone = light.outerCone;
		light.data.InnerCone = light.innerCone;
		light.data.Falloff = light.falloff;
		light.data.DiffuseColor = irr::video::SColorf(
			light.color_diffuse.r * light.intensity,
			light.color_diffuse.g * light.intensity,
			light.color_diffuse.b * light.intensity);
		light.node->setLightData(light.data);
		light.node->setRadius(light.radius);
		light.node->setVisible(light.visible);
	}

    void setDebugSpriteVisible(bool visible = true)
	{
		m_drawDebugSprites = visible;
		m_applyDebugSpriteVisSetting = true;
	}
	bool isDebugSpriteVisible()
	{
		return m_drawDebugSprites;
	}

	bool getEntityAABBIntersection(entityid e1, entityid e2);

	void swapTexture(entityid id, unsigned int texture_id);

private:
	bool m_drawDebugSprites = false, m_applyDebugSpriteVisSetting = false;
};
