#include "WeaponController.h"

#include "spdlog/spdlog.h"

#include "Engine/Engine.h"

#include <IMGUI/imgui.h>
#include "InventoryController.h"

#include <algorithm>

// No NOMINMAX in this project, and Engine.h drags in windows.h — without these
// the std::min calls below expand into the macro and fail with errors that name
// a type rather than the macro.
#undef max
#undef min

// windows.h ALSO defines MB_RIGHT — as the MessageBox alignment flag 0x00080000 —
// which silently shadows the mouse-button enum. It compiles cleanly and then
// isMouseButtonPressed() falls through to `default: button_code = 0` and returns
// false forever, so alt-fire simply never registers. Cost a runtime debug session
// here; the weapon .cpp files already carry this same #undef for the same reason.
// Note there is no windows.h MB_LEFT, so the left button works and hides the bug.
#undef MB_RIGHT

#define current_weapon m_player_weapon.at(m_current_weapon)

void WeaponController::init()
{
	// Destroy any existing weapons before reinitialising (e.g. edit→game mode switch).
	// Without this, old scene-node/mesh pointers become dangling after the scene is
	// cleared, and the dynamic_cast in Weapon_Minigun::init() crashes on the stale vptr.
	if (!m_player_weapon.empty())
	{
		for (auto i = 0U; i < m_player_weapon.size(); i++)
			m_player_weapon.at(i)->destroy();
		m_player_weapon.clear();
	}

	m_firstUpdate = false;
	m_current_weapon = WEAP_NONE;

	m_player_ammo.clear();
	for (auto i = 0U; i < AMMO_COUNT; i++)
	{
		m_player_ammo.emplace_back(0U);
	}

	m_player_weapon.emplace_back(std::make_unique<Weapon_None>(m_weapon_none));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Melee>(m_weapon_melee));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Revolver>(m_weapon_revolver));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Shotgun>(m_weapon_shotgun));
	m_player_weapon.emplace_back(std::make_unique<Weapon_HeavyRifle>(m_weapon_heavyrifle));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Crossbow>(m_weapon_crossbow));
	m_player_weapon.emplace_back(std::make_unique<Weapon_LMG>(m_weapon_lmg));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Sniper>(m_weapon_sniper));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Launcher>(m_weapon_launcher));
	m_player_weapon.emplace_back(std::make_unique<Weapon_DualSMG>(m_weapon_dualsmg));
	m_player_weapon.emplace_back(std::make_unique<Weapon_SkullStaff>(m_weapon_skullstaff));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Sawnoffs>(m_weapon_sawnoffs));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Pitchfork>(m_weapon_pitchfork));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Rifle>(m_weapon_rifle));
	m_player_weapon.emplace_back(std::make_unique<Weapon_SMG>(m_weapon_smg));

	// The player starts with nothing but empty hands. Every weapon above is still
	// constructed and init()ed — they need their scene nodes and effect pools —
	// but ownership decides what can be switched to, and that starts at WEAP_NONE
	// alone. Pickups grant the rest through giveWeapon().
	//
	// Sized from m_player_weapon rather than WEAP_COUNT: the enum lists weapons
	// that were never registered, and indexing this by an unregistered slot would
	// be a silent out-of-range read.
	m_owned.assign(m_player_weapon.size(), false);
	if (!m_owned.empty())
		m_owned[WEAP_NONE] = true;
}

bool WeaponController::hasWeapon(PLAYER_WEAPON type) const
{
	const auto slot = static_cast<unsigned int>(type);
	return slot < m_owned.size() && m_owned[slot];
}

bool WeaponController::giveWeapon(PLAYER_WEAPON type, bool autoEquip)
{
	const auto slot = static_cast<unsigned int>(type);

	// Silently ignore a slot that was never registered — a pickup placed in a map
	// with a stale weaponType should do nothing, not crash or arm something else.
	if (slot >= m_owned.size())
	{
		spdlog::warn("WeaponController::giveWeapon(): weapon {} is not a registered slot", slot);
		return false;
	}

	const bool isNew = !m_owned[slot];

	m_owned[slot] = true;

	// Only raise a genuinely new weapon. Walking over a gun you already carry
	// should not yank the one in your hands away mid-fight.
	if (isNew && autoEquip)
		switchWeapon(type);

	return isNew;
}

