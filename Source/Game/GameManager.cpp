#include "GameManager.h"

#include <fstream>
#include <cereal/archives/xml.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Resource/FilePaths.h"
#include "Engine/Sound/SoundManager.h"
#include "Item/ItemDatabase.h"
#include "Utility/Utility.h"

#include "Player/PlayerController.h"
#include "Player/FreeCameraController.h"

GameManager* GameManager::s_Instance = nullptr;

GameManager::GameManager()
{
	if (s_Instance)
	{
		Utility::Error("Pointer to class \'RenderManager\' is invalid");
	}
	s_Instance = this;

	loadConfiguration();

	m_hasGameInitialized = false;
}

void GameManager::init(const std::string &args)
{
	m_hasGameInitialized = true;

	loadConfiguration();

	ImGui::GetIO().MouseDrawCursor = false;

	m_currentGameArguments = args;

	ItemDatabase::Load();

	Engine::Get()->setGameMode();

	WorldManager::Get()->loadScene(args);

	g_FreeCameraController = std::make_unique<FreeCameraController>();
	g_FreeCameraController->init();

	// The player controller is NOT constructed here any more. Which one to build
	// depends on the spawn marker, which is not known until the scene's first
	// game-mode frame — see the MT_PLAYER_START case in GameplaySystem.
	//
	// Its teardown stays in destroy() regardless. That is deliberate; the comment
	// there records the crash it fixes.
}

void GameManager::update(float dt, bool editor_mode)
{
	// If not in debug, escape key activates the menu

	static auto esc_pressed = false;
	if (InputManager::Get()->getKeyPressOnce(KEY_ESCAPE, &esc_pressed, true) && !editor_mode)
	{
		Engine::Get()->stateManager()->setStatePauseResume(ESID_MENU);
	}

	g_FreeCameraController->update(dt);

	if (g_PlayerController && WorldManager::Get()->managerSystem()->getEntityByName("player").isValid())
	{
		g_PlayerController->update(dt);
	}

}

void GameManager::updateUI(float dt)
{
	if (g_PlayerController && WorldManager::Get()->managerSystem()->getEntityByName("player").isValid())
	{
		g_PlayerController->updateUI(dt);
	}
}

void GameManager::destroy()
{
	m_hasGameInitialized = false;

	Engine::Get()->setGameMode(false);

	if (g_FreeCameraController)
	{
		g_FreeCameraController->destroy();
		g_FreeCameraController.reset();
	}

	// Always tear down the player controller when it exists — its destroy() path
	// unregisters weapon viewmodel/LDR-effect nodes from the RenderManager.
	// The old player-entity-valid guard skipped this when the player had died,
	// leaving those registrations dangling after clearScene() deleted the nodes
	// (access violation in the LDR pass on game→edit transitions).
	if (g_PlayerController)
	{
		g_PlayerController->destroy();
		g_PlayerController.reset();
	}

	// The underwater muffle sits on SoLoud's master bus and outlives the
	// controller, but the per-frame driver that clears it lives in
	// SoundSystem::update(), which only runs in game mode. Quitting to the editor
	// while submerged would otherwise leave the whole editor muffled.
	if (SoundManager::Get())
		SoundManager::Get()->sound()->setUnderwater(false, 0.0f);
}

void GameManager::reset()
{
	if (m_hasGameInitialized)
	{
		destroy();
		init(m_currentGameArguments);
	}
}

void GameManager::pause()
{
	Engine::Get()->setGameMode(false);
}

void GameManager::resume()
{
	ImGui::GetIO().MouseDrawCursor = false;
	InputManager::Get()->centerMouse();

	Engine::Get()->setGameMode();
}

void GameManager::loadConfiguration()
{
	try
	{
		std::ifstream ifs_game("config/game.xml");
		cereal::XMLInputArchive game_config(ifs_game);

		game_config(m_configuration);
	}
	catch (cereal::Exception& ex)
	{
		spdlog::warn("Failed to load game configuration: {}, default values used", ex.what());

		m_configuration = GameConfiguration();

		std::ofstream ofs_game("config/game.xml");
		cereal::XMLOutputArchive game_config(ofs_game);

		game_config(m_configuration);
	}
}

void GameManager::saveConfiguration(GameConfiguration& configuration)
{
	std::ofstream ofs_game("config/game.xml");
	cereal::XMLOutputArchive game_config(ofs_game);

	m_configuration = configuration;
	game_config(configuration);
}