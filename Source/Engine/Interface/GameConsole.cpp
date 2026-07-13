#include "Engine/Interface/GameConsole.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "Engine/Brush/BrushGeometry.h"
#include "Engine/Engine.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Script/ScriptManager.h"
#include "Editor/ImGuiLogSink.h"

#include "Game/Components.h"

#include "angelscript.h"
#include "angelscript/sdk/add_on/scripthelper/scripthelper.h"

// Line colors
static const ImVec4 kColEcho    (0.55f, 0.55f, 0.55f, 1.0f); // "]command" echo
static const ImVec4 kColOutput  (0.90f, 0.90f, 0.90f, 1.0f); // command output
static const ImVec4 kColWarn    (1.00f, 0.85f, 0.30f, 1.0f); // spdlog warn
static const ImVec4 kColError   (1.00f, 0.35f, 0.35f, 1.0f); // spdlog err / script errors
static const ImVec4 kColLogDim  (0.50f, 0.58f, 0.65f, 1.0f); // spdlog info/debug/trace
static const ImVec4 kColCVar    (0.40f, 0.85f, 0.90f, 1.0f); // cvar prints
static const ImVec4 kColAccent  (0.35f, 0.55f, 0.95f, 1.0f); // bottom edge / prompt

static std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

GameConsole::GameConsole()
{
	clearInputBuffer();
	registerBuiltins();
}

GameConsole::~GameConsole()
{
	if (m_scriptCtx)
	{
		m_scriptCtx->Release();
		m_scriptCtx = nullptr;
	}
}

void GameConsole::setOpen(bool open)
{
	if (open && !m_open)
	{
		m_focusInput = true;
		m_scrollToBottom = true;
	}
	m_open = open;
}

void GameConsole::clearInputBuffer()
{
	std::fill(m_inputBuffer, m_inputBuffer + sizeof m_inputBuffer, '\0');
}

void GameConsole::printLine(const ImVec4& color, const std::string& text)
{
	while (m_lines.size() >= k_maxLines)
		m_lines.pop_front();
	m_lines.push_back({ color, text });
}

void GameConsole::print(const std::string& text)
{
	printLine(kColOutput, text);
}

void GameConsole::registerCommand(const std::string& name, const std::string& help,
	std::function<void(const std::vector<std::string>&, const std::string&)> handler)
{
	m_commands[name] = { help, std::move(handler) };
}

// --- Engine log mirroring ----------------------------------------------------

void GameConsole::drainEngineLog()
{
	std::vector<ImGuiLogEntry> fresh;
	m_logSeq = ImGuiLogSink::instance()->entriesSince(m_logSeq, fresh);

	for (auto& entry : fresh)
	{
		// The spdlog formatter appends a newline — strip it
		std::string text = entry.text;
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();

		ImVec4 color = kColLogDim;
		if (entry.level == spdlog::level::warn)
			color = kColWarn;
		else if (entry.level >= spdlog::level::err)
			color = kColError;

		printLine(color, text);
	}
}

// --- Command execution ---------------------------------------------------------

void GameConsole::executeCommand(const std::string& rawLine)
{
	std::string line = trim(rawLine);
	if (line.empty())
		return;

	printLine(kColEcho, "]" + line);
	m_scrollToBottom = true;

	// Session history (skip consecutive duplicates)
	if (m_history.empty() || m_history.back() != line)
		m_history.push_back(line);
	m_historyPos = -1;

	// Script shorthand: >engine::setTimeScale(0.5);
	if (line[0] == '>')
	{
		runScript(trim(line.substr(1)));
		return;
	}

	// Split into tokens; also keep the raw remainder after the command name
	std::istringstream buffer(line);
	std::vector<std::string> tokens((std::istream_iterator<std::string>(buffer)),
	                                 std::istream_iterator<std::string>());
	if (tokens.empty())
		return;

	const std::string& cmd = tokens[0];
	std::vector<std::string> args(tokens.begin() + 1, tokens.end());

	std::string rawArgs;
	{
		size_t afterCmd = line.find_first_of(" \t");
		if (afterCmd != std::string::npos)
			rawArgs = trim(line.substr(afterCmd));
	}

	auto it = m_commands.find(cmd);
	if (it != m_commands.end())
	{
		it->second.handler(args, rawArgs);
		return;
	}

	// CVar fallback (Quake semantics): bare name prints, "name value" sets
	if (WorldManager::Get()->getCVarExists(cmd))
	{
		if (!rawArgs.empty())
		{
			WorldManager::Get()->setCVar(cmd, rawArgs);
			printLine(kColCVar, "\"" + cmd + "\" set to \"" + rawArgs + "\"");
		}
		else
		{
			printLine(kColCVar, "\"" + cmd + "\" is: \"" + WorldManager::Get()->getCVarValue(cmd) + "\"");
		}
		return;
	}

	printLine(kColError, "Unknown command \"" + cmd + "\"");
}