void WeaponController::giveAllWeapons()
{
	m_owned.assign(m_player_weapon.size(), true);
}

// Walks in 'step' direction until it finds an owned slot, wrapping at both ends.
// Bounded by the slot count, so an unowned-everything state cannot spin forever;
// returning 'from' unchanged is the caller's cue that there is nowhere to go.
unsigned int WeaponController::nextOwnedSlot(unsigned int from, int step) const
{
	const int count = static_cast<int>(m_owned.size());
	if (count <= 0)
		return from;

	int slot = static_cast<int>(from);

	for (int i = 0; i < count; ++i)
	{
		slot += step;

		if (slot < 0)      slot = count - 1;
		if (slot >= count) slot = 0;

		if (m_owned[slot])
			return static_cast<unsigned int>(slot);
	}

	return from;
}

void WeaponController::update()
{
	if (!m_firstUpdate)
	{
		PlayerWeapon::precacheSharedSounds();

		// Bound by what was actually registered, NOT WEAP_COUNT — the enum lists
		// every weapon that exists but init() only pushes the uncommented ones,
		// so .at(WEAP_COUNT-1) throws std::out_of_range. Mirrors the destroy loop.
		for (auto i = 0U; i < m_player_weapon.size(); i++)
		{
			m_player_weapon.at(i)->precache();
			m_player_weapon.at(i)->init();
		}

		m_firstUpdate = true;
	}

	// Complete a pending weapon switch once the current weapon's unequip anim is done.
	// This runs before input so that a switch completing this frame is visible to the
	// input code below — preventing startUnequip() from being called on an already-hidden
	// weapon (which would set isUnequipping=true on an invisible node and lock the system).
	if (m_pendingWeapon >= 0 && !current_weapon->isUnequipping())
	{
		// Recorded HERE, where the switch completes, rather than in switchWeapon().
		// A switch that is superseded while the old weapon is still holstering never
		// became the weapon you were holding, and Q must not return to it.
		if (static_cast<unsigned int>(m_pendingWeapon) != m_current_weapon)
			m_previousWeapon = m_current_weapon;

		m_current_weapon = static_cast<unsigned int>(m_pendingWeapon);
		m_pendingWeapon = -1;
		current_weapon->equip();
	}

	// Mouse wheel opens the selection bar and moves through it, rather than
	// switching outright — the HL2 behaviour, and the reason the bar exists: you
	// can spin past four weapons without equipping the three you did not want.
	// It commits the same way a bucket key does.
	float wheel = InputManager::Get()->getMouseWheelDelta();
	if (wheel < 0.f)
		selectionStep(+1);
	else if (wheel > 0.f)
		selectionStep(-1);

	// [ ] keys
	static bool lb = false, rb = false;
	if (!InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LBRACKET) && lb)
		lb = false;
	if (!InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_RBRACKET) && rb)
		rb = false;
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LBRACKET) && !lb)
	{
		selectionStep(-1);
		lb = true;
	}
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_RBRACKET) && !rb)
	{
		selectionStep(+1);
		rb = true;
	}

	// Number keys: 0 holsters, 1..WEAPCAT_COUNT-1 are the selection buckets.
	//
	// These deliberately do NOT pass ignore_process_flag, so they go quiet while
	// the inventory panel or the viewmodel debug window has taken input.
	static const int numKeys[10] = {
		KEY_NUM0, KEY_NUM1, KEY_NUM2, KEY_NUM3, KEY_NUM4,
		KEY_NUM5, KEY_NUM6, KEY_NUM7, KEY_NUM8, KEY_NUM9
	};
	static bool numKeyState[10] = {};

	if (InputManager::Get()->getKeyPressOnce(numKeys[0], &numKeyState[0]))
	{
		cancelSelection();
		switchWeapon(WEAP_NONE);
	}

	for (int cat = 1; cat < WEAPCAT_COUNT && cat < 10; cat++)
	{
		if (InputManager::Get()->getKeyPressOnce(numKeys[cat], &numKeyState[cat]))
			selectCategory(cat);
	}

	// Q — back to the weapon held before this one.
	static bool qState = false;
	if (InputManager::Get()->getKeyPressOnce(KEY_Q, &qState))
	{
		cancelSelection();
		switchToLastWeapon();
	}

	// An open selection waits: attack takes the highlighted weapon, alt-fire throws
	// the bar away and leaves the current weapon alone. No timeout — the bar costs
	// nothing to leave open, because nothing is equipped until it closes.
	//
	// Both reads pass ignore_process_flag so they see through the mouse block that
	// is keeping these same buttons away from the weapon.
	if (isSelectionOpen())
	{
		if (InputManager::Get()->isMouseButtonPressed(MB_LEFT, true))
		{
			commitSelection();
			m_swallowMouseUntilRelease = true;
		}
		else if (InputManager::Get()->isMouseButtonPressed(MB_RIGHT, true))
		{
			cancelSelection();
			m_swallowMouseUntilRelease = true;
		}
	}

	// Hold the block until the button that closed the bar is released, so that
	// click is not then read as a trigger pull by the weapon it just chose.
	if (m_swallowMouseUntilRelease
		&& !InputManager::Get()->isMouseButtonPressed(MB_LEFT, true)
		&& !InputManager::Get()->isMouseButtonPressed(MB_RIGHT, true))
	{
		m_swallowMouseUntilRelease = false;
	}

	// Set every frame rather than toggled at the edges: self-healing, so no path
	// out of the selection can strand the mouse in a blocked state. Nothing else
	// in the codebase touches this flag.
	InputManager::Get()->blockMouseInput(isSelectionOpen() || m_swallowMouseUntilRelease);

	current_weapon->update();

	drawWeaponSelection();

	// Hitmarker overlay — driven by registerHitFeedback() from any weapon's damage
	PlayerWeapon::drawHitFeedback();

	// Bound by registered weapons, NOT WEAP_COUNT: the enum lists every weapon
	// that exists but init() only pushes the uncommented ones, so .at() past
	// the end throws std::out_of_range (and this runs every frame).
	for (auto i = 0U; i < m_player_weapon.size(); i++)
	{
		m_player_weapon.at(i)->persist();
	}

	if (m_current_weapon != WEAP_NONE)
	{
		current_weapon->updateWeaponSway(Engine::Get()->getDeltaTime());
	}

	if (InputManager::Get()->isActionPressed("reload"))
	{
		current_weapon->reload();
	}

	drawViewmodelDebugUI();
}

