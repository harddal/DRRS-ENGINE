#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Editor/ImGuiLogSink.h"

#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Shlwapi.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

#include <string>
#include <vector>

// ---- Core cross-file state (extern'd in EditorInterface_Internal.h) ----
EditorWindowData m_windowData;
std::string g_currentScenePath;
std::vector<entityid> g_undoEntities;

void SetIMGUI_SceneEditorTheme()
{
	// Accent: steel blue  #1A6CB0 / hover #2580CC / active #105088
	// Base:   mid slate   #222530 / panel #2A2E3C / raised #313649

	// -- Text --
	ImGui::PushStyleColor(ImGuiCol_Text,                ImVec4(0.880f, 0.890f, 0.910f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TextDisabled,        ImVec4(0.420f, 0.440f, 0.480f, 1.000f));

	// -- Backgrounds --
	ImGui::PushStyleColor(ImGuiCol_WindowBg,            ImVec4(0.165f, 0.176f, 0.220f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg,             ImVec4(0.145f, 0.153f, 0.192f, 0.980f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg,           ImVec4(0.118f, 0.125f, 0.157f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_Border,              ImVec4(0.255f, 0.267f, 0.325f, 1.000f));

	// -- Widget surfaces (inputs, checkboxes, combos) --
	ImGui::PushStyleColor(ImGuiCol_FrameBg,             ImVec4(0.200f, 0.212f, 0.263f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,      ImVec4(0.235f, 0.247f, 0.306f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive,       ImVec4(0.102f, 0.196f, 0.333f, 1.000f));

	// -- Headers (tree nodes, selectables, collapsing headers) --
	ImGui::PushStyleColor(ImGuiCol_Header,              ImVec4(0.102f, 0.224f, 0.400f, 0.600f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered,       ImVec4(0.145f, 0.408f, 0.698f, 0.800f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive,        ImVec4(0.145f, 0.408f, 0.698f, 1.000f));

	// -- Title bars --
	ImGui::PushStyleColor(ImGuiCol_TitleBg,             ImVec4(0.118f, 0.125f, 0.157f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,    ImVec4(0.118f, 0.125f, 0.157f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive,       ImVec4(0.102f, 0.216f, 0.388f, 1.000f));

	// -- Buttons --
	ImGui::PushStyleColor(ImGuiCol_Button,              ImVec4(0.122f, 0.129f, 0.161f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,       ImVec4(0.145f, 0.408f, 0.698f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,        ImVec4(0.102f, 0.314f, 0.565f, 1.000f));

	// -- Sliders & checkmarks --
	ImGui::PushStyleColor(ImGuiCol_SliderGrab,          ImVec4(0.145f, 0.408f, 0.698f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,    ImVec4(0.200f, 0.490f, 0.800f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_CheckMark,           ImVec4(0.200f, 0.540f, 0.900f, 1.000f));

	// -- Scrollbar --
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,       ImVec4(0.255f, 0.267f, 0.325f, 1.000f));

	// -- Separators --
	ImGui::PushStyleColor(ImGuiCol_Separator,           ImVec4(0.255f, 0.267f, 0.325f, 1.000f));

	// -- Tabs --
	ImGui::PushStyleColor(ImGuiCol_Tab,                 ImVec4(0.130f, 0.137f, 0.173f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TabHovered,          ImVec4(0.118f, 0.314f, 0.553f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TabActive,           ImVec4(0.102f, 0.224f, 0.416f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TabUnfocused,        ImVec4(0.130f, 0.137f, 0.173f, 1.000f));
	ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive,  ImVec4(0.082f, 0.165f, 0.298f, 1.000f));

	// -- Docking --
	ImGui::PushStyleColor(ImGuiCol_DockingPreview,      ImVec4(0.145f, 0.408f, 0.698f, 0.700f));
	ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg,      ImVec4(0.118f, 0.125f, 0.157f, 1.000f));
}

void EditorInterface::draw()
{
	SetIMGUI_SceneEditorTheme();

	detectKeyShortcuts();

	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::Begin("editor_main_window", nullptr,
	             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
	             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMouseInputs);
	ImGui::PopStyleVar(2); // WindowPadding + WindowBorderSize — only needed for this host window
	{
		const float menubar_h   = ImGui::GetFrameHeight();
		const float toolbar_h   = m_windowData.draw_toolbar   ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f : 0.0f;
		const float statusbar_h = m_windowData.draw_statusbar ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y        : 0.0f;

		ImGui::SetCursorPosY(menubar_h + toolbar_h);
		float ds_height = io.DisplaySize.y - menubar_h - toolbar_h - statusbar_h;
		ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.0f, ds_height), ImGuiDockNodeFlags_PassthruCentralNode);

		// Restore opaque WindowBg for all child windows drawn inside this fullscreen container.
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.165f, 0.176f, 0.220f, 1.0f));

		draw_menubar_main();

		draw_window_spawn_entity();
		draw_window_spawn_prefab();
		draw_window_spawn_mesh();
		draw_window_spawn_prop();
		draw_window_vegetation_painter();
		draw_window_texture_painter();
		draw_window_hierarchy();
		draw_window_prop_ent();
		draw_window_prop_scene();
		draw_window_help_about();
		draw_window_scene_stats();
		draw_window_console();
		draw_window_log();
		draw_window_editor_settings();
		draw_window_texture_browser();
		draw_window_add_component();
		draw_window_entity_debug_info();
		draw_window_entity_builder();
		draw_window_script_editor();

		g_sceneInteractor.draw();

		ImGui::PopStyleColor(); // opaque WindowBg for children
	}
	ImGui::End();

	// Toolbar and statusbar are submitted after the host window so they win
	// hit-testing over editor_main_window and receive mouse clicks correctly.
	draw_toolbar();
	draw_statusbar();

	// Top-level modals — submitted last so they render above everything.
	drawScriptEditorPopups();

	ImGui::PopStyleColor(31); // 30 theme colors + 1 transparent main window bg
}

void EditorInterface::detectKeyShortcuts()
{
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_DELETE) && !s_builderEntityCreated)
	{
		if (g_sceneInteractor.isPropSelected())
			g_sceneInteractor.deleteProp();
		else
			g_sceneInteractor.deleteEntity();
	}

	auto control = false;
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LCONTROL))
	{
		control = true;
	}

	{
		if (control && InputManager::Get()->isKeyPressed(KEY_O))
			function_open_scene();
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_S))
			funtion_save_scene();
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_B))
			m_windowData.draw_window_entity_builder = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_P))
			m_windowData.draw_window_prop_ent = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_H))
			m_windowData.draw_window_hiearchy = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_G))
			function_play_scene();
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_M))
			function_showhide_menubar();
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_E))
			m_windowData.draw_window_spawn_entity = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_R))
			m_windowData.draw_window_spawn_prefab = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_Q))
			m_windowData.draw_window_prop_scene = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_TAB))
			m_windowData.draw_window_console = true;
	}
	{
		if (control && InputManager::Get()->isKeyPressed(KEY_T))
			draw_window_texture_browser();
	}
	{
		static bool key_x = false;
		if (control && InputManager::Get()->getKeyRelease(KEY_X, &key_x))
		{
			if (g_sceneInteractor.isPropSelected()) g_sceneInteractor.cutProp();
			else                                    g_sceneInteractor.cutEntity();
		}

		static bool key_c = false;
		if (control && InputManager::Get()->getKeyRelease(KEY_C, &key_c))
		{
			if (g_sceneInteractor.isPropSelected()) g_sceneInteractor.copyProp();
			else                                    g_sceneInteractor.copyEntity();
		}

		static bool key_v = false;
		if (control && InputManager::Get()->getKeyRelease(KEY_V, &key_v))
		{
			if (g_sceneInteractor.hasPropClipboard()) g_sceneInteractor.pasteProp();
			else                                      g_sceneInteractor.pasteEntity();
		}

		static bool key_z = false;
		if (control && InputManager::Get()->getKeyRelease(KEY_Z, &key_z))
			g_sceneInteractor.undoTransform();

		static bool key_y = false;
		if (control && InputManager::Get()->getKeyRelease(KEY_Y, &key_y))
			g_sceneInteractor.redoTransform();
	}

	{
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM1))
			g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::TRANSLATE);
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM2))
			g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::ROTATE);
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM3))
			g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::SCALE);
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM4))
			g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::LOCAL);
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM5))
			g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::WORLD);
		if (control && InputManager::Get()->isKeyPressed(KEY_NUM6))
			g_sceneInteractor.useSnap(!g_sceneInteractor.isSnap());
	}
}

