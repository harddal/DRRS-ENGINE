#pragma once

#include "EditorInterface.h"
#include <map>
#include <vector>
#include <string>

// Recursive folder tree node used by the prop/vegetation mesh browsers.
// Defined here so both EditorInterface_Assets.cpp and EditorInterface_Painting.cpp can share it.
struct PropMeshFolder
{
    std::map<std::string, PropMeshFolder> subfolders;
    std::vector<std::pair<std::string, std::string>> files; // {display name, full path}
};

// --------------------------------------------------------------------------
// Cross-file state — defined in their respective .cpp, shared via extern
// --------------------------------------------------------------------------

#include "Engine/Types.h"

// Defined in EditorInterface.cpp
extern EditorWindowData m_windowData;
extern std::string g_currentScenePath;
extern std::vector<entityid> g_undoEntities;

// Defined in EditorInterface_EntityBuilder.cpp
// True after "Create Entity" is pressed, cleared when the builder is reset/closed.
extern bool s_builderEntityCreated;

// Defined in EditorInterface_Assets.cpp
extern PropMeshFolder s_propMeshRoot;

// Defined in EditorInterface_EntityBuilder.cpp
// The mesh node currently displayed in the builder preview scene.
// draw_component_properties (MESH case) drives the animation preview through this.
namespace irr { namespace scene { class IAnimatedMeshSceneNode; } }
extern irr::scene::IAnimatedMeshSceneNode* s_previewNode;

// --------------------------------------------------------------------------
// Shared free functions
// --------------------------------------------------------------------------

// Rescans content/mesh/ and rebuilds s_propMeshRoot.
// Defined in EditorInterface_Assets.cpp, called from Painting.cpp as well.
void s_loadPropMeshList();

// Defined in EditorInterface_ScriptEditor.cpp — renders any pending script
// editor modals at the top level, above all docked windows.
void drawScriptEditorPopups();

// Defined in EditorInterface_Components.cpp — removable token chips for a
// comma-separated entity-name list (red = unresolved, yellow = duplicate).
// Shared by the component panels and the brush editor's trigger section.
void s_drawNameListChips(std::string& csv, const char* idLabel);
