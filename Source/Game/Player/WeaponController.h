#pragma once

#include <memory>
#include <vector>

#include "WeaponData.h"

#include "Weapon/Weapon_None.h"

#include "Weapon/Weapon_Melee.h"
#include "Weapon/Weapon_Revolver.h"
#include "Weapon/Weapon_Shotgun.h"
#include "Weapon/Weapon_HeavyRifle.h"
#include "Weapon/Weapon_Crossbow.h"
#include "Weapon/Weapon_LMG.h"
#include "Weapon/Weapon_Sniper.h"
#include "Weapon/Weapon_Launcher.h"
#include "Weapon/Weapon_DualSMG.h"
#include "Weapon/Weapon_SkullStaff.h"
#include "Weapon/Weapon_Sawnoffs.h"
#include "Weapon/Weapon_Pitchfork.h"


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

	// --- Ownership -----------------------------------------------------------
	// Every weapon is CONSTRUCTED and init()ed at startup regardless — they need
	// their scene nodes and effect pools built, and tearing that down per pickup
	// would be a lot of churn for nothing. Ownership gates only what the player
	// can switch TO, which is the part that actually matters.
	//
	// The player starts owning WEAP_NONE alone. Everything else arrives through
	// giveWeapon(), which is what a pickup calls.
	//
	// Returns true when the weapon was NOT already owned — so a caller can tell
	// "you found a new gun" from "you topped up one you had" and react differently.
	bool giveWeapon(PLAYER_WEAPON type, bool autoEquip = true);
	bool hasWeapon(PLAYER_WEAPON type) const;

	// For a debug command or a test map that wants the old behaviour back
	void giveAllWeapons();

	std::vector<unsigned int> m_player_ammo;
	std::vector<std::unique_ptr<PlayerWeapon>> m_player_weapon;

private:
	bool m_firstUpdate;
	bool m_showViewmodelDebug = false;

	void setViewmodelDebug(bool open);
	void drawViewmodelDebugUI();

	// Highest valid index into m_player_weapon (registered weapons only)
	unsigned int lastWeaponSlot() const;

	// One flag per registered slot. Sized in init() alongside m_player_weapon, so
	// it cannot drift out of step with what was actually registered.
	std::vector<bool> m_owned;

	// The next owned slot walking in 'step' direction from 'from', wrapping.
	// Returns 'from' when nothing else is owned, which is what makes the cycling
	// safe with a single weapon: the caller sees no change and does nothing.
	unsigned int nextOwnedSlot(unsigned int from, int step) const;

	unsigned int m_current_weapon;
	int m_pendingWeapon = -1; // >= 0 while waiting for unequip anim to finish

	Weapon_None m_weapon_none;
	Weapon_Melee m_weapon_melee;
	Weapon_Pistol m_weapon_pistol;
	Weapon_Revolver m_weapon_revolver;
	Weapon_BoltDriver m_weapon_boltdriver;
	Weapon_HeavyRifle m_weapon_heavyrifle;
	Weapon_Crossbow m_weapon_crossbow;
	Weapon_LMG m_weapon_lmg;
	Weapon_Sniper m_weapon_sniper;
	Weapon_Launcher m_weapon_launcher;
	Weapon_DualSMG m_weapon_dualsmg;
	Weapon_SkullStaff m_weapon_skullstaff;
	Weapon_Sawnoffs m_weapon_sawnoffs;
	Weapon_Pitchfork m_weapon_pitchfork;
	Weapon_Minigun m_weapon_minigun;
	Weapon_RocketLauncher m_weapon_rocketlauncher;
	Weapon_Shotgun m_weapon_shotgun;
	Weapon_MiningLaser m_weapon_mininglaser;
	Weapon_BioRifle m_weapon_biorifle;
	Weapon_GrenadeLauncher m_weapon_grenadelauncher;
	Weapon_FuelRodCannon m_weapon_fuelrodcannon;

};