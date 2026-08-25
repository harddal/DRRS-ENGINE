#include "WeaponController.h"

#include "spdlog/spdlog.h"

#include "Engine/Engine.h"

#include <IMGUI/imgui.h>
#include "InventoryController.h"

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
		m_current_weapon = static_cast<unsigned int>(m_pendingWeapon);
		m_pendingWeapon = -1;
		current_weapon->equip();
	}

	// Mouse wheel: scroll up = previous weapon, scroll down = next weapon
	float wheel = InputManager::Get()->getMouseWheelDelta();
	if (wheel < 0.f)
		switchNextWeapon();
	else if (wheel > 0.f)
		switchPreviousWeapon();

	// [ ] keys
	static bool lb = false, rb = false;
	if (!InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LBRACKET) && lb)
		lb = false;
	if (!InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_RBRACKET) && rb)
		rb = false;
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LBRACKET) && !lb)
	{
		switchPreviousWeapon();
		lb = true;
	}
	if (InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_RBRACKET) && !rb)
	{
		switchNextWeapon();
		rb = true;
	}

	// Number keys 0–9: 0 = no weapon, 1–N = weapon slot N (auto-scales to vector size)
	static const int numKeys[10] = {
		KEY_NUM0, KEY_NUM1, KEY_NUM2, KEY_NUM3, KEY_NUM4,
		KEY_NUM5, KEY_NUM6, KEY_NUM7, KEY_NUM8, KEY_NUM9
	};
	static bool numKeyState[10] = {};
	const int weaponCount = static_cast<int>(m_player_weapon.size());
	for (int i = 0; i < 10 && i < weaponCount; i++)
	{
		if (InputManager::Get()->getKeyPressOnce(numKeys[i], &numKeyState[i]))
			switchWeapon(static_cast<PLAYER_WEAPON>(i));
	}

	current_weapon->update();

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

void WeaponController::addAmmo(AMMO_TYPE type, unsigned int amount)
{
	m_player_ammo.at(type) += amount;
}

void WeaponController::setAmmo(AMMO_TYPE type, unsigned int amount)
{
	m_player_ammo.at(type) = amount;
}

// Highest valid index into m_player_weapon. Cycling must wrap on this, not on
// WEAP_COUNT — the enum covers weapons that were never registered.
unsigned int WeaponController::lastWeaponSlot() const
{
	return m_player_weapon.empty() ? 0u : static_cast<unsigned int>(m_player_weapon.size()) - 1u;
}

void WeaponController::switchNextWeapon()
{
	unsigned int target = (m_current_weapon >= lastWeaponSlot()) ? WEAP_NONE : m_current_weapon + 1;
	if (target == m_current_weapon) return;

	m_pendingWeapon = static_cast<int>(target);
	if (!current_weapon->isUnequipping())
		current_weapon->startUnequip();
}

void WeaponController::switchPreviousWeapon()
{
	unsigned int target = (m_current_weapon == WEAP_NONE) ? lastWeaponSlot() : m_current_weapon - 1;
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

	m_pendingWeapon = static_cast<int>(weapon);
	if (!current_weapon->isUnequipping())
		current_weapon->startUnequip();
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