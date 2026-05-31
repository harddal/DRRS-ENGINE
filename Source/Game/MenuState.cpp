#include "Game/MenuState.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/World/WorldManager.h"

#include "Interface/MainMenuGUI.h"

void MenuState::init(std::string args)
{
	ImGui::GetIO().MouseDrawCursor = true;
	m_fadeStart = std::chrono::steady_clock::now();

	PhysicsManager::Get()->createScene();
	WorldManager::Get()->importScene(_asset_scn_pak("dm_turbine"));

	SoundManager::Get()->sound()->play2D("content/sound/music/main_menu.wav", true);
}

void MenuState::update(float dt)
{
	ImGui::GetIO().MouseDrawCursor = true;

	WorldManager::Get()->scriptSystem()->update();

	if (m_isIngameMenu)
	{
		if (InputManager::Get()->getKeyPressOnce(KEY_ESCAPE, &m_escKeyLock, true) && !MainMenu::IsEscapeKeyLocked())
		{
			Engine::Get()->stateManager()->setStatePauseResume(ESID_GAME);
		}
	}
}

void MenuState::updateUI(float dt)
{
	MainMenu::Draw(m_isIngameMenu);

	float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fadeStart).count();
	if (elapsed < k_fadeInDuration)
	{
		float alpha = 1.0f - (elapsed / k_fadeInDuration);
		ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, alpha));

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(RenderManager::Get()->getConfiguration().width, RenderManager::Get()->getConfiguration().height));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
		ImGui::Begin("##fade", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(RenderManager::Get()->getConfiguration().width, RenderManager::Get()->getConfiguration().height), col);
		ImGui::End();
		ImGui::PopStyleColor();
	}
}

void MenuState::destroy()
{
	Engine::Get()->clearScene();
}

void MenuState::pause()
{
	SoundManager::Get()->sound()->soloud().stopAll();
}

void MenuState::resume()
{
	MainMenu::Reset();
	
	m_isIngameMenu = true;

	InputManager::Get()->centerMouse();

	m_escKeyLock = true;
}
