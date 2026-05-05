#include "AnimatedSpriteLoader.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"

#include "Utility/INIFile.h"
#include "Utility/Utility.h"

#include "Engine/Resource/FilePaths.h"

using namespace boost;
using namespace filesystem;
using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

void AnimatedSpriteLoader::loadAnimatedSprites()
{
	const path dir = "content/texture/sprite/animated/";
	recursive_directory_iterator it(dir), end;

	std::vector<std::wstring> files;
	for (auto& entry : make_iterator_range(it, end)) {
		if (is_regular(entry)) {
			files.emplace_back(entry.path().native());
		}
	}

	auto iter = 0U;
	for (auto& file : files) {
		const auto
			filepath = std::string(file.begin(), file.end()),
			filename = Utility::FilenameFromPath(filepath),
			file_ext = Utility::FileExtensionFromPath(filepath);

		if (file_ext == std::string(".sprite"))
		{
			CIniFile configLoader;
			if (!configLoader.Load(std::string("content/texture/sprite/animated/") + filename + file_ext)) {
				continue;
			}

			AnimatedSprite sprite;
			sprite.id = iter;
			sprite.name = configLoader.GetKeyValue("sprite", "name");
			sprite.file = configLoader.GetKeyValue("sprite", "file");
			sprite.frame_x = atoi(configLoader.GetKeyValue("sprite", "frame_x").c_str());
			sprite.frame_y = atoi(configLoader.GetKeyValue("sprite", "frame_y").c_str());

			auto source_image = RenderManager::Get()->driver()->createImageFromFile(
				std::string(std::string("content/texture/sprite/animated/") + sprite.file).c_str());
			
			IImage* temp_image = RenderManager::Get()->driver()->createImage(
				source_image->getColorFormat(), irr::core::dimension2d<u32>(sprite.frame_x, sprite.frame_y));

			if (sprite.frame_x == 0 || sprite.frame_y == 0)
			{
				sprite.texture_list.push_back(RenderManager::Get()->driver()->addTexture("error", source_image));
				m_animatedSpriteList.emplace_back(sprite);
				continue;
			}

			auto counter = 0;
			for (auto i = 0U; i < source_image->getDimension().Height / sprite.frame_y; i++) {
				for (auto j = 0U; j < source_image->getDimension().Width / sprite.frame_x; j++) {

					source_image->copyTo(temp_image, irr::core::vector2d<s32>(0, 0), irr::core::rect<int>(
						sprite.frame_x * j, sprite.frame_y * i, sprite.frame_x * (i + 8), sprite.frame_y * (j + 8)));

					sprite.texture_list.push_back(RenderManager::Get()->driver()->addTexture(std::to_string(counter++).c_str(), temp_image));
				}
			}

			temp_image->drop();
			source_image->drop();

			m_animatedSpriteList.emplace_back(sprite);
			iter++;
		}

	}
}

AnimatedSprite AnimatedSpriteLoader::getAnimatedSprite(const std::string& name)
{
	for (auto sprite : m_animatedSpriteList)
	{
		if (sprite.name == name)
		{
			return sprite;
		}
	}

	spdlog::error("Sprite does not exist \'{0}\' in AnimatedSpriteLoader::getAnimatedSprite", name);
	for (auto sprite : m_animatedSpriteList)
	{
		if (sprite.name == "error")
		{
			return sprite;
		}
	}
}