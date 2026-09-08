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
	ImGui::InputFloat("Scale", &s_scale, 0.0f, 0.0f, "%.2f");
	ImGui::SetItemTooltip("Multiplier on the model's natural height.\n"
	                      "Mixamo's centimetre unit scale is normalised out first,\n"
	                      "so 1.00 gives a real-world-sized character (~1.7 units).");

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
