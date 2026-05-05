#include "DialogManager.h"

#include <boost/range/iterator_range.hpp>
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>
#include <tinyxml2/tinyxml2.h>

#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/World/WorldManager.h"
#include "Game/Components.h"
#include "Player/PlayerController.h"

using namespace boost;
using namespace filesystem;
using namespace tinyxml2;

std::vector<DialogData> DialogManager::m_dialogs;

void DialogManager::LoadDialogs()
{
	const path dir = "content/dialog/";
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

		if (file_ext == std::string(".dialog")) {
			XMLDocument dialog_xml;

			if (dialog_xml.LoadFile(std::string(std::string("content/dialog/") + filename + file_ext).c_str()) != XML_NO_ERROR) {
				spdlog::warn("Failed to load dialog file \'" + std::string(std::string("content/dialog/") + filename + file_ext + "\' "));

				continue;
			}

			DialogData data;
			DialogEntry entry;

			data.name = filename;

			auto dialog_root = dialog_xml.FirstChild()->NextSibling();
			auto dialog_value = dialog_root->FirstChildElement();

			for (;
				dialog_value != nullptr;
				dialog_value = dialog_value->NextSiblingElement())
			{
				if (std::string(dialog_value->Name()) == "data")
				{
					auto anim_subvalue = dialog_value->FirstChildElement();

					for (;
						anim_subvalue != nullptr;
						anim_subvalue = anim_subvalue->NextSiblingElement())
					{
						std::string dialog_text     = anim_subvalue->GetText(),
								    dialog_requires = anim_subvalue->Attribute("requires");

						unsigned int id = 0U;

						if (anim_subvalue->Attribute("id") != nullptr)
						{
							id = atoi(anim_subvalue->Attribute("id"));
						}

						entry.id = id;
						entry.requirements = dialog_requires;
						entry.text = dialog_text;

						data.entries.push_back(entry);
					}
				}
			}

			m_dialogs.push_back(data);

			dialog_xml.Clear();
		}
	}
}
