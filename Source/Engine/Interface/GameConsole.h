#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <IMGUI/imgui.h>

class asIScriptContext;

// ---------------------------------------------------------------------------
// GameConsole — Quake 3-style drop-down console.
//
// Slide-down overlay (top ~45% of the screen), "]" prompt with command
// history (up/down) and tab completion, a registered-command table, cvar
// get/set fallback (WorldManager's GlobalCVar store), engine log mirroring
// (ImGuiLogSink), and arbitrary AngelScript execution ("script <code>" or a
// leading '>').
//
// Engine owns one instance: setOpen() follows the tilde toggle and draw(dt)
// is called every frame in game mode with REAL (variable) frame time so the
// slide animation is unaffected by timescale/hit-stop. Input blocking while
// open is handled by Engine (consoleBlocking), not here.
// ---------------------------------------------------------------------------

class GameConsole
{
public:
	GameConsole();
	~GameConsole();

	void draw(float dtMs);
	void setOpen(bool open);
	bool isOpen() const { return m_open; }

	void clearInputBuffer();

	// Scrollback output (bounded deque; oldest lines dropped)
	void print(const std::string& text);                    // white — command output
	void printLine(const ImVec4& color, const std::string& text);

	// Register a console command. Handler receives the whitespace-split args
	// (excluding the command name) AND the raw untokenized remainder of the
	// line (for commands like 'echo'/'script' that need spaces preserved).
	void registerCommand(const std::string& name, const std::string& help,
		std::function<void(const std::vector<std::string>&, const std::string&)> handler);

	void executeCommand(const std::string& line);

private:
	struct Command
	{
		std::string help;
		std::function<void(const std::vector<std::string>&, const std::string&)> handler;
	};

	struct Line
	{
		ImVec4 color;
		std::string text;
	};

	void registerBuiltins();
	void drainEngineLog();
	void runScript(const std::string& code);

	static int textEditCallbackStub(ImGuiInputTextCallbackData* data);
	int textEditCallback(ImGuiInputTextCallbackData* data);

	std::map<std::string, Command> m_commands;  // sorted — powers help + completion

	std::deque<Line> m_lines;
	static constexpr size_t k_maxLines = 1000;

	std::vector<std::string> m_history;
	int m_historyPos = -1;                      // -1 = editing a fresh line

	char m_inputBuffer[512];

	uint64_t m_logSeq = 0;                      // ImGuiLogSink drain position

	bool  m_open = false;
	float m_slide = 0.0f;                       // 0 = retracted, 1 = fully open
	bool  m_focusInput = false;
	bool  m_scrollToBottom = false;

	asIScriptContext* m_scriptCtx = nullptr;    // reused so exceptions are readable
};
