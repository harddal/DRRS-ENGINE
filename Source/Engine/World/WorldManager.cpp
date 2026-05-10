#include "Engine/World/WorldManager.h"

#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Renderer/Lightmapper/LightmapBaker.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Prop/PropManager.h"

#include <string>
#include <fstream>
#include <sstream>
#include <cstdio>   // std::remove

#define MINIZ_IMPLEMENTATION
#include "miniz/miniz.h"

#include <spdlog/spdlog.h>
#include <tinyxml2.h>

#include "Utility/Utility.h"
#include "Engine/Engine.h"

#include "Editor/SceneInteractionManager.h"

using namespace anax;
using namespace cereal;
using namespace std;
using namespace tinyxml2;

WorldManager* WorldManager::s_Instance = nullptr;

WorldManager::WorldManager()
{
    if (s_Instance) { Utility::Error("Pointer to class \'WorldManager\' is invalid"); }
    s_Instance = this;

    m_entityIDArray.fill(false);

    m_gameWorld.addSystem(m_managerSystem);
    m_gameWorld.addSystem(m_cameraSystem);
    m_gameWorld.addSystem(m_transformSystem);
    m_gameWorld.addSystem(m_renderSystem);
    m_gameWorld.addSystem(m_tweenSystem);
    m_gameWorld.addSystem(m_physicsSystem);
    m_gameWorld.addSystem(m_cctSystem);
    m_gameWorld.addSystem(m_scriptSystem);
    m_gameWorld.addSystem(m_soundSystem);
	m_gameWorld.addSystem(m_navigationSystem);
	m_gameWorld.addSystem(m_gameplaySystem);

    m_physicsSystem.init();
    m_cctSystem.init(PhysicsManager::Get()->scene());

    m_gameWorld.refresh();
}

WorldManager::~WorldManager()
{
    m_gameWorld.removeAllSystems();
    
    delete s_Instance;
}

void WorldManager::update(irr::f32 dt)
{
#ifdef NDEBUG
	updateEntityQueues();

	m_managerSystem.update();
	m_transformSystem.update();
	m_renderSystem.update();
	m_cameraSystem.update();

	if (PropManager::Get()) PropManager::Get()->update();

	if (Engine::Get()->isGameMode())
	{
		m_scriptSystem.update();
		m_tweenSystem.update(dt);
		m_navigationSystem.update(dt);
		m_physicsSystem.update(dt);
		m_cctSystem.update(dt);
		m_gameplaySystem.update();
	}

	m_soundSystem.update();
#else
	m_worldCurrent = Engine::Get()->GetCounter();

    updateEntityQueues();

	m_managerCurrent = Engine::Get()->GetCounter();
    m_managerSystem.update();
	m_managerTime = Engine::Get()->GetCounter() - m_managerCurrent;

	m_transformCurrent = Engine::Get()->GetCounter();
    m_transformSystem.update();
	m_transformTime = Engine::Get()->GetCounter() - m_transformCurrent;

	m_cameraCurrent = Engine::Get()->GetCounter();
	m_cameraSystem.update();
	m_cameraTime = Engine::Get()->GetCounter() - m_cameraCurrent;

	if (PropManager::Get()) PropManager::Get()->update();

	m_renderCurrent = Engine::Get()->GetCounter();
    m_renderSystem.update();
	m_renderTime = Engine::Get()->GetCounter() - m_renderCurrent;

    if (Engine::Get()->isGameMode())
    {
		m_scriptCurrent = Engine::Get()->GetCounter();
        m_scriptSystem.update();
		m_scriptTime = Engine::Get()->GetCounter() - m_scriptCurrent;

		m_tweenSystem.update(dt);
		m_navigationSystem.update(dt);

		m_physicsCurrent = Engine::Get()->GetCounter();
        m_physicsSystem.update(dt);
		m_physicsTime = Engine::Get()->GetCounter() - m_physicsCurrent;

        m_cctSystem.update(dt);

		m_gameplayCurrent = Engine::Get()->GetCounter();
		m_gameplaySystem.update();
		m_gameplayTime = Engine::Get()->GetCounter() - m_gameplayCurrent;
    }

	m_soundCurrent = Engine::Get()->GetCounter();
    m_soundSystem.update();
	m_soundTime = Engine::Get()->GetCounter() - m_soundCurrent;

	m_worldTime = Engine::Get()->GetCounter() - m_worldCurrent;
#endif
}

