#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include <irrlicht/source/Irrlicht/COpenGLTexture.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"
#include "Engine/Prop/PropManager.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <cctype>

using namespace boost;
using namespace filesystem;

// ---------------------------------------------------------------------------
// Module-local asset list state
// ---------------------------------------------------------------------------
static std::vector<std::string> m_entityList, m_prefabList, m_meshList;
static std::vector<irr::video::ITexture*> m_imageEntityIconList;

static bool m_hasLoadedEntityList  = false;
static bool m_hasLoadedPrefabList  = false;
static bool m_hasLoadedMeshList    = false;

// ---------------------------------------------------------------------------
// Texture browser — folder tree + thumbnail grid
// ---------------------------------------------------------------------------

// Recursive folder tree over content/texture/, same shape as PropMeshFolder.
// files holds bare filenames (with extension) that live directly in that folder.
struct TextureFolder
{
    std::map<std::string, TextureFolder> subfolders;
    std::vector<std::string> files;
};
static TextureFolder s_textureRoot;
static bool          s_textureTreeLoaded    = false;
static std::string   s_texCurrentFolderPath; // "" == content/texture/ root

static void s_insertTextureFile(TextureFolder& folder,
    const std::vector<std::string>& parts, size_t idx)
{
    if (idx == parts.size() - 1)
    {
        folder.files.push_back(parts[idx]);
        return;
    }
    s_insertTextureFile(folder.subfolders[parts[idx]], parts, idx + 1);
}

// Thumbnails reuse the driver's own texture cache (driver()->getTexture() already
// dedupes by path) — this cache just remembers the resolved ImTextureID per path
// so the grid doesn't re-resolve the GL handle every frame.
struct TextureThumbnail
{
    ImTextureID texId  = 0;
    bool        loaded = false; // load attempt finished (texId may still be 0 on failure)
};
static std::unordered_map<std::string, TextureThumbnail> s_thumbCache;
static std::deque<std::string>       s_thumbPendingQueue;
static std::unordered_set<std::string> s_thumbQueuedSet;
static const int k_thumbLoadsPerFrame = 6; // spreads a big folder's disk loads across frames

// Returns a resolved ImTextureID, or 0 if the thumbnail hasn't loaded yet (queues it).
static ImTextureID s_getOrQueueThumbnail(const std::string& fullPath)
{
    auto it = s_thumbCache.find(fullPath);
    if (it != s_thumbCache.end())
        return it->second.texId;

    if (s_thumbQueuedSet.insert(fullPath).second)
        s_thumbPendingQueue.push_back(fullPath);
    return 0;
}

static void s_processThumbnailQueue()
{
    int budget = k_thumbLoadsPerFrame;
    while (budget > 0 && !s_thumbPendingQueue.empty())
    {
        std::string path = s_thumbPendingQueue.front();
        s_thumbPendingQueue.pop_front();
        s_thumbQueuedSet.erase(path);

        TextureThumbnail thumb;
        auto* tex = RenderManager::Get()->driver()->getTexture(path.c_str());
        if (tex)
        {
            GLuint glTex = static_cast<irr::video::COpenGLTexture*>(tex)->getOpenGLTextureName();
            thumb.texId = (ImTextureID)(uintptr_t)glTex;
        }
        thumb.loaded = true;
        s_thumbCache[path] = thumb;
        --budget;
    }
}

static TextureFolder* s_findTextureFolder(const std::string& path)
{
    TextureFolder* cur = &s_textureRoot;
    std::string token;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            if (!token.empty())
            {
                auto it = cur->subfolders.find(token);
                if (it == cur->subfolders.end()) return nullptr;
                cur = &it->second;
                token.clear();
            }
        }
        else token += path[i];
    }
    return cur;
}

static bool s_containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
    return it != haystack.end();
}

