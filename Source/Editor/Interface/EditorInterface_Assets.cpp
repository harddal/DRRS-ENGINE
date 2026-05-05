#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"
#include "Engine/Prop/PropManager.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>

using namespace boost;
using namespace filesystem;

// ---------------------------------------------------------------------------
// Module-local asset list state
// ---------------------------------------------------------------------------
static std::vector<std::string> m_entityList, m_textureList, m_prefabList, m_meshList;
static std::vector<irr::video::ITexture*> m_imageEntityIconList;
static std::vector<irr::video::ITexture*> m_imageTextureFileList;

static bool m_hasLoadedEntityList  = false;
static bool m_hasLoadedTextureList = false;
static bool m_hasLoadedPrefabList  = false;
static bool m_hasLoadedMeshList    = false;

// ---------------------------------------------------------------------------
// Prop mesh tree — non-static so Painting.cpp can access s_propMeshRoot
// ---------------------------------------------------------------------------
PropMeshFolder s_propMeshRoot;
static bool    s_propMeshListLoaded = false;

static void s_insertPropMesh(PropMeshFolder& folder,
    const std::vector<std::string>& parts, size_t idx, const std::string& fullPath)
{
    if (idx == parts.size() - 1)
    {
        folder.files.push_back({ parts[idx], fullPath });
        return;
    }
    s_insertPropMesh(folder.subfolders[parts[idx]], parts, idx + 1, fullPath);
}

void s_loadPropMeshList()
{
    s_propMeshRoot = PropMeshFolder{};
    const std::vector<std::string> exts = { "obj", "fbx", "glb", "gltf", "b3d" };

    try
    {
        recursive_directory_iterator it("content/mesh/"), end;
        for (auto& entry : make_iterator_range(it, end))
        {
            if (!is_regular(entry)) continue;
            std::string path(entry.path().native().begin(), entry.path().native().end());
            for (auto& c : path) if (c == '\\') c = '/';
            const std::string ext = path.substr(path.find_last_of('.') + 1);
            bool validExt = false;
            for (const auto& e : exts) if (ext == e) { validExt = true; break; }
            if (!validExt) continue;

            // Strip "content/mesh/" prefix to get the tree-relative path
            const std::string prefix = "content/mesh/";
            std::string rel = path;
            if (rel.size() > prefix.size() && rel.substr(0, prefix.size()) == prefix)
                rel = rel.substr(prefix.size());

            // Split by '/' into path parts
            std::vector<std::string> parts;
            std::string token;
            for (char c : rel)
            {
                if (c == '/') { if (!token.empty()) { parts.push_back(token); token.clear(); } }
                else token += c;
            }
            if (!token.empty()) parts.push_back(token);

            if (!parts.empty())
                s_insertPropMesh(s_propMeshRoot, parts, 0, path);
        }
    }
    catch (...) {}
}

// ---------------------------------------------------------------------------
// Asset list load functions
// ---------------------------------------------------------------------------

void EditorInterface::loadEntityList()
{
	m_entityList.clear();

	recursive_directory_iterator it("content/entity/"), end;

	std::vector<std::string> files;
	for (auto& entry : make_iterator_range(it, end))
	{
		if (is_regular(entry))
		{
			files.emplace_back(std::string(entry.path().native().begin(), entry.path().native().end()));
		}
	}

	m_imageEntityIconList.push_back(RenderManager::Get()->driver()->getTexture("content/texture/sprite/3x3.png"));
	m_imageEntityIconList.clear();

	for (const auto& file : files)
	{
		auto fname = file.substr(file.find_first_of('\\') + 1, std::string::npos);
		auto fname_noext = fname.substr(0, fname.size() - 4);

		m_entityList.emplace_back(fname_noext);
	}
}

void EditorInterface::loadPrefabList()
{
	m_prefabList.clear();

	recursive_directory_iterator it("content/prefab/"), end;

	std::vector<std::string> files;
	for (auto& entry : make_iterator_range(it, end))
	{
		if (is_regular(entry))
		{
			files.emplace_back(std::string(entry.path().native().begin(), entry.path().native().end()));
		}
	}

	for (const auto& file : files)
	{
		auto fname = file.substr(file.find_first_of('\\') + 1, std::string::npos);
		auto fname_noext = fname.substr(0, fname.size() - 4);

		m_prefabList.emplace_back(fname_noext);
	}
}

void EditorInterface::loadTextureList()
{
	m_textureList.clear();

	const std::string base = "content/texture/";
	recursive_directory_iterator it(base), end;

	for (auto& entry : make_iterator_range(it, end))
	{
		if (!is_regular(entry)) continue;

		std::string fullPath = entry.path().string();
		std::replace(fullPath.begin(), fullPath.end(), '\\', '/');

		if (fullPath.size() <= base.size()) continue;
		std::string relPath = fullPath.substr(base.size());

		auto dotPos = relPath.rfind('.');
		if (dotPos == std::string::npos) continue;
		std::string ext = relPath.substr(dotPos);
		if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") continue;

		// Store without extension so _asset_tex() can reconstruct the full path
		m_textureList.emplace_back(relPath.substr(0, dotPos));
	}
}