void WorldManager::updateEntityQueues()
{
    for (auto id : m_killedEntityIDQueue)
    {
        auto& ent = m_managerSystem.getEntityByID(id);
        if (ent.isValid()) { m_gameWorld.killEntity(ent); }
    }
    m_killedEntityIDQueue.clear();

    m_gameWorld.refresh();

    for (auto desc : m_entitySpawnQueue)
    {
        this->deserializeEntity(desc.file, desc.id, desc.preserve_transform, desc.name, 
            desc.position, desc.rotation, desc.scale);
    }
    m_entitySpawnQueue.clear();

    m_gameWorld.refresh();
}

entityid WorldManager::getNewID()
{
	entityid id = _entity_null_value;

	bool flag = true;
	for (auto i = 0U; i < _entity_null_value; i++) {
		if (!m_entityIDArray[i]) {
			id = i;

			m_entityIDArray[i] = true;
			flag = false;

			break;
		}
	}

	if (flag) {
		spdlog::warn("World reports entity count exceeded entity_null_value " + std::to_string(_entity_null_value));

		return _entity_null_value;
	}
	else
	{
		return id;
	}
}

void WorldManager::freeEntityID(entityid id) { if (id < _entity_null_value) { m_entityIDArray[id] = false; } }

bool WorldManager::killEntityByName(const std::string& name)
{
    auto &ent = m_managerSystem.getEntityByName(name);

    if (ent.isValid()) {

        m_killedEntityIDQueue.emplace_back(ent.getComponent<DescriptorComponent>().id);

        //ent.kill();
        // DEBUG If the world refreshes the entity is removed in the frame so other functions that relie on its ID will fail
        //       Not refreshing the world 'waits' until the end of the frame to 'remove' the entity
        //       May cause problems if an entity is 'killed' then accessed before a world refresh
        //world()->refresh();

        return true;
    }

    return false;
}

bool WorldManager::killEntityByID(int id)
{
    if (id >= 0) {
        auto &ent = m_managerSystem.getEntityByID(id);

        if (ent.isValid()) {

            m_killedEntityIDQueue.emplace_back(id);

            //ent.kill();
            // DEBUG If the world refreshes the entity is removed in the frame so other functions that relie on its ID will fail
            //       Not refreshing the world 'waits' until the end of the frame to 'remove' the entity
            //       May cause problems if an entity is 'killed' then accessed before a world refresh
            //world()->refresh();

            return true;
        }

        spdlog::warn("Entity invalid in WorldManager::killEntityByID()");

        return false;
    }

    //log::write("Entity ID out of range in WorldManager::killEntityByID()", LOG_WARNING);
    return false;
}

void WorldManager::killAllEntities()
{
    //_sound->soundEngine()->removeAllSoundSources();
    for (auto entity : m_gameWorld.getEntities()) { m_gameWorld.killEntity(entity); }

    m_gameWorld.refresh();

    // Clears any remaining physx actors, mainly trimeshes
    // DEBUG
    //PhysicsManager::Get()->destroyScene();

    m_entityIDArray.fill(false);

    if (PropManager::Get())
        PropManager::Get()->clearAll();
}

