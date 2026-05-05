#pragma once

#include <string>

struct DebugWindowData
{
	bool draw_menubar_main = true;
};

namespace DebugInterface
{
	void draw();

	void detectKeyShortcuts();

	void draw_menubar_main();
};
