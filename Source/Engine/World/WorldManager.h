#pragma once

#include <string>

#include <irrlicht.h>
#include <anax/anax.hpp>

#include <cereal/cereal.hpp>
#include <cereal/archives/xml.hpp>

#include "Engine/World/Components.h"
#include "Engine/World/Systems.h"

#include "Engine/Types.h"
#include "Game/GameplaySystem.h"
#include "Game/NPC/NPCSystem.h"

#include "Engine/Renderer/RenderManager.h"

struct GlobalCVar
{
    std::string name, value;

    GlobalCVar(std::string name, std::string value)
    {
        this->name = name;
        this->value = value;
    }
};

struct EntitySpawnDescriptor
{
    entityid id;

    std::string file;
    std::string name;

    bool preserve_transform;

    irr::core::vector3df position;
    irr::core::vector3df rotation;
    irr::core::vector3df scale;

    EntitySpawnDescriptor() : 
        id(_entity_null_value), preserve_transform(false), position(irr::core::vector3df(0.0f, 0.0f, 0.0f)), rotation(irr::core::vector3df(0.0f, 0.0f, 0.0f)), scale(irr::core::vector3df(1.0f, 1.0f, 1.0f)) {}
    EntitySpawnDescriptor(const std::string& file, bool preserve_transform = false, const std::string& name = "",
        const irr::core::vector3df& position = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& rotation = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& scale    = irr::core::vector3df(1.0f, 1.0f, 1.0f)) : id(_entity_null_value)
    {
        this->file = file;
        this->preserve_transform = preserve_transform;
        this->name = name;
        this->position = position;
        this->rotation = rotation;
        this->scale = scale;
    }

    void load(const std::string& file, bool preserve_transform = false, const std::string& name = "",
        const irr::core::vector3df& position = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& rotation = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& scale    = irr::core::vector3df(1.0f, 1.0f, 1.0f))
    {
        this->file = file;
        this->preserve_transform = preserve_transform;
        this->name = name;
        this->position = position;
        this->rotation = rotation;
        this->scale = scale;
    }
};

struct SceneDescriptor
{
    std::string name, creator, notes, skydome_texture;

    irr::video::SColorf ambient_light;

	bool useFXAA, useBloom, useTonemapping, useSharpen, useAutoExposure;
	float bloomThreshold, bloomStrength, tonemapExposure, tonemapwhitePoint, sharpenStrength;

	bool  usePixelate  = false;
	float pixelateSize = 4.0f;

	// Fog
	float fogDensity  = 0.0f;
	float fogStart    = 5.0f;
	irr::video::SColorf fogColor = irr::video::SColorf(0.0f, 0.3f, 0.6f, 1.0f);

	// Color grade
	bool  useColorGrade  = false;
	float cgSaturation   = 1.0f;
	float cgBrightness   = 0.0f;
	float cgTintR        = 1.0f;
	float cgTintG        = 1.0f;
	float cgTintB        = 1.0f;

	// Posterize
	bool  usePosterize   = false;
	float posterizeLevels  = 24.0f;
	float posterizeStrength = 0.7f;

