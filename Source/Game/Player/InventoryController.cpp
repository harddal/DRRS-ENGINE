#include "InventoryController.h"

#include <IMGUI/imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Script/ScriptManager.h"

#include "Game/Item/ItemDatabase.h"

bool g_PlayerInventoryIsDisplaying = false;
bool g_LockPlayerForInput          = false;

namespace
{
	// Icons are square and drawn at a fixed size rather than scaled to the panel:
	// every item icon in content/texture/ui/item is 64px, and letting them stretch
	// with the window made the small ones mushy.
	const float kIconSize    = 64.0f;
	const float kPanelWidth  = 620.0f;
	const float kPanelHeight = 420.0f;

	void pushInventoryTheme()
	{
		ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.898f, 0.850f, 0.858f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled,  ImVec4(0.498f, 0.450f, 0.458f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.08f,  0.08f,  0.09f,  0.96f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg,       ImVec4(0.11f,  0.11f,  0.12f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.26f,  0.26f,  0.28f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.14f,  0.14f,  0.15f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.20f,  0.30f,  0.60f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_Tab,           ImVec4(0.14f,  0.14f,  0.15f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TabHovered,    ImVec4(0.28f,  0.40f,  0.68f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TabActive,     ImVec4(0.20f,  0.30f,  0.60f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f,  0.18f,  0.20f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f,  0.72f,  0.72f,  0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.84f,  0.78f,  0.78f,  0.6f));
	}

	const int kThemeColorCount = 13;
}

void InventoryController::init()
{
	m_displayInventory = false;
	m_selected         = static_cast<size_t>(-1);
	m_activeCategory.clear();

	g_PlayerInventoryIsDisplaying = false;
	g_LockPlayerForInput          = false;

	// NOT cleared here. init() runs on every editor->game toggle and on every
	// scene load, and the save sidecar is applied on the frame AFTER the load —
	// wiping the pouch here would throw away what player.sav had just restored.
	// A genuinely new game starts with an empty PlayerInventory anyway.
}

bool InventoryController::canUseItem(const std::string& id) const
{
	const ItemDef& def = ItemDatabase::Get(id);

	// No definition, or no script: nothing can refuse the use. An item with no
	// onUse at all is still "usable" — that is the right default for a key that
	// a door script only ever tests with has().
	if (!def.valid() || !def.scriptComponent.hasCanUseEventFunc)
		return true;

	return ScriptManager::Get()->executeBool(
		def.scriptComponent, def.scriptComponent.canUseEventFunc, 0, /*fallback=*/true);
}

bool InventoryController::isUsable(const std::string& id) const
{
	const ItemDef& def = ItemDatabase::Get(id);

	// No hook, no use. Consuming a keycard because someone clicked a button
	// would be a way to destroy the key to the next door.
	return def.valid() && def.scriptComponent.hasOnUseEventFunc;
}

bool InventoryController::useItem(const std::string& id)
{
	if (!isUsable(id))
		return false;

	if (!canUseItem(id))
		return false;

	const ItemDef& def = ItemDatabase::Get(id);

	ScriptManager::Get()->execute(
		def.scriptComponent, def.scriptComponent.onUseEventFunc, 0);

	m_inventory.remove(id, 1);

	// The selection is an index into a vector that just shrank. Cleared rather
	// than clamped: after using the last medkit the player should be looking at
	// nothing, not at whatever slid into that slot.
	m_selected = static_cast<size_t>(-1);

	return true;
}

bool InventoryController::giveItem(const std::string& id, int count, const std::string& data)
{
	const ItemDef& def = ItemDatabase::Get(id);
	if (!def.valid())
	{
		spdlog::warn("InventoryController::giveItem(): unknown item '{}'", id);
		return false;
	}

	if (count <= 0)
		return false;

	// Auto-use items are consumed on the spot — but only while the use would
	// actually achieve something. A medkit walked over at full health falls
	// through to storage instead of being wasted, which is the rule the old
	// medkit_small.asc hand-rolled inline before there was anywhere to put it.
	if (def.autoUse && isUsable(id))
	{
		int consumed = 0;

		while (consumed < count && canUseItem(id))
		{
			ScriptManager::Get()->execute(
				def.scriptComponent, def.scriptComponent.onUseEventFunc, 0);

			++consumed;
		}

		count -= consumed;

		if (count <= 0)
			return true;
	}

	return m_inventory.add(id, count, data) > 0;
}

void InventoryController::update()
{
	static bool tabPressed = false;
	if (InputManager::Get()->getKeyPressOnce(KEY_TAB, &tabPressed, true))
	{
		m_displayInventory = !m_displayInventory;
		InputManager::Get()->centerMouse();
	}

	ImGui::GetIO().MouseDrawCursor = m_displayInventory;
	InputManager::Get()->canProcessInput(!m_displayInventory);

	g_PlayerInventoryIsDisplaying = m_displayInventory;

	if (!m_displayInventory)
		return;

	pushInventoryTheme();
	drawPanel();
	ImGui::PopStyleColor(kThemeColorCount);
}

void InventoryController::drawPanel()
{
	const auto screen = RenderManager::Get()->getConfiguration();

	ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Always);
	ImGui::SetNextWindowPos(
		ImVec2((screen.width - kPanelWidth) * 0.5f, (screen.height - kPanelHeight) * 0.5f),
		ImGuiCond_Always);

	if (!ImGui::Begin("Inventory", nullptr,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const auto categories = ItemDatabase::Categories();

	if (m_activeCategory.empty() && !categories.empty())
		m_activeCategory = categories.front();

	if (ImGui::BeginTabBar("##inventory_tabs"))
	{
		for (auto& category : categories)
		{
			// Capitalised for the tab label only — the category string itself
			// stays lower-case, because it is the value in the .item file.
			std::string label = category;
			if (!label.empty())
				label[0] = static_cast<char>(toupper(label[0]));

			const auto indices = m_inventory.indicesInCategory(category);

			// The count rides in the label so the player can see what is where
			// without opening every tab.
			if (!indices.empty())
				label += " (" + std::to_string(indices.size()) + ")";

			label += "###" + category; // stable id: the visible count must not re-create the tab

			if (!ImGui::BeginTabItem(label.c_str()))
				continue;

			if (m_activeCategory != category)
			{
				// Changing tab drops the selection. Keeping it would leave the
				// description panel describing an item from the tab you just left.
				m_activeCategory = category;
				m_selected       = static_cast<size_t>(-1);
			}

			ImGui::BeginChild("##grid", ImVec2(kPanelWidth * 0.58f, 0), true);

			if (indices.empty())
			{
				ImGui::TextDisabled("Nothing here.");
			}
			else
			{
				const float avail   = ImGui::GetContentRegionAvail().x;
				const int   perRow  = (avail > kIconSize) ? static_cast<int>(avail / (kIconSize + 10.0f)) : 1;
				int         column  = 0;

				for (auto index : indices)
				{
					const ItemStack& stack = m_inventory.stacks()[index];
					const ItemDef&   def   = ItemDatabase::Get(stack.id);

					ImGui::PushID(static_cast<int>(index));

					const bool selected = (m_selected == index);
					if (selected)
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.30f, 0.60f, 1.0f));

					bool clicked = false;

					if (def.iconTexture)
					{
						// 1.92 signature: explicit string id first, ImTextureRef second.
						// PushID above already scopes it, so a constant id is fine.
						clicked = ImGui::ImageButton("##icon",
							ImTextureRef(reinterpret_cast<ImTextureID>(def.iconTexture)),
							ImVec2(kIconSize, kIconSize));
					}
					else
					{
						// A missing icon must still be selectable — several items
						// point at textures that do not exist yet.
						clicked = ImGui::Button(def.name.c_str(), ImVec2(kIconSize + 8.0f, kIconSize + 8.0f));
					}

					if (selected)
						ImGui::PopStyleColor();

					if (clicked)
						m_selected = index;

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", def.name.c_str());

					// Stack count in the corner of the icon
					if (stack.count > 1)
					{
						const ImVec2 min = ImGui::GetItemRectMin();
						const ImVec2 max = ImGui::GetItemRectMax();

						ImGui::GetWindowDrawList()->AddText(
							ImVec2(max.x - 18.0f, max.y - 18.0f),
							IM_COL32(255, 255, 255, 255),
							("x" + std::to_string(stack.count)).c_str());

						(void)min;
					}

					ImGui::PopID();

					if (++column < perRow)
						ImGui::SameLine();
					else
						column = 0;
				}
			}

			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##detail", ImVec2(0, 0), true);

			// Re-validated every frame rather than trusted: using the last of a
			// stack erases it, and a stale index would read the wrong item.
			if (m_selected < m_inventory.stacks().size())
			{
				const ItemStack& stack = m_inventory.stacks()[m_selected];
				const ItemDef&   def   = ItemDatabase::Get(stack.id);

				ImGui::TextWrapped("%s", def.name.c_str());
				ImGui::Separator();
				ImGui::Spacing();

				if (def.desc.empty())
					ImGui::TextDisabled("No description.");
				else
					ImGui::TextWrapped("%s", def.desc.c_str());

				if (stack.count > 1)
				{
					ImGui::Spacing();
					ImGui::TextDisabled("Carrying %d", stack.count);
				}

				ImGui::Spacing();
				ImGui::Spacing();

				// Greyed rather than hidden when the use would do nothing, so the
				// player can see that the option exists and that it is not
				// available right now.
				// Three states, not two: no hook at all (no button), a hook that
				// says not now (greyed, with a reason), and usable.
				if (!isUsable(stack.id))
				{
					// Nothing drawn. A key has no action, and an empty greyed
					// button would only invite clicking.
				}
				else if (!canUseItem(stack.id))
				{
					ImGui::BeginDisabled();
					ImGui::Button("Use", ImVec2(80.0f, 0.0f));
					ImGui::EndDisabled();
					ImGui::TextDisabled("Would have no effect.");
				}
				else if (ImGui::Button("Use", ImVec2(80.0f, 0.0f)))
				{
					// Copied, not referenced: useItem() erases the stack this
					// string lives in.
					const std::string id = stack.id;
					useItem(id);
				}
			}
			else
			{
				ImGui::TextDisabled("Select an item.");
			}

			ImGui::EndChild();

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

void InventoryController::destroy()
{
	m_displayInventory            = false;
	g_PlayerInventoryIsDisplaying = false;

	ImGui::GetIO().MouseDrawCursor = false;
	InputManager::Get()->canProcessInput(true);
}