// --- AngelScript execution ------------------------------------------------------

void GameConsole::runScript(const std::string& code)
{
	if (code.empty())
		return;

	if (!ScriptManager::Get() || !ScriptManager::Get()->getEngine())
	{
		printLine(kColError, "Script engine not available");
		return;
	}

	asIScriptEngine* engine = ScriptManager::Get()->getEngine();

	if (!m_scriptCtx)
		m_scriptCtx = engine->CreateContext();

	// Compile errors go through the engine's message callback -> spdlog ->
	// ImGuiLogSink -> mirrored into this scrollback in red on the next draw.
	int r = ExecuteString(engine, code.c_str(), nullptr, m_scriptCtx);

	if (r == asEXECUTION_EXCEPTION)
	{
		printLine(kColError, std::string("Script exception: ") + m_scriptCtx->GetExceptionString()
			+ " (line " + std::to_string(m_scriptCtx->GetExceptionLineNumber()) + ")");
	}
	else if (r < 0)
	{
		printLine(kColError, "Script compile error");
	}
}

// --- Built-in commands ------------------------------------------------------------

void GameConsole::registerBuiltins()
{
	registerCommand("help", "list all commands",
		[this](const std::vector<std::string>&, const std::string&)
	{
		for (auto& pair : m_commands)
			print("  " + pair.first + " - " + pair.second.help);
		print("Any registered cvar: <name> prints, <name> <value> sets");
		print("AngelScript: script <code>, or prefix a line with >");
	});
	m_commands["cmdlist"] = m_commands["help"];

	registerCommand("clear", "clear the scrollback",
		[this](const std::vector<std::string>&, const std::string&)
	{
		m_lines.clear();
	});
	m_commands["cls"] = m_commands["clear"];

	registerCommand("echo", "print text",
		[this](const std::vector<std::string>&, const std::string& raw)
	{
		print(raw);
	});

	registerCommand("quit", "exit the game",
		[](const std::vector<std::string>&, const std::string&)
	{
		Engine::Get()->exit();
	});

	registerCommand("cvarlist", "list all cvars",
		[this](const std::vector<std::string>&, const std::string&)
	{
		const auto& cvars = WorldManager::Get()->getCVars();
		if (cvars.empty())
		{
			print("no cvars set");
			return;
		}
		for (const auto& cvar : cvars)
			printLine(kColCVar, "  " + cvar.name + " = \"" + cvar.value + "\"");
	});

	registerCommand("timescale", "[0.1-2.0] set world speed (bullet time); no arg prints",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (!args.empty())
			Engine::Get()->setTimeScale(static_cast<float>(atof(args[0].c_str())));
		print("Time scale: " + std::to_string(Engine::Get()->getTimeScale()));
	});

	registerCommand("hitstop", "<ms> freeze the world briefly (real-time recovery)",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (args.empty())
		{
			printLine(kColError, "usage: hitstop <ms>");
			return;
		}
		Engine::Get()->requestHitStop(static_cast<float>(atof(args[0].c_str())));
	});

	registerCommand("script", "<code> execute AngelScript (also: prefix line with >)",
		[this](const std::vector<std::string>&, const std::string& raw)
	{
		runScript(raw);
	});

	registerCommand("brush_test", "run the CSG brush geometry self-tests",
		[this](const std::vector<std::string>&, const std::string&)
	{
		const int failures = BrushGeometry::runSelfTests();
		if (failures == 0)
			print("BrushGeometry self-tests: all passed");
		else
			printLine(kColError, "BrushGeometry self-tests: " + std::to_string(failures) + " failure(s) — see log");
	});

	registerCommand("brush_add", "[size] add a test CSG box brush at the crosshair",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		auto* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (!cam || !BrushManager::Get())
		{
			printLine(kColError, "no active camera / brush manager");
			return;
		}

		irr::core::vector3df target = cam->getAbsolutePosition() +
			Math::GetDirectionVector(cam->getRotation(), true) * 10.0f;
		auto rdata = PhysicsManager::Get()->raycast(
			cam->getAbsolutePosition(),
			Math::GetDirectionVector(cam->getRotation(), true),
			100.0);
		if (rdata.hit)
			target = Utility::PxVec3_To_IrrVec3(rdata.data.getAnyHit(0).position);

		const float size = args.empty() ? 2.0f : std::max(0.25f, static_cast<float>(atof(args[0].c_str())));
		const irr::core::vector3df half(size * 0.5f, size * 0.5f, size * 0.5f);
		Brush box = BrushGeometry::makeBox(irr::core::aabbox3df(target - half, target + half));

		const uint32_t id = BrushManager::Get()->addBrush(box);
		if (id == 0)
			printLine(kColError, "brush_add: invalid plane set");
		else
			print("added brush " + std::to_string(id) + " (" + std::to_string(size) + " units)");
	});

	registerCommand("brush_stats", "print CSG brush and chunk counts",
		[this](const std::vector<std::string>&, const std::string&)
	{
		if (!BrushManager::Get())
			return;
		print("brushes: " + std::to_string(BrushManager::Get()->getAllBrushes().size()));
	});

	registerCommand("brush_clear", "remove all CSG brushes",
		[this](const std::vector<std::string>&, const std::string&)
	{
		if (!BrushManager::Get())
			return;
		BrushManager::Get()->clearAll();
		print("brushes cleared");
	});

	registerCommand("stats", "toggle the stats overlay",
		[](const std::vector<std::string>&, const std::string&)
	{
		Engine::Get()->setDefaultStatsDrawingEnabled(!Engine::Get()->isDefaultStatsDrawingEnabled());
	});

	registerCommand("debug", "toggle game debug features",
		[](const std::vector<std::string>&, const std::string&)
	{
		Engine::Get()->setGameDebugFeaturesEnabled(!Engine::Get()->isGameDebugFeaturesEnabled());
	});

	registerCommand("scene", "<name> load a scene as a fresh game state",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (args.empty())
		{
			printLine(kColError, "usage: scene <name>");
			return;
		}
		Engine::Get()->stateManager()->destroyState(ENGINE_STATE_ID::ESID_GAME);
		Engine::Get()->stateManager()->initState(ENGINE_STATE_ID::ESID_GAME, _asset_scn(args[0]));
	});

	registerCommand("spawn", "<entity> [count] spawn entity file at the crosshair",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (args.empty())
		{
			printLine(kColError, "usage: spawn <entity> [count]");
			return;
		}

		auto rdata = PhysicsManager::Get()->raycast(
			RenderManager::Get()->sceneManager()->getActiveCamera()->getAbsolutePosition(),
			Math::GetDirectionVector(RenderManager::Get()->sceneManager()->getActiveCamera()->getRotation(), true),
			100.0);

		irr::core::vector3df target;
		if (rdata.hit)
			target = Utility::PxVec3_To_IrrVec3(rdata.data.getAnyHit(0).position);

		int count = args.size() >= 2 ? std::max(1, atoi(args[1].c_str())) : 1;
		for (int i = 0; i < count; i++)
		{
			if (!WorldManager::Get()->spawnEntity(_asset_ent(args[0]), "", false, target))
			{
				printLine(kColError, "Entity '" + args[0] + "' not found");
				return;
			}
		}
		print("spawned " + std::to_string(count) + "x " + args[0]);
	});

	registerCommand("damage", "<entity-name> <amount> damage an entity",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (args.size() < 2)
		{
			printLine(kColError, "usage: damage <entity-name> <amount>");
			return;
		}
		anax::Entity& entity = WorldManager::Get()->managerSystem()->getEntityByName(args[0]);
		if (!entity.isValid() || !entity.hasComponent<DescriptorComponent>())
		{
			printLine(kColError, "Entity '" + args[0] + "' not found");
			return;
		}
		WorldManager::Get()->gameplaySystem()->damageEntity(
			entity.getComponent<DescriptorComponent>().id,
			static_cast<unsigned int>(atoi(args[1].c_str())));
		print("damaged '" + args[0] + "' for " + args[1]);
	});

	registerCommand("heal", "<entity-name> <amount> heal an entity",
		[this](const std::vector<std::string>& args, const std::string&)
	{
		if (args.size() < 2)
		{
			printLine(kColError, "usage: heal <entity-name> <amount>");
			return;
		}
		anax::Entity& entity = WorldManager::Get()->managerSystem()->getEntityByName(args[0]);
		if (!entity.isValid() || !entity.hasComponent<DescriptorComponent>())
		{
			printLine(kColError, "Entity '" + args[0] + "' not found");
			return;
		}
		WorldManager::Get()->gameplaySystem()->healEntity(
			entity.getComponent<DescriptorComponent>().id,
			static_cast<unsigned int>(atoi(args[1].c_str())));
		print("healed '" + args[0] + "' for " + args[1]);
	});
}

