#include "MainMenuGUI.h"

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>

#include "Utility/Utility.h"

#include "Engine/Input/InputManager.h"

#include <string>
#include <algorithm>

using namespace ImGui;

static bool lockout_escape_key = false;

static void ApplyTheme()
{
	ImGuiStyle& s = ImGui::GetStyle();

	s.WindowPadding     = ImVec2(14, 8);
	s.FramePadding      = ImVec2(8, 4);
	s.ItemSpacing       = ImVec2(8, 7);
	s.ItemInnerSpacing  = ImVec2(6, 4);
	s.WindowRounding    = 3.0f;
	s.FrameRounding     = 2.0f;
	s.GrabRounding      = 2.0f;
	s.WindowBorderSize  = 1.0f;
	s.FrameBorderSize   = 0.0f;

	ImVec4* c = s.Colors;
	c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.92f, 0.88f, 1.00f);
	c[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
	c[ImGuiCol_WindowBg]             = ImVec4(0.07f, 0.07f, 0.09f, 0.96f);
	c[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.07f, 0.09f, 0.00f);
	c[ImGuiCol_PopupBg]              = ImVec4(0.07f, 0.07f, 0.09f, 0.98f);
	c[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.28f, 0.90f);
	c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
	c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
	c[ImGuiCol_FrameBgActive]        = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
	c[ImGuiCol_TitleBg]              = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
	c[ImGuiCol_TitleBgActive]        = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.05f, 0.07f, 0.75f);
	c[ImGuiCol_MenuBarBg]            = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
	c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
	c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.34f, 1.00f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.65f, 0.32f, 0.05f, 1.00f);
	c[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.52f, 0.10f, 1.00f);
	c[ImGuiCol_SliderGrab]           = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_SliderGrabActive]     = ImVec4(0.95f, 0.58f, 0.15f, 1.00f);
	c[ImGuiCol_Button]               = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
	c[ImGuiCol_ButtonHovered]        = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_ButtonActive]         = ImVec4(0.65f, 0.32f, 0.05f, 1.00f);
	c[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
	c[ImGuiCol_HeaderHovered]        = ImVec4(0.80f, 0.42f, 0.08f, 0.85f);
	c[ImGuiCol_HeaderActive]         = ImVec4(0.65f, 0.32f, 0.05f, 1.00f);
	c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
	c[ImGuiCol_SeparatorHovered]     = ImVec4(0.80f, 0.42f, 0.08f, 0.75f);
	c[ImGuiCol_SeparatorActive]      = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_ResizeGrip]           = ImVec4(0.28f, 0.28f, 0.34f, 0.50f);
	c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.80f, 0.42f, 0.08f, 0.75f);
	c[ImGuiCol_ResizeGripActive]     = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_Tab]                  = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
	c[ImGuiCol_TabHovered]           = ImVec4(0.80f, 0.42f, 0.08f, 0.90f);
	c[ImGuiCol_TabActive]            = ImVec4(0.55f, 0.28f, 0.05f, 1.00f);
	c[ImGuiCol_PlotLines]            = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_PlotLinesHovered]     = ImVec4(0.95f, 0.58f, 0.15f, 1.00f);
	c[ImGuiCol_PlotHistogram]        = ImVec4(0.80f, 0.42f, 0.08f, 1.00f);
	c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.95f, 0.58f, 0.15f, 1.00f);
	c[ImGuiCol_TextSelectedBg]       = ImVec4(0.80f, 0.42f, 0.08f, 0.35f);
	c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
}

