#include "ImGuiExtensions.h"

#include <array>

#include <IMGUI/imgui.h>
#include <ImGUI/imgui_internal.h>

#include "Engine/Engine.h"

using namespace ImGui;

static ImU32 ColorU32(int r, int g, int b, int a)
{
	return (ImU32)ImColor(r, g, b, a);
}

bool ImColorPicker(const char* label, ImColor* color)
{
	static const ImVec2 SV_SIZE       = ImVec2(200.0f, 200.0f);
	static const float  HUE_WIDTH     = 16.0f;
	static const float  SWATCH_WIDTH  = 32.0f;
	static const float  GAP           = 6.0f;
	static const float  CURSOR_RADIUS = 5.0f;

	bool value_changed = false;

	ImDrawList* draw_list  = GetWindowDrawList();
	ImVec2      picker_pos = GetCursorScreenPos();

	float hue, sat, val;
	ColorConvertRGBtoHSV(color->Value.x, color->Value.y, color->Value.z, hue, sat, val);

	// Hue is undefined when the color is black or fully desaturated — RGB->HSV
	// will snap it to 0, making the hue slider jump to the top while dragging
	// into dark/gray areas.  Persist the last meaningful hue in ImGui storage
	// so it is restored on the next frame.
	ImGuiStorage* storage  = GetStateStorage();
	ImGuiID       hue_id   = GetID("##hue_state");
	float         saved_hue = storage->GetFloat(hue_id, hue);
	if (sat < 0.005f || val < 0.005f)
		hue = saved_hue;

	// -----------------------------------------------------------------------
	// SV square  (saturation = X left->right, value = Y top->bottom inverted)
	// -----------------------------------------------------------------------
	{
		ImVec2 sv_min = picker_pos;
		ImVec2 sv_max = ImVec2(sv_min.x + SV_SIZE.x, sv_min.y + SV_SIZE.y);

		float rh, gh, bh;
		ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, rh, gh, bh);
		ImU32 hue_col = (ImU32)ImColor(rh, gh, bh, 1.0f);
		ImU32 white   = ColorU32(255, 255, 255, 255);
		ImU32 black   = ColorU32(  0,   0,   0, 255);
		ImU32 transp  = ColorU32(  0,   0,   0,   0);

		// Pass 1 -- horizontal white->hue-color gradient (controls saturation)
		draw_list->AddRectFilledMultiColor(sv_min, sv_max,
			white, hue_col, hue_col, white);

		// Pass 2 -- vertical transparent->black gradient (controls value)
		draw_list->AddRectFilledMultiColor(sv_min, sv_max,
			transp, transp, black, black);

		// Crosshair at current sat/val
		ImVec2 cp(sv_min.x + sat * SV_SIZE.x,
		          sv_min.y + (1.0f - val) * SV_SIZE.y);
		draw_list->AddCircle(cp, CURSOR_RADIUS,     white, 12);
		draw_list->AddCircle(cp, CURSOR_RADIUS - 2, black, 12);

		InvisibleButton("##sv", SV_SIZE);
		if (IsItemActive() && GetIO().MouseDown[0])
		{
			ImVec2 m = GetIO().MousePos;
			sat = ImClamp((m.x - sv_min.x) / SV_SIZE.x,        0.0f, 1.0f);
			val = ImClamp(1.0f - (m.y - sv_min.y) / SV_SIZE.y, 0.0f, 1.0f);
			value_changed = true;
		}
	}

	// -----------------------------------------------------------------------
	// Hue bar (vertical rainbow strip)
	// -----------------------------------------------------------------------
	SameLine(0, GAP);
	{
		ImVec2 hue_min = GetCursorScreenPos();
		ImVec2 hue_max = ImVec2(hue_min.x + HUE_WIDTH, hue_min.y + SV_SIZE.y);

		const ImU32 rainbow[7] = {
			ColorU32(255,   0,   0, 255),
			ColorU32(255, 255,   0, 255),
			ColorU32(  0, 255,   0, 255),
			ColorU32(  0, 255, 255, 255),
			ColorU32(  0,   0, 255, 255),
			ColorU32(255,   0, 255, 255),
			ColorU32(255,   0,   0, 255),
		};
		float seg = SV_SIZE.y / 6.0f;
		for (int i = 0; i < 6; ++i)
		{
			draw_list->AddRectFilledMultiColor(
				ImVec2(hue_min.x, hue_min.y +  i      * seg),
				ImVec2(hue_max.x, hue_min.y + (i + 1) * seg),
				rainbow[i], rainbow[i], rainbow[i + 1], rainbow[i + 1]);
		}

		// Marker line
		float   my    = hue_min.y + hue * SV_SIZE.y;
		ImU32   white = ColorU32(255, 255, 255, 255);
		draw_list->AddRect(
			ImVec2(hue_min.x - 2, my - 2),
			ImVec2(hue_max.x + 2, my + 2),
			white);

		InvisibleButton("##hue", ImVec2(HUE_WIDTH, SV_SIZE.y));
		if (IsItemActive() && GetIO().MouseDown[0])
		{
			hue = ImClamp((GetIO().MousePos.y - hue_min.y) / SV_SIZE.y, 0.0f, 1.0f);
			value_changed = true;
		}
	}

	// -----------------------------------------------------------------------
	// Color preview swatch
	// -----------------------------------------------------------------------
	SameLine(0, GAP);
	{
		ImVec2 sw_min = GetCursorScreenPos();
		ImVec2 sw_max = ImVec2(sw_min.x + SWATCH_WIDTH, sw_min.y + SV_SIZE.y);

		draw_list->AddRectFilled(sw_min, sw_max, (ImU32)ImColor(color->Value.x, color->Value.y, color->Value.z, 1.0f));
		draw_list->AddRect(sw_min, sw_max, ColorU32(100, 100, 100, 255));

		InvisibleButton("##swatch", ImVec2(SWATCH_WIDTH, SV_SIZE.y));
	}

	// Persist the hue so it survives dragging into black/gray regions
	storage->SetFloat(hue_id, hue);

	// Apply HSV changes before drawing the RGB inputs
	if (value_changed)
		*color = ImColor::HSV(hue, sat, val);

	// -----------------------------------------------------------------------
	// RGB text inputs below the picker
	// -----------------------------------------------------------------------
	SetCursorScreenPos(ImVec2(picker_pos.x, picker_pos.y + SV_SIZE.y + GAP));
	bool text_changed = ColorEdit3(label, &color->Value.x);

	return value_changed | text_changed;
}
