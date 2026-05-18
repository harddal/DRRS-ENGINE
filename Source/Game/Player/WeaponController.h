#pragma once

#include <memory>
#include <vector>

#include "WeaponData.h"

#include "Weapon/Weapon_None.h"
#include "Weapon/Weapon_Melee.h"
#include "Weapon/Weapon_Pistol.h"
#include "Weapon/Weapon_Rifle.h"
#include "Weapon/Weapon_Minigun.h"
#include "Weapon/Weapon_RocketLauncher.h"
#include "Weapon/Weapon_Shotgun.h"
#include "Weapon/Weapon_PulseRifle.h"

class WeaponController
{
public:
	void init();
	void update();
	void destroy();

	void addAmmo(AMMO_TYPE type, unsigned int amount);
	void setAmmo(AMMO_TYPE type, unsigned int amount);

	void switchNextWeapon();
	void switchPreviousWeapon();
	void switchWeapon(PLAYER_WEAPON type);
	void unequipWeapon();

	std::vector<unsigned int> m_player_ammo;
	std::vector<std::unique_ptr<PlayerWeapon>> m_player_weapon;

private:
	bool m_firstUpdate;

	unsigned int m_current_weapon;
	int m_pendingWeapon = -1; // >= 0 while waiting for unequip anim to finish

	Weapon_None m_weapon_none;
	Weapon_Melee m_weapon_melee;
	Weapon_Pistol m_weapon_pistol;
	Weapon_Rifle m_weapon_rifle;
	Weapon_Minigun m_weapon_minigun;
	Weapon_RocketLauncher m_weapon_rocketlauncher;
	Weapon_Shotgun m_weapon_shotgun;
	Weapon_PulseRifle m_weapon_pulserifle;

};