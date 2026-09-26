#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Engine/Resource/FilePaths.h"
#include "Utility/ProcessRunner.h"
#include "Utility/Utility.h"

#include <IMGUI/imgui.h>
#include <spdlog/spdlog.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Windows.h defines these as macros and the project does not set NOMINMAX.
#undef max
#undef min

// ---------------------------------------------------------------------------
// Mixamo Importer — Phase 1
//
// Drives the verified CLI pipeline (Tools/mixamo_to_glb.py) through
// ProcessRunner and streams its output into this panel. The clip grid, material
// reassignment and animated preview are Phase 3/4; this exists to prove the
// process plumbing against a pipeline already known to work.
//
// See "To Do Lists/mixamo_importer_plan.md".
// ---------------------------------------------------------------------------

namespace
{
	ProcessRunner s_proc;

	char  s_folderBuf[512] = {};
	int   s_sizeIndex      = 1;          // index into k_textureSizes
	float s_scale          = 1.0f;
	bool  s_flipY          = false;
	bool  s_flipGreen      = false;
	bool  s_loadedSaved    = false;      // panel was primed from mixamo_convert.json

	const int   k_textureSizes[]      = { 512, 1024, 2048, 4096 };
	const char* k_textureSizeLabels[] = { "512", "1024", "2048", "4096" };

	std::vector<std::string> s_log;
	bool s_scrollLogToBottom = false;

	// Folder contents, refreshed when the path changes.
	std::vector<std::string> s_fbxFiles;
	int         s_textureCount = 0;
	std::string s_scannedPath;

	// Sticky result banner, kept after the process exits.
	enum class Result { None, Success, Failed, Cancelled };
	Result      s_result = Result::None;
	std::string s_resultDetail;

	bool hasExtension(const std::string& name, const char* ext)
	{
		const size_t n = strlen(ext);
		if (name.size() < n) return false;
		return _stricmp(name.c_str() + name.size() - n, ext) == 0;
	}

