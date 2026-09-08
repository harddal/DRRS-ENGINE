#include "SkillPanel.h"

#include <algorithm>
#include <cctype>   // toupper
#include <cfloat>   // FLT_MIN, for ImGui's "fill the width" button size
#include <cstdio>   // snprintf

#include <IMGUI/imgui.h>

#include "Engine/Renderer/RenderManager.h"

#include "Game/Player/PlayerController.h"
#include "Game/Player/WeaponController.h"
#include "Game/Skill/SkillSystem.h"

namespace
{
	const float kPanelWidth   = 780.0f;
	const float kPanelHeight  = 520.0f;
	const float kTreeWidth    = 240.0f;

	// Node states, as colours. Kept together so the four cases are visibly a set
	// and a fifth cannot be added without picking a colour for it.
	const ImVec4 kColOwned   (0.62f, 0.82f, 0.55f, 1.0f);  // bought, more to come
	const ImVec4 kColMaxed   (0.45f, 0.72f, 0.95f, 1.0f);  // nothing left to buy
	const ImVec4 kColBuyable (0.95f, 0.86f, 0.55f, 1.0f);  // affordable right now
	const ImVec4 kColBlocked (0.48f, 0.46f, 0.50f, 1.0f);  // refused, for a reason

	// A local theme rather than sharing the pouch's. The two panels are not
	// required to look alike and coupling them would mean editing the inventory
	// to restyle the skill tree — but if they ever should match, this is the
	// block to lift out, not to copy again.
	void pushSkillTheme()
	{
		ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.898f, 0.850f, 0.858f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled,  ImVec4(0.498f, 0.450f, 0.458f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.07f,  0.07f,  0.09f,  0.96f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg,       ImVec4(0.10f,  0.10f,  0.12f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.26f,  0.26f,  0.30f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.13f,  0.13f,  0.15f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.28f,  0.24f,  0.52f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f,  0.16f,  0.19f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f,  0.30f,  0.58f,  1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.48f,  0.44f,  0.72f,  1.0f));
	}

	const int kThemeColorCount = 10;

	// "+8% per rank" / "+2 per rank". Always written with the sign it has, and
	// a positive value always reads as an improvement — that is the WEAPON_STAT
	// sense convention, which holds even for the inverted stats because
	// statInv() is what applies them. A "+10% fire rate" row on a member that is
	// really a cooldown genuinely does shorten the cooldown.
	std::string describeRow(const SkillDef& def, WeaponController* weapons)
	{
		char buf[192];

		if (def.effect == SEFF_UNLOCK)
		{
			const char* what = nullptr;

			// An unlock always names a specific target weapon, so the bit can be
			// resolved to the weapon's own name for it rather than printed as an
			// index nobody can read.
			if (weapons)
			{
				if (PlayerWeapon* weapon = weapons->weapon(def.target))
					what = weapon->unlockName(def.unlockBit);
			}

			snprintf(buf, sizeof(buf), "Unlocks: %s", what ? what : "a capability");
			return buf;
		}

		if (def.op == SOP_MUL)
			snprintf(buf, sizeof(buf), "%s  %+.0f%% per rank",
			         weaponStatName(def.stat), def.valuePerRank * 100.0f);
		else
			snprintf(buf, sizeof(buf), "%s  %+.4g per rank",
			         weaponStatName(def.stat), def.valuePerRank);

		return buf;
	}
}

// ---------------------------------------------------------------------------
// The backend-neutral half. Nothing below this line until draw() knows what
// ImGui is, and that is on purpose — see the note in the header.
// ---------------------------------------------------------------------------