unsigned int WorldManager::spawnEntity(const std::string& file, const std::string& name, bool preserve_transform,
    const irr::core::vector3df& position,
    const irr::core::vector3df& rotation,
    const irr::core::vector3df& scale)
{
    EntitySpawnDescriptor entity;

    bool flag = true;
    for (auto i = 0U; i < _entity_null_value; i++) {
        if (!m_entityIDArray[i]) {
            entity.id = i;

            m_entityIDArray[i] = true;
            flag = false;

            break;
        }
    }

    if (flag) {
        spdlog::warn("World reports entity count exceeded entity_null_value " + std::to_string(_entity_null_value));
    }

    entity.load(file, preserve_transform, name, position, rotation, scale);
    m_entitySpawnQueue.emplace_back(entity);

    return entity.id;
}

void WorldManager::exportEntity(anax::Entity& entity, const std::string& file, bool save_transform)
{
	std::ofstream ofs_entity(file);
	XMLOutputArchive archive(ofs_entity);
	
	serializeEntity(entity, archive, save_transform);
}

void WorldManager::importPrefab(const std::string& file)
{
	deserializeEntity(file, _entity_null_value, true);
}

void WorldManager::exportPrefab(const std::string& file)
{
	std::ofstream ofs_entity(file);
	XMLOutputArchive archive(ofs_entity);

	try
	{
		archive.setNextName("prefab");
		archive.startNode();

		//archive(prefab_data);

		archive.finishNode();
	}
	catch (Exception& ex)
	{
		spdlog::warn("Failed to serialize prefab data {0}", ex.what());
	}

	// TODO: Loop through entities in the prefab and serialize them
	// for (entity in prefab)
	//     serializeEntity(entity)
}

