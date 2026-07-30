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

		recalculateNormals,
		texturePointFilter,
		isViewmodel,
		navCookable;

	irr::s32 fps;

	irr::u32 currentFrame;
	std::vector<sAnimationData> animationList;
    sAnimationData lastPlayedAnimation;

    // Look up a clip by name; null if the mesh has no such animation.
    // Linear scan is the right call here: animationList holds a handful of
    // entries in one contiguous block, so a hash map's hashing + indirection
    // would cost more than the string compares it saves.
    // NOTE: the result points into animationList — don't hold it across an
    // edit to the list.
    const sAnimationData* findAnimation(const std::string& name) const
    {
        for (const auto& a : animationList)
            if (a.name == name)
                return &a;

        return nullptr;
    }

	std::shared_ptr<AnimationCallback> animation_call_back;

	std::vector<std::string> textures;

    // PBR texture maps — bound to their fixed slots at load time.
    // Leave empty to fall back to uniform-based roughness/metallic (fully backward compatible).
    std::string texNormal;
    std::string texRoughness;
    std::string texMetallic;
    std::string texEmission;

    irr::scene::IAnimatedMesh* trimesh;
	irr::scene::IAnimatedMeshSceneNode* node;

	irr::scene::ITriangleSelector* selector;

    irr::video::E_MATERIAL_TYPE renderMaterial;
    std::string shaderName; // shader key for ShaderMaterialManager; empty = "phong_perpixel"

    // Lightmap data — populated by LightmapBaker after a bake pass.
    // Null until the entity has been baked.
    MeshLightmap* lightmap = nullptr;

    // Per-material shader params uploaded as MaterialTypeParams[0..7] on node creation.
    float materialTypeParams[8] = {};

    // Per-material opacity multiplier (uAlpha in phong_perpixel.frag), applied via
    // the material's DiffuseColor alpha. 1.0 = fully opaque (default, matches the
    // previous hardcoded behavior). Only visible on transparent material types —
    // used to fade skybox cloud layers and other alpha-blended geometry.
    float opacity = 1.0f;

    // Runtime-only: tracks whether this node is currently registered with the
    // RenderManager 3D-skybox pass. Kept in sync each frame by RenderSystem
    // against the presence of a SkyboxComponent. Not serialized.
    bool registeredSky = false;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(mesh), CEREAL_NVP(textures), CEREAL_NVP(isVisible), CEREAL_NVP(isAnimated),
			CEREAL_NVP(receiveShadows), CEREAL_NVP(castShadows), CEREAL_NVP(transparent), CEREAL_NVP(disableZDraw), CEREAL_NVP(recalculateNormals), CEREAL_NVP(texturePointFilter),
			CEREAL_NVP(renderMaterial), CEREAL_NVP(isViewmodel),
			CEREAL_NVP(navCookable));
        try { archive(CEREAL_NVP(shaderName)); } catch (cereal::Exception&) {}
        try { archive(cereal::make_nvp("mtp0", materialTypeParams[0]),
                      cereal::make_nvp("mtp1", materialTypeParams[1]),
                      cereal::make_nvp("mtp2", materialTypeParams[2]),
                      cereal::make_nvp("mtp3", materialTypeParams[3]),
                      cereal::make_nvp("mtp4", materialTypeParams[4]),
                      cereal::make_nvp("mtp5", materialTypeParams[5]),
                      cereal::make_nvp("mtp6", materialTypeParams[6]),
                      cereal::make_nvp("mtp7", materialTypeParams[7])); }
        catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(texNormal));    } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(texRoughness)); } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(texMetallic));  } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(texEmission));  } catch (cereal::Exception&) {}
        try { archive(CEREAL_NVP(opacity));      } catch (cereal::Exception&) {}
	}

	MeshComponent() :
		isPrimitive(false), isVisible(true), isAnimated(false), receiveShadows(false), castShadows(false),
		transparent(false), disableZDraw(false), isPreview(false), recalculateNormals(false), texturePointFilter(false), isViewmodel(false), navCookable(false),
        fps(60), currentFrame(0), trimesh(nullptr), node(nullptr), selector(nullptr), 
        renderMaterial(irr::video::EMT_SOLID) {}
};