void SkillPanel::buildView(std::vector<SkillTreeView>& out, bool showEmptyTrees)
{
	out.clear();

	SkillSystem*     skills = SkillSystem::Get();
	const SkillDef*  defs   = SkillSystem::defs();
	const int        count  = SkillSystem::defCount();

	WeaponController* weapons =
		g_PlayerController ? g_PlayerController->weaponController() : nullptr;

	const PLAYER_WEAPON held = weapons ? weapons->currentWeaponType() : WEAP_NONE;

	for (int t = STREE_NONE + 1; t < STREE_COUNT; ++t)
	{
		const SKILL_TREE tree = static_cast<SKILL_TREE>(t);

		// An assigned-but-empty tree is never shown, not even by the testing
		// toggle. Eight of the ten are empty by design and they would be eight
		// blank columns.
		bool hasRows = false;
		for (int i = 0; i < count && !hasRows; ++i)
			hasRows = (defs[i].tree == tree);

		if (!hasRows)
			continue;

		// Which weapon's resolved numbers this column reads. The held weapon
		// wins so the readouts follow what is in the player's hands; otherwise
		// the first one they own in this tree; otherwise — only reachable with
		// the testing toggle on — whichever weapon is assigned to it at all.
		PLAYER_WEAPON anyInTree  = WEAP_NONE;
		PLAYER_WEAPON firstOwned = WEAP_NONE;
		bool          heldInTree = false;

		for (int w = WEAP_NONE + 1; w < WEAP_COUNT; ++w)
		{
			const PLAYER_WEAPON type = static_cast<PLAYER_WEAPON>(w);

			if (weaponSkillTree(type) != tree)
				continue;

			if (anyInTree == WEAP_NONE)
				anyInTree = type;

			if (!weapons || !weapons->hasWeapon(type))
				continue;

			if (firstOwned == WEAP_NONE)
				firstOwned = type;

			if (type == held)
				heldInTree = true;
		}

		const bool ownsOne = (firstOwned != WEAP_NONE);

		if (!ownsOne && !showEmptyTrees)
			continue;

		SkillTreeView view;
		view.tree           = tree;
		view.name           = skillTreeName(tree);
		view.representative = heldInTree ? held : (ownsOne ? firstOwned : anyInTree);
		view.spent          = skills->spentInTree(tree);

		if (view.representative != WEAP_NONE)
			view.representativeName = weaponDisplayName(view.representative);

		// Tiers, in table order of first appearance rather than sorted: the
		// table is authored top to bottom and a tree that ever skips a tier
		// number should still read in the order it was written.
		for (int i = 0; i < count; ++i)
		{
			if (defs[i].tree != tree)
				continue;

			const int tier = defs[i].tier;

			auto existing = std::find_if(view.tiers.begin(), view.tiers.end(),
				[tier](const SkillTierView& v) { return v.tier == tier; });

			if (existing == view.tiers.end())
			{
				SkillTierView tv;
				tv.tier           = tier;
				tv.pointsRequired = SkillSystem::tierRequirement(tier);
				tv.open           = (view.spent >= tv.pointsRequired);

				view.tiers.push_back(tv);
				existing = view.tiers.end() - 1;
			}

			// Rows sharing an id are ONE node. Skip a row whose id is already
			// present in this tier — its effects are collected below.
			const std::string id = defs[i].id;

			if (std::find_if(existing->nodes.begin(), existing->nodes.end(),
					[&id](const SkillNodeView& n) { return n.id == id; }) != existing->nodes.end())
				continue;

			SkillNodeView node;
			node.id          = id;
			node.name        = defs[i].name ? defs[i].name : id;
			node.description = defs[i].description ? defs[i].description : "";
			node.rank        = skills->rank(id.c_str());
			node.maxRank     = defs[i].maxRank;
			node.cost        = defs[i].costPerRank;
			node.owned       = node.rank > 0;
			node.maxed       = node.rank >= node.maxRank;
			node.buyable     = skills->canBuy(id.c_str(), &node.blockedReason);

			// Every row carrying this id, so a skill that both unlocks something
			// and buffs a stat describes both halves.
			for (int j = 0; j < count; ++j)
			{
				if (defs[j].tree != tree || id != defs[j].id)
					continue;

				node.effects.push_back(describeRow(defs[j], weapons));

				if (defs[j].effect == SEFF_UNLOCK)
				{
					node.isUnlock = true;
					continue;
				}

				// What this stat is doing RIGHT NOW, read off the same resolved
				// cache the weapon reads. A row targeted at one weapon is shown
				// against that weapon; an untargeted row against the column's
				// representative.
				const PLAYER_WEAPON subject =
					(defs[j].target == WEAP_COUNT) ? view.representative : defs[j].target;

				if (subject == WEAP_NONE)
					continue;

				char buf[128];
				snprintf(buf, sizeof(buf), "%s: x%.3f  %+.2f",
				         weaponStatName(defs[j].stat),
				         skills->statMul(subject, defs[j].stat),
				         skills->statAdd(subject, defs[j].stat));

				if (!node.currentEffect.empty())
					node.currentEffect += "\n";

				node.currentEffect += buf;
			}

			existing->nodes.push_back(node);
		}

		out.push_back(std::move(view));
	}
}

// ---------------------------------------------------------------------------
// The ImGui half. Everything from here down is expected to be thrown away when
// the UI moves to RmlUi; it should contain no decisions, only drawing.
// ---------------------------------------------------------------------------

