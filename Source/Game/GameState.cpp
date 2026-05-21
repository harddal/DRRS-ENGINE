#include "Game/GameState.h"

#include "Engine/Engine.h"

void GameState::init(std::string args)
{
	GameManager::Get()->init(args);

	WorldManager::Get()->cameraSystem()->switchToPlayerCamera();
}

void GameState::update(float dt)
{
	GameManager::Get()->update(dt);
}

void GameState::updateUI(float dt)
{
	GameManager::Get()->updateUI(dt);
}

void GameState::destroy()
{
	GameManager::Get()->destroy();

	Engine::Get()->clearScene();
}

void GameState::pause()
{
	GameManager::Get()->pause();
}

void GameState::resume()
{
	GameManager::Get()->resume();
}