	// C++14 here (the project default is stdcpp14), so no std::filesystem.
	// mixamo_to_glb.py writes mixamo_convert.json beside the FBXs recording what
	// the folder was last converted with. Priming the panel from it is what stops
	// this button quietly re-converting an asset at the wrong scale: the paladin
	// needs 0.18412, and the 1.0 default produced a 12.9-unit character.
	//
	// Six flat scalars do not justify pulling simdjson (and C++17) into this
	// file, so the value is lifted out by hand.
	bool readJsonScalar(const std::string& text, const char* key, std::string& out)
	{
		const std::string needle = "\"" + std::string(key) + "\"";
		const size_t k = text.find(needle);
		if (k == std::string::npos)
			return false;
		const size_t colon = text.find(':', k + needle.size());
		if (colon == std::string::npos)
			return false;
		size_t b = text.find_first_not_of(" \t\r\n", colon + 1);
		if (b == std::string::npos)
			return false;

		size_t e;
		if (text[b] == '"')
		{
			++b;
			e = text.find('"', b);
		}
		else
		{
			e = text.find_first_of(",}\r\n", b);
		}
		if (e == std::string::npos || e < b)
			return false;

		out = text.substr(b, e - b);
		while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))
			out.pop_back();
		return !out.empty();
	}

	void loadSavedSettings(const std::string& folder)
	{
		s_loadedSaved = false;
		if (folder.empty())
			return;

		std::string path = folder;
		if (path.back() != '\\' && path.back() != '/')
			path += '\\';
		path += "mixamo_convert.json";

		std::ifstream in(path.c_str());
		if (!in)
			return;

		std::ostringstream buf;
		buf << in.rdbuf();
		const std::string text = buf.str();

		std::string value;
		if (readJsonScalar(text, "scale", value))
		{
			try { s_scale = std::stof(value); } catch (...) {}
		}
		if (readJsonScalar(text, "size", value))
		{
			int px = 0;
			try { px = std::stoi(value); } catch (...) {}
			for (int i = 0; i < IM_ARRAYSIZE(k_textureSizes); ++i)
				if (k_textureSizes[i] == px)
					s_sizeIndex = i;
		}
		if (readJsonScalar(text, "flip_y", value))
			s_flipY = (value == "true");
		if (readJsonScalar(text, "flip_green", value))
			s_flipGreen = (value == "true");

		s_loadedSaved = true;
		spdlog::info("Mixamo importer: primed from {} (scale {:.5f}, size {})",
			path, s_scale, k_textureSizes[s_sizeIndex]);
	}

	void scanFolder(const std::string& folder)
	{
		s_fbxFiles.clear();
		s_textureCount = 0;
		s_scannedPath  = folder;

		if (folder.empty())
			return;

		std::string pattern = folder;
		if (pattern.back() != '\\' && pattern.back() != '/')
			pattern += '\\';
		pattern += '*';

		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;

			const std::string name = fd.cFileName;
			if (hasExtension(name, ".fbx"))
				s_fbxFiles.push_back(name);
			else if (hasExtension(name, ".png") || hasExtension(name, ".jpg") ||
			         hasExtension(name, ".jpeg") || hasExtension(name, ".tga"))
				++s_textureCount;
		}
		while (FindNextFileA(h, &fd));

		FindClose(h);
		std::sort(s_fbxFiles.begin(), s_fbxFiles.end());

		loadSavedSettings(folder);
	}

	std::string scriptPath()
	{
		return Utility::ExecutableDirectory() + "..\\Tools\\mixamo_to_glb.py";
	}

	bool scriptExists()
	{
		return GetFileAttributesA(scriptPath().c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	void startImport()
	{
		s_log.clear();
		s_result = Result::None;
		s_resultDetail.clear();

		std::vector<std::string> args;
		args.push_back("-X");
		args.push_back("utf8");          // force UTF-8 stdout regardless of console codepage
		args.push_back(scriptPath());
		args.push_back(s_folderBuf);
		args.push_back("--size");
		args.push_back(std::to_string(k_textureSizes[s_sizeIndex]));
		args.push_back("--scale");
		args.push_back(std::to_string(s_scale));
		if (s_flipY)     args.push_back("--flip-y");
		if (s_flipGreen) args.push_back("--flip-green");

		if (!s_proc.start("python", args, Utility::ExecutableDirectory()))
		{
			s_result       = Result::Failed;
			s_resultDetail = s_proc.lastError();
			s_log.push_back("ERROR: " + s_resultDetail);
		}
	}
}

void EditorInterface::draw_window_mixamo_importer()
{
	if (!m_windowData.draw_window_mixamo_importer)
		return;

	// Drain the child's output every frame, whether or not the window is focused,
	// so the pipe never backs up and the log stays complete.
	const bool wasRunning = s_proc.isRunning();
	{
		std::vector<std::string> fresh = s_proc.drainOutput();
		for (size_t i = 0; i < fresh.size(); ++i)
		{
			spdlog::info("[mixamo] {}", fresh[i]);
			s_log.push_back(fresh[i]);
		}
		if (!fresh.empty())
			s_scrollLogToBottom = true;
	}

	// Latch the outcome on the frame the process finishes.
	if (wasRunning && !s_proc.isRunning())
	{
		if (s_proc.wasCancelled())
		{
			s_result       = Result::Cancelled;
			s_resultDetail = "Import cancelled.";
		}
		else if (s_proc.exitCode() == 0)
		{
			s_result       = Result::Success;
			s_resultDetail = "Import complete.";
			spdlog::info("Mixamo importer: finished successfully");
		}
		else
		{
			s_result       = Result::Failed;
			s_resultDetail = "Tool exited with code " + std::to_string(s_proc.exitCode()) + ".";
			spdlog::error("Mixamo importer: {}", s_resultDetail);
		}
	}

	if (!ImGui::Begin("Mixamo Importer", &m_windowData.draw_window_mixamo_importer))
	{
		ImGui::End();
		return;
	}

	const bool running = s_proc.isRunning();

	// ---- Source folder ----------------------------------------------------
	ImGui::TextDisabled("Source folder containing the per-clip FBX exports and their textures.");

	ImGui::BeginDisabled(running);
	ImGui::SetNextItemWidth(-90.0f);
	ImGui::InputText("##mx_folder", s_folderBuf, sizeof(s_folderBuf));
	ImGui::SameLine();
	if (ImGui::Button("Browse...", ImVec2(80.0f, 0.0f)))
	{
		const std::string start = (s_folderBuf[0] != '\0')
			? std::string(s_folderBuf)
			: Utility::ExecutableDirectory() + g_mesh_path;

		const std::string picked = Utility::OpenFolderDialog("Select Mixamo export folder", start.c_str());
		if (!picked.empty())
		{
			strncpy(s_folderBuf, picked.c_str(), sizeof(s_folderBuf) - 1);
			s_folderBuf[sizeof(s_folderBuf) - 1] = '\0';
		}
	}
	ImGui::EndDisabled();

	if (s_scannedPath != s_folderBuf)
		scanFolder(s_folderBuf);

	if (s_folderBuf[0] != '\0')
	{
		if (s_fbxFiles.empty())
		{
			ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "No FBX files found in this folder.");
		}
		else
		{
			ImGui::Text("%d FBX clip%s, %d texture%s",
			            static_cast<int>(s_fbxFiles.size()), s_fbxFiles.size() == 1 ? "" : "s",
			            s_textureCount, s_textureCount == 1 ? "" : "s");
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				for (size_t i = 0; i < s_fbxFiles.size(); ++i)
					ImGui::TextUnformatted(s_fbxFiles[i].c_str());
				ImGui::EndTooltip();
			}
			if (s_textureCount == 0)
				ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
				                   "No textures here - the character will import untextured.");
		}
	}

	ImGui::Separator();

	// ---- Options ----------------------------------------------------------
	ImGui::BeginDisabled(running);

	ImGui::SetNextItemWidth(90.0f);
	ImGui::Combo("Texture size", &s_sizeIndex, k_textureSizeLabels, IM_ARRAYSIZE(k_textureSizeLabels));
	ImGui::SetItemTooltip("Longest edge after resizing. Textures smaller than this are left alone.");

	ImGui::SetNextItemWidth(90.0f);
	// 5dp: the per-asset scales that matter here are small and exact (0.18412),
	// and %.2f would silently round one to 0.18.
	ImGui::InputFloat("Scale", &s_scale, 0.0f, 0.0f, "%.5f");
	ImGui::SetItemTooltip("Multiplier on the model's natural height.\n"
	                      "The centimetre unit scale is normalised out first, so 1.00\n"
	                      "USUALLY gives a real-world-sized character (~1.7 units) -\n"
	                      "but that normalisation fails on some rigs. Always check the\n"
	                      "'model extent' line in the log against a known character.");

	if (s_loadedSaved)
	{
		ImGui::TextColored(ImVec4(0.55f, 0.80f, 0.55f, 1.0f),
			"Settings loaded from this folder's mixamo_convert.json");
		ImGui::SetItemTooltip("This folder has been converted before, so the values above\n"
		                      "are the ones it was last built with rather than the defaults.\n"
		                      "Change them here to re-convert differently; the file is\n"
		                      "rewritten after every successful run.");
	}

	ImGui::Checkbox("Flip Y", &s_flipY);
	ImGui::SetItemTooltip("Rotate 180 degrees. Use if the character faces backwards in-engine.");
	ImGui::SameLine();
	ImGui::Checkbox("Flip normal green", &s_flipGreen);
	ImGui::SetItemTooltip("Invert the normal map green channel (DirectX -> OpenGL).");

	ImGui::EndDisabled();

	ImGui::Separator();

	// ---- Run / cancel -----------------------------------------------------
	const bool canRun = !running && !s_fbxFiles.empty() && scriptExists();

	ImGui::BeginDisabled(!canRun);
	if (ImGui::Button("Import", ImVec2(90.0f, 0.0f)))
		startImport();
	ImGui::EndDisabled();

	if (!scriptExists())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Tools/mixamo_to_glb.py not found");
		ImGui::SetItemTooltip("%s", scriptPath().c_str());
	}

	if (running)
	{
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
			s_proc.cancel();

		ImGui::SameLine();
		if (s_proc.hasProgress())
		{
			char label[32];
			snprintf(label, sizeof(label), "%.0f%%", s_proc.progress() * 100.0f);
			ImGui::ProgressBar(s_proc.progress(), ImVec2(-1.0f, 0.0f), label);
		}
		else
		{
			// The CLI script reports no progress markers, so animate an
			// indeterminate sweep rather than showing a stuck bar at 0%.
			const float t = static_cast<float>(ImGui::GetTime());
			const float sweep = 0.5f - 0.5f * cosf(t * 3.0f);
			const std::string stage = s_proc.stage();
			ImGui::ProgressBar(sweep, ImVec2(-1.0f, 0.0f),
			                   stage.empty() ? "working..." : stage.c_str());
		}
	}
	else if (s_result != Result::None)
	{
		ImGui::SameLine();
		const ImVec4 col = (s_result == Result::Success) ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
		                 : (s_result == Result::Cancelled) ? ImVec4(0.80f, 0.80f, 0.45f, 1.0f)
		                                                   : ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
		ImGui::TextColored(col, "%s", s_resultDetail.c_str());
	}

	// ---- Output -----------------------------------------------------------
	ImGui::Separator();
	ImGui::TextDisabled("Output");

	ImGui::BeginChild("##mx_log", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
	for (size_t i = 0; i < s_log.size(); ++i)
	{
		const std::string& line = s_log[i];
		const bool isError = line.find("ERROR") != std::string::npos ||
		                     line.find("Traceback") != std::string::npos;
		const bool isWarn  = line.find("WARNING") != std::string::npos;

		if (isError)      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
		else if (isWarn)  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.40f, 1.0f));

		ImGui::TextUnformatted(line.c_str());

		if (isError || isWarn)
			ImGui::PopStyleColor();
	}
	if (s_scrollLogToBottom)
	{
		ImGui::SetScrollHereY(1.0f);
		s_scrollLogToBottom = false;
	}
	ImGui::EndChild();

	ImGui::End();
}