static bool MenuTextButton(const char* label, float width)
{
	const float line_h = ImGui::GetTextLineHeight();
	const float btn_h  = line_h * 1.4f;
	const ImVec2 pos   = ImGui::GetCursorScreenPos();

	bool clicked = ImGui::InvisibleButton(label, ImVec2(width, btn_h));
	bool hovered = ImGui::IsItemHovered();
	bool active  = ImGui::IsItemActive();

	ImU32 col;
	if      (active)  col = IM_COL32(242, 148, 38,  255);
	else if (hovered) col = IM_COL32(255, 255, 255, 255);
	else              col = IM_COL32(172, 172, 167, 255);

	ImDrawList*  dl        = ImGui::GetWindowDrawList();
	ImFont*      font      = ImGui::GetFont();
	const float  font_size = ImGui::GetFontSize();
	const ImVec2 text_pos(pos.x, pos.y + (btn_h - line_h) * 0.5f);

	dl->AddText(font, font_size, ImVec2(text_pos.x + 2, text_pos.y + 2), IM_COL32(0, 0, 0, 200), label);
	dl->AddText(font, font_size, ImVec2(text_pos.x + 1, text_pos.y + 1), IM_COL32(0, 0, 0, 120), label);
	dl->AddText(font, font_size, text_pos, col, label);

	return clicked;
}

static bool
	show_window_background = false,
	show_window_load_game  = false,
	show_window_controls   = false,
	show_window_remap_key  = false,
	show_window_settings   = false,
	show_window_save_game  = false;

// ---- File browser shared state ----
static std::string        fb_current_dir  = "saves";
static std::vector<std::string> fb_subdirs;
static std::vector<std::string> fb_pak_files;
static bool               fb_needs_refresh = false;
static char               fb_save_name[128] = "save";

static void FB_Refresh()
{
    fb_subdirs.clear();
    fb_pak_files.clear();

    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    fs::path p(fb_current_dir);
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
        return;

    for (auto& entry : boost::make_iterator_range(fs::directory_iterator(p, ec), {}))
    {
        if (fs::is_directory(entry, ec))
            fb_subdirs.push_back(entry.path().filename().string());
        else if (fs::is_regular_file(entry, ec) && entry.path().extension().string() == ".pak")
            fb_pak_files.push_back(entry.path().filename().string());
    }

    std::sort(fb_subdirs.begin(),   fb_subdirs.end());
    std::sort(fb_pak_files.begin(), fb_pak_files.end());
}

bool MainMenu::IsEscapeKeyLocked()
{
	return lockout_escape_key;
}

void MainMenu::Reset()
{
	show_window_background = false;
	show_window_load_game  = false;
	show_window_controls   = false;
	show_window_remap_key  = false;
	show_window_settings   = false;
	show_window_save_game  = false;
}