void SkillPanel::drawNode(const SkillNodeView& node)
{
	const ImVec4 color = node.maxed   ? kColMaxed
	                   : node.owned   ? kColOwned
	                   : node.buyable ? kColBuyable
	                                  : kColBlocked;

	// An unlock reads as a different SHAPE, not just a different colour: a
	// capability is a one-time thing and a rank track would misdescribe it.
	char label[192];

	if (node.isUnlock)
		snprintf(label, sizeof(label), "%s %s###%s",
		         node.owned ? "[*]" : "[ ]", node.name.c_str(), node.id.c_str());
	else
		snprintf(label, sizeof(label), "%s   %d/%d###%s",
		         node.name.c_str(), node.rank, node.maxRank, node.id.c_str());

	ImGui::PushStyleColor(ImGuiCol_Text, color);

	// Disabled rather than hidden. A node the player cannot afford yet is the
	// thing that makes a tree read as a tree, and its tooltip is where the
	// reason lives.
	ImGui::BeginDisabled(!node.buyable);

	const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));

	ImGui::EndDisabled();
	ImGui::PopStyleColor();

	// Outside the BeginDisabled pair on purpose: a disabled item does not report
	// hover, and the tooltip on a REFUSED node is the most useful one there is.
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);

		ImGui::TextColored(color, "%s", node.name.c_str());

		if (!node.description.empty())
			ImGui::TextUnformatted(node.description.c_str());

		if (!node.effects.empty())
		{
			ImGui::Separator();
			for (auto& line : node.effects)
				ImGui::BulletText("%s", line.c_str());
		}

		if (!node.currentEffect.empty())
		{
			ImGui::Separator();
			ImGui::TextDisabled("Now: %s", node.currentEffect.c_str());
		}

		ImGui::Separator();

		if (node.maxed)
			ImGui::TextColored(kColMaxed, "Fully upgraded");
		else if (node.buyable)
			ImGui::TextColored(kColBuyable, "Costs %d point%s - click to buy",
			                   node.cost, node.cost == 1 ? "" : "s");
		else
			ImGui::TextColored(kColBlocked, "%s", node.blockedReason.c_str());

		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	if (clicked)
	{
		// buy() re-resolves on success and the view is rebuilt next frame, so
		// there is nothing to refresh by hand here. It also re-checks canBuy()
		// itself, which is what makes clicking a stale node harmless.
		SkillSystem::Get()->buy(node.id.c_str());
	}
}

void SkillPanel::draw()
{
	if (!m_open)
		return;

	buildView(m_view, m_showEmptyTrees);

	const auto screen = RenderManager::Get()->getConfiguration();

	pushSkillTheme();

	ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Always);
	ImGui::SetNextWindowPos(
		ImVec2((screen.width - kPanelWidth) * 0.5f, (screen.height - kPanelHeight) * 0.5f),
		ImGuiCond_Always);

	if (!ImGui::Begin("Skills", nullptr,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		ImGui::PopStyleColor(kThemeColorCount);
		return;
	}

	SkillSystem* skills = SkillSystem::Get();

	ImGui::TextColored(kColBuyable, "%d unspent", skills->unspentPoints());
	ImGui::SameLine();
	ImGui::TextDisabled("| %d spent | kills award points", skills->spentPoints());

	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110.0f);

	// Testing affordance, not shipping behaviour: a tree is meant to appear when
	// the run hands you a weapon in it.
	ImGui::Checkbox("All trees", &m_showEmptyTrees);

	ImGui::Separator();

	if (m_view.empty())
	{
		ImGui::TextDisabled("No upgradeable weapon yet.");
		ImGui::TextDisabled("Only the Launcher and the Skull Staff have trees.");
		ImGui::End();
		ImGui::PopStyleColor(kThemeColorCount);
		return;
	}

	// One column per tree, side by side in a horizontally scrolling strip, so
	// the layout does not have to be re-thought when a third tree appears.
	ImGui::BeginChild("##trees", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	for (size_t i = 0; i < m_view.size(); ++i)
	{
		const SkillTreeView& tree = m_view[i];

		if (i > 0)
			ImGui::SameLine();

		ImGui::BeginChild(tree.name.c_str(), ImVec2(kTreeWidth, 0), true);

		// Capitalised for display only; skillTreeName() returns the lower-case
		// id, which is what the console prints and must stay stable.
		std::string title = tree.name;
		if (!title.empty())
			title[0] = static_cast<char>(toupper(title[0]));

		ImGui::TextColored(ImVec4(0.78f, 0.72f, 0.95f, 1.0f), "%s", title.c_str());

		if (!tree.representativeName.empty())
			ImGui::TextDisabled("%s", tree.representativeName.c_str());

		ImGui::TextDisabled("%d spent here", tree.spent);
		ImGui::Separator();

		for (auto& tier : tree.tiers)
		{
			if (tier.open)
			{
				ImGui::TextDisabled("Tier %d", tier.tier);
			}
			else
			{
				// The requirement is spelled out rather than just greyed, so a
				// locked tier says what to do about it.
				ImGui::TextColored(kColBlocked, "Tier %d - needs %d spent",
				                   tier.tier, tier.pointsRequired);
			}

			for (auto& node : tier.nodes)
				drawNode(node);

			ImGui::Spacing();
		}

		ImGui::EndChild();
	}

	ImGui::EndChild();

	ImGui::End();
	ImGui::PopStyleColor(kThemeColorCount);
}
