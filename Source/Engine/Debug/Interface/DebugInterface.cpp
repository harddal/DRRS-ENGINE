#include "DebugInterface.h"

#include "Engine/Resource/FilePaths.h"

#include "Utility/Utility.h"

#include <IMGUI/imgui.h>

#include "Engine/Interface/ImGuiExtensions.h"

#include <string>
#include <vector>

#include "Engine/Engine.h"

#include "Game/Components.h"

DebugWindowData m_windowData;

void SetIMGUI_EntityEditorTheme()
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.898f, 0.850f, 0.858f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.498f, 0.450f, 0.458f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.6f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.6f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.78f, 0.78f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.72f, 0.72f, 0.5f));
	ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.847f, 0.780f, 0.650f, 1.f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}

void DebugInterface::draw()
{
	SetIMGUI_EntityEditorTheme();

	detectKeyShortcuts();

	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
	ImGui::Begin("debug_main_window", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
	{
		ImGui::PopStyleColor();

		draw_menubar_main();
	}
	ImGui::End();

	ImGui::PopStyleColor(16);
}

void DebugInterface::draw_menubar_main()
{
	if (!m_windowData.draw_menubar_main) { return; }

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New"))
			{
				const int result = MessageBox(nullptr, "Destroy current scene?", "New Scene", MB_YESNO);
				switch (result)
				{
				case IDYES: WorldManager::Get()->killAllEntities();
					break;
				default: break;
				}
			}

			if (ImGui::MenuItem("Open", "CTRL-O"))
			{
				
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save", "CTRL+S"))
			{
				
			}
			if (ImGui::MenuItem("Save As..."))
			{
				
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Quit", "ALT+F4"))
			{
				Engine::Get()->exit();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "CTRL+Z", false, false))
			{
			}
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, false))
			{
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Cut", "CTRL+X")) {  }
			if (ImGui::MenuItem("Copy", "CTRL+C")) {  }
			if (ImGui::MenuItem("Paste", "CTRL+V")) {  }
			if (ImGui::MenuItem("Delete", "DEL")) {  }

			ImGui::Separator();

			if (ImGui::MenuItem("Select All", "CTRL+A", false, false))
			{
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window")) 
		{ 
			ImGui::EndMenu(); 
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void DebugInterface::detectKeyShortcuts()
{
	
}