void WorldManager::importScene(const std::string& file)
{
	const bool isZip = file.size() >= 4 &&
	                   file.compare(file.size() - 4, 4, ".pak") == 0;

	if (isZip)
	{
		auto* smgr   = RenderManager::Get()->sceneManager();
		auto* driver = RenderManager::Get()->driver();
		auto* fs     = smgr->getFileSystem();

		// Mount the zip — all IFileSystem reads now look inside it transparently
		if (!fs->addFileArchive(file.c_str(), /*ignoreCase=*/true, /*ignorePaths=*/true, irr::io::EFAT_ZIP))
		{
			spdlog::error("WorldManager::importScene: failed to mount zip '{}'", file);
			return;
		}

		// Extract scene.scn from the virtual FS into a temporary file alongside
		// the zip, then load it with the existing deserializeEntity path.
		irr::io::IReadFile* rfile = fs->createAndOpenFile("scene.scn");
		if (!rfile)
		{
			spdlog::error("WorldManager::importScene: 'scene.scn' not found in '{}'", file);
			fs->removeFileArchive(fs->getFileArchiveCount() - 1);
			return;
		}

		// Derive a temp path next to the pak (e.g. "content/scene/map.pak" -> "content/scene/map_tmp.scn")
		std::string tmpScn = file.substr(0, file.size() - 4) + "_tmp.scn";
		{
			std::vector<char> buf(static_cast<size_t>(rfile->getSize()));
			rfile->read(buf.data(), static_cast<irr::u32>(buf.size()));
			rfile->drop();

			std::ofstream ofs(tmpScn, std::ios::binary);
			if (!ofs)
			{
				spdlog::error("WorldManager::importScene: failed to write temp scene file '{}'", tmpScn);
				fs->removeFileArchive(fs->getFileArchiveCount() - 1);
				return;
			}
			ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
		}

		deserializeEntity(tmpScn, _entity_null_value, true);
		std::remove(tmpScn.c_str());

		// Apply baked lightmaps — the zip is still mounted so driver->getTexture
		// and fs->createAndOpenFile find the lightmap files transparently
		LightmapBaker::loadLightmaps(fs, driver);

		// Load static props and their baked lightmaps
		irr::io::IReadFile* propsFile = fs->createAndOpenFile("props.xml");
		if (propsFile && PropManager::Get())
		{
			std::vector<char> propsBuf(static_cast<size_t>(propsFile->getSize()));
			propsFile->read(propsBuf.data(), static_cast<irr::u32>(propsBuf.size()));
			propsFile->drop();

			std::string tmpProps = file.substr(0, file.size() - 4) + "_tmp_props.xml";
			{
				std::ofstream ofs(tmpProps, std::ios::binary);
				ofs.write(propsBuf.data(), static_cast<std::streamsize>(propsBuf.size()));
			}
			{
				std::ifstream ifs(tmpProps);
				cereal::XMLInputArchive ar(ifs);
				PropManager::Get()->deserialize(ar);
			}
			std::remove(tmpProps.c_str());

			PropManager::Get()->loadPropLightmaps(fs, driver);
		}
		else if (propsFile)
		{
			propsFile->drop();
		}

		// Load baked navmesh if present
		if (NavigationManager::Get())
		{
			irr::io::IReadFile* navFile = fs->createAndOpenFile("navmesh.nav");
			if (navFile)
			{
				std::vector<uint8_t> navBytes(static_cast<size_t>(navFile->getSize()));
				navFile->read(navBytes.data(), static_cast<irr::u32>(navBytes.size()));
				navFile->drop();
				NavigationManager::Get()->loadNavMesh(navBytes.data(), navBytes.size());
			}
			else
			{
				spdlog::warn("WorldManager::importScene: no navmesh.nav in '{}' — bake one in the editor", file);
			}
		}

		fs->removeFileArchive(fs->getFileArchiveCount() - 1);
	}
	else
	{
		// Loose .scn — original behaviour unchanged
		deserializeEntity(file, _entity_null_value, true);
	}

	// Common post-load steps
	auto &entities = world()->getEntities();
	for (auto &entity : entities)
	{
		auto &descriptor = entity.getComponent<DescriptorComponent>();
		auto &transform  = entity.getComponent<TransformComponent>();

		// DEBUG TODO: Find all parents and assign them their children by name format 'childName childID parentName parentID' no spaces
	}

	RenderManager::Get()->sceneManager()->setAmbientLight(m_currentSceneDescriptor.ambient_light);
	RenderManager::Get()->swapSkyDomeTexture(m_currentSceneDescriptor.skydome_texture);

	RenderManager::Get()->setPostProcessPassEnabled("fxaa", m_currentSceneDescriptor.useFXAA);
	RenderManager::Get()->setBloomEnabled(m_currentSceneDescriptor.useBloom);
	RenderManager::Get()->bloomBrightCallback()->threshold = m_currentSceneDescriptor.bloomThreshold;
	RenderManager::Get()->bloomCompositeCallback()->strength = m_currentSceneDescriptor.bloomStrength;
	RenderManager::Get()->setTonemapEnabled(m_currentSceneDescriptor.useTonemapping);
	RenderManager::Get()->tonemapCallback()->exposure = m_currentSceneDescriptor.tonemapExposure;
	RenderManager::Get()->tonemapCallback()->whitePoint = m_currentSceneDescriptor.tonemapwhitePoint;
	RenderManager::Get()->setSharpenEnabled(m_currentSceneDescriptor.useSharpen);
	RenderManager::Get()->sharpenCallback()->strength = m_currentSceneDescriptor.sharpenStrength;
	RenderManager::Get()->setAutoExposure(m_currentSceneDescriptor.useAutoExposure);
	RenderManager::Get()->setPixelateEnabled(m_currentSceneDescriptor.usePixelate);
	RenderManager::Get()->pixelateCallback()->pixelSize = m_currentSceneDescriptor.pixelateSize;

	{
		auto* cb = RenderManager::Get()->mainShaderCallback();
		cb->fogDensity  = m_currentSceneDescriptor.fogDensity;
		cb->fogStart    = m_currentSceneDescriptor.fogStart;
		cb->fogColor[0] = m_currentSceneDescriptor.fogColor.r;
		cb->fogColor[1] = m_currentSceneDescriptor.fogColor.g;
		cb->fogColor[2] = m_currentSceneDescriptor.fogColor.b;
	}
	RenderManager::Get()->setColorGradeEnabled(m_currentSceneDescriptor.useColorGrade);
	RenderManager::Get()->colorGradeCallback()->saturation    = m_currentSceneDescriptor.cgSaturation;
	RenderManager::Get()->colorGradeCallback()->brightness    = m_currentSceneDescriptor.cgBrightness;
	RenderManager::Get()->colorGradeCallback()->colorTint[0]  = m_currentSceneDescriptor.cgTintR;
	RenderManager::Get()->colorGradeCallback()->colorTint[1]  = m_currentSceneDescriptor.cgTintG;
	RenderManager::Get()->colorGradeCallback()->colorTint[2]  = m_currentSceneDescriptor.cgTintB;
	RenderManager::Get()->setPosterizeEnabled(m_currentSceneDescriptor.usePosterize);
	RenderManager::Get()->posterizeCallback()->levels   = m_currentSceneDescriptor.posterizeLevels;
	RenderManager::Get()->posterizeCallback()->strength = m_currentSceneDescriptor.posterizeStrength;
	RenderManager::Get()->setFilmGrainEnabled(m_currentSceneDescriptor.useFilmGrain);
	RenderManager::Get()->filmGrainCallback()->strength = m_currentSceneDescriptor.filmGrainStrength;

	if (PropManager::Get() && !Engine::Get()->isEditorMode())
		PropManager::Get()->setVegetationBatched(true);

	g_currentScene = Utility::FilenameFromPath(file);
}