	// Film grain
	bool  useFilmGrain      = false;
	float filmGrainStrength = 0.025f;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(name), CEREAL_NVP(creator), CEREAL_NVP(notes), CEREAL_NVP(skydome_texture),
			CEREAL_NVP(ambient_light.r), CEREAL_NVP(ambient_light.g), CEREAL_NVP(ambient_light.b),
			CEREAL_NVP(useFXAA), CEREAL_NVP(useBloom), CEREAL_NVP(useTonemapping), CEREAL_NVP(useSharpen), CEREAL_NVP(useAutoExposure),
			CEREAL_NVP(bloomThreshold), CEREAL_NVP(bloomStrength), CEREAL_NVP(tonemapExposure), CEREAL_NVP(tonemapwhitePoint), CEREAL_NVP(sharpenStrength)
		);
		// Optional fields added after initial release — old .scn files omit them; use inline defaults.
		try { archive(CEREAL_NVP(usePixelate)); } catch (...) {}
		try { archive(CEREAL_NVP(pixelateSize)); } catch (...) {}
		try { archive(CEREAL_NVP(fogDensity)); } catch (...) {}
		try { archive(CEREAL_NVP(fogStart)); } catch (...) {}
		try { archive(CEREAL_NVP(fogColor.r)); } catch (...) {}
		try { archive(CEREAL_NVP(fogColor.g)); } catch (...) {}
		try { archive(CEREAL_NVP(fogColor.b)); } catch (...) {}
		try { archive(CEREAL_NVP(useColorGrade)); } catch (...) {}
		try { archive(CEREAL_NVP(cgSaturation)); } catch (...) {}
		try { archive(CEREAL_NVP(cgBrightness)); } catch (...) {}
		try { archive(CEREAL_NVP(cgTintR)); } catch (...) {}
		try { archive(CEREAL_NVP(cgTintG)); } catch (...) {}
		try { archive(CEREAL_NVP(cgTintB)); } catch (...) {}
		try { archive(CEREAL_NVP(usePosterize)); } catch (...) {}
		try { archive(CEREAL_NVP(posterizeLevels)); } catch (...) {}
		try { archive(CEREAL_NVP(posterizeStrength)); } catch (...) {}
		try { archive(CEREAL_NVP(useFilmGrain)); } catch (...) {}
		try { archive(CEREAL_NVP(filmGrainStrength)); } catch (...) {}
	}
	
    SceneDescriptor& operator=(SceneDescriptor desc)
    {
        std::swap(name, desc.name);
		std::swap(creator, desc.creator);
		std::swap(notes, desc.notes);
        std::swap(skydome_texture, desc.skydome_texture);
        std::swap(ambient_light, desc.ambient_light);

		std::swap(useFXAA, desc.useFXAA);
		std::swap(useBloom, desc.useBloom);
		std::swap(useTonemapping, desc.useTonemapping);
		std::swap(useSharpen, desc.useSharpen);
		std::swap(useAutoExposure, desc.useAutoExposure);

		std::swap(bloomThreshold, desc.bloomThreshold);
		std::swap(bloomStrength, desc.bloomStrength);
		std::swap(tonemapExposure, desc.tonemapExposure);
		std::swap(tonemapwhitePoint, desc.tonemapwhitePoint);
		std::swap(sharpenStrength, desc.sharpenStrength);

		std::swap(usePixelate, desc.usePixelate);
		std::swap(pixelateSize, desc.pixelateSize);

		std::swap(fogDensity, desc.fogDensity);
		std::swap(fogStart,   desc.fogStart);
		std::swap(fogColor,   desc.fogColor);

		std::swap(useColorGrade,  desc.useColorGrade);
		std::swap(cgSaturation,   desc.cgSaturation);
		std::swap(cgBrightness,   desc.cgBrightness);
		std::swap(cgTintR,        desc.cgTintR);
		std::swap(cgTintG,        desc.cgTintG);
		std::swap(cgTintB,        desc.cgTintB);

		std::swap(usePosterize,       desc.usePosterize);
		std::swap(posterizeLevels,    desc.posterizeLevels);
		std::swap(posterizeStrength,  desc.posterizeStrength);

		std::swap(useFilmGrain,      desc.useFilmGrain);
		std::swap(filmGrainStrength, desc.filmGrainStrength);

        return *this;
    }

    SceneDescriptor()
    {
        skydome_texture = "content/texture/color/black.png";
        ambient_light = irr::video::SColorf(0.5f, 0.5f, 0.5f);

		useFXAA         = true;
		useBloom        = true;
		useTonemapping  = true;
		useSharpen      = true;
		useAutoExposure = false;

		bloomThreshold    = RenderManager::Get()->bloomBrightCallback()->threshold;
		bloomStrength     = RenderManager::Get()->bloomCompositeCallback()->strength;
		tonemapExposure   = RenderManager::Get()->tonemapCallback()->exposure;
		tonemapwhitePoint = RenderManager::Get()->tonemapCallback()->whitePoint;
		sharpenStrength   = RenderManager::Get()->sharpenCallback()->strength;
		pixelateSize      = RenderManager::Get()->pixelateCallback()->pixelSize;
    }
};

class WorldManager
{
public:
    WorldManager& operator=(const WorldManager&) = delete;

    WorldManager();
    ~WorldManager();

    void update(irr::f32 dt);

    void updateEntityQueues();

	entityid getNewID();
    void freeEntityID(entityid id);

    bool killEntityByName(const std::string& name);
    bool killEntityByID(int id);
    void killAllEntities();

    unsigned int spawnEntity(const std::string& file, const std::string&  name = "", bool preserve_transform = false,
        const irr::core::vector3df& position = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& rotation = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& scale    = irr::core::vector3df(1.0f, 1.0f, 1.0f));
	void exportEntity(anax::Entity& entity, const std::string& file, bool save_transform = false);