void EditorInterface::loadMeshList()
{
	m_meshList.clear();

	recursive_directory_iterator it("content/mesh/"), end;

	std::vector<std::string> files;
	for (auto& entry : make_iterator_range(it, end))
	{
		if (is_regular(entry))
		{
			files.emplace_back(std::string(entry.path().native().begin(), entry.path().native().end()));
		}
	}

	for (const auto& file : files)
	{
		auto fname = file.substr(file.find_first_of('\\') + 1, std::string::npos);
		auto fname_noext = fname.substr(0, fname.size() - 4);

		if (fname.substr(fname.size() - 3) == "b3d")
		{
			m_meshList.emplace_back(fname_noext);
		}
	}
}

// ---------------------------------------------------------------------------
// Spawn windows
// ---------------------------------------------------------------------------

void EditorInterface::draw_window_spawn_entity()
{
	if (!m_windowData.draw_window_spawn_entity)
	{
		m_hasLoadedEntityList = false;
		return;
	}

	if (!m_hasLoadedEntityList)
	{
		loadEntityList();
		m_hasLoadedEntityList = true;
	}

	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(250, 500), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Entity Spawn Menu", &m_windowData.draw_window_spawn_entity))
	{
		static char s_spawn_filter[128] = "";
		float refreshW = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.f;
		ImGui::SetNextItemWidth(-refreshW - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputTextWithHint("##spawn_filter", "Filter...", s_spawn_filter, sizeof(s_spawn_filter));
		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
			loadEntityList();
		ImGui::Separator();

		if (s_spawn_filter[0] != '\0')
		{
			for (const auto& path : m_entityList)
			{
				std::string filename = path.substr(path.find_last_of('\\') + 1);
				bool match = false;
				const char* haystack = filename.c_str();
				const char* needle   = s_spawn_filter;
				for (; *haystack; ++haystack)
				{
					if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle))
					{
						const char* h = haystack, *n = needle;
						while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { ++h; ++n; }
						if (!*n) { match = true; break; }
					}
				}
				if (!match) continue;
				ImGui::PushID(path.c_str());
				if (ImGui::Button(filename.c_str()))
				{
					WorldManager::Get()->spawnEntity(_asset_ent(path));
					g_sceneInteractor.selectNewSpawnedEntityNextFrame();
				}
				ImGui::PopID();
			}
		}
		else
		{

		std::vector<std::string> folder;

		bool cont = false;
		for (const auto& path : m_entityList)
		{
			for (const auto& subpath : folder)
			{
				if (path.substr(0, path.find_first_of('\\')) == subpath)
				{
					cont = true;

					break;
				}

				cont = false;
			}

			if (!cont)
			{
				folder.emplace_back(path.substr(0, path.find_first_of('\\')));
			}
		}

		for (const auto& subpath : folder)
		{
			if (ImGui::CollapsingHeader(subpath.c_str()))
			{
				for (const auto& path : m_entityList)
				{
					if (path.substr(0, path.find_first_of('\\')) == subpath)
					{
						if (ImGui::Button(path.substr(path.find_last_of('\\') + 1).c_str()))
						{
							WorldManager::Get()->spawnEntity(_asset_ent(path));
							g_sceneInteractor.selectNewSpawnedEntityNextFrame();
						}
					}
				}
			}
		}
		} // else (no filter)
	}
	ImGui::End();
}

void EditorInterface::draw_window_spawn_prefab()
{
	//if (!m_windowData.draw_window_spawn_prefab) {
	//    m_hasLoadedPrefabList = false;
	//    return;
	//}

	//if (!m_hasLoadedPrefabList) {
	//    loadPrefabList();
	//    m_hasLoadedPrefabList = true;
	//}

	//ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(250, 700));
	//if (ImGui::Begin("Prefab Spawn Menu", &m_windowData.draw_window_spawn_prefab, ImGuiWindowFlags_AlwaysAutoResize)) {
	//    for (const auto& ename : m_prefabList) {
	//        if (ImGui::Button(ename.c_str())) {
	//            /*entityid spawn = */WorldManager::Get()->spawnPrefab(_asset_pre(ename));
	//            //g_sceneInteractor.setSelectedEntity(spawn);
	//            //g_currentEntity = spawn;
	//        }
	//    }
	//    /* if (ImGui::Button(noext.c_str())) {
	//         g_sceneInteractor.setSelectedEntity(WorldManager::Get()->spawnEntity(file.c_str()));
	//     }*/
	//}
	//ImGui::End();
}

void EditorInterface::draw_window_spawn_mesh()
{
	/*if (!m_windowData.draw_window_spawn_mesh) {
	    m_hasLoadedMeshList = false;

	    return;
	}

	if (!m_hasLoadedMeshList) {
	    loadMeshList();
	    m_hasLoadedMeshList = true;
	}

	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(250, 700), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Mesh Spawn Menu", &m_windowData.draw_window_spawn_mesh)) {
	    for (const auto& ename : m_meshList) {
	        if (ImGui::Button(ename.c_str())) {
	            WorldManager::Get()->spawnStaticMesh(_asset_b3d(ename));
	        }
	    }
	}
	ImGui::End();*/
}