void WeaponController::destroy()
{
	for (auto i = 0U; i < m_player_weapon.size(); i++)
	{
		m_player_weapon.at(i)->destroy();
	}

	m_firstUpdate = false;
}

// Clamps to the pool's cap and reports what fitted. A caller that gets 0 back
// knows the player was already full, which is what lets a weapon pickup leave
// itself in the world instead of being silently eaten for no benefit.
unsigned int WeaponController::addAmmo(AMMO_TYPE type, unsigned int amount)
{
	if (type <= AMMO_NONE || type >= AMMO_COUNT || amount == 0)
		return 0;

	const unsigned int cap     = static_cast<unsigned int>(ammoReserveMax(type));
	const unsigned int current = m_player_ammo.at(type);

	if (current >= cap)
		return 0;

	const unsigned int accepted = std::min(amount, cap - current);

	m_player_ammo.at(type) = current + accepted;

	return accepted;
}

void WeaponController::setAmmo(AMMO_TYPE type, unsigned int amount)
{
	if (type <= AMMO_NONE || type >= AMMO_COUNT)
		return;

	m_player_ammo.at(type) = std::min(amount, static_cast<unsigned int>(ammoReserveMax(type)));
}

unsigned int WeaponController::reserveAmmo(AMMO_TYPE type) const
{
	if (type <= AMMO_NONE || type >= AMMO_COUNT || type >= m_player_ammo.size())
		return 0;

	return m_player_ammo.at(type);
}

unsigned int WeaponController::takeAmmo(AMMO_TYPE type, unsigned int amount)
{
	if (type <= AMMO_NONE || type >= AMMO_COUNT || amount == 0)
		return 0;

	const unsigned int taken = std::min(amount, m_player_ammo.at(type));

	m_player_ammo.at(type) -= taken;

	return taken;
}