	// NOIMP
	void importPrefab(const std::string& file);
	// NOIMP
	void exportPrefab(const std::string& file);
	
	void importScene(const std::string& file);
	void exportScene(const std::string& file);

	SceneDescriptor getCurrentSceneDescriptor() const { return m_currentSceneDescriptor; }
	void setCurrentSceneDescriptor(SceneDescriptor& desc) { m_currentSceneDescriptor = desc; }
	
    bool getCVarExists(const std::string& name);
    std::string getCVarValue(const std::string& name);
    void setCVar(const std::string& name, const std::string& value);
    void removeCVar(const std::string& name);
    void clearCVars();

    anax::World* world() { return &m_gameWorld; }

    CameraSystem*    cameraSystem()    { return &m_cameraSystem; }
    ManagerSystem*   managerSystem()   { return &m_managerSystem; }
    PhysicsSystem*   physicsSystem()   { return &m_physicsSystem; }
    CCTSubsystem*    cctSystem()       { return &m_cctSystem; }
    RenderSystem*    renderSystem()    { return &m_renderSystem; }
    ScriptSystem*    scriptSystem()    { return &m_scriptSystem; }
    SoundSystem*     soundSystem()     { return &m_soundSystem; }
    TransformSystem* transformSystem() { return &m_transformSystem; }
	TweenSystem*      tweenSystem()      { return &m_tweenSystem; }
	NavigationSystem* navigationSystem() { return &m_navigationSystem; }
	GameplaySystem*   gameplaySystem()   { return &m_gameplaySystem; }
	NPCSystem*        npcSystem()        { return &m_npcSystem; }

    static WorldManager* Get() { return s_Instance; }

	irr::f32 getWorldTime() { return m_worldTime; }
	irr::f32 getCameraTime() { return m_cameraTime; }
	irr::f32 getManagerTime() { return m_managerTime; }
	irr::f32 getPhysicsTime() { return m_physicsTime; }
	irr::f32 getRenderTime() { return m_renderTime; }
	irr::f32 getScriptTime() { return m_scriptTime; }
	irr::f32 getSoundTime() { return m_soundTime; }
	irr::f32 getTransformTime() { return m_transformTime; }
	irr::f32 getGameplayTime() { return m_gameplayTime; }
	irr::f32 getNpcTime()     { return m_npcTime; }

protected:
    std::vector<GlobalCVar> m_globalCVarList;

    void serializeEntity(anax::Entity& entity, cereal::XMLOutputArchive& archive, bool save_transform = false);
    entityid deserializeEntity(const std::string& file = "", entityid id = _entity_null_value,
        bool use_saved_transform = false, const std::string& name = "",
        const irr::core::vector3df& position = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& rotation = irr::core::vector3df(0.0f, 0.0f, 0.0f),
        const irr::core::vector3df& scale    = irr::core::vector3df(1.0f, 1.0f, 1.0f));

private:
    static WorldManager* s_Instance;

	SceneDescriptor m_currentSceneDescriptor;
	
    anax::World m_gameWorld;

    std::vector<entityid> m_killedEntityIDQueue;
    std::vector<EntitySpawnDescriptor> m_entitySpawnQueue;

    std::array<bool, _entity_null_value> m_entityIDArray;

    CameraSystem m_cameraSystem;
    ManagerSystem m_managerSystem;
    PhysicsSystem m_physicsSystem;
    CCTSubsystem m_cctSystem;
    RenderSystem m_renderSystem;
    ScriptSystem m_scriptSystem;
    SoundSystem m_soundSystem;
    TransformSystem m_transformSystem;
	TweenSystem m_tweenSystem;
	NavigationSystem m_navigationSystem;
	GameplaySystem m_gameplaySystem;
	NPCSystem      m_npcSystem;

	irr::f32 m_worldCurrent, m_worldLast, m_worldTime;
	irr::f32 m_cameraCurrent, m_cameraLast, m_cameraTime;
	irr::f32 m_managerCurrent, m_managerLast, m_managerTime;
	irr::f32 m_physicsCurrent, m_physicsLast, m_physicsTime;
	irr::f32 m_renderCurrent, m_renderLast, m_renderTime;
	irr::f32 m_scriptCurrent, m_scriptLast, m_scriptTime;
	irr::f32 m_soundCurrent, m_soundLast, m_soundTime;
	irr::f32 m_transformCurrent, m_transformLast, m_transformTime;
	irr::f32 m_gameplayCurrent, m_gameplayLast, m_gameplayTime;
	irr::f32 m_npcCurrent, m_npcTime;
};