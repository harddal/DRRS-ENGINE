#pragma once

#include <vector>

#include "anax/Component.hpp"

#include "irrlicht.h"

#include "cereal/cereal.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

class AnimationCallback;

// Forward-declare so MeshComponent can hold a pointer without pulling in the
// full LightmapBaker header (and xatlas) everywhere MeshComponent is used.
struct MeshLightmap;

enum TRIANGLE_PICKING_FLAG
{
	TPF_NOT_PICKABLE = 0,
	TPF_PICKABLE = 1
};

struct sAnimationData
{
	bool loop;
	std::string name;
	irr::core::vector2di frames;

    sAnimationData() : loop(false) {}
    sAnimationData(std::string name, unsigned int start, unsigned int end, bool loop = false)
    {
        this->loop = loop;
        this->name = name;
        frames = irr::core::vector2di(start, end);
    }
};

struct MeshComponent : anax::Component
{
	std::string mesh;

	bool
		isPrimitive,
		isVisible,
		isAnimated,
		receiveShadows,
		castShadows,
		transparent,
		disableZDraw,
		isPreview,

		// DEPRECATED
		disableDeferredRendering,

		recalculateNormals,
		texturePointFilter,
		isViewmodel,
		navCookable;

	irr::s32 fps;

	irr::u32 currentFrame;
	std::vector<sAnimationData> animationList;
    sAnimationData lastPlayedAnimation;

	std::shared_ptr<AnimationCallback> animation_call_back;

	std::vector<std::string> textures;

    irr::scene::IAnimatedMesh* trimesh;
	irr::scene::IAnimatedMeshSceneNode* node;

	irr::scene::ITriangleSelector* selector;

    irr::video::E_MATERIAL_TYPE renderMaterial;

    // Lightmap data — populated by LightmapBaker after a bake pass.
    // Null until the entity has been baked.
    MeshLightmap* lightmap = nullptr;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(mesh), CEREAL_NVP(textures), CEREAL_NVP(isVisible), CEREAL_NVP(isAnimated),
			CEREAL_NVP(receiveShadows), CEREAL_NVP(castShadows), CEREAL_NVP(transparent), CEREAL_NVP(disableZDraw), CEREAL_NVP(recalculateNormals), CEREAL_NVP(texturePointFilter),
			CEREAL_NVP(disableDeferredRendering), CEREAL_NVP(renderMaterial), CEREAL_NVP(isViewmodel),
			CEREAL_NVP(navCookable));
	}

	MeshComponent() :
		isPrimitive(false), isVisible(true), isAnimated(false), receiveShadows(false), castShadows(false),
		transparent(false), disableZDraw(false), isPreview(false), disableDeferredRendering(false), recalculateNormals(false), texturePointFilter(false), isViewmodel(false), navCookable(false),
        fps(60), currentFrame(0), trimesh(nullptr), node(nullptr), selector(nullptr), 
        renderMaterial(irr::video::EMT_SOLID) {}
};
