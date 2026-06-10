#include "GameManager.h"

#include <fstream>
#include <cereal/archives/xml.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Resource/FilePaths.h"
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

	g_PlayerController = std::make_unique<PlayerController>();
	g_PlayerController->init();
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

	if (g_PlayerController && WorldManager::Get()->managerSystem()->getEntityByName("player").isValid())
	{
		g_PlayerController->destroy();
		g_PlayerController.reset();
	}
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