#include "ItemDatabase.h"

#include <algorithm>

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>

#include <spdlog/spdlog.h>

#include "Utility/INIFile.h"
#include "Utility/Utility.h"

#include "Engine/Resource/FilePaths.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/WorldManager.h"

using namespace boost;
using namespace filesystem;

static std::vector<ItemDef> g_ItemList;
static bool                 g_Loaded = false;
static const ItemDef        g_NullItem;

void ItemDatabase::Load()
{
	if (g_Loaded)
		return;

	Reload();
}

void ItemDatabase::Reload()
{
	g_ItemList.clear();

	const path dir = "content/item/";

	boost::system::error_code ec;
	if (!exists(dir, ec))
	{
		spdlog::warn("ItemDatabase: no content/item/ directory — no items loaded");
		g_Loaded = true;
		return;
	}

	std::vector<std::string> files;
	for (auto& entry : make_iterator_range(recursive_directory_iterator(dir), {}))
	{
		if (!is_regular_file(entry))
			continue;

		const std::string p = entry.path().string();
		// NO LEADING DOT. FileExtensionFromPath strips it ("image.png" -> "png"),
		// and the old database compared against ".item" — which never matched, so
		// it silently loaded ZERO items on every run.
		if (Utility::FileExtensionFromPath(p) == "item")
			files.emplace_back(p);
	}

	// Sorted so the load order is the same on every machine. Nothing depends on
	// it any more now that ids are strings, but a stable order keeps the log
	// readable and makes a missing file obvious.
	std::sort(files.begin(), files.end());

	for (auto& filePath : files)
	{
		CIniFileA ini; // narrow explicitly — CIniFile is the wide type under _UNICODE
		if (!ini.Load(filePath))
		{
			spdlog::warn("ItemDatabase: failed to parse '{}'", filePath);
			continue;
		}

		ItemDef def;

		// THE FILENAME IS THE ID. Not a key inside the file: two items could then
		// declare the same id and the duplicate would win silently, and renaming
		// a file would leave saves pointing at an id that still resolves to the
		// old content.
		def.id = Utility::FilenameFromPath(filePath);

		def.name        = ini.GetKeyValue("item", "name");
		def.desc        = ini.GetKeyValue("item", "desc");
		def.icon        = ini.GetKeyValue("item", "icon");
		def.script      = ini.GetKeyValue("item", "script");
		def.pickupSound = ini.GetKeyValue("item", "pickupsound");

		const std::string category = ini.GetKeyValue("item", "category");
		if (!category.empty())
			def.category = category;

		def.autoUse = Utility::EvalTrueFalse(ini.GetKeyValue("item", "autouse").c_str());

		const std::string stackable = ini.GetKeyValue("item", "stackable");
		if (!stackable.empty())
			def.stackable = Utility::EvalTrueFalse(stackable.c_str());

		const std::string maxStack = ini.GetKeyValue("item", "maxstack");
		if (!maxStack.empty())
			def.maxStack = atoi(maxStack.c_str());

		if (def.maxStack < 1)
			def.maxStack = 1;

		if (def.name.empty())
			def.name = def.id;

		// Both of these are GUARDED because this path has never actually run
		// before: the old loader compared the extension against ".item" while
		// FileExtensionFromPath strips the dot, so it matched nothing and loaded
		// zero items. Load() is called from GameManager::init() before the scene
		// exists, and neither manager is guaranteed to be up at that point.
		if (!def.icon.empty() && RenderManager::Get() && RenderManager::Get()->driver())
			def.iconTexture = RenderManager::Get()->driver()->getTexture(_asset_tex(def.icon).c_str());

		if (!def.script.empty())
		{
			def.scriptComponent.script = def.script;

			if (WorldManager::Get() && WorldManager::Get()->scriptSystem())
				WorldManager::Get()->scriptSystem()->compile(def.scriptComponent);
			else
				spdlog::warn("ItemDatabase: script system not ready — '{}' has no usable onUse", def.id);
		}

		g_ItemList.emplace_back(std::move(def));
	}

	g_Loaded = true;

	spdlog::info("ItemDatabase: loaded {} item definition(s)", g_ItemList.size());
}

const ItemDef& ItemDatabase::Get(const std::string& id)
{
	for (auto& def : g_ItemList)
	{
		if (def.id == id)
			return def;
	}

	return g_NullItem;
}

bool ItemDatabase::Exists(const std::string& id)
{
	return Get(id).valid();
}

const std::vector<ItemDef>& ItemDatabase::All()
{
	return g_ItemList;
}

std::vector<std::string> ItemDatabase::Categories()
{
	// The canonical three come first and in this order whether or not anything
	// currently uses them, so the tab strip does not reshuffle as the player
	// picks things up. Anything else a .item file names is appended in load
	// order — which is what makes a new category a data-only change.
	static const char* canonical[] = {
		ItemCategory::KEY, ItemCategory::UPGRADE, ItemCategory::CONSUMABLE
	};

	std::vector<std::string> out(std::begin(canonical), std::end(canonical));

	for (auto& def : g_ItemList)
	{
		if (std::find(out.begin(), out.end(), def.category) == out.end())
			out.emplace_back(def.category);
	}

	return out;
}