// For a test map or the console — fills every pool to its cap.
void WeaponController::giveAllAmmo()
{
	for (auto i = 1U; i < AMMO_COUNT; i++)
		m_player_ammo.at(i) = static_cast<unsigned int>(ammoReserveMax(static_cast<AMMO_TYPE>(i)));
}

PLAYER_WEAPON WeaponController::currentWeaponType() const
{
	if (m_current_weapon >= m_player_weapon.size() || !m_player_weapon.at(m_current_weapon))
		return WEAP_NONE;

	return m_player_weapon.at(m_current_weapon)->weaponType();
}

int WeaponController::currentDisplayAmmo() const
{
	if (m_current_weapon >= m_player_weapon.size() || !m_player_weapon.at(m_current_weapon))
		return -1;

	return m_player_weapon.at(m_current_weapon)->displayAmmo();
}

int WeaponController::currentReserveAmmo() const
{
	const AMMO_TYPE type = weaponAmmoType(currentWeaponType());

	// AMMO_NONE covers both the empty hands and the weapons that never reload
	// from a pool. Reported as -1 rather than 0 so the HUD shows no reserve at
	// all instead of an alarming zero next to a knife.
	if (type == AMMO_NONE)
		return -1;

	return static_cast<int>(reserveAmmo(type));
}

void WeaponController::loadWeaponMagState(PLAYER_WEAPON type, const WeaponMagState& state)
{
	// Searched by reported type rather than indexed by the enum, so this stays
	// correct even if the registration order in init() ever stops matching it.
	for (auto& weapon : m_player_weapon)
	{
		if (weapon && weapon->weaponType() == type)
		{
			weapon->loadMagState(state);
			return;
		}
	}
}

// Highest valid index into m_player_weapon. Cycling must wrap on this, not on
// WEAP_COUNT — the enum covers weapons that were never registered.
unsigned int WeaponController::lastWeaponSlot() const
{
	return m_player_weapon.empty() ? 0u : static_cast<unsigned int>(m_player_weapon.size()) - 1u;
}

void WeaponController::switchNextWeapon()
{
	// Skips straight over anything not picked up, so the wheel walks the guns the
	// player actually has rather than stopping on empty slots.
	unsigned int target = nextOwnedSlot(m_current_weapon, +1);
	if (target == m_current_weapon) return;

	m_pendingWeapon = static_cast<int>(target);
	if (!current_weapon->isUnequipping())
		current_weapon->startUnequip();
}

void WeaponController::switchPreviousWeapon()
{
	unsigned int target = nextOwnedSlot(m_current_weapon, -1);
	if (target == m_current_weapon) return;

	m_pendingWeapon = static_cast<int>(target);
	if (!current_weapon->isUnequipping())
		current_weapon->startUnequip();
}

void WeaponController::switchWeapon(PLAYER_WEAPON weapon)
{
	// Reject unregistered slots as well as WEAP_COUNT itself
	if (static_cast<unsigned int>(weapon) >= m_player_weapon.size()
		|| static_cast<unsigned int>(weapon) == m_current_weapon) return;

	// ...and anything the player has not picked up. This is the single gate:
	// the number keys, the wheel and the bracket keys all come through here, so
	// nothing can reach an unowned weapon by another route.
	if (!hasWeapon(weapon)) return;

	m_pendingWeapon = static_cast<int>(weapon);
	if (!current_weapon->isUnequipping())
		current_weapon->startUnequip();
}

// --- Selection (Half-Life 2 buckets) ----------------------------------------

int WeaponController::firstOwnedInCategory(int category, int fromSlot) const
{
	const int count = static_cast<int>(m_player_weapon.size());
	if (count <= 0)
		return -1;

	// Walk the whole slot list once starting just after fromSlot, so the search
	// wraps within the bucket without needing the bucket's members listed
	// anywhere. fromSlot < 0 starts from the beginning.
	const int start = (fromSlot < 0) ? 0 : ((fromSlot + 1) % count);

	for (int i = 0; i < count; i++)
	{
		const int slot = (start + i) % count;

		if (!m_owned[slot])
			continue;

		if (weaponCategory(static_cast<PLAYER_WEAPON>(slot)) != category)
			continue;

		return slot;
	}

	return -1;
}

