#pragma once

#include "Engine/Types.h"

#include <anax/anax.hpp>

struct DialogEntry
{
	unsigned int id = 0U;

	std::string text, requirements;
};

struct DialogData
{
	std::string name;

	std::vector<DialogEntry> entries;
};

class DialogManager
{
public:
	static void LoadDialogs();

	static DialogData GetDialog(std::string name)
	{
		for (auto i = 0U; i < m_dialogs.size(); i++)
		{
			if (m_dialogs[i].name == name)
			{
				return m_dialogs.at(i);
			}
		}
	}

private:
	static std::vector<DialogData> m_dialogs;

};