void MainMenu::Draw(bool is_ingame_menu)
{
	static bool theme_applied = false;
	if (!theme_applied) { ApplyTheme(); theme_applied = true; }

	static bool window_open = true;

	static sKeyActionPair currentKeyPair("null", KEY_UNKNOWN);
	
	/*PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	SetNextWindowPos(ImVec2(0, 0));
	SetNextWindowSize(ImVec2(RenderManager::Get()->getConfiguration().width, RenderManager::Get()->getConfiguration().height));
	Begin("Background Window", &show_window_background,
		ImGuiWindowFlags_NoTitleBar |  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
	End();
	PopStyleColor();*/
	
	{
		const bool any_subwindow_open = show_window_load_game || show_window_save_game || show_window_settings || show_window_controls || show_window_remap_key;

		PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		PushStyleColor(ImGuiCol_Border,   ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		if (any_subwindow_open) PushStyleVar(ImGuiStyleVar_Alpha, 0.35f);
		Begin("MainMenu", &window_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs * any_subwindow_open);
		{
			const float screen_w = RenderManager::Get()->getConfiguration().width;
			const float screen_h = RenderManager::Get()->getConfiguration().height;
			SetWindowPos(ImVec2(screen_w * 0.07f, screen_h * 0.52f));

			SetWindowFontScale(1.8f);
			const float btn_w = 240.0f;

			if (!is_ingame_menu)
			{
				if (MenuTextButton("New Game", btn_w)) {
					show_window_save_game = false;
					show_window_controls = false;
					show_window_settings = false;
					show_window_load_game = false;

					Engine::Get()->stateManager()->setState(ESID_GAME, _asset_scn(GameManager::Get()->getConfiguration().new_game_scene));
				}
			}
			else
			{
				if (MenuTextButton("Resume Game", btn_w))
				{
					show_window_save_game = false;
					show_window_controls = false;
					show_window_settings = false;
					show_window_load_game = false;

					Engine::Get()->stateManager()->setStatePauseResume(ESID_GAME);
				}

				if (MenuTextButton("Save Game", btn_w))
				{
					fb_needs_refresh      = true;
					show_window_save_game = true;
					show_window_controls  = false;
					show_window_settings  = false;
					show_window_load_game = false;
				}
			}

			if (MenuTextButton("Load Game", btn_w))
			{
				fb_needs_refresh      = true;
				show_window_load_game = true;
				show_window_controls  = false;
				show_window_settings  = false;
				show_window_save_game = false;
			}

			if (MenuTextButton("Controls", btn_w))
			{
				show_window_controls = true;

				show_window_load_game = false;
				show_window_settings = false;
				show_window_save_game = false;
			}

			if (MenuTextButton("Settings", btn_w))
			{
				show_window_settings = true;

				show_window_load_game = false;
				show_window_controls = false;
				show_window_save_game = false;
			}

			if (MenuTextButton("Exit", btn_w))
			{
				Engine::Get()->exit();
			}

			End();
		}
		if (any_subwindow_open) PopStyleVar();
		PopStyleColor(2);
	}

	if (fb_needs_refresh)
	{
		FB_Refresh();
		fb_needs_refresh = false;
	}

	// ---- Shared file browser helper ----
	// Draws the directory listing into the current window.
	// Returns the full path of a .pak file if one was clicked, otherwise empty.
	auto DrawFileBrowser = [&]() -> std::string
	{
		std::string selected;

		// Current path + Up button
		ImGui::TextDisabled("%s", fb_current_dir.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("^  Up"))
		{
			namespace fs = boost::filesystem;
			fs::path parent = fs::path(fb_current_dir).parent_path();
			if (!parent.empty() && parent != fs::path(fb_current_dir))
			{
				fb_current_dir = parent.string();
				FB_Refresh();
			}
		}

		ImGui::Separator();

		const float list_h = 260.0f * RenderManager::Get()->getConfiguration().dpi_scale;
		ImGui::BeginChild("##fb_list", ImVec2(360.0f * RenderManager::Get()->getConfiguration().dpi_scale, list_h), true);

		for (auto& d : fb_subdirs)
		{
			std::string label = "[" + d + "]";
			if (ImGui::Selectable(label.c_str(), false))
			{
				fb_current_dir += "/" + d;
				FB_Refresh();
			}
		}

		for (auto& f : fb_pak_files)
		{
			if (ImGui::Selectable(f.c_str(), false))
			{
				selected = fb_current_dir + "/" + f;

				// Pre-fill save name with stem of clicked file
				std::string stem = f.size() > 4 ? f.substr(0, f.size() - 4) : f;
				strncpy(fb_save_name, stem.c_str(), sizeof(fb_save_name) - 1);
				fb_save_name[sizeof(fb_save_name) - 1] = '\0';
			}
		}

		if (fb_subdirs.empty() && fb_pak_files.empty())
			ImGui::TextDisabled("(empty)");

		ImGui::EndChild();

		return selected;
	};

	if (show_window_save_game)
	{
		SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(4096.0f, 4096.0f));
		Begin("Save Game", &show_window_save_game, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
		{
			lockout_escape_key = true;
			SetWindowPos(ImVec2(RenderManager::Get()->getConfiguration().width  / 2 - GetWindowSize().x / 2,
			                    RenderManager::Get()->getConfiguration().height / 2 - GetWindowSize().y / 2));

			DrawFileBrowser(); // clicking a file pre-fills the name box but doesn't save

			ImGui::Spacing();
			ImGui::Text("Filename:");
			ImGui::SameLine();
			ImGui::PushID("SaveNameInput");
			ImGui::SetNextItemWidth(240.0f * RenderManager::Get()->getConfiguration().dpi_scale);
			ImGui::InputText("", fb_save_name, sizeof(fb_save_name));
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::TextDisabled(".pak");

			ImGui::Spacing();

			if (Button("Save", _window_button_size))
			{
				std::string path = fb_current_dir + "/" + std::string(fb_save_name) + ".pak";
				WorldManager::Get()->exportScene(path);
				FB_Refresh();
				show_window_save_game = false;
				lockout_escape_key = false;
			}
			ImGui::SameLine();
			if (Button("Cancel", _window_button_size))
			{
				show_window_save_game = false;
				lockout_escape_key = false;
			}

			End();
		}
	}

	if (show_window_load_game)
	{
		SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(4096.0f, 4096.0f));
		Begin("Load Game", &show_window_load_game, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
		{
			SetWindowPos(ImVec2(RenderManager::Get()->getConfiguration().width  / 2 - GetWindowSize().x / 2,
			                    RenderManager::Get()->getConfiguration().height / 2 - GetWindowSize().y / 2));

			std::string to_load = DrawFileBrowser();
			if (!to_load.empty())
			{
				if (is_ingame_menu)
					Engine::Get()->stateManager()->destroyState(ESID_GAME);

				Engine::Get()->stateManager()->setState(ESID_GAME, to_load);
				show_window_load_game = false;
			}

			ImGui::Spacing();
			if (Button("Cancel", _window_button_size))
				show_window_load_game = false;

			End();
		}
	}

	if (show_window_settings)
	{
		const float settings_max_h = RenderManager::Get()->getConfiguration().height - 40.0f;
		SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(4096.0f, settings_max_h));
		Begin("Settings", &show_window_settings, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |  ImGuiWindowFlags_NoMove);
		{
			lockout_escape_key = true;

			SetWindowPos(ImVec2(RenderManager::Get()->getConfiguration().width / 2 - GetWindowSize().x / 2, RenderManager::Get()->getConfiguration().height / 2 - GetWindowSize().y / 2));

			static auto render = RenderManager::Get()->getConfiguration();
			static auto physics = PhysicsManager::Get()->getConfiguration();
			static auto sound = SoundManager::Get()->getConfiguration();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.42f, 0.08f, 1.00f));
			ImGui::Text("DISPLAY");
			ImGui::PopStyleColor();
			ImGui::Separator();
			ImGui::Spacing();

			static bool invalidMode = false;

			if (render.aspect == -1 || render.mode == -1)
			{
				invalidMode = true;
			}

			ImGui::Checkbox("Custom Resolution", &invalidMode);
			
			static int currentaspectRatioItem = render.aspect;
			static int currentResolutionItem = render.mode;

			if (!invalidMode)
			{
				const char* aspectRatioItems[] = { "4:3", "5:4", "16:9", "16:10" };
				Combo("Aspect Ratio", &currentaspectRatioItem, aspectRatioItems, static_cast<int>((sizeof aspectRatioItems / sizeof *aspectRatioItems)));

				switch (currentaspectRatioItem)
				{
				case 0:
				{
					const char* resolutionItems[] = { "640x480", "800x600", "1024x768", "1280x960", "1600x1200", "2048x1536" };

					if (currentResolutionItem > 5)
					{
						currentResolutionItem = 0;
					}
					else if (currentResolutionItem == -1)
					{
						invalidMode = true;
						break;
					}

					Combo("Resolution", &currentResolutionItem, resolutionItems, static_cast<int>((sizeof resolutionItems / sizeof *resolutionItems)));

					break;
				}

				case 1:
				{
					const char* resolutionItems[] = { "1280x1024", "2560x2048" };

					if (currentResolutionItem > 1)
					{
						currentResolutionItem = 0;
					}
					else if (currentResolutionItem == -1)
					{
						invalidMode = true;
						break;
					}

					Combo("Resolution", &currentResolutionItem, resolutionItems, static_cast<int>((sizeof resolutionItems / sizeof *resolutionItems)));
	
					break;
				}

				case 2:
				{
					const char* resolutionItems[] = { "1280x720", "1366x768", "1600x900", "1920x1080", "2550x1440", "3840x2160" };

					if (currentResolutionItem > 5)
					{
						currentResolutionItem = 0;
					}
					else if (currentResolutionItem == -1)
					{
						invalidMode = true;
						break;
					}

					Combo("Resolution", &currentResolutionItem, resolutionItems, static_cast<int>((sizeof resolutionItems / sizeof *resolutionItems)));
	
					break;
				}

				case 3:
				{
					const char* resolutionItems[] = { "1280x800", "1440x900", "1680x1050", "1920x1200", "2560x1600", "3840x2400" };

					if (currentResolutionItem > 5)
					{
						currentResolutionItem = 0;
					}
					else if (currentResolutionItem == -1)
					{
						invalidMode = true;
						break;
					}

					Combo("Resolution", &currentResolutionItem, resolutionItems, static_cast<int>((sizeof resolutionItems / sizeof *resolutionItems)));

					break;
				}

				default: invalidMode = true;
					break;
				}
			}
			
			if (invalidMode)
			{
				InputInt("Width", &render.width);
				InputInt("Height", &render.height);
			}

			ImGui::Checkbox("Fullscreen", &render.fullscreen);
			ImGui::Checkbox("Vertical Sync", &render.vSync);
			ImGui::SliderInt("Framerate Limit", &render.frameLimit, 30, 240);
			ImGui::SliderInt("Aniostropic Filtering", &render.anisotropyFactor, 0, 16);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.42f, 0.08f, 1.00f));
			ImGui::Text("SOUND");
			ImGui::PopStyleColor();
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::SliderFloat("Master Volume", &sound.volume, 0.f, 1.f);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.42f, 0.08f, 1.00f));
			ImGui::Text("PHYSICS");
			ImGui::PopStyleColor();
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::SliderInt("CPU Threads", &physics.threads, 1, static_cast<int>(std::thread::hardware_concurrency()));
			
			ImGui::Spacing();
			ImGui::Spacing();

			if (Button("Save", _window_button_size))
			{
				if (!invalidMode)
				{
					render.aspect = currentaspectRatioItem;
					render.mode = currentResolutionItem;

					RenderManager::Get()->getResolutionFromMode(render.aspect, render.mode, render.width, render.height);
				}
				else
				{
					render.aspect = -1;
					render.mode = -1;
				}
				
				RenderManager::Get()->saveConfiguration(render);
				PhysicsManager::Get()->saveConfiguration(physics);
				SoundManager::Get()->saveConfiguration(sound);

				render = RenderManager::Get()->getConfiguration();
				physics = PhysicsManager::Get()->getConfiguration();
				sound = SoundManager::Get()->getConfiguration();

				show_window_settings = false;

				lockout_escape_key = false;
			}
			
			ImGui::SameLine();
			
			if (Button("Cancel", _window_button_size))
			{			
				render = RenderManager::Get()->getConfiguration();
				physics = PhysicsManager::Get()->getConfiguration();
				sound = SoundManager::Get()->getConfiguration();

				currentaspectRatioItem = render.aspect;
				currentResolutionItem = render.mode;
				
				show_window_settings = false;

				lockout_escape_key = false;
			}

			End();
		}
	}

	if (show_window_controls)
	{
		const float controls_max_h = RenderManager::Get()->getConfiguration().height - 40.0f;
		SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(4096.0f, controls_max_h));
		Begin("Controls", &show_window_controls, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |  ImGuiWindowFlags_NoMove);
		{
			lockout_escape_key = true;

			SetWindowPos(ImVec2(RenderManager::Get()->getConfiguration().width / 2 - GetWindowSize().x / 2, RenderManager::Get()->getConfiguration().height / 2 - GetWindowSize().y / 2));

			static auto config = InputManager::Get()->getConfiguration();

			config.key_forward = InputManager::Get()->getKeyActionPairList().at(0).key;
			config.key_backward = InputManager::Get()->getKeyActionPairList().at(1).key;
			config.key_strafel = InputManager::Get()->getKeyActionPairList().at(2).key;
			config.key_strafer = InputManager::Get()->getKeyActionPairList().at(3).key;
			config.key_jump = InputManager::Get()->getKeyActionPairList().at(4).key;
			config.key_crouch = InputManager::Get()->getKeyActionPairList().at(5).key;
			config.key_sprint = InputManager::Get()->getKeyActionPairList().at(6).key;
			config.key_use = InputManager::Get()->getKeyActionPairList().at(7).key;
			config.key_reload = InputManager::Get()->getKeyActionPairList().at(8).key;
			config.key_inventory = InputManager::Get()->getKeyActionPairList().at(9).key;
			
			const float btn_x = ImGui::CalcTextSize("Strafe Right:").x + ImGui::GetStyle().WindowPadding.x + 8.0f;

			ImGui::Text("Forward:");     ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_forward + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("forward", config.key_forward);
			}

			ImGui::Text("Backward:");    ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_backward + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("backward", config.key_backward);
			}

			ImGui::Text("Strafe Left:"); ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_strafel + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("strafel", config.key_strafel);
			}

			ImGui::Text("Strafe Right:"); ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_strafer + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("strafer", config.key_strafer);
			}

			ImGui::Text("Jump:");        ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_jump + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("jump", config.key_jump);
			}

			ImGui::Text("Crouch:");      ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_crouch + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("crouch", config.key_crouch);
			}

			ImGui::Text("Sprint:");      ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_sprint + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("sprint", config.key_sprint);
			}

			ImGui::Text("Interact:");    ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_use + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("use", config.key_use);
			}

			ImGui::Text("Reload:");      ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_reload + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("reload", config.key_reload);
			}

			ImGui::Text("Inventory:");   ImGui::SameLine(btn_x); if (Button(KEYBOARD_KEY_STRING[config.key_inventory + 1], _window_button_size))
			{
				show_window_controls = false;
				show_window_remap_key = true;

				currentKeyPair = sKeyActionPair("inventory", config.key_inventory);
			}

			ImGui::Spacing();
			ImGui::Spacing();
			
			ImGui::SliderFloat("Mouse Sensitivity", &config.xsens, 0.05f, 1.f);

			ImGui::Spacing();
			ImGui::Spacing();
			
			if (Button("Save", _window_button_size))
			{
				config.ysens = config.xsens;

				InputManager::Get()->saveConfiguration(config);

				config = InputManager::Get()->getConfiguration();
				
				show_window_controls = false;

				lockout_escape_key = false;
			}
			ImGui::SameLine();
			
			if (Button("Cancel", _window_button_size))
			{
				config = InputManager::Get()->getConfiguration();

				InputManager::Get()->getKeyActionPairList().clear();

				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("forward", config.key_forward));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("backward", config.key_backward));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("strafel", config.key_strafel));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("strafer", config.key_strafer));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("jump", config.key_jump));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("crouch", config.key_crouch));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("sprint", config.key_sprint));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("use", config.key_use));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("reload", config.key_reload));
				InputManager::Get()->getKeyActionPairList().emplace_back(sKeyActionPair("inventory", config.key_inventory));
				
				show_window_controls = false;

				lockout_escape_key = false;
			}
			
			End();
		}
	}

	if (show_window_remap_key)
	{
		Begin("Remap Input", &show_window_remap_key, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |  ImGuiWindowFlags_NoMove);
		{
			lockout_escape_key = true;
			
			SetWindowPos(ImVec2(RenderManager::Get()->getConfiguration().width / 2 - GetWindowSize().x / 2, RenderManager::Get()->getConfiguration().height / 2 - GetWindowSize().y / 2));

			ImGui::Text(std::string("Press a key to rebind \'" + currentKeyPair.getAction() + "\'").c_str());

			for (auto i = 0; i < KEY_F1; i++)
			{
				if (InputManager::Get()->isKeyPressed(i, true))
				{
					if (i == KEY_ESCAPE) 
					{
						show_window_remap_key = false;
						show_window_controls = true;

						currentKeyPair = sKeyActionPair("null", KEY_UNKNOWN);

						lockout_escape_key = false;
						
						break;
					}
					
					if (i != KEY_TILDE && i != KEY_LSYSTEM && i != KEY_RSYSTEM && i != KEY_MENU && i != KEY_BACKSPACE)
					{
						for (auto& pair : InputManager::Get()->getKeyActionPairList())
						{
							if (pair.getAction() == currentKeyPair.getAction())
							{
								pair.key = i;

								show_window_remap_key = false;
								show_window_controls = true;

								currentKeyPair = sKeyActionPair("null", KEY_UNKNOWN);

								lockout_escape_key = false;

								break;
							}
						}

						break;
					}
				}
			}
			
			End();
		}
	}
}
