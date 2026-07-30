#pragma once

#include <memory>
#include <vector>

#include "WeaponData.h"

#include "Weapon/Weapon_None.h"

#include "Weapon/Weapon_Melee.h"
#include "Weapon/Weapon_Revolver.h"
#include "Weapon/Weapon_Shotgun.h"
#include "Weapon/Weapon_HeavyRifle.h"


#include "Weapon/Weapon_Pistol.h"
#include "Weapon/Weapon_BoltDriver.h"
#include "Weapon/Weapon_Minigun.h"
#include "Weapon/Weapon_RocketLauncher.h"
#include "Weapon/Weapon_MiningLaser.h"
#include "Weapon/Weapon_BioRifle.h"
#include "Weapon/Weapon_GrenadeLauncher.h"
#include "Weapon/Weapon_FuelRodCannon.h"

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
	bool m_showViewmodelDebug = false;

	void setViewmodelDebug(bool open);
	void drawViewmodelDebugUI();

	// Highest valid index into m_player_weapon (registered weapons only)
	unsigned int lastWeaponSlot() const;

	unsigned int m_current_weapon;
	int m_pendingWeapon = -1; // >= 0 while waiting for unequip anim to finish

	Weapon_None m_weapon_none;
	Weapon_Melee m_weapon_melee;
	Weapon_Pistol m_weapon_pistol;
	Weapon_Revolver m_weapon_revolver;
	Weapon_BoltDriver m_weapon_boltdriver;
	Weapon_HeavyRifle m_weapon_heavyrifle;
	Weapon_Minigun m_weapon_minigun;
	Weapon_RocketLauncher m_weapon_rocketlauncher;
	Weapon_Shotgun m_weapon_shotgun;
	Weapon_MiningLaser m_weapon_mininglaser;
	Weapon_BioRifle m_weapon_biorifle;
	Weapon_GrenadeLauncher m_weapon_grenadelauncher;
	Weapon_FuelRodCannon m_weapon_fuelrodcannon;

};