int WeaponController::buildSelectionOrder(int* out, int max) const
{
	int n = 0;

	// Category-major, then slot order WITHIN the category — exactly the two loops
	// drawWeaponSelection() uses, so position in this list is position on screen.
	// Walking m_player_weapon directly would follow enum order instead, which
	// interleaves the buckets and would send the highlight jumping across the bar.
	for (int cat = 1; cat < WEAPCAT_COUNT && n < max; cat++)
	{
		for (int slot = 0; slot < static_cast<int>(m_player_weapon.size()) && n < max; slot++)
		{
			if (m_owned[slot] && weaponCategory(static_cast<PLAYER_WEAPON>(slot)) == cat)
				out[n++] = slot;
		}
	}

	return n;
}

void WeaponController::selectionStep(int direction)
{
	int order[32];
	const int count = buildSelectionOrder(order, 32);

	if (count <= 0)
		return; // nothing owned but empty hands

	// Step from the highlight when the bar is already open, otherwise from the
	// weapon in hand — so the first notch of the wheel moves off what you are
	// holding rather than re-selecting it.
	const int anchor = isSelectionOpen()
		? m_selectionSlot
		: static_cast<int>(m_current_weapon);

	int at = -1;
	for (int i = 0; i < count; i++)
	{
		if (order[i] == anchor)
		{
			at = i;
			break;
		}
	}

	int next;
	if (at < 0)
	{
		// The anchor is not in the list at all — empty hands, which sits in no
		// bucket. Enter at whichever end the player is scrolling towards.
		next = (direction > 0) ? 0 : (count - 1);
	}
	else
	{
		next = at + direction;
		if (next < 0)      next = count - 1;
		if (next >= count) next = 0;
	}

	m_selectionSlot     = order[next];
	m_selectionCategory = weaponCategory(static_cast<PLAYER_WEAPON>(m_selectionSlot));
}

void WeaponController::selectCategory(int category)
{
	if (category <= WEAPCAT_NONE || category >= WEAPCAT_COUNT)
		return;

	// A different bucket commits whatever the open one was showing before moving
	// on — otherwise tapping 4 then 5 would silently discard the 4.
	if (isSelectionOpen() && m_selectionCategory != category)
		commitSelection();

	const bool advancing = isSelectionOpen() && m_selectionCategory == category;

	// Advancing searches from the current highlight; opening starts from the
	// weapon in hand, so pressing the bucket you are already holding moves to the
	// NEXT one in it rather than re-selecting what you have.
	const int from = advancing
		? m_selectionSlot
		: ((weaponCategory(currentWeaponType()) == category)
			? static_cast<int>(m_current_weapon)
			: -1);

	const int found = firstOwnedInCategory(category, from);

	if (found < 0)
		return; // nothing owned in this bucket: leave whatever was open alone

	m_selectionCategory = category;
	m_selectionSlot     = found;
}

void WeaponController::commitSelection()
{
	const int slot = m_selectionSlot;

	m_selectionCategory = -1;
	m_selectionSlot     = -1;

	if (slot < 0)
		return;

	// switchWeapon() early-returns when this is already the weapon in hand, which
	// is deliberate: committing with attack while holding the highlighted weapon
	// closes the bar and lets the click through as a shot, rather than eating it.
	switchWeapon(static_cast<PLAYER_WEAPON>(slot));
}

void WeaponController::cancelSelection()
{
	m_selectionCategory = -1;
	m_selectionSlot     = -1;
}

void WeaponController::switchToLastWeapon()
{
	// Falls back to cycling when the remembered weapon is gone or is the one in
	// hand, so Q always does something rather than appearing broken.
	if (m_previousWeapon < m_player_weapon.size()
		&& m_previousWeapon != m_current_weapon
		&& m_owned[m_previousWeapon])
	{
		switchWeapon(static_cast<PLAYER_WEAPON>(m_previousWeapon));
		return;
	}

	switchNextWeapon();
}

