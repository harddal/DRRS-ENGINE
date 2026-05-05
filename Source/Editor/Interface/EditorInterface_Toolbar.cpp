#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"

#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Renderer/Lightmapper/LightmapBaker.h"

#include <cstdio>
#include <string>
#include <cmath>
#include <algorithm>

void EditorInterface::draw_toolbar()
{
	if (!m_windowData.draw_toolbar) return;

	ImGuiIO& io   = ImGui::GetIO();
	ImGuiStyle& s = ImGui::GetStyle();

	const float menubar_h = ImGui::GetFrameHeight();
	const float toolbar_h = ImGui::GetFrameHeight() + s.ItemSpacing.y * 2.0f;

	ImGui::SetNextWindowPos(ImVec2(0.0f, menubar_h));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbar_h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s.ItemSpacing.x, s.ItemSpacing.y));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.094f, 0.098f, 0.122f, 1.0f));

	ImGui::Begin("##editor_toolbar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoDocking   | ImGuiWindowFlags_NoSavedSettings   |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();

	const ImVec4 active_btn = ImVec4(0.102f, 0.314f, 0.565f, 1.0f);
	const ImVec4 play_color = ImVec4(0.10f, 0.42f, 0.18f, 1.0f);
	const float  gap        = s.ItemSpacing.x * 2.0f;
	const float  sp         = s.ItemSpacing.x;
	const float  sep_w      = ImGui::CalcTextSize("|").x;

	auto mode  = g_sceneInteractor.getWidgetToolMode();
	auto coord = g_sceneInteractor.getWidgetCoordMode();
	bool snap  = g_sceneInteractor.isSnap();

	// Pre-compute button widths for centering
	auto btnW = [&](const char* label) {
		return ImGui::CalcTextSize(label).x + s.FramePadding.x * 2.0f;
	};
	char snap_val_str[16];
	snprintf(snap_val_str, sizeof(snap_val_str), "%.4g", g_sceneInteractor.getSnapUnit());
	float snap_text_w = ImGui::CalcTextSize(snap_val_str).x;
	const float lm_res_w   = 70.0f;
	const float lm_bias_w  = 55.0f;
	const float lm_prog_w  = 100.0f;
	float total_w =
		btnW("Translate") + sp + btnW("Rotate") + sp + btnW("Scale") +
		gap + sep_w + gap +
		btnW("Local") + sp + btnW("World") +
		gap + sep_w + gap +
		btnW("Snap  ON") + sp + btnW("-") + sp + snap_text_w + sp + btnW("+") +
		gap + sep_w + gap +
		btnW("Res:") + sp + lm_res_w + gap + btnW("Bias:") + sp + lm_bias_w + sp + btnW("Bake") +
		gap + sep_w + gap +
		ImGui::CalcTextSize("NavMesh:").x + sp + ImGui::CalcTextSize("Not baked").x + sp + btnW("Bake NavMesh") +
		gap + sep_w + gap +
		btnW("Play");

	float center_x = (ImGui::GetContentRegionAvail().x - total_w) * 0.5f;
	if (center_x > 0.0f)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x);

	// -- Operation buttons --
	bool tActive = (mode == ImTransformControl::TRANSLATE);
	if (tActive) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button("Translate")) g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::TRANSLATE);
	if (tActive) ImGui::PopStyleColor();

	ImGui::SameLine();
	bool rActive = (mode == ImTransformControl::ROTATE);
	if (rActive) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button("Rotate")) g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::ROTATE);
	if (rActive) ImGui::PopStyleColor();

	ImGui::SameLine();
	bool scActive = (mode == ImTransformControl::SCALE);
	if (scActive) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button("Scale")) g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::SCALE);
	if (scActive) ImGui::PopStyleColor();

	// -- Separator --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");

	// -- Coordinate space buttons --
	ImGui::SameLine(0.0f, gap);
	bool lActive = (coord == ImTransformControl::LOCAL);
	if (lActive) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button("Local")) g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::LOCAL);
	if (lActive) ImGui::PopStyleColor();

	ImGui::SameLine();
	bool wActive = (coord == ImTransformControl::WORLD);
	if (wActive) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button("World")) g_sceneInteractor.setTransformWidgetMode(TRANSFORM_WIDGET_MODE::WORLD);
	if (wActive) ImGui::PopStyleColor();

	// -- Separator --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");

	// -- Snap toggle --
	ImGui::SameLine(0.0f, gap);
	if (snap) ImGui::PushStyleColor(ImGuiCol_Button, active_btn);
	if (ImGui::Button(snap ? "Snap  ON" : "Snap OFF")) g_sceneInteractor.useSnap(!snap);
	if (snap) ImGui::PopStyleColor();

	// -- Snap value controls --
	{
		static constexpr float k_snap_presets[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f };
		static constexpr int   k_snap_count     = static_cast<int>(sizeof(k_snap_presets) / sizeof(k_snap_presets[0]));
		float curr_snap = g_sceneInteractor.getSnapUnit();
		int snap_idx = 0;
		for (int i = 0; i < k_snap_count; ++i)
			if (std::abs(k_snap_presets[i] - curr_snap) < 0.001f) { snap_idx = i; break; }

		ImGui::SameLine();
		if (ImGui::Button("-"))
			g_sceneInteractor.setSnap(k_snap_presets[std::max(0, snap_idx - 1)]);
		ImGui::SameLine();
		ImGui::TextUnformatted(snap_val_str);
		ImGui::SameLine();
		if (ImGui::Button("+"))
			g_sceneInteractor.setSnap(k_snap_presets[std::min(k_snap_count - 1, snap_idx + 1)]);
	}

	// -- Separator --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");

	// -- Lightmaps -- 
	static LightmapBaker::BakeSettings s_bakeSettings;
	static int s_bakeResolution = static_cast<int>(s_bakeSettings.resolution);
	s_bakeSettings.blurRadius = 4;

	ImGui::SameLine(0.0f, gap);
	ImGui::TextUnformatted("Res:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(lm_res_w);
	ImGui::PushID("BakeResolution");
	if (ImGui::InputInt("##res", &s_bakeResolution, 0, 0))
	{
		s_bakeResolution = irr::core::clamp(s_bakeResolution, 64, 2048);
		s_bakeSettings.resolution = static_cast<uint32_t>(s_bakeResolution);
	}
	ImGui::PopID();

	ImGui::SameLine(0.0f, gap);
	ImGui::TextUnformatted("Bias:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(lm_prog_w);
	ImGui::PushID("BakeShadowBias");
	ImGui::InputFloat("##bias", &s_bakeSettings.shadowBias, 0.0f, 0.0f, "%.3f");
	ImGui::PopID();

	// Flush any completed CPU bakes to the GPU every frame
	if (LightmapBaker::isBaking() || LightmapBaker::hasPendingUploads())
		LightmapBaker::flushPendingUploads(RenderManager::Get()->driver());

	const bool bakeRunning = LightmapBaker::isBaking() || LightmapBaker::hasPendingUploads();
	ImGui::SameLine();
	if (bakeRunning)
	{
		const float progress = LightmapBaker::getBakeProgress();
		char label[32];
		snprintf(label, sizeof(label), "%.0f%%", progress * 100.0f);
		ImGui::ProgressBar(progress, ImVec2(lm_prog_w, 0.0f), label);

		ImGui::BeginDisabled();
		ImGui::SameLine();
		ImGui::Button("Bake Shadows");
		ImGui::EndDisabled();
	}
	else
	{
		if (ImGui::Button("Bake Shadows"))
		{
			spdlog::info("EditorInterface: starting lightmap bake...");
			LightmapBaker::bakeSceneAsync(s_bakeSettings);
		}
	}

	// -- Separator --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");

	// -- NavMesh --
	ImGui::SameLine(0.0f, gap);
	ImGui::TextUnformatted("NavMesh:");
	ImGui::SameLine();
	if (NavigationManager::Get() && NavigationManager::Get()->isNavMeshBuilt())
		ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Baked");
	else
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Not baked");

	ImGui::SameLine();
	if (ImGui::Button("Bake NavMesh"))
	{
		if (NavigationManager::Get())
		{
			NavigationManager::Get()->clearGeometry();

			int cookCount = 0;
			for (auto& entity : WorldManager::Get()->world()->getEntities())
			{
				if (!entity.hasComponent<MeshComponent>())
					continue;

				auto& mesh = entity.getComponent<MeshComponent>();
				if (!mesh.navCookable || !mesh.trimesh)
					continue;

				irr::core::matrix4 worldTransform;
				if (entity.hasComponent<TransformComponent>() && entity.getComponent<TransformComponent>().node)
					worldTransform = entity.getComponent<TransformComponent>().node->getAbsoluteTransformation();

				NavigationManager::Get()->addMeshGeometry(mesh.trimesh, worldTransform);
				++cookCount;
			}

			if (cookCount > 0)
			{
				spdlog::info("EditorInterface: baking navmesh from {} mesh(es)...", cookCount);
				NavigationManager::Get()->buildNavMesh();
			}
			else
			{
				spdlog::warn("EditorInterface: no meshes flagged as navCookable");
			}
		}
	}

	// -- Separator --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");

	// -- Play button --
	ImGui::SameLine(0.0f, gap);
	ImGui::PushStyleColor(ImGuiCol_Button, play_color);
	if (ImGui::Button("Play")) function_play_scene();
	ImGui::PopStyleColor();

	ImGui::End();
}

void EditorInterface::draw_statusbar()
{
	if (!m_windowData.draw_statusbar) return;

	ImGuiIO& io   = ImGui::GetIO();
	ImGuiStyle& s = ImGui::GetStyle();

	const float statusbar_h = ImGui::GetFrameHeight() + s.ItemSpacing.y;

	ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - statusbar_h));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, statusbar_h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s.ItemSpacing.x, s.ItemSpacing.y * 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.094f, 0.098f, 0.122f, 1.0f));

	ImGui::Begin("##editor_statusbar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoDocking   | ImGuiWindowFlags_NoSavedSettings   |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	const float gap = 16.0f;

	// -- Selected entity --
	std::string entity_label = "Selected Entity: ", selected_label = "None";
	if (g_sceneInteractor.isEntitySelected())
	{
		for (auto& ent : WorldManager::Get()->world()->getEntities())
		{
			if (ent.getComponent<DescriptorComponent>().id == g_currentEntity)
			{
				selected_label = ent.getComponent<DescriptorComponent>().name;
				break;
			}
		}
	}
	entity_label += selected_label;
	ImGui::TextUnformatted(entity_label.c_str());

	// -- Transform mode --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");
	ImGui::SameLine(0.0f, gap);
	ImGui::TextUnformatted(g_sceneInteractor.getWidgetToolModeStr().c_str());
	ImGui::SameLine();
	ImGui::TextUnformatted(g_sceneInteractor.getWidgetCoordModeStr().c_str());

	// -- Snap --
	ImGui::SameLine(0.0f, gap); ImGui::TextDisabled("|");
	ImGui::SameLine(0.0f, gap);
	if (g_sceneInteractor.isSnap())
		ImGui::TextColored(ImVec4(0.200f, 0.540f, 0.900f, 1.0f), "Snap ON");
	else
		ImGui::TextDisabled("Snap OFF");

	// -- Stats (right-aligned) --
	char stats[128];
	snprintf(stats, sizeof(stats), "FPS: %d   Triangles: %d   Scene Nodes: %d   Entities: %d   PhysX: %d",
		RenderManager::Get()->driver()->getFPS(),
		RenderManager::Get()->driver()->getPrimitiveCountDrawn(),
		RenderManager::Get()->sceneManager()->getRootSceneNode()->getChildren().size(),
		WorldManager::Get()->world()->getEntityCount(),
		PhysicsManager::Get()->scene()->getNbActors(physx::PxActorTypeFlags(physx::PxActorTypeFlag::eRIGID_DYNAMIC)));
	float stats_w  = ImGui::CalcTextSize(stats).x;
	float right_x  = io.DisplaySize.x - stats_w - s.WindowPadding.x * 2.0f;
	ImGui::SameLine(right_x > ImGui::GetCursorPosX() ? right_x : 0.0f);
	ImGui::TextDisabled("%s", stats);

	ImGui::End();
}