// --- Input line callbacks (history + tab completion + char filter) ---------------

int GameConsole::textEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	return static_cast<GameConsole*>(data->UserData)->textEditCallback(data);
}

int GameConsole::textEditCallback(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCharFilter:
		// The toggle key must never land in the buffer
		if (data->EventChar == '`' || data->EventChar == '~')
			return 1;
		return 0;

	case ImGuiInputTextFlags_CallbackHistory:
	{
		const int prevPos = m_historyPos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (m_historyPos == -1)
				m_historyPos = static_cast<int>(m_history.size()) - 1;
			else if (m_historyPos > 0)
				m_historyPos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (m_historyPos != -1 && ++m_historyPos >= static_cast<int>(m_history.size()))
				m_historyPos = -1;
		}

		if (prevPos != m_historyPos)
		{
			const char* text = (m_historyPos >= 0) ? m_history[m_historyPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, text);
		}
		return 0;
	}

	case ImGuiInputTextFlags_CallbackCompletion:
	{
		// Locate the start of the word being completed
		const char* wordEnd = data->Buf + data->CursorPos;
		const char* wordStart = wordEnd;
		while (wordStart > data->Buf)
		{
			const char c = wordStart[-1];
			if (c == ' ' || c == '\t' || c == '>')
				break;
			wordStart--;
		}
		std::string word(wordStart, wordEnd);
		if (word.empty())
			return 0;

		// Candidates: command names + cvar names
		std::vector<std::string> candidates;
		for (auto& pair : m_commands)
			if (pair.first.compare(0, word.size(), word) == 0)
				candidates.push_back(pair.first);
		for (const auto& cvar : WorldManager::Get()->getCVars())
			if (cvar.name.compare(0, word.size(), word) == 0)
				candidates.push_back(cvar.name);

		if (candidates.empty())
		{
			printLine(kColEcho, "No match for \"" + word + "\"");
			return 0;
		}

		if (candidates.size() == 1)
		{
			data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(word.size()));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
			return 0;
		}

		// Multiple matches: complete to the longest common prefix, list candidates
		std::string prefix = candidates[0];
		for (size_t i = 1; i < candidates.size(); i++)
		{
			size_t j = 0;
			while (j < prefix.size() && j < candidates[i].size() && prefix[j] == candidates[i][j])
				j++;
			prefix.resize(j);
		}

		if (prefix.size() > word.size())
		{
			data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(word.size()));
			data->InsertChars(data->CursorPos, prefix.c_str());
		}

		std::string list;
		for (auto& candidate : candidates)
			list += candidate + "  ";
		printLine(kColEcho, list);
		m_scrollToBottom = true;
		return 0;
	}
	}

	return 0;
}