// --- Selection HUD -----------------------------------------------------------
//
// Deliberately drawn with the RenderManager 2D primitives rather than ImGui: this
// is gameplay HUD, it has to sit in the same layer as the crosshair and ammo, and
// the ImGui path is reserved for editor and debug windows.
//
// The whole thing is one function reading only m_selection*, so restyling it later
// touches nothing else.
void WeaponController::drawWeaponSelection()
{
	if (!isSelectionOpen())
		return;

	auto* rm = RenderManager::Get();
	const auto cfg = rm->getConfiguration();

	// TWO TRAPS IN THE 2D API, both found by running this rather than by reading it.
	//
	// 1. The renderable lists are flushed images -> rectangles -> text, so a
	//    rectangle ALWAYS covers an image no matter what order they were queued
	//    in. A filled panel behind an icon is therefore impossible; the cell is
	//    drawn as four thin border rects instead, leaving the icon clear.
	//
	// 2. renderText2D()'s vector2di overload builds rect(x, y, 0, 0), so hcenter
	//    centres between x and ZERO — every centred label lands at half its
	//    intended position. Always pass a real rect when centring.
	const int cellW = static_cast<int>(cfg.width * 0.115f);
	const int cellH = cellW / 2;              // the icons are rendered 2:1
	const int gap   = static_cast<int>(cellW * 0.06f);
	const int line  = static_cast<int>(cfg.height * 0.028f);

	const int buckets = WEAPCAT_COUNT - 1;    // WEAPCAT_NONE is not a bucket
	const int totalW  = buckets * cellW + (buckets - 1) * gap;

	int x = (static_cast<int>(cfg.width) - totalW) / 2;
	const int y = static_cast<int>(cfg.height * 0.06f);

	// Deepest column, so the name label clears ALL of them — the automatics
	// bucket holds four and a fixed offset would land on top of its last cell.
	int columnBottom = y + cellH;

	for (int cat = 1; cat < WEAPCAT_COUNT; cat++)
	{
		const bool isOpenBucket = (cat == m_selectionCategory);

		int members[16];
		int memberCount = 0;
		for (int slot = 0; slot < static_cast<int>(m_player_weapon.size())
			&& memberCount < 16; slot++)
		{
			if (m_owned[slot]
				&& weaponCategory(static_cast<PLAYER_WEAPON>(slot)) == cat)
				members[memberCount++] = slot;
		}

		// Bucket number above the column. Dimmed when empty — an empty bucket
		// keeps its place so the row does not reflow as weapons are picked up.
		rm->renderText2D(
			irr::core::stringw(cat),
			TEXT_DEFAULT_FONT::SMALL,
			irr::core::rect<irr::s32>(x, y - line - gap / 2, x + cellW, y - gap / 2),
			memberCount > 0 ? irr::video::SColor(230, 255, 235, 190)
			                : irr::video::SColor(90, 150, 150, 150),
			true, true);

		int cy = y;

		for (int m = 0; m < (memberCount > 0 ? memberCount : 1); m++)
		{
			const bool empty = (memberCount == 0);
			const int  slot  = empty ? -1 : members[m];
			const bool highlighted = !empty && isOpenBucket && slot == m_selectionSlot;

			// The icon FIRST, so the border rects land on top of its edges rather
			// than the other way round.
			if (!empty)
			{
				const char* icon = weaponIconPath(static_cast<PLAYER_WEAPON>(slot));
				auto* tex = icon ? rm->driver()->getTexture(icon) : nullptr;

				if (tex)
				{
					rm->renderImage2DScaled(
						tex,
						irr::core::rect<irr::s32>(x, cy, x + cellW, cy + cellH),
						highlighted ? irr::video::SColor(255, 255, 255, 255)
						            : irr::video::SColor(150, 170, 180, 195),
						true);
				}
				else
				{
					rm->renderText2D(
						irr::core::stringw(weaponDisplayName(static_cast<PLAYER_WEAPON>(slot))),
						TEXT_DEFAULT_FONT::SMALL,
						irr::core::rect<irr::s32>(x, cy, x + cellW, cy + cellH),
						irr::video::SColor(220, 235, 235, 235), true, true);
				}
			}

			// Border, as four thin rects. Thicker and warm when highlighted, so the
			// selection reads at a glance without covering the weapon.
			const int bw = highlighted ? (std::max)(3, cellH / 26) : (std::max)(1, cellH / 60);
			const irr::video::SColor edge =
				highlighted ? irr::video::SColor(240, 245, 180, 60)
				            : (empty ? irr::video::SColor(60, 120, 130, 140)
				                     : irr::video::SColor(120, 150, 165, 180));

			rm->renderRectangle2D(irr::core::rect<irr::s32>(x, cy, x + cellW, cy + bw), edge);
			rm->renderRectangle2D(irr::core::rect<irr::s32>(x, cy + cellH - bw, x + cellW, cy + cellH), edge);
			rm->renderRectangle2D(irr::core::rect<irr::s32>(x, cy, x + bw, cy + cellH), edge);
			rm->renderRectangle2D(irr::core::rect<irr::s32>(x + cellW - bw, cy, x + cellW, cy + cellH), edge);

			cy += cellH + gap / 2;
		}

		if (cy > columnBottom)
			columnBottom = cy;

		x += cellW + gap;
	}

	// Name of the highlighted weapon, under the row. The icons carry the
	// recognition; this settles the ambiguous ones (two SMGs, two shotguns).
	if (m_selectionSlot >= 0)
	{
		rm->renderText2D(
			irr::core::stringw(weaponDisplayName(static_cast<PLAYER_WEAPON>(m_selectionSlot))),
			TEXT_DEFAULT_FONT::SMALL,
			irr::core::rect<irr::s32>(0, columnBottom + gap,
				static_cast<irr::s32>(cfg.width), columnBottom + gap + line),
			irr::video::SColor(240, 255, 235, 190), true, true);

		// The bar no longer times out, so how to leave it has to be on screen.
		rm->renderText2D(
			L"ATTACK  select      ALT-FIRE  cancel",
			TEXT_DEFAULT_FONT::SMALL,
			irr::core::rect<irr::s32>(0, columnBottom + gap + line + gap / 2,
				static_cast<irr::s32>(cfg.width),
				columnBottom + gap + line + gap / 2 + line),
			irr::video::SColor(150, 190, 200, 210), true, true);
	}
}

