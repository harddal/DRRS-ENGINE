#include "WeaponController.h"

#include "spdlog/spdlog.h"

#include "Engine/Engine.h"

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
	m_player_weapon.emplace_back(std::make_unique<Weapon_Pistol>(m_weapon_pistol));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Shotgun>(m_weapon_shotgun));
	m_player_weapon.emplace_back(std::make_unique<Weapon_PulseRifle>(m_weapon_pulserifle));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Rifle>(m_weapon_rifle));
	m_player_weapon.emplace_back(std::make_unique<Weapon_Minigun>(m_weapon_minigun));
	m_player_weapon.emplace_back(std::make_unique<Weapon_RocketLauncher>(m_weapon_rocketlauncher));
}

void WeaponController::update()
{
	if (!m_firstUpdate)
	{
		for (auto i = 0U; i < WEAP_COUNT; i++)
		{
			m_player_weapon.at(i)->precache();
			m_player_weapon.at(i)->init();
		}

		m_firstUpdate = true;
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

	for (auto i = 0U; i < WEAP_COUNT; i++)
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
}

void WeaponController::destroy()
{
	for (auto i = 0U; i < WEAP_COUNT; i++)
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

void WeaponController::switchNextWeapon()
{
	current_weapon->unequip();

	if (m_current_weapon == WEAP_COUNT - 1)
	{
		m_current_weapon = WEAP_NONE;
	}
	else
	{
		m_current_weapon++;
	}

	current_weapon->equip();

	//spdlog::debug("CURRENT WEAPON: {0}", m_current_weapon);
}

void WeaponController::switchPreviousWeapon()
{
	current_weapon->unequip();

	if (m_current_weapon == WEAP_NONE)
	{
		m_current_weapon = WEAP_COUNT - 1;
	}
	else
	{
		m_current_weapon--;
	}

	current_weapon->equip();

	//spdlog::debug("CURRENT WEAPON: {0}", m_current_weapon);
}

void WeaponController::switchWeapon(PLAYER_WEAPON weapon)
{
	current_weapon->unequip();

	if (weapon == WEAP_COUNT)
	{
		return;
	}

	m_current_weapon = weapon;

	current_weapon->equip();
}

void WeaponController::unequipWeapon()
{
	m_current_weapon = WEAP_NONE;
}