void EditorInterface::draw_window_spawn_prop()
{
    if (!m_windowData.draw_window_spawn_prop)
    {
        s_propMeshListLoaded = false;
        return;
    }

    if (!s_propMeshListLoaded)
    {
        s_loadPropMeshList();
        s_propMeshListLoaded = true;
    }

    ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(340, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Prop Spawn Menu", &m_windowData.draw_window_spawn_prop))
    {
        ImGui::End();
        return;
    }

    // Pending prop configuration
    static char shaderBuf[64] = "phong_perpixel";
    static bool receivesLightmap  = false;
    static bool castShadows       = true;
    static bool hasCollision      = false;
    static bool useConvexCollision = false;

    ImGui::Text("Shader:");
    ImGui::SameLine();
    ImGui::PushItemWidth(200.f);
    ImGui::InputText("##propSpawnShader", shaderBuf, sizeof(shaderBuf));
    ImGui::PopItemWidth();

    ImGui::Checkbox("Receives Lightmap", &receivesLightmap);
    ImGui::SameLine();
    ImGui::Checkbox("Cast Shadows", &castShadows);
    ImGui::Checkbox("Has Collision", &hasCollision);
    if (hasCollision) { ImGui::SameLine(); ImGui::Checkbox("Convex Hull", &useConvexCollision); }

    if (ImGui::Button("Refresh List")) { s_loadPropMeshList(); }

    ImGui::Separator();
    ImGui::Text("Click a mesh to place it as a prop at origin:");
    ImGui::BeginChild("##propMeshScroll", ImVec2(0, 0), false);

    // Recursive folder tree draw
    std::function<void(const PropMeshFolder&)> drawFolder =
        [&](const PropMeshFolder& folder)
    {
        for (const auto& kv : folder.subfolders)
        {
            if (ImGui::TreeNode(kv.first.c_str()))
            {
                drawFolder(kv.second);
                ImGui::TreePop();
            }
        }
        for (const auto& f : folder.files)
        {
            if (ImGui::Button(f.first.c_str()))
            {
                if (PropManager::Get())
                {
                    StaticProp prop;
                    prop.mesh              = f.second;
                    prop.defaultShader     = shaderBuf;
                    prop.receivesLightmap  = receivesLightmap;
                    prop.castShadows       = castShadows;
                    prop.hasCollision      = hasCollision;
                    prop.useConvexCollision = useConvexCollision;
                    prop.position = { 0.f, 0.f, 0.f };
                    prop.rotation = { 0.f, 0.f, 0.f };
                    prop.scale    = { 1.f, 1.f, 1.f };

                    uint32_t newId = PropManager::Get()->addProp(prop);
                    g_sceneInteractor.setSelectedProp(newId);
                    m_windowData.draw_window_prop_ent = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.second.c_str());
        }
    };
    drawFolder(s_propMeshRoot);

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Texture browser
// ---------------------------------------------------------------------------

void EditorInterface::draw_window_texture_browser()
{
	if (!m_windowData.draw_window_texture_browser)
		return;

	static char s_filter[128] = {};

	ImGui::SetNextWindowSize(ImVec2(340, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Texture Browser", &m_windowData.draw_window_texture_browser))
	{
		ImGui::End();
		return;
	}

	ImGui::Text("%d textures", static_cast<int>(m_textureList.size()));
	ImGui::SameLine();
	ImGui::PushItemWidth(-1);
	ImGui::InputText("##tx_filter", s_filter, sizeof(s_filter));
	ImGui::PopItemWidth();
	ImGui::Separator();

	ImGui::BeginChild("##tx_list", ImVec2(0, 0), false);
	for (const auto& name : m_textureList)
	{
		// Filter by substring (case-insensitive not needed — just use find)
		if (s_filter[0] != '\0' && name.find(s_filter) == std::string::npos)
			continue;

		// Show just the filename portion as the label, full rel-path as tooltip
		auto slash = name.rfind('/');
		const char* label = (slash != std::string::npos) ? name.c_str() + slash + 1 : name.c_str();

		if (ImGui::Selectable(label))
		{
			g_currentSelectedTexture   = name;
			m_windowData.draw_window_texture_browser = false;
		}
		if (ImGui::IsItemHovered() && slash != std::string::npos)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndTooltip();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

void EditorInterface::show_window_texture_browser()
{
	if (!m_hasLoadedTextureList)
	{
		loadTextureList();
		m_hasLoadedTextureList = true;
	}

	m_windowData.draw_window_texture_browser = true;
}

// ---------------------------------------------------------------------------
// Prop properties (consolidated into draw_window_prop_ent)
// ---------------------------------------------------------------------------

void EditorInterface::draw_window_prop_prop()
{
    // Consolidated into draw_window_prop_ent — nothing to do here.
}