void WeaponController::unequipWeapon()
{
	m_current_weapon = WEAP_NONE;
}

void WeaponController::setViewmodelDebug(bool open)
{
	m_showViewmodelDebug = open;
	ImGui::GetIO().MouseDrawCursor  = open;
	InputManager::Get()->canProcessInput(!open);
	g_PlayerInventoryIsDisplaying   = open;
}

void WeaponController::drawViewmodelDebugUI()
{
	// Toggle with F2 (only detectable while input is active, i.e. while window is closed)
	static bool f2Last = false;
	bool f2Now = InputManager::Get()->isKeyPressed(KEY_F2);
	if (f2Now && !f2Last)
		setViewmodelDebug(true);
	f2Last = f2Now;

	if (!m_showViewmodelDebug)
		return;

	PlayerWeapon* weap = current_weapon.get();
	if (!weap || !weap->debugViewmodelNode())
		return;

	ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
	ImGui::Begin("Viewmodel Transform", &m_showViewmodelDebug);

	// Detect close via the ImGui X button
	if (!m_showViewmodelDebug)
	{
		setViewmodelDebug(false);
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("%s", weap->debugWeaponName().c_str());
	ImGui::Separator();

	auto& pos = weap->debugViewPositionOffset();
	auto& rot = weap->debugViewRotationOffset();

	float p[3] = { pos.X, pos.Y, pos.Z };
	float r[3] = { rot.X, rot.Y, rot.Z };

	bool changed = false;
	changed |= ImGui::DragFloat3("Position", p, 0.005f, -3.0f, 3.0f, "%.4f");
	changed |= ImGui::DragFloat3("Rotation", r, 0.5f,  -180.0f, 180.0f, "%.2f");

	if (changed)
	{
		pos = irr::core::vector3df(p[0], p[1], p[2]);
		rot = irr::core::vector3df(r[0], r[1], r[2]);
		weap->debugViewmodelNode()->setPosition(pos);
		weap->debugViewmodelNode()->setRotation(rot);
	}

	ImGui::Separator();
	ImGui::TextDisabled("Copy-paste:");

	char posBuf[128], rotBuf[128];
	snprintf(posBuf, sizeof(posBuf), "irr::core::vector3df(%.4ff, %.4ff, %.4ff)", pos.X, pos.Y, pos.Z);
	snprintf(rotBuf, sizeof(rotBuf), "irr::core::vector3df(%.2ff, %.2ff, %.2ff)", rot.X, rot.Y, rot.Z);
	ImGui::InputText("##pos", posBuf, sizeof(posBuf), ImGuiInputTextFlags_ReadOnly);
	ImGui::InputText("##rot", rotBuf, sizeof(rotBuf), ImGuiInputTextFlags_ReadOnly);

	// Muzzle attachment. The offset is in the muzzle JOINT's local space, i.e.
	// model units, so the useful drag range is the size of the gun mesh (tens of
	// units on the glTF weapons) rather than the sub-1.0 range the viewmodel
	// position above uses. Fire while dragging to see where the flash lands.
	if (WeaponEffects* fx = weap->debugEffects())
	{
		ImGui::Separator();
		ImGui::TextDisabled("Muzzle offset (joint-local, model units)");

		auto& muzzle = fx->debugMuzzleOffset();
		float m[3] = { muzzle.X, muzzle.Y, muzzle.Z };

		if (ImGui::DragFloat3("Muzzle", m, 0.25f, -200.0f, 200.0f, "%.2f"))
		{
			muzzle = irr::core::vector3df(m[0], m[1], m[2]);
			fx->applyMuzzleOffset();
		}

		char muzBuf[128];
		snprintf(muzBuf, sizeof(muzBuf), "irr::core::vector3df(%.2ff, %.2ff, %.2ff)",
			muzzle.X, muzzle.Y, muzzle.Z);
		ImGui::InputText("##muzzle", muzBuf, sizeof(muzBuf), ImGuiInputTextFlags_ReadOnly);
	}

	// Clip stabilisation. Fire repeatedly while dragging: Amount 0 is the raw
	// animation, 1 pins the reference point dead still. Dragging Ref along the
	// barrel axis trades muzzle steadiness against stock steadiness.
	if (weap->hasClipStabilization())
	{
		ImGui::Separator();
		ImGui::TextDisabled("Clip stabilisation (ref is joint-local, model units)");

		auto& amount = weap->debugStabilizationAmount();
		auto& ref    = weap->debugStabilizationOffset();

		ImGui::SliderFloat("Amount", &amount, 0.0f, 1.0f, "%.2f");

		float s[3] = { ref.X, ref.Y, ref.Z };
		if (ImGui::DragFloat3("Ref", s, 0.25f, -200.0f, 200.0f, "%.2f"))
			ref = irr::core::vector3df(s[0], s[1], s[2]);

		char stabBuf[160];
		snprintf(stabBuf, sizeof(stabBuf), "%.2ff  /  irr::core::vector3df(%.2ff, %.2ff, %.2ff)",
			amount, ref.X, ref.Y, ref.Z);
		ImGui::InputText("##stab", stabBuf, sizeof(stabBuf), ImGuiInputTextFlags_ReadOnly);
	}

	// Projectile arc. Fire repeatedly while dragging: Speed and Gravity trade off
	// against each other, so sweep one at a time. Aim Range is the horizon the arc
	// is solved to land on — shorter keeps the shot flat, longer lobs it.
	PlayerWeapon::BallisticTuning bal;
	if (weap->debugBallistics(bal) && bal.speed && bal.gravity && bal.maxAimRange)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Ballistics (applies to the NEXT shot)");

		ImGui::DragFloat("Speed",     bal.speed,       1.0f, 10.0f, 400.0f, "%.1f");
		ImGui::DragFloat("Gravity",   bal.gravity,     0.1f,  0.0f,  40.0f, "%.2f");
		ImGui::DragFloat("Aim range", bal.maxAimRange, 1.0f,  5.0f, 500.0f, "%.1f");

		char balBuf[160];
		snprintf(balBuf, sizeof(balBuf), "speed %.1ff  gravity %.2ff  range %.1ff",
			*bal.speed, *bal.gravity, *bal.maxAimRange);
		ImGui::InputText("##ballistics", balBuf, sizeof(balBuf), ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::End();
}