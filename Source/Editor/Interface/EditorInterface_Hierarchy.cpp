#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Engine/Brush/BrushManager.h"
#include "Game/Components.h"

#include <iomanip>
#include <sstream>
#include <string>

void EditorInterface::draw_window_hierarchy()
{
	if (!m_windowData.draw_window_hiearchy) { return; }

	ImGui::SetNextWindowSize(DPI_SCALED_IMVEC2(250, 600), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Scene Hierarchy", &m_windowData.draw_window_hiearchy))
	{
		static char s_hier_filter[128] = "";
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##hier_filter", "Filter...", s_hier_filter, sizeof(s_hier_filter));
		ImGui::Separator();

		if (g_sceneInteractor.isLinkPicking())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 60, 255));
			ImGui::TextWrapped("Pick link target: click an entity (Esc cancels)");
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		// ---- World brushes (CSG) ----
		if (BrushManager::Get() && !BrushManager::Get()->getAllBrushes().empty())
		{
			if (ImGui::CollapsingHeader("World Brushes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const auto& brush : BrushManager::Get()->getAllBrushes())
				{
					if (s_hier_filter[0] != '\0' &&
					    brush.name.find(s_hier_filter) == std::string::npos)
						continue;

					const bool selected = g_sceneInteractor.isBrushInSelection(brush.id);
					ImGui::PushID(static_cast<int>(brush.id) + 0x40000000);
					if (ImGui::Selectable(brush.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						g_sceneInteractor.setSelectedBrush(brush.id);
						m_windowData.draw_window_brush_editor = true;
					}
					ImGui::PopID();
				}
			}
			ImGui::Separator();
		}

		for (auto ent : WorldManager::Get()->world()->getEntities())
		{
			auto& desc = ent.getComponent<DescriptorComponent>();

			if (s_hier_filter[0] != '\0')
			{
				bool match = false;
				const char* haystack = desc.name.c_str();
				const char* needle   = s_hier_filter;
				for (; *haystack; ++haystack)
				{
					if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle))
					{
						const char* h = haystack, *n = needle;
						while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { ++h; ++n; }
						if (!*n) { match = true; break; }
					}
				}
				if (!match) continue;
			}

			const bool selected = (g_currentEntity == desc.id);

			ImGui::PushID(static_cast<int>(desc.id));
			if (ImGui::Selectable(desc.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
			{
				if (g_sceneInteractor.isLinkPicking())
				{
					g_sceneInteractor.completeLinkPick(desc.id);
				}
				else
				{
					g_sceneInteractor.setSelectedEntity(desc.id);
					g_currentSelectedObjectType = static_cast<unsigned int>(SELECTED_OBJECT_TYPE::ENTITY);
					m_windowData.draw_window_prop_ent = true;
				}
			}
			ImGui::PopID();
		}
	}
	ImGui::End();
}