// --- Drawing ------------------------------------------------------------------------

void GameConsole::draw(float dtMs)
{
	// Slide runs on REAL frame time so the animation is identical during
	// hit-stop / bullet time
	const float slideDurationMs = 150.0f;
	float target = m_open ? 1.0f : 0.0f;
	if (m_slide < target)
		m_slide = std::min(target, m_slide + dtMs / slideDurationMs);
	else if (m_slide > target)
		m_slide = std::max(target, m_slide - dtMs / slideDurationMs);

	if (m_slide <= 0.001f)
		return;

	drainEngineLog();

	const float screenW = static_cast<float>(RenderManager::Get()->getConfiguration().width);
	const float screenH = static_cast<float>(RenderManager::Get()->getConfiguration().height);
	const float height = screenH * 0.45f;

	ImGui::SetNextWindowPos(ImVec2(0.0f, -height * (1.0f - m_slide)));
	ImGui::SetNextWindowSize(ImVec2(screenW, height));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.08f, 0.92f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.02f, 0.03f, 0.05f, 0.90f));
	ImGui::PushStyleColor(ImGuiCol_Separator, kColAccent);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	if (ImGui::Begin("##GameConsole", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
	{
		// Scrollback
		if (ImGui::BeginChild("##scrollback", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

			for (auto& line : m_lines)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, line.color);
				ImGui::TextUnformatted(line.text.c_str());
				ImGui::PopStyleColor();
			}

			// Pin to bottom while the user hasn't scrolled up; force on command
			if (m_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
				ImGui::SetScrollHereY(1.0f);
			m_scrollToBottom = false;

			ImGui::PopStyleVar();
		}
		ImGui::EndChild();

		ImGui::Separator();

		// Input line
		ImGui::PushStyleColor(ImGuiCol_Text, kColAccent);
		ImGui::TextUnformatted("]");
		ImGui::PopStyleColor();
		ImGui::SameLine();

		if (m_focusInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusInput = false;
		}

		ImGui::PushItemWidth(-1.0f);
		const ImGuiInputTextFlags flags =
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_CallbackCompletion |
			ImGuiInputTextFlags_CallbackHistory |
			ImGuiInputTextFlags_CallbackCharFilter;

		if (ImGui::InputText("##consoleinput", m_inputBuffer,
			sizeof m_inputBuffer / sizeof *m_inputBuffer, flags, &textEditCallbackStub, this))
		{
			executeCommand(m_inputBuffer);
			clearInputBuffer();
			m_focusInput = true;
		}
		ImGui::PopItemWidth();
	}
	ImGui::End();

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(3);
}