// Recursively gathers every texture matching `filter` into out as {rel path w/ ext, filename}.
static void s_collectFilteredTextures(const TextureFolder& folder, const std::string& folderPath,
    const std::string& filter, std::vector<std::pair<std::string, std::string>>& out)
{
    for (const auto& f : folder.files)
    {
        if (!filter.empty() && !s_containsCaseInsensitive(f, filter))
            continue;
        std::string rel = folderPath.empty() ? f : folderPath + "/" + f;
        out.emplace_back(rel, f);
    }
    for (const auto& kv : folder.subfolders)
    {
        std::string childPath = folderPath.empty() ? kv.first : folderPath + "/" + kv.first;
        s_collectFilteredTextures(kv.second, childPath, filter, out);
    }
}

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
	s_textureRoot = TextureFolder{};

	const std::string base = "content/texture/";

	try
	{
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
			for (auto& c : ext) c = static_cast<char>(tolower((unsigned char)c));
			if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") continue;

			std::vector<std::string> parts;
			std::string token;
			for (char c : relPath)
			{
				if (c == '/') { if (!token.empty()) { parts.push_back(token); token.clear(); } }
				else token += c;
			}
			if (!token.empty()) parts.push_back(token);

			if (!parts.empty())
				s_insertTextureFile(s_textureRoot, parts, 0);
		}
	}
	catch (...) {}
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
						ImGui::PushID(path.c_str());
						if (ImGui::Button(path.substr(path.find_last_of('\\') + 1).c_str()))
						{
							WorldManager::Get()->spawnEntity(_asset_ent(path));
							g_sceneInteractor.selectNewSpawnedEntityNextFrame();
						}
						ImGui::PopID();
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
    ImGui::SetItemTooltip("Shader for newly placed props: phong_perpixel (default lit),\nfoliage/grass (wind sway), phong_perpixel_transparent (glass).");
    ImGui::PopItemWidth();

    ImGui::Checkbox("Receives Lightmap", &receivesLightmap);
    ImGui::SetItemTooltip("Apply baked lightmap lighting to the prop when lightmaps are baked.");
    ImGui::SameLine();
    ImGui::Checkbox("Cast Shadows", &castShadows);
    ImGui::Checkbox("Has Collision", &hasCollision);
    ImGui::SetItemTooltip("Give the prop a physics collider so things can't pass through it.");
    if (hasCollision)
    {
        ImGui::SameLine();
        ImGui::Checkbox("Convex Hull", &useConvexCollision);
        ImGui::SetItemTooltip("Simplified convex collision instead of exact triangles -\ncheaper; fine for solid chunky objects.");
    }

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
            ImGui::PushID(f.second.c_str());
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
            ImGui::PopID();
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

	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(680, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Texture Browser", &m_windowData.draw_window_texture_browser))
	{
		ImGui::End();
		return;
	}

	s_processThumbnailQueue();

	const float dpi = RenderManager::Get()->getConfiguration().dpi_scale;

	ImGui::PushItemWidth(-1);
	ImGui::InputTextWithHint("##tx_filter", "Filter...", s_filter, sizeof(s_filter));
	ImGui::PopItemWidth();
	ImGui::Separator();

	// ---- Left pane: folder tree ----
	ImGui::BeginChild("##tx_tree", ImVec2(160.0f * dpi, 0), true);
	{
		std::function<void(const std::string&, TextureFolder&)> drawFolder =
			[&](const std::string& path, TextureFolder& folder)
		{
			for (auto& kv : folder.subfolders)
			{
				std::string childPath = path.empty() ? kv.first : path + "/" + kv.first;
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (childPath == s_texCurrentFolderPath) flags |= ImGuiTreeNodeFlags_Selected;
				bool open = ImGui::TreeNodeEx(kv.first.c_str(), flags);
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
					s_texCurrentFolderPath = childPath;
				if (open)
				{
					drawFolder(childPath, kv.second);
					ImGui::TreePop();
				}
			}
		};

		ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_DefaultOpen;
		if (s_texCurrentFolderPath.empty()) rootFlags |= ImGuiTreeNodeFlags_Selected;
		bool rootOpen = ImGui::TreeNodeEx("content/texture", rootFlags);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			s_texCurrentFolderPath.clear();
		if (rootOpen)
		{
			drawFolder("", s_textureRoot);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// ---- Right pane: thumbnail grid ----
	ImGui::BeginChild("##tx_grid", ImVec2(0, 0), true);
	{
		std::vector<std::pair<std::string, std::string>> items; // {rel path w/ ext, filename}
		if (s_filter[0] != '\0')
		{
			// A non-empty filter searches the whole tree, ignoring the selected folder.
			s_collectFilteredTextures(s_textureRoot, "", s_filter, items);
		}
		else
		{
			TextureFolder* cur = s_findTextureFolder(s_texCurrentFolderPath);
			if (cur)
			{
				for (const auto& f : cur->files)
				{
					std::string rel = s_texCurrentFolderPath.empty() ? f : s_texCurrentFolderPath + "/" + f;
					items.emplace_back(rel, f);
				}
			}
		}

		const float thumbSize = 64.0f * dpi;
		const float cellPad   = 8.0f * dpi;
		const float cellW     = thumbSize + cellPad;
		int columns = static_cast<int>(ImGui::GetContentRegionAvail().x / cellW);
		if (columns < 1) columns = 1;

		int col = 0;
		for (const auto& item : items)
		{
			const std::string fullPath = "content/texture/" + item.first;

			ImGui::PushID(fullPath.c_str());
			ImGui::BeginGroup();

			ImTextureID texId = s_getOrQueueThumbnail(fullPath);
			bool clicked;
			if (texId)
				clicked = ImGui::ImageButton("##thumb", ImTextureRef(texId), ImVec2(thumbSize, thumbSize));
			else
				clicked = ImGui::Button("...", ImVec2(thumbSize, thumbSize)); // pending load / failed to load

			if (clicked)
			{
				g_currentSelectedTexture = fullPath;
				m_windowData.draw_window_texture_browser = false;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(fullPath.c_str());
				ImGui::EndTooltip();
			}

			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbSize);
			ImGui::TextWrapped("%s", item.second.c_str());
			ImGui::PopTextWrapPos();

			ImGui::EndGroup();
			ImGui::PopID();

			if (++col < columns)
				ImGui::SameLine(0.0f, cellPad);
			else
				col = 0;
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

void EditorInterface::show_window_texture_browser(const std::string& requestId)
{
	if (!s_textureTreeLoaded)
	{
		loadTextureList();
		s_textureTreeLoaded = true;
	}

	g_textureBrowserRequestID = requestId;
	g_currentSelectedTexture  = "null";
	m_windowData.draw_window_texture_browser = true;
}

// ---------------------------------------------------------------------------
// Prop properties (consolidated into draw_window_prop_ent)
// ---------------------------------------------------------------------------

void EditorInterface::draw_window_prop_prop()
{
    // Consolidated into draw_window_prop_ent — nothing to do here.
}
