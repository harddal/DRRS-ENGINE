#include "EditorInterface.h"
#include "EditorInterface_Internal.h"

#include "Editor/EditorState.h"
#include "Engine/Resource/FilePaths.h"

#include <IMGUI/imgui.h>
#include "Engine/Interface/ImGuiExtensions.h"

#include "Engine/Engine.h"
#include "Game/Components.h"
#include "Engine/Prop/PropManager.h"
#include "Editor/TexturePainter.h"

#include <string>
#include <functional>

void EditorInterface::draw_window_vegetation_painter()
{
    if (!m_windowData.draw_window_vegetation_painter) return;

    VegetationPainter& painter = g_sceneInteractor.getPainter();

    if (!ImGui::Begin("Vegetation Painter", &m_windowData.draw_window_vegetation_painter))
    {
        ImGui::End();
        return;
    }

    // Active toggle
    if (painter.m_active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.85f, 0.3f, 1.0f));
        if (ImGui::Button("Paint Mode: ON ", ImVec2(-1, 0))) painter.m_active = false;
        ImGui::PopStyleColor(2);
    }
    else
    {
        if (ImGui::Button("Paint Mode: OFF", ImVec2(-1, 0))) painter.m_active = true;
    }

    ImGui::Text("Brush");

    ImGui::Text("Radius:");      ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::InputFloat("##vp_radius",   &painter.m_radius,   0.f, 0.f, "%.1f");
    if (painter.m_radius < 0.1f) painter.m_radius = 0.1f;
    ImGui::PopItemWidth();

    ImGui::Text("Per tick:");    ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::InputInt("##vp_count", &painter.m_countPerTick);
    if (painter.m_countPerTick < 1) painter.m_countPerTick = 1;
    ImGui::PopItemWidth();

    ImGui::Text("Interval (s):"); ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::InputFloat("##vp_interval", &painter.m_interval, 0.f, 0.f, "%.2f");
    if (painter.m_interval < 0.01f) painter.m_interval = 0.01f;
    ImGui::PopItemWidth();

    ImGui::Separator();
    ImGui::Text("Prop");

    static const char* vpShaderNames[] = { "phong_perpixel", "foliage", "grass" };
    static const int   vpShaderCount   = 3;
    int vpCurrentShader = 0;
    for (int s = 0; s < vpShaderCount; s++)
        if (painter.m_shader == vpShaderNames[s]) { vpCurrentShader = s; break; }
    ImGui::Text("Shader:");
    if (ImGui::Combo("##vp_shader", &vpCurrentShader, vpShaderNames, vpShaderCount))
        painter.m_shader = vpShaderNames[vpCurrentShader];

    ImGui::Text("Scale min:"); ImGui::SameLine(); ImGui::PushItemWidth(60);
    ImGui::InputFloat("##vp_scalemin", &painter.m_scaleMin, 0.f, 0.f, "%.2f");
    if (painter.m_scaleMin < 0.01f) painter.m_scaleMin = 0.01f;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("max:"); ImGui::SameLine(); ImGui::PushItemWidth(60);
    ImGui::InputFloat("##vp_scalemax", &painter.m_scaleMax, 0.f, 0.f, "%.2f");
    if (painter.m_scaleMax < painter.m_scaleMin) painter.m_scaleMax = painter.m_scaleMin;
    ImGui::PopItemWidth();

    ImGui::Text("Cull dist:"); ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::InputFloat("##vp_cull", &painter.m_cullDistance, 0.f, 0.f, "%.1f");
    if (painter.m_cullDistance < 0.f) painter.m_cullDistance = 0.f;
    ImGui::PopItemWidth();

    ImGui::Separator();

    // ── Mesh browser ─────────────────────────────────────────────────────────
    static bool s_vpMeshListLoaded = false;
    if (!s_vpMeshListLoaded)
    {
        s_loadPropMeshList();
        s_vpMeshListLoaded = true;
    }

    if (ImGui::Button("Refresh Mesh List")) s_loadPropMeshList();
    ImGui::Text("Click [+] to add a mesh to the palette:");
    ImGui::BeginChild("##vpMeshBrowser", ImVec2(0, 300), true);

    std::function<void(const PropMeshFolder&)> drawVpFolder =
        [&](const PropMeshFolder& folder)
    {
        for (const auto& kv : folder.subfolders)
        {
            if (ImGui::TreeNode(kv.first.c_str()))
            {
                drawVpFolder(kv.second);
                ImGui::TreePop();
            }
        }
        for (const auto& f : folder.files)
        {
            ImGui::PushID(f.second.c_str());
            if (ImGui::SmallButton("+"))
            {
                // Only add if not already in palette
                bool already = false;
                for (const auto& e : painter.m_palette)
                    if (e.meshPath == f.second) { already = true; break; }
                if (!already)
                {
                    PaletteEntry entry;
                    entry.meshPath = f.second;
                    painter.m_palette.push_back(entry);
                }
            }
            ImGui::SameLine();
            ImGui::Text("%s", f.first.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.second.c_str());
            ImGui::PopID();
        }
    };
    drawVpFolder(s_propMeshRoot);

    ImGui::EndChild();

	// ── Palette ──────────────────────────────────────────────────────────────
	ImGui::Text("Palette (%d)", static_cast<int>(painter.m_palette.size()));

	for (int i = 0; i < static_cast<int>(painter.m_palette.size()); i++)
	{
		ImGui::PushID(i);
		auto& entry = painter.m_palette[i];

		ImGui::Checkbox("##vp_en", &entry.enabled);
		ImGui::SameLine();

		std::string label = entry.meshPath;
		size_t slash = label.find_last_of("/\\");
		if (slash != std::string::npos) label = label.substr(slash + 1);
		ImGui::Text("%s", label.c_str());
		ImGui::SameLine();

		if (ImGui::SmallButton("X"))
		{
			painter.m_palette.erase(painter.m_palette.begin() + i);
			ImGui::PopID();
			break;
		}

		ImGui::PopID();
	}

	ImGui::Separator();
    ImGui::End();
}

