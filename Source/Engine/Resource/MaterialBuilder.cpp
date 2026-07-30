#include "Engine/Resource/MaterialBuilder.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

#include <spdlog/spdlog.h>

#include "Utility/Utility.h"

using namespace std;
using namespace boost;
using namespace filesystem;

void MaterialBuilder::buildMaterialTable()
{
	if (!m_materialMap.empty()) return;

	static const pair<const char*, E_MANAGED_MATERIAL> k_patterns[] = {
		{ "(dev)",                         MAT_INVALID },
		{ "(grass|dirt|earth|sand|snow)",  MAT_EARTH   },
		{ "(gravel|pebbles)",              MAT_GRAVEL  },
		{ "(sludge|mud|goo|water|wet)",    MAT_WATER   },
		{ "(concrete|brick|rock|stone)",   MAT_STONE   },
		{ "(metal|iron|steel)",            MAT_METAL   },
		{ "(glass|tile|ceramic|ice)",      MAT_GLASS   },
		{ "(carpet|soft|felt)",            MAT_CARPET  },
		{ "(wood|plank|bark|board)",       MAT_WOOD    },
	};

	// Compile regex objects once at startup.
	static vector<pair<regex, E_MANAGED_MATERIAL>> k_compiled;
	if (k_compiled.empty())
	{
		for (const auto& p : k_patterns)
			k_compiled.emplace_back(regex(p.first), p.second);
	}

	const path dir = "content/";
	recursive_directory_iterator it(dir), end;

	for (auto& entry : make_iterator_range(it, end))
	{
		if (!is_regular_file(entry)) continue;

		const auto filepath = string(entry.path().native().begin(), entry.path().native().end());
		const auto filename = Utility::FilenameFromPath(filepath);
		const auto file_ext = Utility::FileExtensionFromPath(filepath);

		if (m_materialMap.count(filename)) continue;

		if (file_ext != "png" && file_ext != "jpg" && file_ext != "bmp" &&
		    file_ext != "dds" && file_ext != "tga")
			continue;

		smatch result;
		for (const auto& compiled : k_compiled)
		{
			if (regex_search(filename, result, compiled.first))
			{
				m_materialMap.emplace(filename, compiled.second);
				//spdlog::debug("Material Builder Loaded: '{}' as material '{}'", filename, static_cast<int>(compiled.second));
				break;
			}
		}
	}
}

std::string MaterialBuilder::getMaterialName(E_MANAGED_MATERIAL material) const
{
	int idx = static_cast<int>(material);
	if (idx < 0 || idx >= static_cast<int>(g_managedMaterialName.size()))
		return g_managedMaterialName[0];
	return g_managedMaterialName[idx];
}

E_MANAGED_MATERIAL MaterialBuilder::getMaterialFromTexture(const std::string& texture) const
{
	auto it = m_materialMap.find(texture);
	return it != m_materialMap.end() ? it->second : MAT_INVALID;
}

std::string MaterialBuilder::getMaterialNameFromTexture(const std::string& texture) const
{
	auto it = m_materialMap.find(texture);
	if (it != m_materialMap.end())
		return g_managedMaterialName[static_cast<int>(it->second)];
	return g_managedMaterialName[0];
}
