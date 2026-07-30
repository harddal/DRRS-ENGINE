#pragma once

#include <string>
#include "Editor/SceneInteractionManager.h"

enum class ENTITY_COMPONENT;

struct EditorWindowData
{
	bool draw_menubar_main = true;

	bool draw_window_hiearchy          = true;
	bool draw_window_prop_ent          = true;
	bool draw_window_prop_scene        = true;
	bool draw_window_help_about        = false;
	bool draw_window_spawn_entity      = true;
    bool draw_window_spawn_prefab      = false;
    bool draw_window_spawn_mesh        = false;
    bool draw_window_texture_browser   = false;
    bool draw_window_scene_stats       = false;
    bool draw_window_console           = false;
    bool draw_window_log               = true;
    bool draw_window_editor_settings   = false;
    bool draw_window_add_component     = false;
	bool draw_window_entity_debug_info = false;
	bool draw_window_entity_builder    = false;
    bool draw_window_spawn_prop        = true;
    bool draw_window_prop_prop         = false;
    bool draw_window_vegetation_painter = false;
    bool draw_window_texture_painter    = false;
    bool draw_window_brush_editor       = true;
    bool draw_window_script_editor      = false;
    bool draw_window_particle_designer  = false;

    bool draw_toolbar    = true;
    bool draw_statusbar  = true;
};

namespace EditorInterface
{
    void draw();

    void detectKeyShortcuts();

    void loadEntityList();
    void loadPrefabList();
    void loadMeshList();
    void loadTextureList();

	void function_open_scene();

	// Return true only when the scene actually reached disk — a cancelled
	// Save As dialog reports false so callers (the quit prompt) can stay put.
	bool funtion_save_scene();
	bool function_save_scene_as();

	void function_play_scene();
	void function_showhide_menubar();

	// Installed as Engine's quit-request handler for the lifetime of the editor
	// state. Always vetoes the immediate quit and raises draw_quit_prompt().
	bool function_request_quit();
	void draw_quit_prompt();

	void draw_menubar_main();
	void draw_window_spawn_entity();
    void draw_window_spawn_prefab();
    void draw_window_spawn_mesh();
	void draw_window_hierarchy();
	void draw_window_prop_scene();
	void draw_window_prop_ent(bool display_override = false);
    void draw_window_texture_browser();
	void draw_window_help_about();
    void draw_window_scene_stats();
    void draw_window_console();
    void draw_window_log();
    void draw_window_editor_settings();
    void draw_window_add_component();
	void draw_window_entity_debug_info();
	void draw_window_entity_builder();
    void draw_window_spawn_prop();
    void draw_window_prop_prop();
    void draw_window_vegetation_painter();
    void draw_window_texture_painter();
    void draw_window_brush_editor();
    void draw_window_script_editor();
    void open_script_in_editor(const std::string& path);
    void draw_window_particle_designer();
    void draw_toolbar();
    void draw_statusbar();

    // requestId identifies the calling field; the caller polls g_textureBrowserRequestID
    // against its own id and reads g_currentSelectedTexture (full "content/texture/....ext"
    // path) once the browser closes with a selection.
    void show_window_texture_browser(const std::string& requestId);

	bool draw_component_properties(ENTITY_COMPONENT component, anax::Entity &entity);
	void add_component(ENTITY_COMPONENT component, anax::Entity &entity);
	bool has_component(ENTITY_COMPONENT component, anax::Entity &entity);
};