void EditorInterface::function_open_scene()
{
	std::string path = Utility::OpenFileDialog(dialog_filter_scene_zip, "content\\scene");

	if (PathFileExistsA(path.c_str()))
	{
		g_currentScenePath = path;
		WorldManager::Get()->killAllEntities();
		g_sceneInteractor.clearSelectedEntities();
		g_sceneInteractor.clearSelectedProp();
		WorldManager::Get()->importScene(path);
	}
}

static void syncSceneDescriptorFromCallbacks()
{
	auto desc = WorldManager::Get()->getCurrentSceneDescriptor();
	desc.ambient_light     = RenderManager::Get()->sceneManager()->getAmbientLight();
	desc.skydome_texture   = RenderManager::Get()->getCurrentSkydomeTexture();
	desc.bloomThreshold    = RenderManager::Get()->bloomBrightCallback()->threshold;
	desc.bloomStrength     = RenderManager::Get()->bloomCompositeCallback()->strength;
	desc.tonemapExposure   = RenderManager::Get()->tonemapCallback()->exposure;
	desc.tonemapwhitePoint = RenderManager::Get()->tonemapCallback()->whitePoint;
	desc.sharpenStrength   = RenderManager::Get()->sharpenCallback()->strength;
	desc.pixelateSize      = RenderManager::Get()->pixelateCallback()->pixelSize;
	desc.usePixelate       = RenderManager::Get()->isPixelateEnabled();
	{
		auto* cb = RenderManager::Get()->mainShaderCallback();
		desc.fogDensity  = cb->fogDensity;
		desc.fogStart    = cb->fogStart;
		desc.fogColor    = irr::video::SColorf(cb->fogColor[0], cb->fogColor[1], cb->fogColor[2], 1.0f);
	}
	desc.useColorGrade     = RenderManager::Get()->isColorGradeEnabled();
	desc.cgSaturation      = RenderManager::Get()->colorGradeCallback()->saturation;
	desc.cgBrightness      = RenderManager::Get()->colorGradeCallback()->brightness;
	desc.cgTintR           = RenderManager::Get()->colorGradeCallback()->colorTint[0];
	desc.cgTintG           = RenderManager::Get()->colorGradeCallback()->colorTint[1];
	desc.cgTintB           = RenderManager::Get()->colorGradeCallback()->colorTint[2];
	desc.usePosterize      = RenderManager::Get()->isPosterizeEnabled();
	desc.posterizeLevels   = RenderManager::Get()->posterizeCallback()->levels;
	desc.posterizeStrength = RenderManager::Get()->posterizeCallback()->strength;
	desc.useFilmGrain      = RenderManager::Get()->isFilmGrainEnabled();
	desc.filmGrainStrength = RenderManager::Get()->filmGrainCallback()->strength;
	WorldManager::Get()->setCurrentSceneDescriptor(desc);
}

static void exportSceneToPath(const std::string& path)
{
	if (path.empty()) return;
	syncSceneDescriptorFromCallbacks();
	WorldManager::Get()->exportScene(path);
}

void EditorInterface::funtion_save_scene()
{
	if (!g_currentScenePath.empty())
	{
		exportSceneToPath(g_currentScenePath);
	}
	else
	{
		function_save_scene_as();
	}
}

void EditorInterface::function_save_scene_as()
{
	std::string path = Utility::SaveFileDialog(dialog_filter_scene_zip, "content\\scene");
	if (!path.empty())
	{
		g_currentScenePath = path;
		exportSceneToPath(path);
	}
}

void EditorInterface::function_play_scene()
{
	syncSceneDescriptorFromCallbacks();
	Engine::Get()->stateManager()->setStatePauseResume(ESID_EDITORGAME);
}

void EditorInterface::function_showhide_menubar()
{
	m_windowData.draw_menubar_main = !m_windowData.draw_menubar_main;
}