void EditorInterface::draw_window_texture_painter()
{
    if (!m_windowData.draw_window_texture_painter) return;

    TexturePainter& tp = g_sceneInteractor.getTexturePainter();

    if (!ImGui::Begin("Texture Painter", &m_windowData.draw_window_texture_painter))
    {
        ImGui::End();
        return;
    }

    // ── Target prop ──────────────────────────────────────────────────────────
    uint32_t selId = g_sceneInteractor.getSelectedProp();
    StaticProp* selProp = (selId != UINT32_MAX) ? PropManager::Get()->getProp(selId) : nullptr;

    if (selProp)
    {
        ImGui::Text("Selected: prop %u (%s)", selId,
                    selProp->mesh.substr(selProp->mesh.find_last_of("/\\") + 1).c_str());

        if (!selProp->isPaintable)
        {
            static int s_splatRes = 512;
            ImGui::Text("Resolution:"); ImGui::SameLine(); ImGui::PushItemWidth(80);
            const char* resItems[] = { "256", "512", "1024" };
            static int  resIndex   = 1;
            if (ImGui::Combo("##tp_res", &resIndex, resItems, 3))
            {
                const int resValues[] = { 256, 512, 1024 };
                s_splatRes = resValues[resIndex];
            }
            ImGui::PopItemWidth();

            if (ImGui::Button("Enable Texture Painting", ImVec2(-1, 0)))
            {
                PropManager::Get()->enablePainting(selId, s_splatRes);
                tp.targetProp = PropManager::Get()->getProp(selId);
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Painting enabled");

            if (ImGui::Button("Set as Paint Target", ImVec2(-1, 0)))
                tp.targetProp = selProp;
        }
    }
    else
    {
        ImGui::TextDisabled("Select a prop to paint on");
    }

    ImGui::Separator();

    // ── Active / target display ───────────────────────────────────────────────
    if (tp.targetProp)
    {
        ImGui::Text("Target: prop %u", tp.targetProp->id);

        if (tp.active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
            if (ImGui::Button("Paint Mode: ON ", ImVec2(-1, 0))) tp.active = false;
            ImGui::PopStyleColor(2);
        }
        else
        {
            if (ImGui::Button("Paint Mode: OFF", ImVec2(-1, 0))) tp.active = true;
        }
    }
    else
    {
        ImGui::TextDisabled("No paint target selected");
    }

    ImGui::Separator();

    // ── Brush ─────────────────────────────────────────────────────────────────
    ImGui::Text("Brush");
    ImGui::Text("Radius:");   ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::InputFloat("##tp_radius",   &tp.radius,   0.f, 0.f, "%.2f");
    if (tp.radius < 0.1f) tp.radius = 0.1f;
    ImGui::PopItemWidth();

    ImGui::Text("Strength:"); ImGui::SameLine(); ImGui::PushItemWidth(80);
    ImGui::SliderFloat("##tp_strength", &tp.strength, 0.01f, 1.0f);
    ImGui::PopItemWidth();

    ImGui::Separator();

    // ── Layer selector ────────────────────────────────────────────────────────
    ImGui::Text("Paint Layer");
    const char* layerNames[] = { "A (base->layer 1)", "B (layer 2)", "C (layer 3)", "D (layer 4)" };
    for (int i = 0; i < 4; ++i)
    {
        if (ImGui::RadioButton(layerNames[i], tp.activeChannel == i))
            tp.activeChannel = i;
    }

    ImGui::Separator();

    // ── Detail textures ───────────────────────────────────────────────────────
    if (tp.targetProp && tp.targetProp->isPaintable)
    {
        ImGui::Text("Detail Textures");

        static char s_detailPath[4][512] = {};
        static bool s_init = false;
        static int  s_browsingSlot = -1;

        // Sync display buffers when target changes
        static StaticProp* s_lastTarget = nullptr;
        if (tp.targetProp != s_lastTarget)
        {
            for (int d = 0; d < 4; ++d)
                strncpy(s_detailPath[d], tp.targetProp->detailTextures[d].c_str(), 511);
            s_lastTarget = tp.targetProp;
            s_init = true;
        }

        const char* slotLabels[] = { "A", "B", "C", "D" };
        auto*       driver       = RenderManager::Get()->driver();

        for (int d = 0; d < 4; ++d)
        {
            ImGui::PushID(d);
            ImGui::Text("Layer %s:", slotLabels[d]); ImGui::SameLine();
            ImGui::PushItemWidth(160);
            if (ImGui::InputText("##tp_dtpath", s_detailPath[d], 512,
                                  ImGuiInputTextFlags_EnterReturnsTrue))
            {
                tp.targetProp->detailTextures[d] = s_detailPath[d];
                auto* tex = driver->getTexture(s_detailPath[d]);
                if (tex)
                    for (unsigned b = 0; b < tp.targetProp->node->getMaterialCount(); ++b)
                        tp.targetProp->node->getMaterial(b).setTexture(3 + d, tex);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("...##tp_browse"))
            {
                s_browsingSlot = d;
                show_window_texture_browser();
            }
            ImGui::PopID();
        }

        // Apply browser selection (browser closes and sets g_currentSelectedTexture one frame before we read it)
        if (s_browsingSlot >= 0 && !m_windowData.draw_window_texture_browser
            && g_currentSelectedTexture != "null")
        {
            std::string fullPath = _asset_tex(g_currentSelectedTexture);
            tp.targetProp->detailTextures[s_browsingSlot] = fullPath;
            strncpy(s_detailPath[s_browsingSlot], fullPath.c_str(), 511);
            auto* tex = driver->getTexture(fullPath.c_str());
            if (tex)
                for (unsigned b = 0; b < tp.targetProp->node->getMaterialCount(); ++b)
                    tp.targetProp->node->getMaterial(b).setTexture(3 + s_browsingSlot, tex);
            s_browsingSlot = -1;
            g_currentSelectedTexture = "null";
        }

        // Tiling
        ImGui::Separator();
        ImGui::Text("Tiling (all layers)");
        float* tiling = RenderManager::Get()->terrainCallback()->tiling;
        ImGui::PushItemWidth(50);
        ImGui::InputFloat("A##til", &tiling[0], 0.f, 0.f, "%.1f"); ImGui::SameLine();
        ImGui::InputFloat("B##til", &tiling[1], 0.f, 0.f, "%.1f"); ImGui::SameLine();
        ImGui::InputFloat("C##til", &tiling[2], 0.f, 0.f, "%.1f"); ImGui::SameLine();
        ImGui::InputFloat("D##til", &tiling[3], 0.f, 0.f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::Separator();

        if (ImGui::Button("Save Splat Map", ImVec2(-1, 0)))
            PropManager::Get()->saveSplatMaps();
    }

    ImGui::End();
}