void WorldManager::exportScene(const std::string& file)
{
	const bool isZip = file.size() >= 4 &&
	                   file.compare(file.size() - 4, 4, ".pak") == 0;

	if (!isZip)
	{
		// ---- Loose .scn — original behaviour unchanged ----
		std::ofstream ofs_entity(file);
		XMLOutputArchive archive(ofs_entity);

		try
		{
			archive.setNextName("scene");
			archive.startNode();
			archive(m_currentSceneDescriptor);
			archive.finishNode();
		}
		catch (Exception& ex)
		{
			spdlog::warn("Failed to serialize scene data {0}", ex.what());
		}

		for (auto e : m_gameWorld.getEntities())
			serializeEntity(e, archive, true);

		return;
	}

	// ---- .pak path — bundle scene XML + baked lightmaps ----
	auto* driver = RenderManager::Get()->driver();

	// Temp directory for PNG encoding (same folder as the zip)
	std::string tempDir = ".";
	{
		const auto slash = file.find_last_of("/\\");
		if (slash != std::string::npos)
			tempDir = file.substr(0, slash);
	}

	// 1. Serialize scene XML to memory (archive must be destroyed before str())
	std::string scnData;
	{
		std::ostringstream scnStream;
		{
			XMLOutputArchive archive(scnStream);
			try
			{
				archive.setNextName("scene");
				archive.startNode();
				archive(m_currentSceneDescriptor);
				archive.finishNode();
			}
			catch (Exception& ex)
			{
				spdlog::warn("Failed to serialize scene data {0}", ex.what());
			}
			for (auto e : m_gameWorld.getEntities())
				serializeEntity(e, archive, true);
		}
		scnData = scnStream.str();
	}

	// 2. Collect lightmap buffers from all baked entities
	auto lightmapFiles = LightmapBaker::collectLightmapFiles(driver, tempDir);

	// 3. Collect navmesh bytes (empty if not yet baked)
	std::vector<uint8_t> navBytes;
	if (NavigationManager::Get() && NavigationManager::Get()->isNavMeshBuilt())
		navBytes = NavigationManager::Get()->serializeNavMesh();

	// 4. Write zip with miniz
	mz_zip_archive zip = {};
	if (!mz_zip_writer_init_file(&zip, file.c_str(), 0))
	{
		spdlog::error("WorldManager::exportScene: failed to create zip '{}'", file);
		return;
	}

	mz_zip_writer_add_mem(&zip, "scene.scn",
	    scnData.data(), scnData.size(), MZ_DEFAULT_COMPRESSION);

	for (auto& lm : lightmapFiles)
	{
		const std::string pngName    = "lightmap_" + std::to_string(lm.id) + ".png";
		const std::string uvmeshName = "lightmap_" + std::to_string(lm.id) + ".uvmesh";

		mz_zip_writer_add_mem(&zip, pngName.c_str(),
		    lm.pngBytes.data(), lm.pngBytes.size(), MZ_DEFAULT_COMPRESSION);
		mz_zip_writer_add_mem(&zip, uvmeshName.c_str(),
		    lm.uvmeshBytes.data(), lm.uvmeshBytes.size(), MZ_DEFAULT_COMPRESSION);
	}

	if (!navBytes.empty())
	{
		mz_zip_writer_add_mem(&zip, "navmesh.nav",
		    navBytes.data(), navBytes.size(), MZ_DEFAULT_COMPRESSION);
	}

	// 4. Serialize static props + their lightmaps
	size_t propCount = 0;
	if (PropManager::Get() && !PropManager::Get()->getAllProps().empty())
	{
		std::ostringstream propsStream;
		{
			cereal::XMLOutputArchive ar(propsStream);
			PropManager::Get()->serialize(ar);
		}
		const std::string propsData = propsStream.str();
		mz_zip_writer_add_mem(&zip, "props.xml",
		    propsData.data(), propsData.size(), MZ_DEFAULT_COMPRESSION);

		auto propLightmapFiles = PropManager::Get()->collectPropLightmapFiles(driver, tempDir);
		for (auto& plm : propLightmapFiles)
		{
			const std::string pngName    = "prop_lightmap_" + std::to_string(plm.propId) + ".png";
			const std::string uvmeshName = "prop_lightmap_" + std::to_string(plm.propId) + ".uvmesh";
			mz_zip_writer_add_mem(&zip, pngName.c_str(),
			    plm.pngBytes.data(), plm.pngBytes.size(), MZ_DEFAULT_COMPRESSION);
			mz_zip_writer_add_mem(&zip, uvmeshName.c_str(),
			    plm.uvmeshBytes.data(), plm.uvmeshBytes.size(), MZ_DEFAULT_COMPRESSION);
		}
		propCount = PropManager::Get()->getAllProps().size();
	}

	if (!mz_zip_writer_finalize_archive(&zip))
		spdlog::error("WorldManager::exportScene: failed to finalise zip '{}'", file);

	mz_zip_writer_end(&zip);

	spdlog::info("WorldManager::exportScene: saved '{}' ({} lightmap(s), {} prop(s), navmesh: {})",
	    file, lightmapFiles.size(), propCount, navBytes.empty() ? "no" : "yes");
}

bool WorldManager::getCVarExists(const std::string& name)
{
    for (auto &cvar : m_globalCVarList) {
        if (cvar.name == name) {
            return true;
        }
    }

    return false;
}

std::string WorldManager::getCVarValue(const std::string& name)
{
    for (auto &cvar : m_globalCVarList) {
        if (cvar.name == name) {
            return cvar.value;
        }
    }

    return std::string();
}

void WorldManager::setCVar(const std::string& name, const std::string& value)
{
    for (auto &cvar : m_globalCVarList) {
        if (cvar.name == name) {
            cvar.value = value;
            return;
        }
    }

    m_globalCVarList.emplace_back(GlobalCVar(name, value));
}

void WorldManager::removeCVar(const std::string& name)
{
    for (auto i = 0U; i < m_globalCVarList.size(); i++) {
        if (m_globalCVarList[i].name == name) {
            m_globalCVarList.erase(m_globalCVarList.begin() + i);
        }
    }
}

void WorldManager::clearCVars()
{
    vector<GlobalCVar>().swap(m_globalCVarList);
}
